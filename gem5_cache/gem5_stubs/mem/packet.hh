#ifndef __MEM_PACKET_HH__
#define __MEM_PACKET_HH__

#include <cstdint>
#include <functional>
#include <list>
#include <memory>
#include "base/printable.hh"
#include "base/types.hh"
#include "mem/request.hh"
#include "sim/cur_tick.hh"

namespace gem5 {

class Packet;
typedef Packet *PacketPtr;
typedef std::shared_ptr<Packet> PacketSPtr;
typedef std::list<PacketPtr> PacketList;
typedef uint64_t PacketId;
class AtomicOpFunctor {
  public:
    virtual ~AtomicOpFunctor() = default;
    virtual void operator()(uint8_t *p) {}
};
typedef AtomicOpFunctor* AtomicOpFunc;

struct MemCmd
{
    enum Command { ReadReq, WriteReq, ReadResp, WriteResp, WriteLineReq,
                   UpgradeReq, UpgradeResp, InvalidateReq, InvalidateResp,
                   WritebackDirty, CleanEvict, SoftPFReq, HardPFReq,
                   WritebackClean, StoreCondReq, WriteClean, ReadExReq,
                   SoftPFExReq,
                   SCUpgradeReq, SCUpgradeFailReq, StoreCondFailReq,
                   ReadRespWithInvalidate, LockedRMWReadReq,
                   ReadCleanReq, ReadSharedReq, HardPFResp,
                   LockedRMWWriteReq, LockedRMWWriteResp,
                   UpgradeFailResp, SwapReq,
                   InvalidCmd };
    static constexpr int NUM_MEM_CMDS = InvalidCmd;
    Command cmd = InvalidCmd;
    MemCmd() = default;
    MemCmd(Command c) : cmd(c) {}
    bool isRead() const { return cmd == ReadReq || cmd == ReadResp ||
                                 cmd == ReadCleanReq || cmd == ReadSharedReq; }
    bool isWrite() const { return cmd == WriteReq || cmd == WriteResp ||
                                  cmd == WriteLineReq || cmd == WriteClean; }
    bool isRequest() const { return cmd == ReadReq || cmd == WriteReq ||
                                    cmd == WriteLineReq || cmd == WriteClean; }
    bool isResponse() const { return !isRequest(); }
    bool needsResponse() const { return isRequest(); }
    bool needsWritable() const { return false; }
    bool isPrefetch() const { return cmd == SoftPFReq || cmd == HardPFReq; }
    bool isLLSC() const { return cmd == StoreCondReq ||
                                 cmd == SCUpgradeReq ||
                                 cmd == SCUpgradeFailReq; }
    bool isSWPrefetch() const { return cmd == SoftPFReq; }
    MemCmd(int idx) : cmd(static_cast<Command>(idx)) {}
    bool operator==(const Command &c) const { return cmd == c; }
    bool operator!=(const Command &c) const { return cmd != c; }
    operator Command() const { return cmd; }
    MemCmd& operator=(const Command &c) { cmd = c; return *this; }
    const char *toString() const { return "MemCmd"; }
};

class Packet : public Printable
{
  public:
    class SenderState {
      public:
        virtual ~SenderState() = default;
    };

    typedef uint32_t FlagsType;
    static const FlagsType SHARED         = 0x0001;
    static const FlagsType EXCLUSIVE      = 0x0002;
    static const FlagsType HAS_DATA       = 0x0004;
    static const FlagsType CACHE_LINE_FILL = 0x0008;
    static const FlagsType EVICT_NEEDS_WRITEBACK = 0x0010;
    static const FlagsType SATISFIED      = 0x0020;
    static const FlagsType FAILED         = 0x0040;
    static const FlagsType EXPRESS_SNOOP  = 0x0080;
    static const FlagsType RESPONDER_HAD_WRITEBACK_IN_CACHE = 0x0100;

    PacketId id = 0;
    Tick headerDelay = 0;
    Tick payloadDelay = 0;
    uint32_t snoopDelay = 0;

    Packet() {}
    Packet(const RequestPtr &_req, MemCmd::Command _cmd)
        : cmd(_cmd), req(_req) {}
    Packet(const RequestPtr &_req, MemCmd::Command _cmd,
           unsigned _size, PacketId _id)
        : cmd(_cmd), size(_size), req(_req), id(_id) {}
    Packet(const RequestPtr &_req, MemCmd::Command _cmd, unsigned _size)
        : cmd(_cmd), size(_size), req(_req) {}
    Packet(const PacketPtr &pkt, bool a, bool b) : cmd(pkt->cmd),
        addr(pkt->addr), size(pkt->size), data(pkt->data), req(pkt->req) {}

