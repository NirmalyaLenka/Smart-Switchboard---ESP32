# Smart Switchboard - ESP32

Drop-in smart control for a traditional switchboard. The physical wall switches
and relays both work at all times. Phone control is a bonus, not a dependency.

## Features

- 4-channel relay control (expandable)
- Physical wall switches work in parallel, always
- Local Wi-Fi web interface, no internet required
- Optional MQTT for internet-based control
- Relay states saved to flash and restored on boot
- Real-time push updates via Server-Sent Events
- Editable channel names in the web UI
- Clean, mobile-friendly control panel

## Quick start

1. Wire up relays and switches per `docs/WIRING_AND_SETUP.md`
2. Open the project in PlatformIO (VS Code extension)
3. Set your Wi-Fi credentials in `src/main.cpp` if needed
4. Run: `pio run --target upload`
5. Run: `pio run --target uploadfs`
6. Connect your phone to the SmartBoard Wi-Fi hotspot (password: switch1234)
7. Open http://192.168.4.1 in a browser

## Project structure

```
smart-switchboard/
  src/
    main.cpp            - ESP32 firmware (Arduino/PlatformIO)
  data/
    index.html          - Web dashboard (uploaded to SPIFFS)
  docs/
    WIRING_AND_SETUP.md - Full wiring guide with pin table
  platformio.ini        - Build configuration
  README.md
```

## Default pin assignments

| Function     | GPIO |
|--------------|------|
| Relay 1      | 26   |
| Relay 2      | 27   |
| Relay 3      | 14   |
| Relay 4      | 12   |
| Switch 1     | 34   |
| Switch 2     | 35   |
| Switch 3     | 32   |
| Switch 4     | 33   |

All pins are defined at the top of `src/main.cpp` and easy to change.

## Control methods

| Method                   | Internet needed | Works offline |
|--------------------------|-----------------|---------------|
| Physical wall switch     | No              | Yes           |
| Local Wi-Fi (AP mode)    | No              | Yes           |
| Home router (STA mode)   | No              | Yes (LAN)     |
| MQTT                     | Yes             | No            |

## MQTT payload format

Send JSON to `home/switchboard/cmd`:

```json
{"ch": 0, "on": 1}
```

- `ch` is 0-indexed (0 to 3)
- `on` is 1 (on) or 0 (off); omit to toggle
- Status is published to `home/switchboard/status` after each change

## Dependencies

Managed automatically by PlatformIO:

- ESP Async WebServer
- AsyncTCP
- ArduinoJson
- PubSubClient (optional, for MQTT)

## Safety note

The wall switches in this system carry only 3.3V logic signals to the ESP32.
They do not interrupt mains voltage directly. Mains wiring is handled entirely
by the relay module. Always isolate the mains circuit at the breaker before
working on relay connections.

## License

MIT
