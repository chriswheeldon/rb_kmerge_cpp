
#include "rb_kmerge.h"
#include "roaring.hh" // the amalgamated roaring.hh includes roaring64map.hh

#include <chrono>
#include <iostream>
#include <random>
#include <vector>

int main(int argc, char *argv[]) {
  auto num_bitmaps = std::stoi(argv[1]);
  auto num_values = std::stoi(argv[2]);

  std::random_device rd;  // a seed source for the random number engine
  std::mt19937 gen(rd()); // mersenne_twister_engine seeded with rd()
  std::uniform_int_distribution<> distrib(1, num_values * 5);
  std::vector<roaring::Roaring> bitmaps;
  for (int i = 0; i < num_bitmaps; i++) {
    roaring::Roaring r{};
    for (int j = 0; j < num_values; j++) {
      auto v = distrib(gen);
      r.add(v);
    }
    bitmaps.push_back(r);
  }

  auto start = std::chrono::high_resolution_clock::now();

  auto range = rb_kmerge(bitmaps);
  for (const auto &entry : range) {
    continue;
  }

  auto end = std::chrono::high_resolution_clock::now();
  std::cout << "Time taken: "
            << std::chrono::duration_cast<std::chrono::milliseconds>(end -
                                                                     start)
                   .count()
            << "ms" << std::endl;
  return 0;
}