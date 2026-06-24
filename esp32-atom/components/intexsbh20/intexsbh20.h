#ifndef INTEXSBH20_H_
#define INTEXSBH20_H_

#include "esphome/core/component.h"
#include "esphome/components/text_sensor/text_sensor.h"
#include "esphome/components/sensor/sensor.h"
#include "SBH20IO.h"

namespace esphome {
namespace sbh20 {

class SBHClimate;
class SBHSwitch;

class IntexSBH20 : public PollingComponent
{
public:
	void setup() override;
	void loop() override;
	void update() override;

	SBH20IO* sbh() { return &sbh_; }

	// ESP32: pins are set from the YAML (must be GPIO < 32)
	void set_clock_pin(uint8_t pin) { clock_pin_ = pin; }
	void set_data_pin(uint8_t pin) { data_pin_ = pin; }
	void set_latch_pin(uint8_t pin) { latch_pin_ = pin; }

	void set_climate(SBHClimate* climate) { climate_ = climate; }
	void set_switch_power(SBHSwitch* switch_power) { switch_power_ = switch_power; }
	void set_switch_filter(SBHSwitch* switch_filter) { switch_filter_ = switch_filter; }
	void set_switch_bubble(SBHSwitch* switch_bubble) { switch_bubble_ = switch_bubble; }
	void set_error_text_sensor(esphome::text_sensor::TextSensor* error) { error_text_ = error; };
	void set_water_temperature_sensor(esphome::sensor::Sensor* temperature) { water_temperature_ = temperature; };
private:
	SBH20IO sbh_;
	uint8_t clock_pin_ = 22;
	uint8_t data_pin_ = 19;
	uint8_t latch_pin_ = 23;
	SBHClimate* climate_ = nullptr;
	SBHSwitch* switch_power_ = nullptr;
	SBHSwitch* switch_filter_ = nullptr;
	SBHSwitch* switch_bubble_ = nullptr;
	esphome::text_sensor::TextSensor* error_text_ = nullptr;
	esphome::sensor::Sensor* water_temperature_ = nullptr;
};

}}

#endif
