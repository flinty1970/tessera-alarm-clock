#include "alarm_clock.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace esphome::alarm_clock {

using tessera::Font;
using tessera::Tone;
namespace ui = tessera::ui;

static int clamp(int v, int lo, int hi) { return std::max(lo, std::min(hi, v)); }

static lv_obj_t *centred_label(lv_obj_t *parent, Font font, theme::Role role) {
  lv_obj_t *label = ui::label(parent, font, role);
  lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
  return label;
}

// A part that has no room is hidden, not drawn outside its tile or card.
static void show(lv_obj_t *part, bool on) {
  if (on) lv_obj_clear_flag(part, LV_OBJ_FLAG_HIDDEN);
  else lv_obj_add_flag(part, LV_OBJ_FLAG_HIDDEN);
}

// The largest of the screen's text fonts that `text` fits in, across and high.
static Font fitting(const std::string &text, int width, int height) {
  for (Font f : {Font::VALUE, Font::HEADLINE, Font::TITLE, Font::BODY_LARGE})
    if (ui::text_width(text, f) <= width && ui::line_height(f) <= height) return f;
  return Font::BODY;
}

// A key: a rounded block with a word in its middle. The card and the popup find the key under a finger themselves
// (on_tap and tap_point), so a key is no LVGL button.
struct Key {
  lv_obj_t *block{}, *label{};
  int x = 0, y = 0, w = 0, h = 0;

  void make(lv_obj_t *parent, int kx, int ky, int kw, int kh) {
    x = kx, y = ky, w = kw, h = kh;
    block = ui::block(parent, theme::RAISED);
    lv_obj_set_pos(block, x, y);
    lv_obj_set_size(block, w, h);
    label = centred_label(block, Font::BODY_LARGE, theme::INK);
    const int lh = ui::line_height(Font::BODY_LARGE);
    lv_obj_set_width(label, w);
    lv_obj_set_pos(label, 0, (h - lh) / 2);
  }
  // Its word, and whether it stands out (the accent) or is one key of several.
  void paint(const std::string &text, bool accent) { paint(text, accent ? Tone::ACCENT : Tone::NORMAL); }
  // The same in a colour that says something: green for on, red for off.
  void paint(const std::string &text, Tone tone) {
    ui::set_text(label, text);
    ui::set_fill(block, tone, theme::RAISED);
    ui::set_color(label, tone != Tone::NORMAL ? theme::ON_ACCENT : theme::INK);
  }
  bool has(tessera::Point p) const {
    const int slack = ui::mm(1);
    return block != nullptr && p.x >= x - slack && p.x < x + w + slack && p.y >= y - slack && p.y < y + h + slack;
  }
};

// The line under the alarm's time, and its colour: red "Off", or green "Daily" (or "Weekdays") with when it rings
// next, where that fits in `width`. While it rings or a snooze waits the line says that, in the screen's own grey.
// A countdown that does not fit beside the rest comes back in `extra`, for a line of its own where there is room.
static Tone state_line(const AlarmClock *plugin, uint32_t epoch, int width, std::string &line, std::string &extra) {
  extra.clear();
  line = plugin->status_text(epoch);
  if (plugin->ringing()) return Tone::NORMAL;
  if (plugin->snoozed_until()) {
    // "Snoozed until 7:09 AM - in 8:40" where that fits: the snooze counts down too.
    const long left = static_cast<long>(plugin->snoozed_until()) - static_cast<long>(epoch);
    if (epoch && left > 0) {
      char count[16];
      snprintf(count, sizeof count, "%ld:%02ld", left / 60, left % 60);
      const std::string in = tessera::fill(plugin->text("rings_in"), "time", count);
      const std::string all = line + " - " + in;
      if (ui::text_width(all, Font::BODY) <= width) line = all;
      else extra = in;
    }
    return Tone::BUSY;
  }
  if (!plugin->enabled()) return Tone::ALERT;
  const std::string mode = plugin->days_text();
  // "Daily - 7:00 AM - in 8:12:40" where that fits, else without the countdown, else the one word.
  const std::string at = mode + " - " + plugin->time_text(epoch);
  const std::string all = at + " - " + line;
  if (ui::text_width(all, Font::BODY) <= width) {
    line = all;
  } else {
    extra = line;
    line = ui::text_width(at, Font::BODY) <= width ? at : mode;
  }
  return Tone::GOOD;
}

// The tile: the alarm's time, and under it when it rings next. A tap opens the card that sets it.
class AlarmTile : public tessera::Tile {
 public:
  explicit AlarmTile(AlarmClock *plugin) : plugin_(plugin) {}

  void create(const tessera::TileContext &c) override {
    tile_ = c.tile;
    width_ = c.width;
    height_ = c.height;
    time_ = centred_label(c.parent, Font::VALUE, theme::INK);
    lv_obj_set_width(time_, width_);
    status_ = centred_label(c.parent, Font::BODY, theme::MUTED);
    lv_obj_set_width(status_, width_);
    extra_ = centred_label(c.parent, Font::BODY, theme::MUTED);
    lv_obj_set_width(extra_, width_);
    // The tile's name from the editor, "Alarm" without one: shown on a tile that is high enough.
    name_ = c.name != nullptr && c.name[0] ? std::string(c.name) : std::string(plugin_->text("alarm"));
    title_ = centred_label(c.parent, Font::BODY, theme::MUTED);
    lv_obj_set_width(title_, width_);
    icon_ = ui::label(c.parent, Font::ICON_SMALL, theme::MUTED);
  }

