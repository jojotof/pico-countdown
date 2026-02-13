# Pico Countdown

A customizable countdown timer for Raspberry Pi Pico with ST7735 TFT display using Arduino framework and TFT_eSPI library.

## Hardware

- **Board**: Raspberry Pi Pico
- **Display**: ST7735 128x160 TFT LCD (AZDelivery)
- **Button**: Push button on GPIO0 (with pull-up)

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

### Configuration Mode
- **Activate**: Hold button during startup
- **Set max counter**: Press button to increment (1-999)
- **Save**: Hold button for 2 seconds
- Counter resets to max value after saving

### Countdown Operation
- Customizable countdown from max value to 0 (e.g., J-60 to J-0)
- Persistent counter and max value stored in flash memory
- Auto-decrement on power-up (after displaying current value)
- Flash writes optimized to preserve memory life (write on button release only)

### Visual Display
- Circular gauge with smooth antialiased graphics
- Animated spinner (stops at J-0)
- Dynamic color coding based on percentage of max value:
  - 🟢 **Green**: > 60%
  - 🟡 **Yellow-Green**: 40-60%
  - 🟠 **Orange**: 20-40%
  - 🔴 **Red**: ≤ 20% (blinks at 0)

### Button Controls
- **Short press**: Decrement counter
- **Hold**: Auto-repeat decrement (after 500ms)
- **Startup hold**: Enter configuration mode

### Build Configuration
- Auto-calculation of initial counter from target date
- `calculate_counter.py` script sets INIT_COUNTER and INIT_MAX_COUNTER
- Default max value: 60 days
- Target date: April 3, 2026

## Build & Upload

```bash
# Build (auto-calculates days from target date)
platformio run -e pico

# Upload
platformio run -e pico -t upload
```

### Customizing Target Date
Edit `calculate_counter.py` to change the target date:
```python
target_date = datetime(2026, 4, 3)  # Year, Month, Day
```

### Customizing Default Max Counter
Edit `calculate_counter.py` to change default max value:
```python
("INIT_MAX_COUNTER", 60),  # Change 60 to your desired default
```

## Usage

### First Boot
1. Upload the firmware
2. Counter initializes to calculated days until target date
3. Max counter initializes to 60 (or custom value)
4. Counter decrements by 1 on each subsequent boot

### Configuration Mode
1. Power off the Pico
2. Hold the button while powering on
3. "CONFIG" appears on screen
4. Release button
5. Press button repeatedly to increment max value (1-999)
6. Hold button for 2 seconds to save
7. "SAVED!" confirmation appears
8. Counter resets to new max value

### Normal Operation
- Each boot: displays current value, then decrements for next boot
- Press button: manually decrement counter
- Counter wraps from 0 to max value

## Technical Details

### Flash Memory Management
- Stores: counter value, max counter, build ID
- Location: Last sector of flash memory
- Optimizations:
  - No flash write on button press (RAM only)
  - Single flash write on button release
  - Prevents excessive wear (~100K write cycle limit)
  - Build ID prevents data corruption on firmware updates

### Color Thresholds
Colors adapt to configured max value using percentages:
- `percentage = (counter / maxCounter) * 100`
- Ensures consistent visual feedback regardless of max value

## Libraries

- TFT_eSPI 2.5.43
- Arduino-mbed framework for Pico
