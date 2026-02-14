
#include "rb_kmerge.h"
#include "roaring.hh" // the amalgamated roaring.hh includes roaring64map.hh

#include <chrono>
#include <iostream>
#include <random>
#include <unordered_map>
#include <vector>

std::unordered_map<std::vector<bool>, roaring::Roaring>
group(const std::vector<roaring::Roaring> &bitmaps) {
  std::unordered_map<std::vector<bool>, roaring::Roaring> groups{};

  unsigned int current_value = 0;
  std::vector<bool> current_group(bitmaps.size(), false);

  auto range = rb_kmerge(bitmaps);
  for (const auto &entry : range) {
    if (entry.value != current_value) {
      if (!current_group.empty()) {
        if (groups.find(current_group) == groups.end()) {
          groups[current_group] = roaring::Roaring{};
        }
        groups[current_group].add(current_value);
      }
      current_value = entry.value;
      std::fill(current_group.begin(), current_group.end(), false);
    }
    current_group[entry.bitmap_index] = true;
  }
  if (!current_group.empty()) {
    if (groups.find(current_group) == groups.end()) {
      groups[current_group] = roaring::Roaring{};
    }
    groups[current_group].add(current_value);
  }

  return groups;
}

int main(int argc, char *argv[]) {
  auto num_bitmaps = std::stoi(argv[1]);
  auto num_values = std::stoi(argv[2]);
  auto spread = std::stoi(argv[3]);

  std::random_device rd;  // a seed source for the random number engine
  std::mt19937 gen(rd()); // mersenne_twister_engine seeded with rd()
  std::uniform_int_distribution<> distrib(1, num_values * spread);
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

  auto g = group(bitmaps);
  std::cout << "Number of groups: " << g.size() << std::endl;

  auto end = std::chrono::high_resolution_clock::now();
  std::cout << "Time taken: "
            << std::chrono::duration_cast<std::chrono::milliseconds>(end -
                                                                     start)
                   .count()
            << "ms" << std::endl;
  return 0;
}