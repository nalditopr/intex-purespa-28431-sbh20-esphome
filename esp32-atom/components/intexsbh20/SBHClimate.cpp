#include "SBHClimate.h"

namespace esphome {
namespace sbh20 {

void SBHClimate::update()
{
	SBH20IO *sbh = get_parent()->sbh();

	if (!sbh->isHeaterOn())
	{
		this->action = esphome::climate::CLIMATE_ACTION_OFF;
		this->mode = esphome::climate::CLIMATE_MODE_OFF;
	}
	else if (sbh->isHeaterStandby())
	{
		this->action = esphome::climate::CLIMATE_ACTION_IDLE;
		this->mode = esphome::climate::CLIMATE_MODE_HEAT;
	}
	else
	{
		this->action = esphome::climate::CLIMATE_ACTION_HEATING;
		this->mode = esphome::climate::CLIMATE_MODE_HEAT;
	}

	int currentTemp = sbh->getCurrentTemperature();

	this->current_temperature = (currentTemp != SBH20IO::UNDEF::USHORT) ? currentTemp : NAN;

	int targetTemp = sbh->getTargetTemperature();

	this->target_temperature = (targetTemp != SBH20IO::UNDEF::USHORT) ? targetTemp : SBH20IO::WATER_TEMP::SET_MIN;

	// The panel only shows the setpoint briefly after a button press, so to learn
	// the target temperature we nudge it with one "down" press (which reveals the
	// setpoint without changing it). Only attempt this when the spa is online,
	// powered and error-free, and back off between tries so we don't spam the bus
	// or the log while idle/disconnected.
	if (targetTemp == SBH20IO::UNDEF::USHORT && !sbh->isAdjustingTarget() &&
	    sbh->isOnline() && sbh->isPowerOn() == true && sbh->getErrorValue() == 0)
	{
		uint32_t now = millis();
		if (last_target_read_ms_ == 0 || (now - last_target_read_ms_) >= TARGET_READ_BACKOFF_MS)
		{
			last_target_read_ms_ = now;
			ESP_LOGD("SBHClimate", "Target temp unknown; reading setpoint via a down press...");
			sbh->forceReadTargetTemperature();
		}
	}

	publish_state();
}

void SBHClimate::control(const esphome::climate::ClimateCall &call)
{
	auto tt = call.get_target_temperature();
	if (tt)
	{
		get_parent()->sbh()->setTargetTemperature(*tt);
	}

	auto mode = call.get_mode();
	if (mode)
	{
		get_parent()->sbh()->setHeaterOn(*mode == climate::CLIMATE_MODE_HEAT);
	}
}

esphome::climate::ClimateTraits SBHClimate::traits()
{
	esphome::climate::ClimateTraits rv;

	rv.set_visual_min_temperature(SBH20IO::WATER_TEMP::SET_MIN);
	rv.set_visual_max_temperature(SBH20IO::WATER_TEMP::SET_MAX);
	rv.set_visual_temperature_step(1);
	rv.add_feature_flags(climate::CLIMATE_SUPPORTS_CURRENT_TEMPERATURE | climate::CLIMATE_SUPPORTS_ACTION);
	rv.set_supported_modes({climate::CLIMATE_MODE_OFF, climate::CLIMATE_MODE_HEAT});

	return rv;
}

}}
