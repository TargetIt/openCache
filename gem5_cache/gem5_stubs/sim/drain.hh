#ifndef __SIM_DRAIN_HH__
#define __SIM_DRAIN_HH__
namespace gem5 {
enum class DrainState { Running, Draining, Drained };
class Drainable {
  public:
    virtual ~Drainable() = default;
    virtual DrainState drain() { return DrainState::Drained; }
    virtual void drainResume() {}
    DrainState drainState() const { return _drainState; }
    void signalDrainDone() { _drainState = DrainState::Drained; }
  private:
    DrainState _drainState = DrainState::Running;
};
}
#endif