  void on_tick(uint32_t epoch) override { paint(epoch); }
  void on_theme() override { paint(tessera::epoch()); }
  void on_tap() override { tessera::open_card(plugin_->plugin_id(), "alarm", "", tile_); }

 private:
  void paint(uint32_t epoch) {
    const std::string time = plugin_->time_text(epoch);
    std::string status, extra;
    const Tone state = state_line(plugin_, epoch, width_, status, extra);
    int sh = ui::line_height(Font::BODY);
    // The title is always above the time, which takes a smaller font on a low tile; the countdown gets a line of its
    // own only where the time stays large.
    const int large = ui::line_height(Font::VALUE), least = ui::line_height(Font::BODY);
    // On a tile too low for all of it the line under the time goes first, then the title.
    const bool lined = height_ >= least + 2 * sh || name_.empty();
    const bool titled = !name_.empty() && height_ >= least + sh;
    const bool second = lined && !extra.empty() && height_ >= large + (titled ? 3 : 2) * sh;
    // A large tile uses its room: the words take the largest font in which they fit across and that leaves the time
    // two thirds of the height, and the time itself is drawn enlarged (the screen's largest font is made for a tile of
    // two or three cells).
    Font words = Font::BODY;
    const int lines = (lined ? 1 : 0) + (second ? 1 : 0) + (titled ? 1 : 0);
    for (Font f : {Font::HEADLINE, Font::TITLE, Font::BODY_LARGE}) {
      if (lines == 0 || lines * ui::line_height(f) > height_ / 3) continue;
      if (ui::text_width(status, f) > width_ || (second && ui::text_width(extra, f) > width_) ||
          (titled && ui::text_width(name_, f) > width_))
        continue;
      words = f;
      break;
    }
    sh = ui::line_height(words);
    const int under = ((lined ? 1 : 0) + (second ? 1 : 0)) * sh, over = titled ? sh : 0;
    const Font face = fitting(time, width_, height_ - under - over);
    const int th0 = ui::line_height(face), tw0 = ui::text_width(time, face) + ui::px(4);
    int scale = 256;   // LVGL's 256 is life size
    if (face == Font::VALUE && ui::pictures())   // a screen with the memory for pictures has it for this too
      for (int s : {1024, 896, 768, 640, 512, 448, 384, 320})
        if (tw0 * s / 256 <= width_ * 9 / 10 && th0 * s / 256 <= height_ - under - over) {
          scale = s;
          break;
        }
    const int th = th0 * scale / 256;
    const int top = std::max(0, (height_ - th - under - over) / 2) + over;
    for (lv_obj_t *part : {title_, status_, extra_}) ui::set_font(part, words);
    show(title_, titled);
    ui::set_text(title_, titled ? name_ : std::string());
    ui::set_color(title_, theme::MUTED);
    // An alarm clock before the title, in the colour of the line under the time (a bell while it rings, the sleeping
    // sign while a snooze waits): the two stand in the middle as one.
    const Font icon_font = ui::line_height(Font::ICON) <= sh ? Font::ICON : Font::ICON_SMALL;
    const std::string icon = ui::icon(plugin_->ringing() ? 0xF009E : plugin_->snoozed_until() ? 0xF04B2 : 0xF0020);
    const int iw = ui::text_width(icon, icon_font) + ui::px(6), nw = ui::text_width(name_, words) + ui::px(4);
    const bool iconed = titled && iw + nw <= width_;
    show(icon_, iconed);
    if (iconed) {
      const int x0 = (width_ - (iw + nw)) / 2;
      ui::set_font(icon_, icon_font);
      ui::set_text(icon_, icon);
      ui::set_tone(icon_, state, theme::MUTED);
      lv_obj_set_pos(icon_, x0, top - over + (sh - ui::line_height(icon_font)) / 2);
      lv_obj_set_width(title_, nw);
      lv_obj_set_pos(title_, x0 + iw, top - over);
    } else {
      lv_obj_set_width(title_, width_);
      lv_obj_set_pos(title_, 0, top - over);
    }
    show(extra_, second);
    ui::set_text(extra_, second ? extra : std::string());
    ui::set_tone(extra_, state, theme::MUTED);
    lv_obj_set_pos(extra_, 0, second ? top + th + sh : 0);
    show(status_, lined);
    ui::set_font(time_, face);
    ui::set_text(time_, time);
    ui::set_tone(time_, plugin_->ringing() ? Tone::ALERT : Tone::NORMAL, plugin_->enabled() ? theme::INK : theme::MUTED);
    // Enlarged, the label is as wide as its text and grows from its middle; life size it spans the tile as before.
    if (scale != 256) {
      lv_obj_set_width(time_, tw0);
      lv_obj_set_style_transform_pivot_x(time_, tw0 / 2, 0);
      lv_obj_set_style_transform_pivot_y(time_, th0 / 2, 0);
      lv_obj_set_style_transform_scale(time_, scale, 0);
      lv_obj_set_pos(time_, (width_ - tw0) / 2, top + (th - th0) / 2);
    } else {
      lv_obj_set_width(time_, width_);
      lv_obj_set_style_transform_scale(time_, 256, 0);
      lv_obj_set_pos(time_, 0, top);
    }
    ui::set_text(status_, status);
    ui::set_tone(status_, state, theme::MUTED);
    lv_obj_set_pos(status_, 0, top + th);
  }

