# Alarm clock

An alarm clock for a Tessera screen. Set it on the glass or from Home Assistant; when it rings the screen wakes and
shows **Snooze** and **Stop**, and on a screen with a speaker it plays the sound you chose.

- A tile that shows the alarm's time: **Off** in red, or **Daily** or **Weekdays** in green with when it rings next.
- A card to set it: the time it is now, the alarm's time, and keys for an hour, ten minutes and a minute.
- A popup when it rings, with a pulsing bell, **Snooze** and **Stop**.
- An item for the top bar: an alarm clock, green with the alarm's time while it is on, grey while it is off.
- Four sounds to choose from.
- Every screen has an alarm of its own, and every part of it is an entity in Home Assistant.

## Set up

1. Add the plugin to a screen in Tessera. The screen builds once.
2. In Layout, place the **Alarm** tile from the library's Plugins group.
3. Tap the tile on the screen. Set the time with the keys, tap **Alarm off** to turn it on (the key turns green and
   reads **Alarm on**), and choose the days: **Every day**, **Weekdays** or **Weekends** with one key, or any days with the row Mon to Sun.

## In Home Assistant

The alarm is kept on the screen, and the screen's device in Home Assistant has the same alarm:

| Entity | What it does |
|---|---|
| **Alarm** (switch) | The alarm on or off. Turning it off also ends a ring or a waiting snooze. |
| **Alarm hour**, **Alarm minute** (numbers) | When it rings. |
| **Alarm days** (number) | The days it rings on, as a sum: 1 Monday, 2 Tuesday, 4 Wednesday, 8 Thursday, 16 Friday, 32 Saturday, 64 Sunday. 127 every day, 31 weekdays, 96 the weekend. |
| **Alarm snooze minutes** (number) | How long a snooze lasts: 1 to 30 minutes, 9 unless you change it. |
| **Alarm ringing** (binary sensor) | On while the alarm rings. |
| **Alarm sound** (switch) | The sound on or off; the popup still shows. Only on a screen with a speaker. |
| **Alarm tone** (select) | Which of the sounds rings. Only on a screen with a speaker. |
| **Alarm volume** (number) | How loud the alarm rings, 0 to 100 %, whatever the screen's volume; 80 unless you change it. The screen's volume comes back when the alarm stops. Only on a screen with a speaker. |

Two automations people ask for (replace `bedroom` with your screen's name):

```yaml
# A guest has left: their alarm does not ring for the next one.
- alias: Alarm off at checkout
  triggers:
    - trigger: time
      at: "11:00:00"
  actions:
    - action: switch.turn_off
      target:
        entity_id: switch.bedroom_alarm

# The lights come on with the alarm.
- alias: Lights with the alarm
  triggers:
    - trigger: state
      entity_id: binary_sensor.bedroom_alarm_ringing
      to: "on"
  actions:
    - action: light.turn_on
      target:
        entity_id: light.bedroom
```

## Good to know

- The alarm rings from the screen's own clock, also while Home Assistant is away.
- Closing the popup any other way than with **Stop** snoozes it, so a finger that missed never ends the alarm.
  Unanswered, it rings for ten minutes.
- While a snooze waits, the alarm's card has **Cancel snooze**.
- On a screen with a speaker the card has a **Sound** key: each tap takes the next sound and plays it once. **Alarm
  tone** in Home Assistant does the same.
- The sound needs a speaker on the screen. It rings, and a sound you choose plays, at **Alarm volume**, not the screen's volume. Without one the alarm still wakes the
  screen and shows the popup.

## Sounds

Beeps, Gentle chime, Rising and Classic alarm. All four were made for this plugin and are MIT like the rest.

## Tried on

- Guition JC4880P443 (ESP32-P4, 4.3 inch): the tile, the card, the popup, snooze and the entities in Home Assistant.
- The sound is tested on a Guition JC4880P443 (ESP32-P4, 4.3 inch) with a 4 ohm 3 watt speaker on its speaker socket.
  At full volume that speaker distorts, so the alarm rings at 80 % unless you set **Alarm volume** otherwise.

## Problems and ideas

Open an issue in this repository, with the screen's model and the plugin's version.
