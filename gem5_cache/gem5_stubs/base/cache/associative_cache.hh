#ifndef __BASE_CACHE_ASSOCIATIVE_CACHE_HH__
#define __BASE_CACHE_ASSOCIATIVE_CACHE_HH__
#include <unordered_map>
namespace gem5 {
template <typename K, typename V> class AssociativeCache {
    std::unordered_map<K, V> map;
    size_t _max;
  public:
    AssociativeCache(size_t max) : _max(max) {}
    V* find(const K& k) { auto it = map.find(k); return it != map.end() ? &it->second : nullptr; }
    void insert(const K& k, const V& v) { map[k] = v; }
};
}
#endif
