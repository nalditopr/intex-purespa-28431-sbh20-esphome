import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import binary_sensor, climate, sensor, switch, text_sensor
from esphome.const import CONF_ID, PLATFORM_ESP32

# ESP32 port: this component uses direct GPIO registers + a core-1 ISR, so it
# only builds for the ESP32 platform.
ESP_PLATFORMS = [PLATFORM_ESP32]

DEPENDENCIES = ['climate', 'sensor', 'switch', 'text_sensor']

# binary_sensor is auto-loaded (not a hard dependency) so the optional "problem" entity
# doesn't force users who don't want it to declare a bare binary_sensor: in their YAML.
AUTO_LOAD = ['binary_sensor']

CONF_CLIMATE = 'climate'
CONF_POWER = 'power'
CONF_FILTER = 'filter'
CONF_BUBBLE = 'bubble'
CONF_WATER_TEMPERATURE = 'water_temperature'
CONF_ERROR_TEXT = 'error_text'
CONF_PROBLEM = 'problem'

CONF_CLOCK_PIN = 'clock_pin'
CONF_DATA_PIN = 'data_pin'
CONF_LATCH_PIN = 'latch_pin'

# The fast ISR path reads/writes GPIO via the 32-bit GPIO_IN/ENABLE registers,
# so all three signals must be on GPIO 0..31.
GPIO_BELOW_32 = cv.int_range(min=0, max=31)

sbh_ns = cg.esphome_ns.namespace('sbh20')

IntexSBH20 = sbh_ns.class_('IntexSBH20', cg.PollingComponent)
SBHClimate = sbh_ns.class_('SBHClimate', climate.Climate)
SBHSwitch = sbh_ns.class_('SBHSwitch', switch.Switch)

CONFIG_SCHEMA = cv.polling_component_schema("5s").extend(
	{
		cv.GenerateID(): cv.declare_id(IntexSBH20),
		# Atom Lite defaults: CLK=G19, DATA=G22, LATCH=G23 (all < 32).
		# (Verified by a live pin-role scan against the spa: G19 carries the
		# ~140 kHz clock, G22 the data, G23 the per-frame latch strobe.)
		cv.Optional(CONF_CLOCK_PIN, default=19): GPIO_BELOW_32,
		cv.Optional(CONF_DATA_PIN, default=22): GPIO_BELOW_32,
		cv.Optional(CONF_LATCH_PIN, default=23): GPIO_BELOW_32,
		cv.Optional(CONF_CLIMATE): climate.climate_schema(SBHClimate).extend(),
		cv.Optional(CONF_POWER): switch.switch_schema(SBHSwitch).extend(),
		cv.Optional(CONF_FILTER): switch.switch_schema(SBHSwitch).extend(),
		cv.Optional(CONF_BUBBLE): switch.switch_schema(SBHSwitch).extend(),
		cv.Optional(CONF_WATER_TEMPERATURE): sensor.sensor_schema().extend(),
		cv.Optional(CONF_ERROR_TEXT): text_sensor.text_sensor_schema().extend(),
		cv.Optional(CONF_PROBLEM): binary_sensor.binary_sensor_schema(device_class='problem').extend(),
	}
)

async def to_code(config):
	var = cg.new_Pvariable(config[CONF_ID])
	await cg.register_component(var, config)

	cg.add(var.set_clock_pin(config[CONF_CLOCK_PIN]))
	cg.add(var.set_data_pin(config[CONF_DATA_PIN]))
	cg.add(var.set_latch_pin(config[CONF_LATCH_PIN]))

	if CONF_CLIMATE in config:
		clim = await climate.new_climate(config[CONF_CLIMATE])
		# wire the climate's Parented<> parent so get_parent()->sbh() is valid; without this
		# get_parent() is null and SBHClimate::update()'s sbh->isAdjustingTarget() dereferences
		# a null instance pointer (LoadProhibited crash on real hardware)
		cg.add(clim.set_parent(var))
		cg.add(var.set_climate(clim))

	if CONF_POWER in config:
		sw = await switch.new_switch(config[CONF_POWER])
		cg.add(sw.set_type(CONF_POWER))
		cg.add(var.set_switch_power(sw))

	if CONF_FILTER in config:
		sw = await switch.new_switch(config[CONF_FILTER])
		cg.add(sw.set_type(CONF_FILTER))
		cg.add(var.set_switch_filter(sw))

	if CONF_BUBBLE in config:
		sw = await switch.new_switch(config[CONF_BUBBLE])
		cg.add(sw.set_type(CONF_BUBBLE))
		cg.add(var.set_switch_bubble(sw))

	if CONF_ERROR_TEXT in config:
		tx = await text_sensor.new_text_sensor(config[CONF_ERROR_TEXT])
		cg.add(var.set_error_text_sensor(tx))

	if CONF_PROBLEM in config:
		bs = await binary_sensor.new_binary_sensor(config[CONF_PROBLEM])
		cg.add(var.set_problem_binary_sensor(bs))

	if CONF_WATER_TEMPERATURE in config:
		tx = await sensor.new_sensor(config[CONF_WATER_TEMPERATURE])
		cg.add(var.set_water_temperature_sensor(tx))
