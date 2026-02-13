#include <cstddef>
#include <cstdint>
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