# Smart Switchboard - Wiring and Setup Guide

This guide walks you through wiring, flashing, and using the smart switchboard.
It is designed to drop into an existing traditional switchboard without replacing it.
The physical switches and relays work completely independently of Wi-Fi, so nothing
breaks if the network goes down or the phone is not nearby.

---

## What it does

- Controls up to 4 mains loads (lights, fans, etc.) via 4-channel relay module
- Physical wall switches still work as normal (they toggle the relay, not interrupt mains)
- Phone control via local Wi-Fi, no internet required
- Optional internet control via MQTT
- Relay states are saved to flash, so they survive power cuts

---

## Parts list

| Part                          | Notes                                |
|-------------------------------|--------------------------------------|
| ESP32 Dev Board               | Any 30-pin or 38-pin variant         |
| 4-channel relay module        | 5V coil, active LOW                  |
| 4x momentary or toggle switch | Your existing wall switches          |
| 5V USB power supply or HI-LINK| Power the ESP32 from mains           |
| Wire                          | Match existing wiring gauge          |

---

## Pin connections

### Relay module to ESP32

| Relay channel | ESP32 GPIO | Relay pin |
|---------------|------------|-----------|
| Channel 1     | GPIO 26    | IN1       |
| Channel 2     | GPIO 27    | IN2       |
| Channel 3     | GPIO 14    | IN3       |
| Channel 4     | GPIO 12    | IN4       |
| Power         | 5V / VIN   | VCC       |
| Ground        | GND        | GND       |

The relay module is active LOW. The firmware drives the pin LOW to energise
the relay (load ON) and HIGH to release it (load OFF).

### Wall switches to ESP32

Each switch connects between a GPIO pin and GND.
The firmware uses INPUT_PULLUP, so the pin reads HIGH when the switch is open
and LOW when it is pressed or closed.

| Switch   | ESP32 GPIO | Other terminal |
|----------|------------|----------------|
| Switch 1 | GPIO 34    | GND            |
| Switch 2 | GPIO 35    | GND            |
| Switch 3 | GPIO 32    | GND            |
| Switch 4 | GPIO 33    | GND            |

Note: GPIOs 34-39 on the ESP32 are input-only. They work fine here since we only
read them. They do not have internal pull-ups on some boards, so add a 10k
resistor from each pin to 3.3V if the readings seem noisy.

### Mains wiring at the relay

SAFETY NOTE: Only work on the relay mains side when the circuit breaker is OFF.
Have a qualified electrician do this if you are not comfortable with mains wiring.

```
Live (mains) ──┬── Relay COM (all 4 channels)
               |
               |
Each relay NO ─┤── Load (light/fan) ──── Neutral
```

The relay Normally Open (NO) contact is used. When the relay energises, it
closes and completes the circuit. Neutral wire runs straight through unchanged.

---

## Software setup (PlatformIO)

1. Install VS Code and the PlatformIO extension.

2. Open the smart-switchboard folder in VS Code.

3. Edit `src/main.cpp` and set your Wi-Fi credentials if you want the
   ESP32 to also join your home router:

   ```cpp
   const char* STA_SSID     = "YourHomeWiFi";
   const char* STA_PASSWORD = "YourPassword";
   ```

   If you leave these empty, the ESP32 still creates its own hotspot
   (SmartBoard / switch1234) and the phone can connect directly to that.

4. Flash the firmware:
   ```
   pio run --target upload
   ```

5. Upload the web dashboard files:
   ```
   pio run --target uploadfs
   ```

---

## Connecting from a phone

### Without internet (access point mode)

1. On the phone, go to Wi-Fi settings.
2. Connect to the network named SmartBoard, password: switch1234.
3. Open a browser and go to http://192.168.4.1
4. The control panel opens. Tap any switch to toggle the relay.
5. Physical wall switches still work in parallel.

### With internet (home router mode)

1. Set STA_SSID and STA_PASSWORD in main.cpp and reflash.
2. The ESP32 connects to your router and gets an IP address.
3. Check the serial monitor at 115200 baud to see the IP printed at startup.
4. Open that IP in any browser on the same network.
5. You can also set up port forwarding on your router to reach it from outside.

### MQTT (full internet control)

1. In `platformio.ini`, uncomment the PubSubClient library line.
2. Uncomment `build_flags = -D MQTT_ENABLED` in platformio.ini.
3. Edit the MQTT settings near the top of main.cpp:
   ```cpp
   const char* MQTT_SERVER = "your.broker.address";
   ```
4. Reflash. The device subscribes to `home/switchboard/cmd`.
5. Send JSON to control a channel:
   ```json
   {"ch": 0, "on": 1}   <- turn channel 1 on
   {"ch": 2, "on": 0}   <- turn channel 3 off
   {"ch": 1}            <- toggle channel 2
   ```
6. Status is published to `home/switchboard/status` after every change.

---

## How physical switches interact with relays

Each time a switch changes state (pressed or released), the firmware toggles
the matching relay. This means the switch does not need to be wired in series
with the load at all. The ESP32 reads the switch and drives the relay.

This is important: the switch never carries mains voltage. It only signals the
ESP32 at 3.3V logic level. This is safer and means the switch works just the
same whether Wi-Fi is on or off.

If the ESP32 loses power, the relay drops out (load turns off). The last state
is saved to flash and restores on the next boot.

---

## Troubleshooting

**Relay clicks but load does not turn on**
Check that you have wired to the NO (Normally Open) terminal, not NC.

**Switch does not respond**
Check the GPIO number in SWITCH_PIN array matches your wiring.
On GPIOs 34-39, add a 10k pull-up resistor to 3.3V if you have not already.

**Web page does not load**
Make sure you ran `pio run --target uploadfs` after the firmware upload.
Open the serial monitor to confirm the IP address printed at boot.

**Relay stays on after power cut**
The saved state is restored on boot. If you want all relays to start OFF
regardless of saved state, comment out the `loadState()` call in setup().

---

## Changing pin assignments

All pins are defined at the top of `src/main.cpp`:

```cpp
const int RELAY_PIN[4]  = {26, 27, 14, 12};
const int SWITCH_PIN[4] = {34, 35, 32, 33};
```

Change these to match whichever GPIOs suit your board layout.
Avoid GPIOs 0, 2, 15 during flashing (strapping pins) and GPIOs 6-11 (flash).
