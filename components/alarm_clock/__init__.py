"""Alarm clock: an alarm set on the glass or from Home Assistant.

The alarm lives in ESPHome entities of plugin.yaml (a switch, an hour and a minute, weekdays only), so it is kept across
a restart and Home Assistant can set it too. The component watches the screen's clock, and when the time comes it wakes
the screen, shows a popup with Snooze and Stop and turns the sensor "Alarm ringing" on. register_plugin() takes the id,
version, tile memory and the screen's texts from the manifest and translations/ beside this folder.

On a screen with a speaker the part parts/sound.yaml hands it over as `speaker`, with its switch `sound_on`: the alarm
then plays, while the switch is on, the sound the list chose from sounds/, built in by smart_display.sound() and played by the core's
sound player (plugin_sound.h).
"""
from pathlib import Path

import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import binary_sensor, number, select, smart_display, speaker, switch
from esphome.const import CONF_ID

DEPENDENCIES = ["smart_display", "binary_sensor", "number", "switch"]

CONF_ENABLED = "enabled"
CONF_DAYS = "days"
CONF_HOUR = "hour"
CONF_MINUTE = "minute"
CONF_SNOOZE = "snooze"
CONF_RINGING = "ringing"
CONF_SPEAKER = "speaker"
CONF_SOUND_ON = "sound_on"
CONF_TONE = "tone"
# The sounds, in the order of the options of the select "Alarm tone" in parts/sound.yaml.
SOUNDS = [Path(__file__).parent / "sounds" / f"{name}.wav" for name in ("beeps", "chime", "rising", "classic")]

alarm_clock_ns = cg.esphome_ns.namespace("alarm_clock")
AlarmClock = alarm_clock_ns.class_("AlarmClock", cg.Component)


def check_sound(config):
    if CONF_SPEAKER in config:
        for sound in SOUNDS:
            smart_display.read_sound(sound)
    return config


CONFIG_SCHEMA = cv.All(
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(AlarmClock),
            cv.Required(CONF_ENABLED): cv.use_id(switch.Switch),
            cv.Required(CONF_DAYS): cv.use_id(number.Number),
            cv.Required(CONF_HOUR): cv.use_id(number.Number),
            cv.Required(CONF_MINUTE): cv.use_id(number.Number),
            cv.Required(CONF_SNOOZE): cv.use_id(number.Number),
            cv.Required(CONF_RINGING): cv.use_id(binary_sensor.BinarySensor),
            cv.Optional(CONF_SPEAKER): cv.use_id(speaker.Speaker),
            cv.Optional(CONF_SOUND_ON): cv.use_id(switch.Switch),
            cv.Optional("full_volume"): cv.use_id(switch.Switch),
            cv.Optional(CONF_TONE): cv.use_id(select.Select),
        }
    ).extend(cv.COMPONENT_SCHEMA),
    check_sound,
)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await smart_display.register_plugin(var, __file__)
    cg.add(var.set_enabled_switch(await cg.get_variable(config[CONF_ENABLED])))
    cg.add(var.set_days_number(await cg.get_variable(config[CONF_DAYS])))
    cg.add(var.set_hour_number(await cg.get_variable(config[CONF_HOUR])))
    cg.add(var.set_minute_number(await cg.get_variable(config[CONF_MINUTE])))
    cg.add(var.set_snooze_number(await cg.get_variable(config[CONF_SNOOZE])))
    cg.add(var.set_ringing_sensor(await cg.get_variable(config[CONF_RINGING])))
    if CONF_SPEAKER in config:
        cg.add(var.set_speaker(await cg.get_variable(config[CONF_SPEAKER])))
        for index, sound in enumerate(SOUNDS):
            cg.add(var.set_alarm(index, smart_display.sound(config[CONF_ID], sound.stem, sound)))
        if CONF_SOUND_ON in config:
            cg.add(var.set_sound_on(await cg.get_variable(config[CONF_SOUND_ON])))
        if "full_volume" in config:
            cg.add(var.set_full_volume(await cg.get_variable(config["full_volume"])))
        if CONF_TONE in config:
            cg.add(var.set_tone(await cg.get_variable(config[CONF_TONE])))
