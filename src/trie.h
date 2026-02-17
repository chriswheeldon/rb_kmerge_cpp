#include <algorithm>
#include <memory>
#include <vector>

template <typename Key, typename Value> class TrieNode {
public:
  TrieNode(Key key, Value value) : key(key), value(std::move(value)) {}

  Value value;
  const Key key;
  std::vector<std::unique_ptr<TrieNode>> children;

  TrieNode &addChild(Key &&key, Value &&value) {
    // children.push_back(
    //     std::make_unique<TrieNode>(std::move(key), std::move(value)));

    // return *children.back();

    // Maintain a sorted vector of children for efficient lookup
    auto it = std::upper_bound(children.begin(), children.end(), key,
                               [](const Key &k, const std::unique_ptr<TrieNode> &child) { return k < child->key; });
    auto child = children.insert(
        it, std::make_unique<TrieNode>(std::move(key), std::move(value)));

    return **child;
  }
};