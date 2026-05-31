#ifndef __SIM_EVENTQ_HH__
#define __SIM_EVENTQ_HH__

#include <functional>
#include <string>
#include "base/types.hh"
#include "sim/serialize.hh"

namespace gem5 {

class EventQueue;

// Simplified Event class hierarchy
class EventBase
{
  public:
    enum FlagsType { PublicRead = 0x01, AutoDelete = 0x02 };
    enum Priority { Default_Pri = 50, Minimum_Pri = 100,
                    Delayed_Writeback_Pri = 0 };
    virtual ~EventBase() = default;
};

class Event : public EventBase
{
  public:
    Event(int priority = Default_Pri, unsigned flags = AutoDelete) {}
    virtual ~Event() = default;
    virtual void process() = 0;
    bool scheduled() const { return false; }
};

class EventFunctionWrapper : public Event
{
  private:
    std::function<void()> callback;
  public:
    EventFunctionWrapper(const std::function<void()> &cb, const std::string &name = "", int pri = Default_Pri)
        : Event(pri), callback(cb) {}
    EventFunctionWrapper(const std::function<void()> &cb, const std::string &name, bool, int pri)
        : Event(pri), callback(cb) {}
    void process() override { callback(); }
};

class GlobalEvent : public EventBase {};

class EventManager
{
  public:
    EventManager() {}
    virtual ~EventManager() = default;
    void schedule(Event &e, Tick when) {}
    void deschedule(Event &e) {}
    void reschedule(Event &e, Tick when) {}
};

} // namespace gem5
#endif
