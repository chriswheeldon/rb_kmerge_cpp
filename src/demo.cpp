
#include "rb_kmerge.h"
#include "roaring.hh" // the amalgamated roaring.hh includes roaring64map.hh
#include "trie.h"

#include <chrono>
#include <cstdint>
#include <iostream>
#include <memory>
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

void commit_to_trie(TrieNode<size_t, std::unique_ptr<roaring::Roaring>> &node,
                    std::vector<bool>::iterator begin,
                    std::vector<bool>::iterator current,
                    std::vector<bool>::iterator end, unsigned int value) {
  auto it = std::find(current, end, true);
  if (it == end) {
    if (!node.value) {
      node.value = std::make_unique<roaring::Roaring>();
    }
    node.value->add(value);
    return;
  }

  size_t index = std::distance(begin, it);

  auto child_iter =
      std::find_if(node.children.begin(), node.children.end(),
                   [&](const auto &child) { return child->key == index; });

  if (child_iter == node.children.end()) {
    auto &child =
        node.addChild(std::move(index), std::unique_ptr<roaring::Roaring>{});
    commit_to_trie(child, begin, it + 1, end, value);
  } else {
    commit_to_trie(**child_iter, begin, it + 1, end, value);
  }
}

TrieNode<size_t, std::unique_ptr<roaring::Roaring>>
group_trie(const std::vector<roaring::Roaring> &bitmaps) {
  auto root = TrieNode<size_t, std::unique_ptr<roaring::Roaring>>(
      (size_t)SIZE_MAX, nullptr);

  unsigned int current_value = 0;
  std::vector<bool> current_group(bitmaps.size(), false);

  auto range = rb_kmerge(bitmaps);
  for (const auto &entry : range) {
    if (entry.value != current_value) {
      if (!current_group.empty()) {
        commit_to_trie(root, current_group.begin(), current_group.begin(),
                       current_group.end(), current_value);
      }
      current_value = entry.value;
      std::fill(current_group.begin(), current_group.end(), false);
    }
    current_group[entry.bitmap_index] = true;
  }
  if (!current_group.empty()) {
    commit_to_trie(root, current_group.begin(), current_group.begin(),
                   current_group.end(), current_value);
  }

  return root;
}

std::tuple<size_t, size_t> summarise_trie(
    const TrieNode<size_t, std::unique_ptr<roaring::Roaring>> &node) {
  // Count number of groups and number of trie nodes
  size_t count = 0;
  size_t nodes = 1;
  if (node.value) {
    count++;
  }
  for (const auto &child : node.children) {
    auto [child_count, child_nodes] = summarise_trie(*child);
    count += child_count;
    nodes += child_nodes;
  }
  return {count, nodes};
}

int main(int argc, char *argv[]) {
  auto num_bitmaps = std::stoi(argv[1]);
  auto num_values = std::stoi(argv[2]);
  auto spread = std::stoi(argv[3]);

  std::random_device rd;  // a seed source for the random number engine
  std::mt19937 gen(rd()); // mersenne_twister_engine seeded with rd()
  std::uniform_int_distribution<> distrib(0, 10);
  std::vector<roaring::Roaring> bitmaps(num_bitmaps * 11);
  for (int i = 0; i < num_values; i++) {
    for (int j = 0; j < num_bitmaps; j++) {
      auto score = distrib(gen);
      bitmaps[j * 11 + score].add(i);
    }
  }

  for (auto &bitmap : bitmaps) {
    bitmap.runOptimize();
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

  start = std::chrono::high_resolution_clock::now();

  auto trie = group_trie(bitmaps);
  auto [group_count, node_count] = summarise_trie(trie);
  std::cout << "Number of groups: " << group_count << std::endl;
  std::cout << "Number of trie nodes: " << node_count << std::endl;

  end = std::chrono::high_resolution_clock::now();
  std::cout << "Time taken: "
            << std::chrono::duration_cast<std::chrono::milliseconds>(end -
                                                                     start)
                   .count()
            << "ms" << std::endl;

  return 0;
}