    Packet(const PacketPtr &pkt, bool a, bool b, bool c)
        : cmd(pkt->cmd), addr(pkt->addr), size(pkt->size),
          data(pkt->data), req(pkt->req) {}
    Packet(const Packet &other)
        : cmd(other.cmd), addr(other.addr), size(other.size),
          data(other.data), req(other.req) {}

    Addr getAddr() const { return addr; }
    unsigned getSize() const { return size; }
    const uint8_t *getConstPtr() const { return data; }
    template <typename T>
    const T *getConstPtr() const { return reinterpret_cast<const T*>(data); }
    uint8_t *getPtr() { return data; }
    void setData(const uint8_t *d) { data = const_cast<uint8_t*>(d); }
    void allocate() {}
    bool isRead() const { return cmd.isRead(); }
    bool isWrite() const { return cmd.isWrite(); }
    bool isRequest() const { return cmd.isRequest(); }
    bool isResponse() const { return cmd.isResponse(); }
    bool isEviction() const { return false; }
    bool needsResponse() const { return cmd.needsResponse(); }
    bool isCacheFill() const { return false; }
    bool isCleanEviction() const { return false; }
    bool needsWritable() const { return false; }
    bool hasSharers() const { return false; }
    void setHasSharers() {}
    void setResponderHadWritable() {}
    MemCmd cmd;
    Addr addr = 0;
    unsigned size = 0;
    uint8_t *data = nullptr;
    RequestPtr req;
    RequestorID requestorId() const { return req ? req->requestorId() : 0; }
    void print(std::ostream &o, int verbosity = 0,
               const std::string &prefix = "") const override {}
    std::string print() const { return ""; }
    void makeResponse() {}
    void makeTimingResponse() {}
    void makeAtomicResponse() {}
    void setCacheResponding() {}
    bool cacheResponding() const { return false; }
    bool hasData() const { return data != nullptr; }
    bool isSecure() const { return false; }
    bool isLLSC() const { return false; }
    bool isUpgrade() const { return false; }
    bool needsExclusive() const { return false; }
    void setDataFromBlock(const uint8_t *blk_data, int blkSize) {}
    bool isInvalidate() const { return false; }
    bool isWriteback() const { return false; }
    void setExpressSnoop(bool v) {}
    void setExpressSnoop() { setExpressSnoop(true); }
    bool isExpressSnoop() const { return false; }
    bool satisfied() const { return true; }
    void setSatisfied() {}
    bool failed() const { return false; }
    bool matchAddr(PacketPtr pkt) const { return addr == pkt->addr; }
    bool matchBlockAddr(PacketPtr pkt, unsigned blk_size) const {
        return (addr & ~(Addr)(blk_size - 1)) == (pkt->addr & ~(Addr)(blk_size - 1));
    }
    bool matchBlockAddr(Addr addr, bool is_secure, unsigned blk_size) const {
        return (this->addr & ~(Addr)(blk_size - 1)) == (addr & ~(Addr)(blk_size - 1));
    }
    bool isClean() const { return false; }
    void pushLabel(const std::string &) {}
    void popLabel() {}
    SenderState *senderState = nullptr;
    template <typename T>
    T *findNextSenderState() const { return static_cast<T*>(senderState); }
    int cmdToIndex() const { return cmd; }
    Addr getBlockAddr(unsigned blkSize) const {
        return addr & ~(Addr)(blkSize - 1);
    }
    Addr getOffset(unsigned blkSize) const { return addr & (blkSize - 1); }
    bool fromCache() const { return false; }
    bool isMaskedWrite() const { return false; }
    bool hasRespData() const { return false; }
    bool isPrint() const { return false; }
    bool trySatisfyFunctional(PacketPtr pkt) { return false; }
    bool trySatisfyFunctional(Printable *obj, Addr addr, bool is_secure,
                              unsigned blk_size, uint8_t *data) { return false; }
    void setBlockCached() {}
    bool isCleanInvalidateRequest() const { return false; }
    bool isWholeLineWrite(unsigned blkSize) const { return false; }
    bool isError() const { return false; }
    void copyError(PacketPtr pkt) {}
    bool isLockedRMW() const { return false; }
    bool responderHadWritable() const { return false; }
    bool isDemand() const { return false; }
    SenderState *popSenderState() {
        SenderState *old = senderState;
        senderState = nullptr;
        return old;
    }
    void copyResponderFlags(PacketPtr pkt) {}
    bool isBlockCached() const { return false; }
    bool mustCheckAbove() const { return false; }
    void writeDataToBlock(uint8_t *data, int blkSize) {}
    void writeData(uint8_t *p) {}
    void dataStatic(uint8_t *p) {}
    bool isAtomicOp() const { return false; }
    AtomicOpFunc getAtomicOp() const { return nullptr; }
    void clearBlockCached() {}
    bool writeThrough() const { return false; }
    void setWriteThrough() {}
    void pushSenderState(SenderState *s) {}
};

}
#endif
