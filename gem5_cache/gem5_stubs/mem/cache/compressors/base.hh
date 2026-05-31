#ifndef __MEM_CACHE_COMPRESSORS_BASE_HH__
#define __MEM_CACHE_COMPRESSORS_BASE_HH__

#include <cstddef>
#include <memory>
#include "base/types.hh"
#include "mem/packet.hh"
#include "params/BaseCacheCompressor.hh"
#include "sim/sim_object.hh"

namespace gem5 {

class BaseCache;
class CacheBlk;

struct CompressionData {
    std::size_t getSizeBits() const { return 0; }
};

namespace compression {

class Base : public SimObject
{
  public:
    typedef BaseCacheCompressorParams Params;
    Base(const Params &p) : SimObject(p) {}
    virtual ~Base() = default;
    void setCache(BaseCache *_cache) { cache = _cache; }
    Cycles getDecompressionLatency(CacheBlk *blk) const { return Cycles(0); }
    std::shared_ptr<CompressionData> compress(
        const uint64_t *data, Cycles &comp_lat, Cycles &decomp_lat) {
        return std::make_shared<CompressionData>();
    }
    void setSizeBits(CacheBlk *blk, std::size_t size) {}
    void setDecompressionLatency(CacheBlk *blk, Cycles lat) {}
  protected:
    BaseCache *cache = nullptr;
};

} // namespace compression
} // namespace gem5

#endif // __MEM_CACHE_COMPRESSORS_BASE_HH__
