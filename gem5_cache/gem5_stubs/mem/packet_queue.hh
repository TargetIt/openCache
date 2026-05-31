#ifndef __MEM_PACKET_QUEUE_HH__
#define __MEM_PACKET_QUEUE_HH__
#include "mem/packet.hh"
namespace gem5 {
class PacketQueue {
  public:
    PacketQueue() = default;
    PacketQueue(PacketQueue &other) {}
    PacketQueue(const std::string &name, const std::string &label) {}
    virtual ~PacketQueue() = default;
    bool isFull() const { return false; }
    Tick deferredPacketReadyTime() const { return MaxTick; }
  protected:
    bool waitingOnRetry = false;
};

class ReqPacketQueue : public PacketQueue {
  public:
    ReqPacketQueue() = default;
    ReqPacketQueue(const std::string &name) : PacketQueue() {}
    ReqPacketQueue(const std::string &name, const std::string &label)
        : PacketQueue(name, label) {}
    template <typename A, typename B>
    ReqPacketQueue(A &a, B &b, const std::string &label)
        : PacketQueue() {}
    void schedSendEvent(Tick time) {}
};

class SnoopRespPacketQueue : public PacketQueue {
  public:
    SnoopRespPacketQueue() = default;
    SnoopRespPacketQueue(const std::string &name) : PacketQueue() {}
    template <typename A, typename B>
    SnoopRespPacketQueue(A &a, B &b, bool c, const std::string &label)
        : PacketQueue() {}
    bool checkConflict(PacketPtr pkt, unsigned blkSize) const { return false; }
};

class RespPacketQueue : public PacketQueue {
  public:
    RespPacketQueue() = default;
    RespPacketQueue(const std::string &name) : PacketQueue() {}
    RespPacketQueue(const std::string &name, const std::string &label)
        : PacketQueue(name, label) {}
    template <typename A, typename B>
    RespPacketQueue(A &a, B &b, bool c, const std::string &label)
        : PacketQueue() {}
};
}
#endif
