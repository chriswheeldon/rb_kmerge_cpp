#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iterator>
#include <vector>

#include "roaring.hh"

struct BitmapEntry {
  size_t bitmap_index;
  uint32_t value;
};

struct BitmapIterators {
  roaring::RoaringSetBitForwardIterator iter;
  roaring::RoaringSetBitForwardIterator end;
  size_t bitmap_index;
};

struct MergeIterator {
  using iterator_category = std::input_iterator_tag;
  using value_type = BitmapEntry;
  using pointer = BitmapEntry *;
  using reference = BitmapEntry &;
  using difference_type = std::ptrdiff_t;

  MergeIterator(); // Default constructor creates end iterator
  MergeIterator(const std::vector<roaring::Roaring> &bitmaps);

  reference operator*() const;
  pointer operator->() const;
  MergeIterator &operator++();
  MergeIterator operator++(int);

  friend bool operator==(const MergeIterator &a, const MergeIterator &b);
  friend bool operator!=(const MergeIterator &a, const MergeIterator &b);

private:
  std::vector<BitmapIterators> iterators;
  mutable BitmapEntry current_entry;
};

struct MergeRange {
  MergeIterator begin() const { return begin_; }
  MergeIterator end() const { return end_; }

  MergeIterator begin_;
  MergeIterator end_;
};

MergeRange rb_kmerge(const std::vector<roaring::Roaring> &bitmaps);

// rb_kmerge_groups: group elements by source-set pattern
// Given N bitmaps, performs a k-way merge and groups elements by which
// combination of input bitmaps contains them.

// Result entry from rb_kmerge_groups
struct KMGroupResult {
  uint64_t *words; // Bitmask of source indices (nwords uint64s)
  roaring::api::roaring_bitmap_t *members; // All elements with this source-set
  int nwords;                              // Number of uint64 words in bitmask
};

// Groups elements by source-set pattern
// Returns array of results and sets n_results to the count
// Caller is responsible for freeing the returned array and its contents using
// rb_kmerge_groups_free
KMGroupResult *rb_kmerge_groups(roaring::api::roaring_bitmap_t **bitmaps,
                                int n_bitmaps, int *n_results);

// Free the results from rb_kmerge_groups
void rb_kmerge_groups_free(KMGroupResult *results, int n_results);

typedef struct {
  uint8_t *key;
  roaring::api::roaring_bitmap_t *bitmap;
} rb_kmerge_slab_trie_result_t;

rb_kmerge_slab_trie_result_t *
rb_kmerge_groups_slab_trie(roaring::api::roaring_bitmap_t **bitmaps,
                           size_t n_bitmaps, size_t *n_results);