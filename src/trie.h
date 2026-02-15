#include <memory>
#include <vector>

template <typename Key, typename Value> class TrieNode {
public:
  TrieNode(Key key, Value value) : key(key), value(std::move(value)) {}

  Value value;
  const Key key;
  std::vector<std::unique_ptr<TrieNode>> children;

  TrieNode &addChild(Key &&key, Value &&value) {
    children.push_back(
        std::make_unique<TrieNode>(std::move(key), std::move(value)));

    return *children.back();
  }
};