  AlarmClock *plugin_;
  int tile_ = -1, width_ = 0, height_ = 0;
  std::string name_;
  lv_obj_t *time_{}, *status_{}, *extra_{}, *title_{}, *icon_{};
};

// The card: wheels for the hour, the minute and AM or PM on a wide card (six keys that move the time on a narrow
// one), and keys that turn the alarm on and choose its days.
class AlarmCard : public tessera::Card {
 public:
  explicit AlarmCard(AlarmClock *plugin) : plugin_(plugin) {}

  void open(const tessera::CardContext &c) override {
    const int W = c.width, H = c.height, g = ui::px(8), km = ui::mm(7);
    // Six keys in a row where each keeps a finger's width, else two rows of three.
    per_row_ = W >= 6 * ui::mm(11) + 5 * g ? 6 : 3;
    has_tone_ = plugin_->has_tone();
    // A row of the seven days where the card is wide; a narrow card chooses them with the one key.
    day_row_ = per_row_ == 6;
#if LV_USE_ROLLER
    rollers_ = day_row_;
#endif
    // With the wheels the keys are two rows (on, days and the sound in one; the seven days), and leave the wheels
    // the larger half of the card.
    const int rows = rollers_ ? 2 : 6 / per_row_ + (has_tone_ ? 2 : 1) + (day_row_ ? 1 : 0);
    const int kh = clamp((H * (rollers_ ? 40 : day_row_ ? 64 : 55) / 100 - (rows - 1) * g) / rows, km, ui::mm(rollers_ ? 11 : 13));
    const int keys_h = rows * kh + (rows - 1) * g;
    const int head = H - keys_h - g;
    const int sh = ui::line_height(Font::BODY);

    now_ = centred_label(c.parent, Font::BODY, theme::MUTED);
    lv_obj_set_width(now_, W);
    time_ = centred_label(c.parent, Font::VALUE, theme::INK);
    lv_obj_set_width(time_, W);
    status_ = centred_label(c.parent, Font::BODY, theme::MUTED);
    lv_obj_set_width(status_, W);
    // An alarm clock beside the alarm's time: in the accent while the alarm is on.
    icon_ = centred_label(c.parent, Font::ICON, theme::ACCENT);
    lv_obj_set_width(icon_, ui::line_height(Font::ICON));
    ui::set_text(icon_, ui::icon(0xF0020));
    head_ = head;
    width_ = W;
    status_h_ = sh;

    const int kw = (W - (per_row_ - 1) * g) / per_row_;
    int y = head + g;
    if (rollers_) {
      make_rollers(c.parent, W, head, sh, g);
    } else {
      for (int i = 0; i < 6; ++i) {
        const int col = i % per_row_;
        if (i && !col) y += kh + g;
        step_[i].make(c.parent, col * (kw + g), y, kw, kh);
      }
      y += kh + g;
    }
    const int half = (W - g) / 2, third = (W - 2 * g) / 3;
    if (rollers_ && has_tone_) {
      on_.make(c.parent, 0, y, third, kh);
      days_.make(c.parent, third + g, y, third, kh);
      tone_.make(c.parent, 2 * (third + g), y, W - 2 * (third + g), kh);
    } else {
      on_.make(c.parent, 0, y, half, kh);
      days_.make(c.parent, half + g, y, W - half - g, kh);
    }
    // The sound, on a screen with a speaker: a tap takes the next one and plays it once.
    if (day_row_) {
      y += kh + g;
      const int dw = (W - 6 * g) / 7;
      for (int i = 0; i < 7; ++i) day_[i].make(c.parent, i * (dw + g), y, i == 6 ? W - 6 * (dw + g) : dw, kh);
    }
    if (has_tone_ && !rollers_) tone_.make(c.parent, 0, y + kh + g, W, kh);
    paint(tessera::epoch());
  }

  void on_tick(uint32_t epoch) override { paint(epoch); }
  void on_theme() override {
    style_rollers();
    paint(tessera::epoch());
  }

  void on_tap() override {
    const tessera::Point p = tessera::tap_point();
    static const int DELTA[6] = {-60, 60, -10, 10, -1, 1};
    for (int i = 0; i < 6 && !rollers_; ++i)
      if (step_[i].has(p)) plugin_->add_minutes(DELTA[i]);
    if (on_.has(p)) plugin_->set_enabled(!plugin_->enabled());
    if (has_tone_ && tone_.has(p)) plugin_->next_tone();
    if (days_.has(p)) {
      // While a snooze waits, this key ends it; otherwise it chooses the days.
      if (plugin_->snoozed_until()) plugin_->stop();
      else plugin_->next_days(!day_row_);
    }
    if (day_row_)
      for (int i = 0; i < 7; ++i)
        if (day_[i].has(p)) plugin_->toggle_day(i);
    paint(tessera::epoch());
  }

