#include "rb_kmerge.h"
#include "slab_trie.h"
#include <algorithm>
#include <sys/types.h>

static auto cmp = [](const BitmapIterators &a, const BitmapIterators &b) {
  return *(a.iter) > *(b.iter);
};

MergeIterator::MergeIterator() {}

MergeIterator::MergeIterator(const std::vector<roaring::Roaring> &bitmaps) {
  for (size_t i = 0; i < bitmaps.size(); i++) {
    if (!bitmaps[i].isEmpty()) {
      iterators.push_back({bitmaps[i].begin(), bitmaps[i].end(), i});
    }
  }

  // Build initial heap
  if (!iterators.empty()) {
    std::make_heap(iterators.begin(), iterators.end(), cmp);
  }
}

MergeIterator::reference MergeIterator::operator*() const {
  const auto &r = iterators.front();
  current_entry.bitmap_index = r.bitmap_index;
  current_entry.value = *(r.iter);
  return current_entry;
}

MergeIterator::pointer MergeIterator::operator->() const {
  const auto &r = iterators.front();
  current_entry.bitmap_index = r.bitmap_index;
  current_entry.value = *(r.iter);
  return &current_entry;
}

MergeIterator &MergeIterator::operator++() {
  if (iterators.empty()) {
    return *this;
  }

  std::pop_heap(iterators.begin(), iterators.end(), cmp);

  auto &r = iterators.back();
  ++r.iter;

  if (r.iter == r.end) {
    iterators.pop_back();
  } else {
    std::push_heap(iterators.begin(), iterators.end(), cmp);
  }

  return *this;
}

MergeIterator MergeIterator::operator++(int) {
  MergeIterator tmp = *this;
  ++(*this);
  return tmp;
}

bool operator==(const MergeIterator &a, const MergeIterator &b) {
  // Two iterators are equal if both have exhausted all iterators
  return a.iterators.empty() && b.iterators.empty();
}

bool operator!=(const MergeIterator &a, const MergeIterator &b) {
  return !(a == b);
}

MergeRange rb_kmerge(const std::vector<roaring::Roaring> &bitmaps) {
  return MergeRange{MergeIterator(bitmaps), MergeIterator()};
}

// ============================================================
// rb_kmerge_groups implementation
// ============================================================

#define KMGROUPS_HT_INIT_CAP 256

// Hash table for grouping by source-set bitmask
struct KMGroupHT {
  char *data;    // Flat array of stride-sized slots
  int capacity;  // Number of slots (power of 2)
  int count;     // Number of occupied slots
  int nwords;    // Width of bitmask in uint64 words
  size_t stride; // Bytes per slot
};

// Slot access helpers
static inline uint64_t *kmg_slot_words(KMGroupHT *ht, int idx) {
  return (uint64_t *)(ht->data + (size_t)idx * ht->stride);
}

static inline roaring::api::roaring_bitmap_t **
kmg_slot_members_ptr(KMGroupHT *ht, int idx) {
  return (roaring::api::roaring_bitmap_t **)(ht->data +
                                             (size_t)idx * ht->stride +
                                             ht->nwords * sizeof(uint64_t));
}

static inline roaring::api::roaring_bulk_context_t *
kmg_slot_bulk_ctx(KMGroupHT *ht, int idx) {
  return (roaring::api::roaring_bulk_context_t
              *)(ht->data + (size_t)idx * ht->stride +
                 ht->nwords * sizeof(uint64_t) +
                 sizeof(roaring::api::roaring_bitmap_t *));
}

static inline bool kmg_slot_empty(KMGroupHT *ht, int idx) {
  uint64_t *w = kmg_slot_words(ht, idx);
  for (int i = 0; i < ht->nwords; i++) {
    if (w[i] != 0)
      return false;
  }
  return true;
}

static inline bool kmg_words_equal(const uint64_t *a, const uint64_t *b,
                                   int nwords) {
  return memcmp(a, b, nwords * sizeof(uint64_t)) == 0;
}

