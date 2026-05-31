#ifndef __MEM_REQUEST_HH__
#define __MEM_REQUEST_HH__
#include <cstdint>
#include <memory>
#include "base/types.hh"
#include "base/context_switch_task_id.hh"

namespace gem5 {

class Request;
typedef std::shared_ptr<Request> RequestPtr;
typedef uint32_t RequestorID;

class Request {
  public:
    typedef uint32_t FlagsType;
    typedef FlagsType Flags;
    static const FlagsType INST_FETCH = 0x1;
    static const FlagsType NO_ALLOCATE = 0x2;
    static const FlagsType STRICT_ORDER = 0x4;
    static const FlagsType SECURE = 0x8;
    static const FlagsType PRIVILEGED = 0x10;
    static const FlagsType NO_ACCESS = 0x20;
    static const FlagsType UNCACHEABLE = 0x40;
    static const FlagsType LLSC = 0x80;
    static const FlagsType MEM_SWAP = 0x100;
    static const FlagsType MEM_SWAP_COND = 0x200;
    static const FlagsType LOCKED_RMW = 0x400;
    static const FlagsType CACHE_MAINTENANCE = 0x1000;
    static const FlagsType CACHE_INVALIDATE = 0x2000;
    static const RequestorID invldRequestorId = UINT32_MAX;
    static const RequestorID wbRequestorId = UINT32_MAX - 1;
    static const RequestorID funcRequestorId = UINT32_MAX - 2;

    Request() {}
    Request(const Request &) = default;
    Request(Addr paddr, unsigned size, FlagsType flags, RequestorID id)
        : _paddr(paddr), _size(size) {}

    Addr getPaddr() const { return _paddr; }
    unsigned getSize() const { return _size; }
    RequestorID requestorId() const { return _requestorId; }
    bool isSecure() const { return false; }
    bool isInstFetch() const { return false; }
    void setFlags(FlagsType) {}
    FlagsType getFlags() const { return 0; }
    bool isRead() const { return true; }
    bool isWrite() const { return false; }
    bool hasContextId() const { return false; }
    uint32_t contextId() const { return 0; }
    bool isUncacheable() const { return false; }
    int taskId() const { return 0; }
    void taskId(unsigned t) { _taskId = t; }
    bool isToPOC() const { return false; }
    void setToPOC() {}
    bool isCacheClean() const { return false; }
    void setExtraData(uint64_t) {}
    uint64_t getExtraData() const { return 0; }
    bool hasPaddr() const { return true; }
    FlagsType getDest() const { return 0; }
    bool isCondSwap() const { return false; }
    bool hasPC() const { return false; }
    Addr getPC() const { return 0; }
    void incAccessDepth() {}
    bool isCacheMaintenance() const { return false; }
    bool isCacheInvalidate() const { return false; }

  private:
    Addr _paddr = 0;
    unsigned _size = 0;
    unsigned _taskId = 0;
    RequestorID _requestorId = 0;
};

}
#endif
