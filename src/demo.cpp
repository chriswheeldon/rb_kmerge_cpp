
#include "rb_kmerge.h"
#include "roaring.hh" // the amalgamated roaring.hh includes roaring64map.hh
#include "trie.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <memory>
#include <random>
#include <unordered_map>
#include <vector>

struct TrieValue {
  roaring::Roaring bitmap;
  roaring::BulkContext bulk_ctx;
};

using RbKmergeTrieNode = TrieNode<size_t, std::unique_ptr<TrieValue>>;

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

void commit_to_trie(RbKmergeTrieNode &node, std::vector<bool>::iterator begin,
                    std::vector<bool>::iterator current,
                    std::vector<bool>::iterator end, unsigned int value) {
  auto it = std::find(current, end, true);
  if (it == end) {
    if (!node.value) {
      node.value = std::make_unique<TrieValue>();
    }
    node.value->bitmap.addBulk(node.value->bulk_ctx, value);
    return;
  }

  size_t index = std::distance(begin, it);

  // auto child_iter =
  //     std::find_if(node.children.begin(), node.children.end(),
  //                  [&](const auto &child) { return child->key == index; });

  // Use lower bound to get the child iterator since children are sorted by key
  auto child_iter =
      std::lower_bound(node.children.begin(), node.children.end(), index,
                       [](const std::unique_ptr<RbKmergeTrieNode> &child,
                          size_t idx) { return child->key < idx; });

  if (child_iter == node.children.end() || (*child_iter)->key != index) {
    auto &child = node.addChild(std::move(index), std::nullptr_t{});
    commit_to_trie(child, begin, it + 1, end, value);
  } else {
    commit_to_trie(**child_iter, begin, it + 1, end, value);
  }
}

RbKmergeTrieNode group_trie(const std::vector<roaring::Roaring> &bitmaps) {
  auto root = RbKmergeTrieNode((size_t)SIZE_MAX, nullptr);

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

std::tuple<size_t, size_t> summarise_trie(const RbKmergeTrieNode &node) {
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

  // auto g = group(bitmaps);
  // std::cout << "Number of groups: " << g.size() << std::endl;

  // auto end = std::chrono::high_resolution_clock::now();
  // std::cout << "Time taken: "
  //           << std::chrono::duration_cast<std::chrono::milliseconds>(end -
  //                                                                    start)
  //                  .count()
  //           << "ms" << std::endl;

  // start = std::chrono::high_resolution_clock::now();

  // auto trie = group_trie(bitmaps);
  // auto [group_count, node_count] = summarise_trie(trie);
  // std::cout << "Number of groups: " << group_count << std::endl;
  // std::cout << "Number of trie nodes: " << node_count << std::endl;

  // end = std::chrono::high_resolution_clock::now();
  // std::cout << "Time taken: "
  //           << std::chrono::duration_cast<std::chrono::milliseconds>(end -
  //                                                                    start)
  //                  .count()
  //           << "ms" << std::endl;

  // // Convert C++ Roaring bitmaps to C API bitmaps for rb_kmerge_groups
  std::vector<roaring::api::roaring_bitmap_t *> c_bitmaps;
  c_bitmaps.reserve(bitmaps.size());
  for (auto &bitmap : bitmaps) {
    c_bitmaps.push_back(&bitmap.roaring);
  }

  start = std::chrono::high_resolution_clock::now();

  int n_groups = 0;
  KMGroupResult *groups_result =
      rb_kmerge_groups(c_bitmaps.data(), c_bitmaps.size(), &n_groups);
  std::cout << "Number of groups: " << n_groups << std::endl;

  // Clean up
  rb_kmerge_groups_free(groups_result, n_groups);

  auto end = std::chrono::high_resolution_clock::now();
  std::cout << "Time taken: "
            << std::chrono::duration_cast<std::chrono::milliseconds>(end -
                                                                     start)
                   .count()
            << "ms" << std::endl;

  start = std::chrono::high_resolution_clock::now();

  KMGroupResult *groups_result_hashmap =
      rb_kmerge_groups_hashmap(c_bitmaps.data(), c_bitmaps.size(), &n_groups);

  std::cout << "Number of groups: " << n_groups << std::endl;

  end = std::chrono::high_resolution_clock::now();
  std::cout << "Time taken: "
            << std::chrono::duration_cast<std::chrono::milliseconds>(end -
                                                                     start)
                   .count()
            << "ms" << std::endl;

  return 0;
}