 private:
  // The wheels: hour, minute and (where the screen writes 7:00 AM) AM or PM, under the line that says when it rings.
  void make_rollers(lv_obj_t *parent, int W, int head, int sh, int g) {
#if LV_USE_ROLLER
    twelve_ = plugin_->time_text(tessera::epoch()).find('M') != std::string::npos;
    const int n = twelve_ ? 3 : 2, top = sh + g, rh = std::max(ui::mm(12), head - top);
    const int rw = std::min((W - (n - 1) * g) / n, ui::mm(32));
    const int x0 = (W - (n * rw + (n - 1) * g)) / 2;
    // Three rows show whole: the chosen one in the middle, its neighbours above and below.
    const int space = ui::px(8);
    Font face = Font::BODY_LARGE;
    for (Font f : {Font::VALUE, Font::HEADLINE, Font::TITLE})
      if (3 * ui::line_height(f) + 2 * space + ui::px(8) <= rh) {
        face = f;
        break;
      }
    std::string hours, minutes;
    char b[8];
    for (int i = 0; i < (twelve_ ? 12 : 24); ++i) {
      snprintf(b, sizeof b, twelve_ ? "%d" : "%02d", twelve_ && i == 0 ? 12 : i);
      hours += i ? "\n" : "";
      hours += b;
    }
    for (int i = 0; i < 60; ++i) {
      snprintf(b, sizeof b, "%02d", i);
      minutes += i ? "\n" : "";
      minutes += b;
    }
    const char *const options[3] = {hours.c_str(), minutes.c_str(), "AM\nPM"};
    for (int i = 0; i < n; ++i) {
      lv_obj_t *r = lv_roller_create(parent);
      lv_roller_set_options(r, options[i], i < 2 ? LV_ROLLER_MODE_INFINITE : LV_ROLLER_MODE_NORMAL);
      ui::set_font(r, face);
      lv_obj_set_style_text_font(r, lv_obj_get_style_text_font(r, LV_PART_MAIN), LV_PART_SELECTED);
      lv_obj_set_style_text_align(r, LV_TEXT_ALIGN_CENTER, 0);
      lv_obj_set_style_text_line_space(r, space, 0);
      lv_obj_set_style_border_width(r, 0, 0);
      lv_obj_set_style_radius(r, ui::px(12), 0);
      lv_obj_set_style_bg_opa(r, LV_OPA_COVER, 0);
      lv_obj_set_style_bg_opa(r, LV_OPA_COVER, LV_PART_SELECTED);
      lv_obj_set_size(r, rw, rh);
      lv_obj_set_pos(r, x0 + i * (rw + g), top);
      lv_obj_add_event_cb(r, &AlarmCard::roller_event, LV_EVENT_VALUE_CHANGED, this);
      roll_[i] = r;
    }
    style_rollers();
#endif
  }

  // The wheels' colours, again when the screen goes light or dark.
  void style_rollers() {
    for (lv_obj_t *r : roll_) {
      if (!r) continue;
      lv_obj_set_style_bg_color(r, ui::color(theme::RAISED), 0);
      lv_obj_set_style_text_color(r, ui::color(theme::MUTED), 0);
      lv_obj_set_style_bg_color(r, ui::tone_color(Tone::ACCENT, theme::INK), LV_PART_SELECTED);
      lv_obj_set_style_text_color(r, ui::color(theme::ON_ACCENT), LV_PART_SELECTED);
    }
  }

#if LV_USE_ROLLER
  // A wheel was turned: the three together are the alarm's new time.
  static void roller_event(lv_event_t *e) {
    auto *self = static_cast<AlarmCard *>(lv_event_get_user_data(e));
    if (self->syncing_ || !self->roll_[0] || !self->roll_[1]) return;
    int h = lv_roller_get_selected(self->roll_[0]);
    const int m = lv_roller_get_selected(self->roll_[1]);
    if (self->twelve_ && self->roll_[2] && lv_roller_get_selected(self->roll_[2])) h += 12;
    self->plugin_->add_minutes(h * 60 + m - (self->plugin_->hour() * 60 + self->plugin_->minute()));
    self->paint(tessera::epoch());
  }

  // A wheel shows the alarm's time, also when Home Assistant changed it; one under a finger is left alone.
  void set_roll(int index, int want) {
    lv_obj_t *r = roll_[index];
    if (!r || lv_obj_is_scrolling(r) || static_cast<int>(lv_roller_get_selected(r)) == want) return;
    syncing_ = true;
    lv_roller_set_selected(r, want, LV_ANIM_OFF);
    syncing_ = false;
  }
#endif

