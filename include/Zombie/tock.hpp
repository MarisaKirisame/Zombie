#pragma once

#include <iostream>
#include <map>
#include <cassert>

#include "assert.hpp"

// start at 0.
// a tock pass whenever a Computer start execution, or a Zombie is created.
// we denote the start and end(open-close) of Computer execution as a tock_range.
// likewise, Zombie also has a tock_range with exactly 1 tock in it.
// One property of all the tock_range is, there is no overlapping.
// A tock_range might either be included by another tock_range,
// include that tock_range, or have no intersection.
// This allow a bunch of tock_range in a tree.
// From a tock, we can regain a Computer (to rerun function),
// or a Zombie, to reuse values.
// We use tock instead of e.g. pointer for indexing,
// because doing so allow a far more compact representation,
// and we use a single index instead of separate index, because Computer and Zombie relate -
// the outer tock_range node can does everything an inner tock_range node does,
// so one can replay that instead if the more fine grained option is absent.
struct Tock {
  int64_t tock;
  Tock() { }
  Tock(int64_t tock) : tock(tock) { }
  Tock& operator++() {
    ++tock;
    return *this;
  }
  Tock operator++(int) {
    return tock++;
  }
  Tock operator+(Tock rhs) const { return tock + rhs.tock; }
  Tock operator-(Tock rhs) const { return tock - rhs.tock; }
  bool operator!=(Tock rhs) const { return tock != rhs.tock; }
  bool operator==(Tock rhs) const { return tock == rhs.tock; }
  bool operator<(Tock rhs) const { return tock < rhs.tock; }
  bool operator<=(Tock rhs) const { return tock <= rhs.tock; }
  bool operator>(Tock rhs) const { return tock > rhs.tock; }
  bool operator>=(Tock rhs) const { return tock >= rhs.tock; }
};

template<>
struct std::numeric_limits<Tock> {
  static Tock min() {
    return std::numeric_limits<decltype(std::declval<Tock>().tock)>::min();
  }
  static Tock max() {
    return std::numeric_limits<decltype(std::declval<Tock>().tock)>::max();
  }
};

// open-close.
using tock_range = std::pair<Tock, Tock>;

template<typename K, typename V>
auto largest_value_le(const std::map<K, V>& m, const K& k) {
  auto it = m.upper_bound(k);
  if (it == m.begin()) {
    return m.end();
  } else {
    return --it;
  }
}

template<typename K, typename V>
auto largest_value_le(std::map<K, V>& m, const K& k) {
  auto it = m.upper_bound(k);
  if (it == m.begin()) {
    return m.end();
  } else {
    return --it;
  }
}

inline bool range_dominate(const tock_range& l, const tock_range& r) {
  return l.first <= r.first && l.second >= r.second;
}

// for now we dont allow empty range, but the code should hopefully work with them.
inline bool range_ok(const tock_range& r) {
  return r.first < r.second;
}

inline bool range_nointersect(const tock_range& l, const tock_range& r) {
  return l.second <= r.first;
}

template<typename V>
struct tock_tree;

template<typename T>
struct NotifyParentChanged {
  void operator()(T&, typename tock_tree<T>::Node*) { }
};

template<typename V>
struct tock_tree {
  struct Node {
    // nullptr iff toplevel
    Node* parent;
    tock_range range;
    V value;
    Node(Node* parent, const tock_range& range, const V& value) :
      parent(parent), range(range), value(value) { }
    Node(Node* parent, const tock_range& range, V&& value) :
      parent(parent), range(range), value(std::move(value)) { }
    std::map<Tock, Node> children;
    bool children_in_range(const Tock& t) const {
      auto it = largest_value_le(children, t);
      if (it == children.end()) {
        return false;
      } else {
        assert(it->second.range.first <= t);
        return t < it->second.range.second;
      }
    }
    const Node& get_shallow(const Tock& t) const {
      auto it = largest_value_le(children, t);
      assert(it != children.end());
      assert(it->second.range.first <= t);
      assert(t < it->second.range.second);
      return it->second;
    }
    Node& get_shallow(const Tock& t) {
      auto it = largest_value_le(children, t);
      assert(it != children.end());
      assert(it->second.range.first <= t);
      assert(t < it->second.range.second);
      return it->second;
    }
    const Node& get_node(const Tock& t) const {
      return children_in_range(t) ? get_shallow(t).get_node(t) : *this;
    }
    Node& get_node(const Tock& t) {
      return children_in_range(t) ? get_shallow(t).get_node(t) : *this;
    }
    // get the most precise range that contain t
    V get(const Tock& t) const {
      return get_node(t).value;
    }
    void delete_node() {
      // the root node is not for deletion.
      assert(parent != nullptr);
      std::map<Tock, Node>& insert_to = parent->children;
      for (auto it = children.begin(); it != children.end();) {
        auto nh = children.extract(it++);
        nh.mapped().parent = parent;
        NotifyParentChanged<V>()(nh.mapped().value, parent);
        insert_to.insert(std::move(nh));
      }
      parent->children.erase(range.first);
    }
    void check_invariant() const {
      std::optional<tock_range> prev_range;
      for (auto p : children) {
        const auto& curr_range = p.second.range;
        assert(range_ok(curr_range));
        assert(range_dominate(range, curr_range));
        if (prev_range.has_value()) {
          assert(range_nointersect(prev_range.value(), curr_range));
        }
        p.second.check_invariant();
        prev_range = curr_range;
      }
    }
  };
  Node n = Node(nullptr, tock_range(std::numeric_limits<Tock>::min(), std::numeric_limits<Tock>::max()), V());
  Node& get_node(const Tock& t) {
    return n.get_node(t);
  }
  const Node& get_node(const Tock& t) const {
    return n.get_node(t);
  }
  bool has_precise(const Tock& t) const {
    return get_node(t).range.first == t;
  }
  Node& get_precise_node(const Tock& t) {
    ASSERT(has_precise(t));
    return get_node(t);
  }
  const Node& get_precise_node(const Tock& t) const {
    ASSERT(has_precise(t));
    return get_node(t);
  }
  // get the most precise range that contain t
  V get(const Tock& t) const {
    return n.get(t);
  }
  void check_invariant() const {
    n.check_invariant();
  }
  Node& put(const tock_range& r, const V& v) {
    return put(r, v);
  }
  Node& put(const tock_range& r, V&& v) {
    Node& n = get_node(r.first);
    // disallow inserting the same node twice
    ASSERT(n.range.first != r.first);
    ASSERT(range_dominate(n.range, r));
    auto* inserted = &n.children;
    auto it = inserted->insert({r.first, Node(&n, r, std::move(v))}).first;
    Node& inserted_node = it->second;
    ++it;
    while (it != inserted->end() && range_dominate(r, it->second.range)) {
      auto nh = inserted->extract(it++);
      nh.mapped().parent = &inserted_node;
      inserted_node.children.insert(std::move(nh));
      NotifyParentChanged<V>()(nh.mapped().value, &inserted_node);
    }
    return inserted_node;
  }
};

template<typename V>
std::ostream& print(std::ostream& os, const typename tock_tree<V>::Node& n) {
  os << "Node " << n.value << " [" << n.range.first << ", " << n.range.second << ")" << " {" << std::endl;
  for (const auto& p : n.children) {
    print<V>(os, p.second);
  }
  return os << "}" << std::endl;
}

template<typename V>
std::ostream& operator<<(std::ostream& os, const tock_tree<V>& v) {
  print<V>(os, v.n);
  return os;
}
