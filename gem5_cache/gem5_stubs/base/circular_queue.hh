#ifndef __BASE_CIRCULAR_QUEUE_HH__
#define __BASE_CIRCULAR_QUEUE_HH__
#include <vector>
namespace gem5 {
template <typename T> class CircularQueue {
    std::vector<T> data;
    size_t _size = 0, _idx = 0;
  public:
    CircularQueue(size_t s) : data(s), _size(s) {}
    T& operator[](size_t i) { return data[i]; }
    const T& operator[](size_t i) const { return data[i]; }
    void advance() { _idx = (_idx + 1) % _size; }
    size_t size() const { return _size; }
    size_t idx() const { return _idx; }
};
}
#endif
