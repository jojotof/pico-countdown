# Pico Countdown

A countdown timer for Raspberry Pi Pico with ST7735 TFT display using Arduino framework and TFT_eSPI library.

## Hardware

- **Board**: Raspberry Pi Pico
- **Display**: ST7735 128x160 TFT LCD (AZDelivery)
- **Button**: Push button on GPIO0

## Wiring

| ST7735 Pin | Pico GPIO |
|------------|-----------|
| MOSI       | GP3       |
| SCLK       | GP2       |
| CS         | GP5       |
| DC         | GP4       |
| RST        | GP6       |
| VCC        | 3.3V      |
| GND        | GND       |

| Component  | Pico GPIO |
|------------|-----------|
| Button     | GP0       |

## Features

- Countdown from 60 to 0 (J-60 to J-0)
- Persistent counter stored in flash memory
- Circular gauge with color coding:
  - 🟢 Green: 30-60
  - 🟠 Orange: 15-29
  - 🔴 Red: 0-14
- Smooth antialiased graphics
- Button controls:
  - Press: Decrement counter
  - Hold: Auto-repeat decrement
- Auto-decrement on power-up

## Build & Upload

```bash
# Build
platformio run -e pico

# Upload
platformio run -e pico -t upload
```

## Libraries

- TFT_eSPI 2.5.43
- Arduino-mbed framework for Pico