// Hash: XOR-fold all words with splitmix64 mixing
static inline uint32_t kmg_hash(const uint64_t *words, int nwords) {
  uint64_t h = 0;
  for (int i = 0; i < nwords; i++) {
    h ^= words[i];
    h ^= h >> 30;
    h *= 0xbf58476d1ce4e5b9ULL;
    h ^= h >> 27;
    h *= 0x94d049bb133111ebULL;
    h ^= h >> 31;
  }
  return (uint32_t)h;
}

static void kmg_ht_init(KMGroupHT *ht, int nwords, int init_cap) {
  ht->nwords = nwords;
  ht->stride = nwords * sizeof(uint64_t) +
               sizeof(roaring::api::roaring_bitmap_t *) +
               sizeof(roaring::api::roaring_bulk_context_t);
  ht->capacity = init_cap;
  ht->count = 0;
  ht->data = (char *)calloc((size_t)init_cap, ht->stride);
}

static void kmg_ht_resize(KMGroupHT *ht) {
  int old_cap = ht->capacity;
  char *old_data = ht->data;
  size_t old_stride = ht->stride;
  int new_cap = old_cap * 2;
  int new_mask = new_cap - 1;

  ht->data = (char *)calloc((size_t)new_cap, ht->stride);
  ht->capacity = new_cap;

  for (int i = 0; i < old_cap; i++) {
    uint64_t *w = (uint64_t *)(old_data + (size_t)i * old_stride);
    bool occupied = false;
    for (int j = 0; j < ht->nwords; j++) {
      if (w[j] != 0) {
        occupied = true;
        break;
      }
    }
    if (!occupied)
      continue;

    roaring::api::roaring_bitmap_t *members =
        *(roaring::api::roaring_bitmap_t **)(old_data + (size_t)i * old_stride +
                                             ht->nwords * sizeof(uint64_t));
    uint32_t h = kmg_hash(w, ht->nwords);
    int idx = h & new_mask;
    while (!kmg_slot_empty(ht, idx))
      idx = (idx + 1) & new_mask;
    memcpy(kmg_slot_words(ht, idx), w, ht->nwords * sizeof(uint64_t));
    *kmg_slot_members_ptr(ht, idx) = members;
    // bulk_ctx is zeroed by calloc
  }

  free(old_data);
}

struct KMGroupSlot {
  roaring::api::roaring_bitmap_t *members;
  roaring::api::roaring_bulk_context_t *bulk_ctx;
};

static KMGroupSlot kmg_ht_get_or_create(KMGroupHT *ht,
                                        const uint64_t *bitmask) {
  KMGroupSlot result;

  if (ht->count * 2 >= ht->capacity)
    kmg_ht_resize(ht);

  int mask = ht->capacity - 1;
  uint32_t h = kmg_hash(bitmask, ht->nwords);
  int idx = h & mask;

  while (!kmg_slot_empty(ht, idx)) {
    if (kmg_words_equal(kmg_slot_words(ht, idx), bitmask, ht->nwords)) {
      result.members = *kmg_slot_members_ptr(ht, idx);
      result.bulk_ctx = kmg_slot_bulk_ctx(ht, idx);
      return result;
    }
    idx = (idx + 1) & mask;
  }

  memcpy(kmg_slot_words(ht, idx), bitmask, ht->nwords * sizeof(uint64_t));
  *kmg_slot_members_ptr(ht, idx) = roaring::api::roaring_bitmap_create();
  // bulk_ctx already zeroed by calloc
  ht->count++;
  result.members = *kmg_slot_members_ptr(ht, idx);
  result.bulk_ctx = kmg_slot_bulk_ctx(ht, idx);
  return result;
}

// Helper structures for heap-based k-way merge
struct OptU32 {
  uint32_t value;
  bool has_value;
};

struct KMergNode {
  int element_idx; // 0-based index of iterator
  OptU32 value;
};

