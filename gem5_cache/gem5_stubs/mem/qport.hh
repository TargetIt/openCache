#ifndef __MEM_QPORT_HH__
#define __MEM_QPORT_HH__
#include "mem/packet.hh"
#include "mem/packet_queue.hh"
#include "sim/port.hh"
namespace gem5 {
class QueuedResponsePort : public ResponsePort {
  public:
    QueuedResponsePort(const std::string &name) : ResponsePort(name) {}
    QueuedResponsePort(const std::string &name, PacketQueue &q) : ResponsePort(name) {}
    virtual ~QueuedResponsePort() = default;
};
static ReqPacketQueue _defaultReqQueue;

class QueuedRequestPort : public RequestPort {
  public:
    ReqPacketQueue &reqQueue;
    QueuedRequestPort(const std::string &name) : RequestPort(name),
        reqQueue(_defaultReqQueue) {}
    QueuedRequestPort(const std::string &name, ReqPacketQueue &req_q,
                      SnoopRespPacketQueue &snoop_q)
        : RequestPort(name), reqQueue(req_q) {}
    virtual ~QueuedRequestPort() = default;
};
}
#endif
