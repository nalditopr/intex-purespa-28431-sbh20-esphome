#include "intexsbh20.h"
#include "SBHSwitch.h"
#include "SBHClimate.h"

namespace esphome {
namespace sbh20 {

void IntexSBH20::setup()
{
	sbh_.setup(LANG::EN, clock_pin_, data_pin_, latch_pin_);
}

void IntexSBH20::loop()
{
	sbh_.loop();
}

void IntexSBH20::update()
{
	ESP_LOGV("intexsbh20", "diag: frames=%u dropped=%u online=%d power=%u ledRaw=0x%04X",
	         sbh_.getTotalFrames(), sbh_.getDroppedFrames(), (int) sbh_.isOnline(), sbh_.isPowerOn(), sbh_.getRawLedValue());

	sbh_.logDebug();

	int errorValue = sbh_.getErrorValue();

	if (problem_)
		problem_->publish_state(errorValue != 0); // dedupes internally; on = panel showing an Exx code

	if (errorValue != 0)
	{
		status_set_warning();

		if (error_text_)
			error_text_->publish_state(sbh_.getErrorMessage(errorValue).c_str());
	}
	else if (status_has_warning())
	{
		status_clear_warning();

		if (error_text_)
			error_text_->publish_state("");
	}

	if (switch_bubble_)
		switch_bubble_->publish_state(sbh_.isBubbleOn());

	if (switch_filter_)
		switch_filter_->publish_state(sbh_.isFilterOn());

	if (switch_power_)
		switch_power_->publish_state(sbh_.isPowerOn());

	if (climate_)
		climate_->update();

	if (water_temperature_) {
		int temp = sbh_.getCurrentTemperature();
		float water_temperature = (temp != SBH20IO::UNDEF::USHORT) ? temp : NAN;
		water_temperature_->publish_state(water_temperature);
	}
}
}}