  void paint(uint32_t epoch) {
    if (rollers_) {
      show(now_, false);
      show(time_, false);
      show(icon_, false);
      std::string line, more;
      const Tone tone = state_line(plugin_, epoch, width_, line, more);
      ui::set_text(status_, line);
      ui::set_tone(status_, tone, theme::MUTED);
      lv_obj_set_pos(status_, 0, 0);
#if LV_USE_ROLLER
      const int h = plugin_->hour();
      set_roll(0, twelve_ ? h % 12 : h);
      set_roll(1, plugin_->minute());
      if (twelve_) set_roll(2, h >= 12 ? 1 : 0);
#endif
      paint_keys();
      return;
    }
    const std::string time = plugin_->time_text(epoch);
    // Three lines: the time it is now, the alarm's time large, and when it rings. On a small card the first gives way,
    // so the alarm's time keeps a size worth reading.
    const int sh = status_h_, iw = ui::line_height(Font::ICON), gap = ui::px(8);
    const bool with_now = head_ >= ui::line_height(Font::HEADLINE) + 2 * sh;
    const int lines = (with_now ? 2 : 1) * sh;
    const Font face = fitting(time, width_, head_ - lines);
    const int th = ui::line_height(face), tw = ui::text_width(time, face);
    const int first = std::max(0, (head_ - th - lines) / 2);
    const int top = first + (with_now ? sh : 0);
    show(now_, with_now);
    ui::set_text(now_, with_now && epoch ? tessera::fill(plugin_->text("now"), "time", tessera::clock_text(epoch)) : std::string());
    ui::set_color(now_, theme::MUTED);
    lv_obj_set_pos(now_, 0, first);
    // The icon left of the time, the two centred together; without it where it is higher than the time or has no room.
    const bool with_icon = iw <= th && iw + gap + tw <= width_;
    const int left = with_icon ? (width_ - iw - gap - tw) / 2 : (width_ - tw) / 2;
    ui::set_font(time_, face);
    ui::set_text(time_, time);
    ui::set_tone(time_, plugin_->ringing() ? Tone::ALERT : Tone::NORMAL, plugin_->enabled() ? theme::INK : theme::MUTED);
    lv_obj_set_width(time_, tw + 2);
    lv_obj_set_pos(time_, left + (with_icon ? iw + gap : 0), top);
    show(icon_, with_icon);
    ui::set_tone(icon_, plugin_->enabled() ? Tone::GOOD : Tone::NORMAL, theme::MUTED);
    lv_obj_set_pos(icon_, left, top + (th - iw) / 2);
    std::string status, extra;
    const Tone state = state_line(plugin_, epoch, width_, status, extra);
    ui::set_text(status_, status);
    ui::set_tone(status_, state, theme::MUTED);
    lv_obj_set_pos(status_, 0, top + th);
    paint_keys();
  }

  void paint_keys() {
    static const char *const WORD[6] = {"hour_down", "hour_up", "ten_down", "ten_up", "minute_down", "minute_up"};
    for (int i = 0; i < 6 && !rollers_; ++i) step_[i].paint(plugin_->text(WORD[i]), false);
    on_.paint(plugin_->text(plugin_->enabled() ? "alarm_on" : "alarm_off"), plugin_->enabled() ? Tone::GOOD : Tone::ALERT);
    if (plugin_->snoozed_until()) days_.paint(plugin_->text("cancel_snooze"), true);
    else days_.paint(plugin_->days() == 127 ? std::string(plugin_->text("every_day")) : plugin_->days_text(), false);
    static const char *const DAY[7] = {"day_mon", "day_tue", "day_wed", "day_thu", "day_fri", "day_sat", "day_sun"};
    if (day_row_)
      for (int i = 0; i < 7; ++i) day_[i].paint(plugin_->text(DAY[i]), ((plugin_->days() >> i) & 1) != 0);
    if (has_tone_) {
      // "Sound: Chime" where the key has the room, else the sound's name alone.
      const std::string full = tessera::fill(plugin_->text("tone_key"), "name", plugin_->tone_name());
      tone_.paint(ui::text_width(full, Font::BODY_LARGE) <= tone_.w - ui::px(8) ? full : plugin_->tone_name(), false);
    }
  }

  AlarmClock *plugin_;
  int per_row_ = 6, head_ = 0, width_ = 0, status_h_ = 0;
  lv_obj_t *now_{}, *time_{}, *status_{}, *icon_{};
  Key step_[6], on_, days_, tone_, day_[7];
  bool has_tone_ = false, day_row_ = false, rollers_ = false, twelve_ = false, syncing_ = false;
  lv_obj_t *roll_[3]{};
};

// The popup while the alarm rings: the time, and Snooze and Stop. Closed any other way (its close key, a tap beside
// it, an alert that comes), the alarm snoozes: a finger that missed never ends it.
class RingPopup : public tessera::Popup {
 public:
  explicit RingPopup(AlarmClock *plugin) : plugin_(plugin) {}

