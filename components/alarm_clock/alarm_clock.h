#pragma once
#include <string>
#include "esphome/core/component.h"
#include "esphome/components/binary_sensor/binary_sensor.h"
#include "esphome/components/number/number.h"
#ifdef USE_SELECT
#include "esphome/components/select/select.h"
#endif
#include "esphome/components/switch/switch.h"
#include "esphome/components/smart_display/plugin_api.h"
#include "esphome/components/smart_display/plugin_sound.h"   // empty on a screen without a speaker

namespace esphome::alarm_clock {

// The plugin: the alarm itself. Its time and its switches are ESPHome entities of plugin.yaml, so they are kept across
// a restart and Home Assistant sets them too; what rings and what is snoozed lives here.
class AlarmClock : public Component, public tessera::Plugin {
 public:
  // How long the alarm rings when nobody answers, and how long the popup stays after Snooze or Stop.
  static constexpr uint32_t RING_SECONDS = 600, CLOSE_AFTER_MS = 600;

  void setup() override;
  float get_setup_priority() const override { return setup_priority::DATA; }
  void on_interval(uint32_t now_ms) override;
  bool settings(tessera::SettingsPage &page) override;

  void set_enabled_switch(switch_::Switch *s) { enabled_ = s; }
  void set_weekdays_switch(switch_::Switch *s) { weekdays_ = s; }
  void set_hour_number(number::Number *n) { hour_ = n; }
  void set_minute_number(number::Number *n) { minute_ = n; }
  void set_snooze_number(number::Number *n) { snooze_ = n; }
  void set_ringing_sensor(binary_sensor::BinarySensor *s) { ringing_sensor_ = s; }
#ifdef USE_SPEAKER
  // The part parts/sound.yaml, on a screen with a speaker: the alarm beeps.
  void set_speaker(speaker::Speaker *s) { sounds_.set_speaker(s); }
  static constexpr int SOUNDS = 4;
  void set_alarm(int index, tessera::Sound s) {
    if (index >= 0 && index < SOUNDS) alarm_[index] = s;
  }
  void set_sound_on(switch_::Switch *s) { sound_on_ = s; }
#ifdef USE_SELECT
  // Which sound rings: the options of the select "Alarm tone", in the order of the sounds.
  void set_tone(select::Select *s) { tone_ = s; }
#endif
  void loop() override { sounds_.loop(); }
  void before_update() override { sounds_.stop(); }
#endif

  bool enabled() const { return enabled_ != nullptr && enabled_->state; }
  bool weekdays_only() const { return weekdays_ != nullptr && weekdays_->state; }
  int hour() const;
  int minute() const;
  // How long a snooze lasts, in minutes: the number "Alarm snooze minutes", 9 unless someone set another.
  int snooze_minutes() const;
  bool ringing() const { return ringing_; }
  uint32_t snoozed_until() const { return snooze_until_; }

  // The keys of the card and the popup.
  void set_enabled(bool on);
  void set_weekdays_only(bool on);
  void add_minutes(int delta);
  void snooze();
  void stop();
  // The sound that rings, on a screen with the list "Alarm tone": its name, and the card's key that takes the next
  // one. A sound chosen there or in Home Assistant plays once, so it is heard before the alarm is set.
  bool has_tone() const;
  std::string tone_name() const;
  void next_tone();

  // The alarm's time as the screen writes a time of day, and the line under it: off, in 7:20:00, snoozed, ringing.
  std::string time_text(uint32_t epoch) const;
  std::string status_text(uint32_t epoch) const;

 protected:
  // The next moment the alarm rings, counted from `epoch` on the screen's own clock.
  uint32_t next_ring(uint32_t epoch) const;
  void start_ring(uint32_t epoch);
  void end_ring();
  // The minute that runs now does not ring: a time set to this very minute waits for tomorrow.
  void skip_this_minute();

  switch_::Switch *enabled_{}, *weekdays_{};
  number::Number *hour_{}, *minute_{}, *snooze_{};
  binary_sensor::BinarySensor *ringing_sensor_{};
  bool ringing_ = false;
  uint32_t ring_started_ = 0, snooze_until_ = 0, handled_minute_ = 0, close_at_ = 0;
#ifdef USE_SPEAKER
  // The sound the list chose.
  const tessera::Sound *chosen() const;
  tessera::Sound alarm_[SOUNDS];
  switch_::Switch *sound_on_{};
#ifdef USE_SELECT
  select::Select *tone_{};
#endif
  tessera::SoundPlayer sounds_;
  uint32_t rang_at_ = 0, preview_until_ = 0;
  int heard_tone_ = -1;
#endif
};

}  // namespace esphome::alarm_clock
