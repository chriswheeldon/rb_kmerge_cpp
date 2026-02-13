#include "rb_kmerge.h"
#include <algorithm>

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
