# Grid 220 Monitor

## Purpose

This ESP32 firmware monitors the availability of the power grid. It listens for periodic UDP heartbeat messages from a second board and clearly signals whether the grid connection is available, lost, starting up, or whether Wi-Fi is unavailable.

## Implementation

The device connects to Wi-Fi, synchronizes its clock with an NTP server, and listens on a configurable UDP port. A valid token received within the configured timeout means that the grid is operating normally. If no valid heartbeat arrives in time, the device switches to the **Grid Lost** state, activates the red LED, and sounds an alarm during configured daytime hours. A hardware button can be held to test the buzzer. The current healthy or failed state is also reported to Healthchecks.io.

## Components Used

- ESP32 development board
- Wi-Fi network connection
- Second ESP32/device sending UDP heartbeat packets
- Green LED — normal grid status
- Red LED — grid-loss status
- Active/passive buzzer driven with a 2 kHz tone
- Push button connected to GND with the ESP32 internal pull-up enabled
- Healthchecks.io endpoint for remote health monitoring
- NTP server (`pool.ntp.org`) for time-based alarm silencing

## Main Arduino Libraries

- `WiFi.h`
- `WiFiUdp.h`
- `WiFiClientSecure.h`
- `HTTPClient.h`
- `time.h`

## Hardware Pins

| Function | GPIO |
| --- | ---: |
| Green LED | 18 |
| Red LED | 19 |
| Buzzer | 21 |
| Test button | 23 |

## Configuration

Before uploading, set the Wi-Fi credentials, shared UDP token, Healthchecks.io URL, timezone settings, and alarm schedule in `boardB_fixed.ino`. Do not commit real Wi-Fi passwords, tokens, or monitoring URLs to a public repository.