  void open(const tessera::PopupContext &c) override {
    const int W = c.width, g = ui::px(8);
    const int disc = c.key;
    const int title_h = std::max(ui::line_height(Font::TITLE), disc);
    // A bell on a disc in the top left, as large as the close key on its right, with two rings that ripple out of it
    // one after the other while the alarm rings.
    for (int i = 0; i < 2; ++i) {
      ring_[i] = round_block(c.parent, disc);
      ui::animate(ring_[i], tessera::Motion::RIPPLE, 1600, i * 800);
    }
    disc_ = round_block(c.parent, disc);
    bell_ = centred_label(c.parent, Font::ICON, theme::ON_ACCENT);
    lv_obj_set_width(bell_, disc);
    lv_obj_set_pos(bell_, 0, (disc - ui::line_height(Font::ICON)) / 2);
    ui::set_text(bell_, ui::icon(0xF009E));
    ui::animate(bell_, tessera::Motion::PULSE, 800);

    title_ = ui::label(c.parent, Font::TITLE, theme::INK);
    lv_obj_set_width(title_, std::max(1, W - c.corner - disc - g));
    lv_obj_set_pos(title_, disc + g, (title_h - ui::line_height(Font::TITLE)) / 2);
    ui::set_text(title_, plugin_->text("ringing"));

    time_ = centred_label(c.parent, Font::VALUE, theme::INK);
    lv_obj_set_width(time_, W);
    lv_obj_set_pos(time_, 0, title_h + g);
    const int th = ui::line_height(Font::VALUE);

    const int kh = clamp(ui::mm(12), ui::mm(7), std::max(ui::mm(7), c.height - title_h - th - 3 * g));
    const int y = title_h + g + th + g, half = (W - g) / 2;
    snooze_.make(c.parent, 0, y, half, kh);
    stop_.make(c.parent, half + g, y, W - half - g, kh);
    paint(tessera::epoch());
  }

  void on_tick(uint32_t epoch) override { paint(epoch); }
  void on_theme() override { paint(tessera::epoch()); }

  void on_tap() override {
    const tessera::Point p = tessera::tap_point();
    if (snooze_.has(p)) plugin_->snooze();
    else if (stop_.has(p)) plugin_->stop();
  }

  void on_dismiss() override { plugin_->snooze(); }

 private:
  void paint(uint32_t epoch) {
    ui::set_text(time_, epoch ? tessera::clock_text(epoch) : plugin_->time_text(epoch));
    snooze_.paint(plugin_->text("snooze"), false);
    stop_.paint(plugin_->text("stop"), true);
  }

  // A round block in the alert's red, `size` across, in the room's top left corner.
  static lv_obj_t *round_block(lv_obj_t *parent, int size) {
    lv_obj_t *block = ui::block(parent, theme::ACCENT);
    ui::set_fill(block, Tone::ALERT, theme::ACCENT);
    lv_obj_set_pos(block, 0, 0);
    lv_obj_set_size(block, size, size);
    lv_obj_set_style_radius(block, LV_RADIUS_CIRCLE, 0);
    return block;
  }

  AlarmClock *plugin_;
  lv_obj_t *title_{}, *time_{}, *disc_{}, *bell_{}, *ring_[2] = {};
  Key snooze_, stop_;
};

void AlarmClock::setup() {
  add_tile("alarm", [this]() { return new AlarmTile(this); });
  add_card("alarm", [this]() { return new AlarmCard(this); }, true);
  // The top bar: an alarm clock, green with the alarm's time while it is on, grey and alone while it is off; amber
  // with the time a snooze ends, red while it rings.
  add_bar_item("alarm", [this]() {
    tessera::BarItem item;
    const uint32_t epoch = tessera::epoch();
    item.shown = true;
    item.icon = 0xF0020;
    if (ringing_) {
      item.tone = Tone::ALERT;
      item.text = time_text(epoch);
    } else if (snooze_until_) {
      item.tone = Tone::BUSY;
      item.text = tessera::clock_text(snooze_until_);
    } else if (enabled()) {
      item.tone = Tone::GOOD;
      item.text = time_text(epoch);
    }
    return item;
  });
}

int AlarmClock::hour() const {
  return hour_ != nullptr && !std::isnan(hour_->state) ? clamp(static_cast<int>(hour_->state), 0, 23) : 7;
}

int AlarmClock::minute() const {
  return minute_ != nullptr && !std::isnan(minute_->state) ? clamp(static_cast<int>(minute_->state), 0, 59) : 0;
}

int AlarmClock::snooze_minutes() const {
  return snooze_ != nullptr && !std::isnan(snooze_->state) ? clamp(static_cast<int>(snooze_->state), 1, 30) : 9;
}

void AlarmClock::skip_this_minute() {
  const uint32_t epoch = tessera::epoch();
  if (epoch) handled_minute_ = epoch / 60;
}

void AlarmClock::set_enabled(bool on) {
  if (enabled_ == nullptr) return;
  on ? enabled_->turn_on() : enabled_->turn_off();
  if (on) skip_this_minute();
  else snooze_until_ = 0;
  tessera::refresh();
}

int AlarmClock::days() const {
  const int mask = days_ != nullptr && days_->state == days_->state ? static_cast<int>(days_->state) & 127 : 127;
  return mask ? mask : 127;
}

void AlarmClock::set_days(int mask) {
  if (days_ == nullptr || !(mask & 127)) return;   // never no day at all
  auto call = days_->make_call();
  call.set_value(mask & 127);
  call.perform();
  tessera::refresh();
}