// Min-heap helpers
static inline void heap_sift_down(KMergNode *heap, int size, int idx) {
  for (;;) {
    int left = (idx << 1) + 1;
    if (left >= size)
      break;
    int right = left + 1;
    int smallest = left;
    if (right < size && heap[right].value.value < heap[left].value.value)
      smallest = right;
    if (!(heap[smallest].value.value < heap[idx].value.value))
      break;
    KMergNode tmp = heap[idx];
    heap[idx] = heap[smallest];
    heap[smallest] = tmp;
    idx = smallest;
  }
}

static inline void heap_heapify(KMergNode *heap, int size) {
  for (int i = (size >> 1) - 1; i >= 0; i--) {
    heap_sift_down(heap, size, i);
  }
}

// Comparison for qsort
static int nwords_for_cmp;
static int kmgroup_cmp(const void *a, const void *b) {
  const uint64_t *wa = ((const KMGroupResult *)a)->words;
  const uint64_t *wb = ((const KMGroupResult *)b)->words;
  for (int i = 0; i < nwords_for_cmp; i++) {
    if (wa[i] < wb[i])
      return -1;
    if (wa[i] > wb[i])
      return 1;
  }
  return 0;
}

KMGroupResult *rb_kmerge_groups(roaring::api::roaring_bitmap_t **bitmaps,
                                int n_bitmaps, int *n_results) {
  if (n_bitmaps <= 0 || !bitmaps) {
    *n_results = 0;
    return nullptr;
  }

  int nwords = (n_bitmaps + 63) / 64;
  if (nwords < 1)
    nwords = 1;

  // Build iterators and min-heap
  roaring::api::roaring_uint32_iterator_t **iters =
      (roaring::api::roaring_uint32_iterator_t **)calloc(
          n_bitmaps, sizeof(roaring::api::roaring_uint32_iterator_t *));
  KMergNode *heap = (KMergNode *)malloc(n_bitmaps * sizeof(KMergNode));
  int heap_size = 0;

  for (int i = 0; i < n_bitmaps; i++) {
    if (!bitmaps[i])
      continue;
    roaring::api::roaring_uint32_iterator_t *it =
        roaring::api::roaring_create_iterator(bitmaps[i]);
    iters[i] = it;
    if (it->has_value) {
      heap[heap_size].element_idx = i;
      heap[heap_size].value.has_value = true;
      heap[heap_size].value.value = it->current_value;
      heap_size++;
    }
  }

  if (heap_size > 1)
    heap_heapify(heap, heap_size);

  // K-merge: group elements by source-set bitmask
  KMGroupHT ht;
  kmg_ht_init(&ht, nwords, KMGROUPS_HT_INIT_CAP);

  // Scratch buffer for building bitmask each iteration
  uint64_t *bitmask_buf = (uint64_t *)calloc(nwords, sizeof(uint64_t));

  while (heap_size > 0) {
    uint32_t current_val = heap[0].value.value;
    memset(bitmask_buf, 0, nwords * sizeof(uint64_t));

    do {
      int src = heap[0].element_idx;
      bitmask_buf[src / 64] |= ((uint64_t)1) << (src % 64);

      roaring::api::roaring_uint32_iterator_t *it = iters[src];
      roaring::api::roaring_advance_uint32_iterator(it);
      if (it->has_value) {
        heap[0].value.value = it->current_value;
        heap_sift_down(heap, heap_size, 0);
      } else {
        heap[0] = heap[heap_size - 1];
        heap_size--;
        if (heap_size > 0)
          heap_sift_down(heap, heap_size, 0);
      }
    } while (heap_size > 0 && heap[0].value.value == current_val);

    KMGroupSlot slot = kmg_ht_get_or_create(&ht, bitmask_buf);
    roaring::api::roaring_bitmap_add(slot.members, current_val);
  }

  free(bitmask_buf);

  // Free iterators
  for (int i = 0; i < n_bitmaps; i++) {
    if (iters[i])
      roaring::api::roaring_free_uint32_iterator(iters[i]);
  }
  free(iters);
  free(heap);

  // Collect results from hash table
  KMGroupResult *results =
      (KMGroupResult *)malloc(ht.count * sizeof(KMGroupResult));
  int ridx = 0;
  for (int i = 0; i < ht.capacity; i++) {
    if (!kmg_slot_empty(&ht, i)) {
      results[ridx].nwords = nwords;
      results[ridx].words = (uint64_t *)malloc(nwords * sizeof(uint64_t));
      memcpy(results[ridx].words, kmg_slot_words(&ht, i),
             nwords * sizeof(uint64_t));
      results[ridx].members = *kmg_slot_members_ptr(&ht, i);
      ridx++;
    }
  }
  free(ht.data);

  // Sort by bitmask for deterministic output
  nwords_for_cmp = nwords;
  qsort(results, ht.count, sizeof(KMGroupResult), kmgroup_cmp);

  *n_results = ht.count;
  return results;
}

