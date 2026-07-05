# Vidloq Audio Firmware

Vidloq Audio is the RTOS firmware for the Heltec Wireless Stick Lite v2.1 audio node used by the Vidloq streaming system. The firmware captures audio from an INMP441 I2S microphone and sends continuous raw PCM audio to the Vidloq Stream Server over Wi-Fi.

<img width="1672" height="941" alt="Vidloq_Audio" src="https://github.com/user-attachments/assets/6d27aab9-4fbd-4bd1-9746-552120e9ce27" />

## What This Project Does

- Connects the Heltec board to a configured Wi-Fi network.
- Captures microphone audio through the INMP441 I2S module.
- Selects the strongest valid microphone channel automatically.
- Streams 16 kHz PCM audio to the Vidloq Stream Server on TCP port `8001`.
- Uses ESP-IDF / FreeRTOS so the firmware demonstrates embedded RTOS task management, networking, and audio capture.

## Hardware

- Heltec Wireless Stick Lite v2.1
- INMP441 I2S microphone module
- USB cable for flashing and serial monitor
- Vidloq Stream Server running at `sparqm.com`

## INMP441 Wiring

| INMP441 Pin | Heltec Pin |
|---|---|
| VDD | 3V3 |
| GND | GND |
| SCK / BCLK | GPIO33 |
| WS / LRCL | GPIO32 |
| SD / DOUT | GPIO36 |
| L/R | GND first; use 3V3 only if the opposite channel is needed |

## Required Setup First

Before building or flashing, open:

```text
main/config.h
```

Set these values first:

```c
#define WIFI_SSID "YOUR_WIFI_NETWORK"
#define WIFI_PASSWORD "YOUR_WIFI_PASSWORD"
#define SERVER_URL "http://sparqm.com"
#define SERVER_HOST "sparqm.com"
#define SERVER_PORT 8001
#define USB_SERIAL_PORT "/dev/cu.usbserial-0001"
```

The Wi-Fi network name, Wi-Fi password, server host, server URL, server port, and USB serial port are intentionally kept in `main/config.h` so the rest of the project does not contain repeated hardcoded connection values.

Do not commit your real Wi-Fi password to GitHub.

## Build, Flash, and Monitor

From the project directory:

```bash
. ~/esp/esp-idf-v5.5.2/export.sh
python3 startup.py --monitor --deep
```

Or use the helper script:

```bash
./flash_with_monitor.sh
```

## Main Files

| File | Purpose |
|---|---|
| `main/config.h` | Required Wi-Fi, server, USB serial port, and audio settings |
| `main/main.c` | RTOS audio capture, Wi-Fi, DNS, and TCP streaming logic |
| `startup.py` | Build, repair, flash, and monitor helper |
| `flash_with_monitor.sh` | Shortcut for flashing and opening the monitor |
| `sdkconfig.defaults` | ESP-IDF project defaults |

## Expected Behavior

After flashing, the Heltec board connects to Wi-Fi, opens a TCP connection to `sparqm.com:8001`, captures INMP441 audio, and sends continuous PCM audio frames to the Vidloq Stream Server.

## Troubleshooting

If the board does not connect to Wi-Fi, confirm the SSID and password in `main/config.h`.

If flashing fails, confirm `USB_SERIAL_PORT` in `main/config.h` matches the port shown by the OS.

If the server receives no audio, confirm the Vidloq Stream Server is running and listening on TCP port `8001`.

If audio is silent, check the INMP441 wiring and try changing the L/R pin from GND to 3V3.
