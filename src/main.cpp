#include <Arduino.h>
#include "TFT_eSPI.h"
#include "hardware/flash.h"
#include "hardware/sync.h"

// Button configuration
#define BUTTON_PIN 0  // GPIO0 for button

// Custom color RGB(64, 64, 64) in RGB565
#define TFT_DARKER 0x4208

// Flash address to save counter (last sector before flash size)
#define FLASH_TARGET_OFFSET (PICO_FLASH_SIZE_BYTES - FLASH_SECTOR_SIZE)
const uint8_t *flash_target_contents = (const uint8_t *) (XIP_BASE + FLASH_TARGET_OFFSET);

// TFT screen initialization
TFT_eSPI tft = TFT_eSPI();

// Global variables
int counter = 60;
bool button_pressed = false;
uint32_t last_press_time = 0;
uint32_t button_hold_time = 0;

// Timing constants
const uint32_t debounce_delay = 150;      // 150ms debounce delay
const uint32_t initial_hold_delay = 500;  // 500ms before auto-repeat
const uint32_t repeat_delay = 150;        // 150ms between each decrement

// Counter initialisation value (set by build script)
#ifndef INIT_COUNTER
#define INIT_COUNTER 60
#endif

#ifndef BUILD_ID
#define BUILD_ID 0
#endif

struct FlashData {
  uint32_t build_id;
  int32_t counter;
};

// Function to read counter from flash
bool readCounterFromFlash(int &counter) {
  FlashData data;
  memcpy(&data, flash_target_contents, sizeof(data));
  
  // Check if this is the same build (not a fresh upload)
  if (data.build_id == BUILD_ID && data.counter >= 0 && data.counter <= 9999) {
    counter = (int)data.counter;
    return true;  // Normal boot
  }
  
  // Fresh upload or invalid data
  counter = INIT_COUNTER;
  return false;  // First boot
}

// Function to save counter to flash
void saveCounterToFlash(int value) {
  FlashData data;
  data.build_id = BUILD_ID;
  data.counter = value;
  
  uint8_t buffer[FLASH_PAGE_SIZE];
  memcpy(buffer, &data, sizeof(data));
  memset(buffer + sizeof(data), 0xFF, FLASH_PAGE_SIZE - sizeof(data));
  
  // Disable interrupts during flash write
  uint32_t ints = save_and_disable_interrupts();
  
  // Erase sector
  flash_range_erase(FLASH_TARGET_OFFSET, FLASH_SECTOR_SIZE);
  
  // Write data
  flash_range_program(FLASH_TARGET_OFFSET, buffer, FLASH_PAGE_SIZE);
  
  // Restore interrupts
  restore_interrupts(ints);
  
  Serial.print("Counter saved: ");
  Serial.println(value);
}

// Function to draw smooth circular gauge
void drawCircularGauge(int value, int maxValue) {
  static bool gauge_initialized = false;
  static int last_angle = -1;

  int centerX = tft.width() / 2;
  int centerY = tft.height() / 2;
  int outerRadius = min(centerX, centerY) - 5;
  int innerRadius = outerRadius - 14;

  uint16_t bgColor = TFT_BLACK;
  uint16_t baseColor = TFT_GREEN;
  uint16_t inverseColor = TFT_DARKGREY;

  // Calculate gauge angle (0-300 degrees)
  int angle = (int)((float)value / maxValue * 300.0f);

  bool resetNeeded = (!gauge_initialized || angle > last_angle);

  if (resetNeeded) {
    tft.fillScreen(bgColor);
    tft.drawSmoothArc(centerX, centerY, outerRadius + 4, outerRadius + 3, 0, 360, TFT_WHITE, bgColor, false);
    tft.drawSmoothArc(centerX, centerY, outerRadius, innerRadius, 30, 330, baseColor, bgColor, false);

    if (angle < 300) {
      tft.drawSmoothArc(centerX, centerY, outerRadius, innerRadius, 30 + angle, 330, inverseColor, bgColor, false);
    }

    gauge_initialized = true;
  } else if (angle < last_angle) {
    // Redraw complete gray arc to avoid antialiasing artifacts
    if (angle < 300) {
      tft.drawSmoothArc(centerX, centerY, outerRadius, innerRadius, 30 + angle, 330, inverseColor, bgColor, false);
    }
  }

  // Display "J-xx" text in center
  char buffer[16];
  snprintf(buffer, sizeof(buffer), "J-%d", value);

  tft.setTextDatum(MC_DATUM); // Middle Center
  tft.setFreeFont(&FreeSansBold18pt7b);

  // Clear text area with fixed bounds (large enough for "J-000")
  int16_t text_box_w = 80;
  int16_t text_box_h = 30;
  tft.fillRect(centerX - text_box_w/2, centerY - text_box_h/2, text_box_w, text_box_h, bgColor);
  
  tft.setTextColor(TFT_WHITE, bgColor);
  tft.drawString(buffer, centerX, centerY);

  last_angle = angle;
}

void setup() {
  Serial.begin(115200);
  delay(100);
  
  Serial.println("Countdown initialization...");
  
  // Initialize TFT screen
  tft.init();
  tft.setRotation(1); // Landscape mode
  
  // Initialize button
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  
  // Load counter from flash
  bool is_normal_boot = readCounterFromFlash(counter);
  
  if (is_normal_boot) {
    Serial.print("Counter loaded: ");
    Serial.println(counter);
    
    // Decrement counter on normal boot
    counter--;
    if (counter < 0) {
      counter = INIT_COUNTER;
    }
    Serial.print("Counter after startup: ");
    Serial.println(counter);
  } else {
    // First boot after upload
    Serial.print("First boot - Counter initialized: ");
    Serial.println(counter);
  }
  
  // Save counter
  saveCounterToFlash(counter);
  
  // Display initial counter
  drawCircularGauge(counter, 60);
  
  Serial.println("Press button to decrement");
}

void loop() {
  // Read button state (LOW = pressed with pull-up)
  bool button_state = (digitalRead(BUTTON_PIN) == LOW);
  uint32_t current_time = millis();
  
  if (button_state) {
    // Button pressed
    if (!button_pressed) {
      // First press
      button_pressed = true;
      button_hold_time = current_time;
      last_press_time = current_time;
      
      // Decrement immediately on first press
      counter--;
      if (counter < 0) {
        counter = 60;
      }
      
      Serial.print("Counter: ");
      Serial.println(counter);
      drawCircularGauge(counter, 60);
      saveCounterToFlash(counter);
    } else {
      // Button held down
      uint32_t hold_duration = current_time - button_hold_time;
      
      // If held long enough, start auto-repeat
      if (hold_duration >= initial_hold_delay) {
        // Check if enough time has elapsed since last decrement
        if (current_time - last_press_time >= repeat_delay) {
          last_press_time = current_time;
          
          // Decrement counter
          counter--;
          if (counter < 0) {
            counter = 60;
          }
          
          Serial.print("Counter (auto): ");
          Serial.println(counter);
          drawCircularGauge(counter, 60);
          saveCounterToFlash(counter);
        }
      }
    }
  } else {
    // Button released
    button_pressed = false;
  }
  
  delay(10); // Small delay to avoid CPU overload
}