void rb_kmerge_groups_free(KMGroupResult *results, int n_results) {
  if (!results)
    return;
  for (int i = 0; i < n_results; i++) {
    free(results[i].words);
    roaring::api::roaring_bitmap_free(results[i].members);
  }
  free(results);
}

// ============================================================
// slab_trie implementation
// ============================================================

size_t length_of_key(size_t key) {
  size_t len = 0;
  while (key > 0) {
    key >>= 8;
    len++;
  }
  return len;
}

rb_kmerge_slab_trie_result_t *
rb_kmerge_groups_slab_trie(roaring::api::roaring_bitmap_t **bitmaps,
                           size_t n_bitmaps, size_t *n_results) {
  if (!bitmaps) {
    *n_results = 0;
    return nullptr;
  }

  slab_trie_node_t root = {};
  slab_trie_iter_t iter = {};

  // Build iterators and min-heap
  roaring::api::roaring_uint32_iterator_t **iters =
      (roaring::api::roaring_uint32_iterator_t **)calloc(
          n_bitmaps, sizeof(roaring::api::roaring_uint32_iterator_t *));

  KMergNode *heap = (KMergNode *)malloc(n_bitmaps * sizeof(KMergNode));
  int heap_size = 0;

  for (int i = 0; i < n_bitmaps; i++) {
    if (!bitmaps[i])
      continue;
    roaring::api::roaring_uint32_iterator_t *it =
        roaring::api::roaring_create_iterator(bitmaps[i]);
    iters[i] = it;
    if (it->has_value) {
      heap[heap_size].element_idx = i;
      heap[heap_size].value.has_value = true;
      heap[heap_size].value.value = it->current_value;
      heap_size++;
    }
  }

  if (heap_size > 1) {
    heap_heapify(heap, heap_size);
  }

  // K-merge: insert elements into slab trie

  while (heap_size > 0) {
    KMergNode *heap_entry = &heap[0];
    size_t current_index = heap_entry->element_idx;
    uint32_t current_val = heap[0].value.value;

    do {
      size_t src = heap[0].element_idx;
      current_index = heap[0].element_idx;

      roaring::api::roaring_uint32_iterator_t *it = iters[src];
      roaring::api::roaring_advance_uint32_iterator(it);
      if (it->has_value) {
        heap[0].value.value = it->current_value;
        heap_sift_down(heap, heap_size, 0);
      } else {
        heap[0] = heap[heap_size - 1];
        heap_size--;
        if (heap_size > 0)
          heap_sift_down(heap, heap_size, 0);
      }
    } while (heap_size > 0 && heap[0].value.value == current_val);

    uint8_t key_buf[sizeof(size_t)];
    memcpy(key_buf, &current_index, sizeof(key_buf));

    slab_trie_entry_t *trie_entry =
        slab_trie_get_or_create(&root, key_buf, length_of_key(current_index));

    if (!trie_entry->data) {
      trie_entry->data = roaring::api::roaring_bitmap_create();
    }

    roaring::api::roaring_bitmap_add(
        (roaring::api::roaring_bitmap_t *)trie_entry->data, current_val);
  }

  return nullptr;
}
