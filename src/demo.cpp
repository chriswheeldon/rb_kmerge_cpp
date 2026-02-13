
#include "roaring.hh" // the amalgamated roaring.hh includes roaring64map.hh

#include <algorithm>
#include <chrono>
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

void timed(const std::vector<BitmapIterators> &iterators,
           std::function<void(std::vector<BitmapIterators> &)> f) {

  auto cloned = iterators;
  auto start = std::chrono::high_resolution_clock::now();
  f(cloned);
  auto end = std::chrono::high_resolution_clock::now();
  std::cout << "Time taken: "
            << std::chrono::duration_cast<std::chrono::milliseconds>(end -
                                                                     start)
                   .count()
            << "ms" << std::endl;
}

void kmerge_iter(std::vector<BitmapIterators> &iterators) {
  std::optional<unsigned int> current;
  while (true) {
    if (iterators.empty()) {
      break;
    }

    current.reset();
    std::make_heap(iterators.begin(), iterators.end(), cmp);
    auto end_of_heap = iterators.end();

    while (true) {
      std::pop_heap(iterators.begin(), end_of_heap--, cmp);

      auto &r = *(end_of_heap);
      auto minimum = *r.iter;

      if (current && minimum > *current) {
        break;
      }
      current = minimum;

      if (++r.iter == r.end) {
        std::swap(*end_of_heap, iterators.back());
        iterators.pop_back();
      }

      if (iterators.empty() || end_of_heap == iterators.begin()) {
        break;
      }
    }
  }
}

void kmerge_iter_simple(std::vector<BitmapIterators> &iterators) {
  while (true) {
    if (iterators.empty()) {
      break;
    }

    std::make_heap(iterators.begin(), iterators.end(), cmp);
    std::pop_heap(iterators.begin(), iterators.end(), cmp);

    auto &r = iterators.back();
    auto minimum = *(r.iter++);
    if (r.iter == r.end) {
      iterators.pop_back();
    }
  }
}

int main(int argc, char *argv[]) {
  auto num_bitmaps = std::stoi(argv[1]);
  auto num_values = std::stoi(argv[2]);

  std::random_device rd;  // a seed source for the random number engine
  std::mt19937 gen(rd()); // mersenne_twister_engine seeded with rd()
  std::uniform_int_distribution<> distrib(1, num_values * 3);
  std::vector<roaring::Roaring> bitmaps;
  for (int i = 0; i < num_bitmaps; i++) {
    roaring::Roaring r{};
    for (int j = 0; j < num_values; j++) {
      auto v = distrib(gen);
      r.add(v);
    }
    bitmaps.push_back(r);
  }

  std::vector<BitmapIterators> iterators;
  std::transform(bitmaps.begin(), bitmaps.end(), std::back_inserter(iterators),
                 [](const roaring::Roaring &r) {
                   return BitmapIterators{r.begin(), r.end()};
                 });

  timed(iterators, kmerge_iter);
  timed(iterators, kmerge_iter_simple);
}