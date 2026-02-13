
#include "roaring.c"
#include "roaring.hh" // the amalgamated roaring.hh includes roaring64map.hh

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <optional>
#include <random>
#include <vector>

struct BitmapIterators {
  roaring::RoaringSetBitForwardIterator iter;
  roaring::RoaringSetBitForwardIterator end;
};

auto cmp = [](const BitmapIterators &a, const BitmapIterators &b) {
  return *(a.iter) > *(b.iter);
};

void timed(const std::vector<BitmapIterators> &bitmaps,
           std::function<void(std::vector<BitmapIterators> &)> f) {

  auto cloned = bitmaps;
  auto start = std::chrono::high_resolution_clock::now();
  f(cloned);
  auto end = std::chrono::high_resolution_clock::now();
  std::cout << "Time taken: "
            << std::chrono::duration_cast<std::chrono::milliseconds>(end -
                                                                     start)
                   .count()
            << "ms" << std::endl;
}

void kmerge_iter(std::vector<BitmapIterators> &bitmaps) {
  std::optional<unsigned int> current;
  while (true) {
    if (bitmaps.empty()) {
      break;
    }

    current.reset();
    std::make_heap(bitmaps.begin(), bitmaps.end(), cmp);
    auto end_of_heap = bitmaps.end();

    while (true) {
      std::pop_heap(bitmaps.begin(), end_of_heap--, cmp);

      auto &r = *(end_of_heap);
      auto minimum = *r.iter;

      if (current && minimum > *current) {
        break;
      }
      current = minimum;

      if (++r.iter == r.end) {
        std::swap(*end_of_heap, bitmaps.back());
        bitmaps.pop_back();
      }

      if (bitmaps.empty() || end_of_heap == bitmaps.begin()) {
        break;
      }
    }
  }
}

void kmerge_iter_simple(std::vector<BitmapIterators> &bitmaps) {
  std::optional<uint32_t> current;
  while (true) {
    if (bitmaps.empty()) {
      break;
    }

    std::make_heap(bitmaps.begin(), bitmaps.end(), cmp);
    std::pop_heap(bitmaps.begin(), bitmaps.end(), cmp);

    auto &r = bitmaps.back();
    auto minimum = *(r.iter++);
    if (r.iter == r.end) {
      bitmaps.pop_back();
    }
  }
}

int main() {
  std::random_device rd;  // a seed source for the random number engine
  std::mt19937 gen(rd()); // mersenne_twister_engine seeded with rd()
  std::uniform_int_distribution<> distrib(1, 10);

  std::vector<roaring::Roaring> bitmaps;
  for (int i = 0; i < 2; i++) {
    roaring::Roaring r{};
    for (int j = 0; j < 3; j++) {
      auto v = distrib(gen);
      r.add(v);
      std::cout << v << " ";
    }
    bitmaps.push_back(r);
    std::cout << std::endl;
  }

  std::vector<BitmapIterators> iterators;
  std::transform(bitmaps.begin(), bitmaps.end(), std::back_inserter(iterators),
                 [](const roaring::Roaring &r) {
                   return BitmapIterators{r.begin(), r.end()};
                 });

  timed(iterators, kmerge_iter);
  timed(iterators, kmerge_iter_simple);
}