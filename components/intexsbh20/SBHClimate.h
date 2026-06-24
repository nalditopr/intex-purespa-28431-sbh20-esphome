#ifndef SBHCLIMATE_H_
#define SBHCLIMATE_H_
#include "esphome/components/climate/climate.h"
#include "intexsbh20.h"

namespace esphome {
namespace sbh20 {

class SBHClimate : public esphome::climate::Climate, public esphome::Parented<esphome::sbh20::IntexSBH20>
{
public:
	void update();
protected:
	virtual void control(const esphome::climate::ClimateCall &call) override;
	virtual esphome::climate::ClimateTraits traits() override;
private:
	uint32_t last_target_read_ms_ = 0;
	static const uint32_t TARGET_READ_BACKOFF_MS = 30000; // ms between setpoint-read attempts
};

}}
#endif
