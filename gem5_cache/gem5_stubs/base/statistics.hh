#ifndef __BASE_STATISTICS_HH__
#define __BASE_STATISTICS_HH__

#include <string>
#include <cstdint>
#include <cstdio>
#include <vector>
#include <map>
#include <functional>
#include "base/types.hh"

namespace gem5
{

namespace statistics
{

class Info { public: virtual ~Info() = default; };
class ScalarBase : public Info {};
class VectorBase : public Info {};

class Scalar : public ScalarBase {
  private:
    Counter _value = 0;
    std::string _name;
  public:
    Scalar() {}
    Scalar(const std::string &n) : _name(n) {}
    Scalar(const Scalar&) = delete;
    void name(const std::string &n) { _name = n; }
    const std::string &name() const { return _name; }
    void set(Counter v) { _value = v; }
    Counter value() const { return _value; }
    void operator++() { ++_value; }
    void operator++(int) { _value++; }
    void operator--() { --_value; }
    void operator--(int) { _value--; }
    void operator+=(Counter v) { _value += v; }
    void operator-=(Counter v) { _value -= v; }
    void operator=(Counter v) { _value = v; }
    Scalar& flags(int) { return *this; }
};

class Vector : public VectorBase {
  private:
    std::vector<Counter> _values;
    std::string _name;
    size_t _size = 1;
  public:
    Vector() {}
    Vector(const std::string &n) : _name(n) {}
    Vector(const Vector&) = delete;
    void name(const std::string &n) { _name = n; }
    const std::string &name() const { return _name; }
    Vector& init(size_t size) { _size = size; _values.resize(size, 0); return *this; }
    size_t size() const { return _size; }
    Counter operator[](int idx) const { return _values[idx]; }
    Counter& operator[](int idx) { return _values[idx]; }
    Vector& subname(int, const std::string&) { return *this; }
    Vector& subdesc(int, const std::string&) { return *this; }
    Counter total() const { Counter t = 0; for (auto v : _values) t += v; return t; }
    void operator++() { for (auto& v : _values) ++v; }
    void operator++(int) { for (auto& v : _values) v++; }
    void operator--() { for (auto& v : _values) --v; }
    void operator--(int) { for (auto& v : _values) v--; }
    void operator+=(Counter v) { for (auto& vv : _values) vv += v; }
    void operator-=(Counter v) { for (auto& vv : _values) vv -= v; }
    Vector& flags(int) { return *this; }
    Counter operator+(const Vector &o) const {
        Counter t = total(); for (size_t i = 0; i < o.size(); i++) t += o[i]; return t;
    }
};

class ValueBase {};
class Value : public ValueBase {
  private: double _value = 0; std::string _name;
  public:
    Value() {}
    void name(const std::string &n) { _name = n; }
    const std::string &name() const { return _name; }
    void set(double v) { _value = v; }
    double value() const { return _value; }
    Value& flags(int) { return *this; }
};

class Distribution {};
class Histogram : public Distribution {};
class Average {
    double _v = 0;
    uint64_t _c = 0;
    std::string _name;
  public:
    Average() {}
    Average(const std::string &n) : _name(n) {}
    void name(const std::string &n) { _name = n; }
    const std::string &name() const { return _name; }
    void operator++() { _c++; _v = _v * 0.9; }
    void operator++(int) { _c++; }
    void operator--() { _c--; }
    void operator--(int) { _c--; }
    void operator+=(double v) { _v += v; _c++; }
    double value() const { return _c > 0 ? _v / _c : 0.0; }
};
class StandardDeviation : public Average {};
class AverageDeviation {};
class VectorDistribution {};
class VectorStandardDeviation {};
class VectorAverage {};

class AverageVector {
  private:
    std::vector<Counter> _values;
    std::string _name;
    size_t _size = 1;
  public:
    AverageVector() {}
    AverageVector(const std::string &n) : _name(n) {}
    void name(const std::string &n) { _name = n; }
    AverageVector& init(size_t size) { _size = size; _values.resize(size, 0); return *this; }
    size_t size() const { return _size; }
    Counter& operator[](int idx) { return _values[idx]; }
    Counter operator[](int idx) const { return _values[idx]; }
    AverageVector& subname(int, const std::string&) { return *this; }
    Counter total() const { Counter t = 0; for (auto v : _values) t += v; return t; }
    void operator++() { for (auto& v : _values) ++v; }
    void operator--() { for (auto& v : _values) --v; }
    void operator+=(Counter v) { for (auto& vv : _values) vv += v; }
    AverageVector& flags(int) { return *this; }
};

class Vector2d {
  private:
    std::vector<std::vector<Counter>> _values;
    std::string _name;
  public:
    Vector2d() {}
    Vector2d(const std::string &n) : _name(n) {}
    Vector2d(const Vector2d&) = delete;
    void name(const std::string &n) { _name = n; }
    Vector2d& init(size_t x, size_t y) { _values.resize(x, std::vector<Counter>(y, 0)); return *this; }
    void subname(int, const std::string&) {}
    std::vector<Counter>& operator[](int idx) { return _values[idx]; }
    Vector2d& flags(int) { return *this; }
};

class FormulaBase {};
class Formula : public FormulaBase {
  public:
    Formula() {}
    Formula(const std::string &n) {}
    Formula flags(int) { return *this; }
    Formula& subname(int, const std::string&) { return *this; }
    template <typename T>
    Formula& operator=(const T&) { return *this; }
};

class Group
{
  private:
    std::vector<Info*> stats;
  protected:
    Group* mergedParent = nullptr;
  public:
    Group() = default;
    Group(const Group&) = delete;
    Group(Group *parent) : mergedParent(parent) {}
    Group(Group *parent, const char *name) : mergedParent(parent) {}
    Group& operator=(const Group&) = delete;
    virtual ~Group() = default;
    virtual void regStats() {}
    virtual void resetStats() {}
    virtual void preDumpStats() {}
    void mergeStatGroup(Group *block) {}
    void addStat(Info *info) {}
    template <typename T> Scalar& addScalar(T& s, const std::string &name) {
        s.name(name); return s;
    }
    template <typename T> Vector& addVector(T& v, const std::string &name) {
        v.name(name); return v;
    }
    template <typename T> Value& addValue(T& v, const std::string &name) {
        v.name(name); return v;
    }
    template <typename T> Distribution& addDistribution(T& d, const std::string &name) {
        return d;
    }
    template <typename T> Average& addAverage(T& v, const std::string &name) {
        return v;
    }
    template <typename T> AverageVector& addAverageVector(T& v, const std::string &name) {
        v.name(name); return v;
    }
    template <typename T> Vector2d& addVector2d(T& v, const std::string &name) {
        v.name(name); return v;
    }
};

// Stat flags
static constexpr int nozero = 0x01;
static constexpr int nonan = 0x02;
static constexpr int total = 0x04;

// Unit types
namespace units {

struct Tick {
    static const char *get() { return "Tick"; }
};
struct Count {
    static const char *get() { return "Count"; }
};
struct Ratio {
    static const char *get() { return "Ratio"; }
};
struct Second {
    static const char *get() { return "s"; }
};
template <typename U, typename V>
struct Rate {
    static const char *get() { return "Rate"; }
};

} // namespace units

// Helper for formula division
struct Constant {
    double val;
    Constant(double v) : val(v) {}
};

inline Constant constant(double v) { return Constant(v); }

// Division operator for Formula assignment
template <typename T>
Formula operator/(const Scalar& a, const T& b) {
    return Formula();
}
template <typename T1, typename T2>
Formula operator/(const T1& a, const T2& b) {
    return Formula();
}
// Addition operators for Formula assignment patterns
inline Counter operator+(Counter c, const Vector &v) {
    Counter t = c; for (size_t i = 0; i < v.size(); i++) t += v[i]; return t;
}
inline Formula operator+(const Formula &f, Counter c) { return Formula(); }
inline Formula operator+(Counter c, const Formula &f) { return Formula(); }
inline Formula operator+(const Formula &a, const Formula &b) { return Formula(); }

} // namespace statistics
} // namespace gem5

#define ADD_STAT(n, ...) n(#n)

#endif