void AlarmClock::toggle_day(int index) { set_days(days() ^ (1 << index)); }

void AlarmClock::next_days(bool single_days) {
  static const int ORDER[10] = {127, 31, 96, 1, 2, 4, 8, 16, 32, 64};
  const int count = single_days ? 10 : 3, now = days();
  int at = -1;
  for (int i = 0; i < count; ++i)
    if (ORDER[i] == now) at = i;
  set_days(ORDER[(at + 1) % count]);
}

std::string AlarmClock::days_text() const {
  static const char *const SHORT[7] = {"day_mon", "day_tue", "day_wed", "day_thu", "day_fri", "day_sat", "day_sun"};
  static const char *const LONG[7] = {"day_monday", "day_tuesday", "day_wednesday", "day_thursday", "day_friday",
                                      "day_saturday", "day_sunday"};
  const int mask = days();
  if (mask == 127) return text("daily");
  if (mask == 31) return text("weekdays");
  if (mask == 96) return text("weekends");
  int count = 0;
  for (int i = 0; i < 7; ++i) count += (mask >> i) & 1;
  std::string out;
  for (int i = 0; i < 7; ++i) {
    if (!((mask >> i) & 1)) continue;
    if (count == 1) return text(LONG[i]);
    if (!out.empty()) out += " ";
    out += text(SHORT[i]);
  }
  return out;
}

void AlarmClock::add_minutes(int delta) {
  if (hour_ == nullptr || minute_ == nullptr) return;
  const int total = (((hour() * 60 + minute() + delta) % 1440) + 1440) % 1440;
  auto h = hour_->make_call();
  h.set_value(total / 60);
  h.perform();
  auto m = minute_->make_call();
  m.set_value(total % 60);
  m.perform();
  skip_this_minute();
  tessera::refresh();
}

uint32_t AlarmClock::next_ring(uint32_t epoch) const {
  const tessera::LocalTime now = tessera::local_time(epoch);
  long delta = (static_cast<long>(hour() * 60 + minute()) - (now.hour * 60 + now.minute)) * 60 - now.second;
  int days = 0;
  if (delta <= 0) {
    delta += 86400;
    days = 1;
  }
  // 1 Sunday to 7 Saturday: at most six days to pass to the next one it rings on.
  for (int i = 0; i < 6 && !rings_on((now.weekday - 1 + days) % 7 + 1); ++i) {
    delta += 86400;
    ++days;
  }
  return epoch + static_cast<uint32_t>(delta);
}

std::string AlarmClock::time_text(uint32_t epoch) const {
  if (epoch) return tessera::clock_text(next_ring(epoch));
  char text[8];
  snprintf(text, sizeof text, "%d:%02d", hour(), minute());
  return text;
}

std::string AlarmClock::status_text(uint32_t epoch) const {
  if (ringing_) return text("ringing");
  if (snooze_until_) return tessera::fill(text("snoozed_until"), "time", tessera::clock_text(snooze_until_));
  if (!enabled()) return text("off");
  if (!epoch) return text("no_clock");
  const uint32_t next = next_ring(epoch);
  const long left = static_cast<long>(next - epoch);
  if (left >= 86400) return tessera::date_text(next);
  // A countdown that moves every second: 8:12:40, and 12:40 in the last hour.
  char count[16];
  if (left >= 3600) snprintf(count, sizeof count, "%ld:%02ld:%02ld", left / 3600, left / 60 % 60, left % 60);
  else snprintf(count, sizeof count, "%ld:%02ld", left / 60, left % 60);
  return tessera::fill(text("rings_in"), "time", count);
}

void AlarmClock::start_ring(uint32_t epoch) {
  ringing_ = true;
  close_at_ = 0;
  ring_started_ = epoch;
  snooze_until_ = 0;
  // At the alarm's own volume while it rings, whatever volume the screen was left at; end_ring() puts that back.
  alarm_volume(true);
  tessera::show_popup(this, [this]() { return new RingPopup(this); });
  tessera::refresh();
}

// The speaker at the alarm's own volume (the number "Alarm volume"), or back at the volume it had before.
void AlarmClock::alarm_volume(bool on) {
#ifdef USE_SPEAKER
  if (speaker_ == nullptr) return;
  if (on && !loud_ && ring_volume_ != nullptr && ring_volume_->state == ring_volume_->state) {
    volume_before_ = speaker_->get_volume();
    speaker_->set_volume(std::min(1.0f, std::max(0.0f, ring_volume_->state / 100.0f)));
    loud_ = true;
  } else if (!on && loud_) {
    speaker_->set_volume(volume_before_);
    loud_ = false;
  }
#endif
}

void AlarmClock::end_ring() {
  ringing_ = false;
  alarm_volume(false);
  // The popup stays for a moment: closed at once, the rest of the same touch would land on the tile under it.
  close_at_ = millis() + CLOSE_AFTER_MS;
  if (!close_at_) close_at_ = 1;
  tessera::refresh();
}

void AlarmClock::snooze() {
  if (!ringing_) return;
  const uint32_t epoch = tessera::epoch();
  end_ring();
  snooze_until_ = epoch ? epoch + static_cast<uint32_t>(snooze_minutes()) * 60 : 0;
}

