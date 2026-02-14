#include "rb_kmerge_c.h"
#include "rb_kmerge.h"
#include "roaring.hh"

extern "C" {

// Holds deserialized C++ bitmaps
struct rb_kmerge_bitmaps {
  std::vector<roaring::Roaring> bitmaps;
};

// Wrapper that holds the C++ iterator
struct rb_kmerge_iter {
  MergeIterator current;
  MergeIterator end;
};

rb_kmerge_bitmaps *rb_kmerge_bitmaps_deserialize(
    const char **buffers, const size_t *buffer_sizes, size_t count) {
  if (!buffers || !buffer_sizes || count == 0) {
    return nullptr;
  }

  try {
    auto *result = new rb_kmerge_bitmaps;
    result->bitmaps.reserve(count);

    for (size_t i = 0; i < count; i++) {
      if (!buffers[i] || buffer_sizes[i] == 0) {
        delete result;
        return nullptr;
      }

      // Safely deserialize from buffer
      roaring::api::roaring_bitmap_t *bm =
          roaring::api::roaring_bitmap_portable_deserialize_safe(
              buffers[i], buffer_sizes[i]);

      if (!bm) {
        delete result;
        return nullptr;
      }

      // Transfer ownership to C++ Roaring object
      result->bitmaps.emplace_back(bm);
    }

    return result;
  } catch (...) {
    return nullptr;
  }
}

void rb_kmerge_bitmaps_free(rb_kmerge_bitmaps *bitmaps) { 
  delete bitmaps; 
}

rb_kmerge_iter *rb_kmerge_create(const rb_kmerge_bitmaps *bitmaps) {
  if (!bitmaps) {
    return nullptr;
  }

  try {
    // Create the iterator range
    auto range = rb_kmerge(bitmaps->bitmaps);

    // Allocate and initialize the wrapper
    auto *iter = new rb_kmerge_iter{range.begin(), range.end()};

    return iter;
  } catch (...) {
    return nullptr;
  }
}

bool rb_kmerge_has_next(const rb_kmerge_iter *iter) {
  return iter && iter->current != iter->end;
}

bool rb_kmerge_get_current(const rb_kmerge_iter *iter, rb_kmerge_entry *out) {
  if (!iter || !out || iter->current == iter->end) {
    return false;
  }
  
  try {
    const auto &entry = *iter->current;
    out->bitmap_index = entry.bitmap_index;
    out->value = entry.value;
    return true;
  } catch (...) {
    return false;
  }
}

void rb_kmerge_advance(rb_kmerge_iter *iter) {
  if (iter && iter->current != iter->end) {
    try {
      ++iter->current;
    } catch (...) {
      // If advance fails, set to end
      iter->current = iter->end;
    }
  }
}

void rb_kmerge_free(rb_kmerge_iter *iter) {
  delete iter;
}

} // extern "C"
