#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <stdbool.h>

typedef struct {
  void *next;
  void *data;
} slab_trie_entry_t;

typedef struct {
  slab_trie_entry_t entries[256];
} slab_trie_node_t;

inline slab_trie_entry_t *slab_trie_lookup(slab_trie_node_t *node,
                                           const uint8_t *key, size_t key_len) {
  for (size_t i = 0; i < key_len - 1; i++) {
    if (!node->entries[key[i]].next) {
      return NULL;
    }
    node = (slab_trie_node_t *)node->entries[key[i]].next;
  }
  return &node->entries[key[key_len - 1]];
}

inline slab_trie_entry_t *slab_trie_get_or_create(slab_trie_node_t *node,
                                                  const uint8_t *key,
                                                  size_t key_len) {
  for (size_t i = 0; i < key_len - 1; i++) {
    if (!node->entries[key[i]].next) {
      node->entries[key[i]].next = calloc(1, sizeof(slab_trie_node_t));
    }
    node = (slab_trie_node_t *)node->entries[key[i]].next;
  }
  return &node->entries[key[key_len - 1]];
}

inline slab_trie_entry_t *slab_trie_insert(slab_trie_node_t *node,
                                           const uint8_t *key, size_t key_len,
                                           void *data) {
  for (size_t i = 0; i < key_len - 1; i++) {
    if (!node->entries[key[i]].next) {
      node->entries[key[i]].next = calloc(1, sizeof(slab_trie_node_t));
    }
    node = (slab_trie_node_t *)node->entries[key[i]].next;
  }
  node->entries[key[key_len - 1]].data = data;
  return &node->entries[key[key_len - 1]];
}

inline void slab_trie_dealloc(slab_trie_node_t *node) {
  for (size_t i = 0; i < 256; i++) {
    if (node->entries[i].next) {
      slab_trie_dealloc((slab_trie_node_t *)node->entries[i].next);
    }
  }
  free(node);
}

// iterator
typedef struct {
  slab_trie_node_t *node;
  int index; // current position in entries array, or -1 before first entry
} slab_trie_iter_frame_t;

typedef struct {
  slab_trie_node_t *root;
  slab_trie_iter_frame_t stack[256];
  uint8_t key[256];
  int depth;
  void *data;
  bool finished;
  bool
      need_descend; // if true, we need to descend into current entry's children
} slab_trie_iter_t;

// Forward declaration
inline void slab_trie_iter_next(slab_trie_iter_t *iter);

inline void slab_trie_iter_init(slab_trie_iter_t *iter,
                                slab_trie_node_t *root) {
  iter->root = root;
  iter->depth = -1;
  iter->data = NULL;
  iter->finished = false;
  iter->need_descend = false;

  if (root) {
    iter->depth = 0;
    iter->stack[0].node = root;
    iter->stack[0].index = -1; // Start before first entry
    slab_trie_iter_next(iter);
  } else {
    iter->finished = true;
  }
}

inline void slab_trie_iter_next(slab_trie_iter_t *iter) {
  if (iter->finished) {
    return;
  }

  // If we need to descend into a child from the previous iteration
  if (iter->need_descend) {
    slab_trie_iter_frame_t *frame = &iter->stack[iter->depth];
    slab_trie_entry_t *entry = &frame->node->entries[frame->index];

    if (entry->next) {
      iter->depth++;
      iter->stack[iter->depth].node = (slab_trie_node_t *)entry->next;
      iter->stack[iter->depth].index = -1;
    }
    iter->need_descend = false;
  }

  while (iter->depth >= 0) {
    slab_trie_iter_frame_t *frame = &iter->stack[iter->depth];
    frame->index++;

    // Search for the next entry with data or children
    while (frame->index < 256) {
      slab_trie_entry_t *entry = &frame->node->entries[frame->index];

      if (entry->data || entry->next) {
        iter->key[iter->depth] = (uint8_t)frame->index;

        if (entry->data) {
          iter->data = entry->data;
          iter->need_descend = (entry->next != NULL);
          return;
        }

        // No data but has children, descend immediately
        if (entry->next) {
          iter->depth++;
          iter->stack[iter->depth].node = (slab_trie_node_t *)entry->next;
          iter->stack[iter->depth].index = -1;
          break; // Continue at new depth
        }
      }

      frame->index++;
    }

    // If we exhausted this level, backtrack
    if (frame->index >= 256) {
      iter->depth--;
    }
  }

  iter->finished = true;
}

inline bool slab_trie_iter_has_next(slab_trie_iter_t *iter) {
  return !iter->finished;
}

inline void *slab_trie_iter_get_data(slab_trie_iter_t *iter) {
  return iter->data;
}

inline const uint8_t *slab_trie_iter_get_key(slab_trie_iter_t *iter) {
  return iter->key;
}

inline size_t slab_trie_iter_get_key_len(slab_trie_iter_t *iter) {
  return iter->depth + 1;
}
