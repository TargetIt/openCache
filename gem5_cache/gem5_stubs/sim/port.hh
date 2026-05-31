#ifndef __SIM_PORT_HH__
#define __SIM_PORT_HH__
#include <string>
#include "base/addr_range.hh"
#include "base/types.hh"
namespace gem5 {

class Packet;
typedef Packet *PacketPtr;

class Port {
  public:
    Port() = default;
    Port(const std::string &n) : _portName(n) {}
    virtual ~Port() = default;
    virtual const std::string &name() const { return _portName; }
    bool isConnected() const { return true; }
  protected:
    std::string _portName;
};

class RequestPort : public Port {
  public:
    RequestPort() = default;
    RequestPort(const std::string &n) : Port(n) {}
    bool isSnooping() const { return false; }
    Tick sendAtomic(PacketPtr pkt) { return 0; }
    void schedTimingSnoopResp(PacketPtr pkt, Tick when) {}
    bool sendTimingReq(PacketPtr pkt) { return true; }
    bool trySatisfyFunctional(PacketPtr pkt) { return false; }
    void sendFunctional(PacketPtr pkt) {}
};

class ResponsePort : public Port {
  public:
    ResponsePort() = default;
    ResponsePort(const std::string &n) : Port(n) {}
    virtual bool recvTimingSnoopResp(PacketPtr pkt) { return false; }
    virtual bool tryTiming(PacketPtr pkt) { return false; }
    virtual bool recvTimingReq(PacketPtr pkt) { return false; }
    virtual Tick recvAtomic(PacketPtr pkt) { return 0; }
    virtual void recvFunctional(PacketPtr pkt) {}
    virtual AddrRangeList getAddrRanges() const { return AddrRangeList(); }
    void sendRetryReq() {}
    void sendRangeChange() {}
    void schedTimingResp(PacketPtr pkt, Tick when) {}
    bool isSnooping() const { return false; }
    bool trySatisfyFunctional(PacketPtr pkt) { return false; }
    void sendTimingSnoopReq(PacketPtr pkt) {}
    Tick sendAtomicSnoop(PacketPtr pkt) { return 0; }
    void sendFunctionalSnoop(PacketPtr pkt) {}
};
}
#endif
