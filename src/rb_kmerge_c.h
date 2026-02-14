#ifndef RB_KMERGE_C_H
#define RB_KMERGE_C_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// Include roaring C API
#include "roaring.h"

#ifdef __cplusplus
extern "C" {
#endif

// Opaque handle to deserialized bitmaps
typedef struct rb_kmerge_bitmaps rb_kmerge_bitmaps;

// Opaque handle to the iterator
typedef struct rb_kmerge_iter rb_kmerge_iter;

// Entry returned by the iterator
typedef struct {
  size_t bitmap_index;
  uint32_t value;
} rb_kmerge_entry;

// Deserialize bitmaps from portable serialized buffers
// buffers: array of pointers to serialized bitmap data
// buffer_sizes: array of buffer sizes (for safe deserialization)
// count: number of bitmaps
// Returns NULL on failure
rb_kmerge_bitmaps *rb_kmerge_bitmaps_deserialize(
    const char **buffers, const size_t *buffer_sizes, size_t count);

// Free the deserialized bitmaps
void rb_kmerge_bitmaps_free(rb_kmerge_bitmaps *bitmaps);

// Create iterator from deserialized bitmaps
rb_kmerge_iter *rb_kmerge_create(const rb_kmerge_bitmaps *bitmaps);

// Check if iterator has more elements (returns false when exhausted)
bool rb_kmerge_has_next(const rb_kmerge_iter *iter);

// Get current entry (returns false if exhausted)
bool rb_kmerge_get_current(const rb_kmerge_iter *iter, rb_kmerge_entry *out);

// Advance to next element
void rb_kmerge_advance(rb_kmerge_iter *iter);

// Free the iterator
void rb_kmerge_free(rb_kmerge_iter *iter);

#ifdef __cplusplus
}
#endif

#endif // RB_KMERGE_C_H
