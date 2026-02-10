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

// Function to read counter from flash
int readCounterFromFlash() {
  uint32_t stored_value = *(uint32_t*)flash_target_contents;
  
  // Check if value is valid (not 0xFFFFFFFF which indicates blank flash)
  if (stored_value == 0xFFFFFFFF || stored_value > 9999) {
    return 60; // Default value
  }
  return (int)stored_value;
}

// Function to save counter to flash
void saveCounterToFlash(int value) {
  uint32_t data = (uint32_t)value;
  uint8_t buffer[FLASH_PAGE_SIZE];
  
  // Prepare buffer with new value
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
  int centerX = tft.width() / 2;
  int centerY = tft.height() / 2;
  int outerRadius = min(centerX, centerY) - 5;
  int innerRadius = outerRadius - 14;
  
  // Black background color
  uint16_t bgColor = TFT_BLACK;
  
  // Clear screen with black background
  tft.fillScreen(bgColor);
  
  // Draw gauge background (full gray arc)
  tft.drawSmoothArc(centerX, centerY, outerRadius + 4 , outerRadius + 3, 0, 360, TFT_WHITE, bgColor, false);
  tft.drawSmoothArc(centerX, centerY, outerRadius, innerRadius, 30, 330, TFT_DARKGREY, bgColor, false);
  
  // Calculate gauge angle (0-360°)
  float angle = (float)value / maxValue * 300.0;
  
  // Draw gauge arc with proper color
  uint16_t gaugeColor;
  if (value < 15) {
    gaugeColor = TFT_RED;
  } else if (value < 30) {
    gaugeColor = TFT_ORANGE;
  } else {
    gaugeColor = TFT_GREEN;
  }
  
  // Draw main arc with antialiasing
  if (angle > 0) {
    tft.drawSmoothArc(centerX, centerY, outerRadius, innerRadius, 30, 30 + (int)angle, gaugeColor, bgColor, false);
  }
  
  // Display "J-xx" text in center
  char buffer[16];
  snprintf(buffer, sizeof(buffer), "J-%d", value);
  
  tft.setTextColor(TFT_WHITE, bgColor);
  tft.setTextDatum(MC_DATUM); // Middle Center
  
  // Use smooth FreeSans font (size 18)
  tft.setFreeFont(&FreeSansBold18pt7b);
  tft.drawString(buffer, centerX, centerY);
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
  counter = readCounterFromFlash();
  Serial.print("Counter loaded: ");
  Serial.println(counter);
  
  // Decrement counter on startup
  counter--;
  if (counter < 0) {
    counter = 60;
  }
  Serial.print("Counter after startup: ");
  Serial.println(counter);
  
  // Save new counter
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