void AlarmClock::stop() {
  snooze_until_ = 0;
  if (ringing_) end_ring();
}

void AlarmClock::on_interval(uint32_t now_ms) {
  const uint32_t epoch = tessera::epoch();
  if (close_at_ && static_cast<int32_t>(now_ms - close_at_) >= 0) {
    close_at_ = 0;
    if (!ringing_) tessera::close_popup(this);
  }
  // Turned off (on the glass or from Home Assistant, as when a guest leaves): a ring or a waiting snooze ends too.
  if (!enabled() && (ringing_ || snooze_until_)) stop();
  if (epoch) {
    if (ringing_) {
      // Nobody answered: it goes quiet by itself.
      if (epoch - ring_started_ >= RING_SECONDS) end_ring();
    } else if (snooze_until_ && epoch >= snooze_until_) {
      start_ring(epoch);
    } else if (enabled() && epoch / 60 != handled_minute_) {
      const tessera::LocalTime now = tessera::local_time(epoch);
      if (now.hour == hour() && now.minute == minute()) {
        handled_minute_ = epoch / 60;
        if (rings_on(now.weekday)) start_ring(epoch);
      }
    }
  }
#ifdef USE_SPEAKER
  // The beeps again every two seconds while the alarm rings, unless its switch is off.
  const tessera::Sound *sound = chosen();
  const bool allowed = sound != nullptr && (sound_on_ == nullptr || sound_on_->state);
  const bool sounding = ringing_ && allowed;
#ifdef USE_SELECT
  // Another sound was chosen, on the card or in Home Assistant: it plays once, to be heard.
  const int tone = tone_ != nullptr ? static_cast<int>(tone_->active_index().value_or(0)) : 0;
  if (heard_tone_ < 0) {
    heard_tone_ = tone;
  } else if (tone != heard_tone_) {
    heard_tone_ = tone;
    if (!ringing_ && allowed) {
      sounds_.stop();
      // Heard as loud as the alarm will ring, not at the screen's volume.
      alarm_volume(true);
      sounds_.play(*sound);
      preview_until_ = now_ms + 9000;
      if (!preview_until_) preview_until_ = 1;
    }
  }
#endif
  const bool previewing = preview_until_ != 0;
  // Over once the sound has played (it needs a moment to start), and at the latest after nine seconds.
  const bool played = preview_until_ && static_cast<int32_t>(now_ms - (preview_until_ - 9000)) >= 800 && !sounds_.playing();
  if (preview_until_ && (ringing_ || !allowed || played || static_cast<int32_t>(now_ms - preview_until_) >= 0)) preview_until_ = 0;
  if (previewing && !preview_until_ && !ringing_) alarm_volume(false);
  if (sounding && !sounds_.playing() && now_ms - rang_at_ >= 2600) {
    sounds_.play(*sound);
    rang_at_ = now_ms;
  } else if (!sounding && !preview_until_ && sounds_.playing()) {
    sounds_.stop();
  }
#endif
  if (ringing_sensor_ != nullptr && (!ringing_sensor_->has_state() || ringing_sensor_->state != ringing_))
    ringing_sensor_->publish_state(ringing_);
}

#ifdef USE_SPEAKER
const tessera::Sound *AlarmClock::chosen() const {
  size_t index = 0;
#ifdef USE_SELECT
  if (tone_ != nullptr) index = tone_->active_index().value_or(0);
#endif
  return index < static_cast<size_t>(SOUNDS) ? &alarm_[index] : nullptr;
}
#endif

bool AlarmClock::has_tone() const {
#if defined(USE_SPEAKER) && defined(USE_SELECT)
  return tone_ != nullptr;
#else
  return false;
#endif
}

std::string AlarmClock::tone_name() const {
#if defined(USE_SPEAKER) && defined(USE_SELECT)
  if (tone_ != nullptr) return tone_->at(tone_->active_index().value_or(0)).value_or(std::string());
#endif
  return std::string();
}

void AlarmClock::next_tone() {
#if defined(USE_SPEAKER) && defined(USE_SELECT)
  if (tone_ == nullptr) return;
  auto call = tone_->make_call();
  call.select_next(true);
  call.perform();
  tessera::refresh();
#endif
}

bool AlarmClock::settings(tessera::SettingsPage &page) {
  page.icon = "\U000F0020";
  page.toggle(text("alarm"), [this] { return enabled(); }, [this](bool on) { set_enabled(on); });
  page.number(text("snooze_minutes"), 1, 30, 1, text("minutes_unit"), [this] { return snooze_minutes(); },
              [this](int v) {
                if (snooze_ == nullptr) return;
                auto call = snooze_->make_call();
                call.set_value(v);
                call.perform();
              });
#ifdef USE_SPEAKER
  if (sound_on_ != nullptr)
    page.toggle(text("sound"), [this] { return sound_on_->state; },
                [this](bool on) { on ? sound_on_->turn_on() : sound_on_->turn_off(); });
#endif
  return true;
}

}  // namespace esphome::alarm_clock
