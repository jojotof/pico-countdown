#include <Arduino.h>
#include "TFT_eSPI.h"
#include "hardware/flash.h"
#include "hardware/sync.h"

// Button configuration
#define BUTTON_PIN 0  // GPIO0 for button

// Flash address to save counter (last sector before flash size)
#define FLASH_TARGET_OFFSET (PICO_FLASH_SIZE_BYTES - FLASH_SECTOR_SIZE)
const uint8_t *flash_target_contents = (const uint8_t *) (XIP_BASE + FLASH_TARGET_OFFSET);

uint32_t current_time = 0;

// TFT screen initialization
TFT_eSPI tft = TFT_eSPI();
TFT_eSprite spinnerOldSprite = TFT_eSprite(&tft);
TFT_eSprite spinnerNewSprite = TFT_eSprite(&tft);
uint16_t bgColor = TFT_BLACK;

// Global variables
int counter = 60;
bool button_pressed = false;
uint32_t last_press_time = 0;
uint32_t button_hold_time = 0;

// Spinner animation configuration
const uint32_t spinner_update_interval = 10;  // Update every 10ms
const float spinner_speed = 4.0;  // Degrees per update
const int spinner_arc_size = 20;  // Arc size in degrees

// Gauge geometry (initialized in setup)
int centerX = 0;
int centerY = 0;
int outerRadius = 0;
int innerRadius = 0;

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
  static uint16_t last_gauge_color = TFT_GREEN;

  // Calculate gauge angle (0-300 degrees)
  int angle = (int)((float)value / maxValue * 300.0f);

  // Determine gauge color based on value
  uint16_t inverseColor = TFT_DARKGREY; // Color for the unfilled portion of the gauge
  uint16_t gaugeColor;
  if (value < 15) {
    gaugeColor = TFT_RED;
  } else if (value < 30) {
    gaugeColor = TFT_ORANGE;
  } else {
    gaugeColor = TFT_GREEN;
  }

  bool colorChanged = (gaugeColor != last_gauge_color);
  bool resetNeeded = (!gauge_initialized || angle > last_angle || colorChanged);

  if (resetNeeded) {
    tft.fillScreen(bgColor);
    tft.drawSmoothArc(centerX, centerY, outerRadius + 5, outerRadius + 3, 0, 360, TFT_WHITE, bgColor, false);
    tft.drawSmoothArc(centerX, centerY, outerRadius, innerRadius, 30, 330, gaugeColor, bgColor, false);

    if (angle < 300) {
      tft.drawSmoothArc(centerX, centerY, outerRadius, innerRadius, 30 + angle, 330, inverseColor, bgColor, false);
    }

    gauge_initialized = true;
    last_gauge_color = gaugeColor;
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

void drawSpinnerAnimation() { 
  static float spinner_angle = 0;
  static uint32_t last_spinner_update = 0;
  static bool sprites_created = false;
  static int spriteSize = 0;
  static int spriteOffsetX = 0;
  static int spriteOffsetY = 0;
  
  if (!sprites_created) {
    // Create sprites for double buffering
    spriteSize = (outerRadius + 6) * 2;
    spriteOffsetX = centerX - outerRadius - 6;
    spriteOffsetY = centerY - outerRadius - 6;
    
    spinnerOldSprite.createSprite(spriteSize, spriteSize);
    spinnerNewSprite.createSprite(spriteSize, spriteSize);
    
    // Initialize old sprite with white circle
    int spriteCenter = outerRadius + 6;
    spinnerOldSprite.fillSprite(bgColor);
    spinnerOldSprite.drawSmoothArc(spriteCenter, spriteCenter, outerRadius + 5, outerRadius + 3, 0, 360, TFT_WHITE, bgColor, false);
    spinnerOldSprite.pushSprite(spriteOffsetX, spriteOffsetY);
    
    sprites_created = true;
  }
  
  if (current_time - last_spinner_update >= spinner_update_interval) {
    last_spinner_update = current_time;
    spinner_angle += spinner_speed;
    if (spinner_angle >= 360) {
      spinner_angle -= 360;
    }
    
    // Draw new frame in newSprite
    int spriteCenter = outerRadius + 6;
    spinnerNewSprite.fillSprite(bgColor);
    spinnerNewSprite.drawSmoothArc(spriteCenter, spriteCenter, outerRadius + 5, outerRadius + 3, 0, 360, TFT_WHITE, bgColor, false);
    
    int start = (int)spinner_angle;
    int end = start + spinner_arc_size;
    
    // Handle wrap-around at 360°
    if (end > 360) {
      // Draw arc in two parts
      spinnerNewSprite.drawSmoothArc(spriteCenter, spriteCenter, outerRadius + 5, outerRadius + 3, start, 360, TFT_BLUE, bgColor, true);
      spinnerNewSprite.drawSmoothArc(spriteCenter, spriteCenter, outerRadius + 5, outerRadius + 3, 0, end - 360, TFT_BLUE, bgColor, true);
    } else {
      spinnerNewSprite.drawSmoothArc(spriteCenter, spriteCenter, outerRadius + 5, outerRadius + 3, start, end, TFT_BLUE, bgColor, true);
    }
    
    // Compare pixels and only update changed ones
    for (int y = 0; y < spriteSize; y++) {
      for (int x = 0; x < spriteSize; x++) {
        uint16_t oldPixel = spinnerOldSprite.readPixel(x, y);
        uint16_t newPixel = spinnerNewSprite.readPixel(x, y);
        
        if (oldPixel != newPixel) {
          tft.drawPixel(spriteOffsetX + x, spriteOffsetY + y, newPixel);
        }
      }
    }
    
    // Swap buffers: copy new to old for next frame
    spinnerNewSprite.pushToSprite(&spinnerOldSprite, 0, 0);
  }
}

void setup() {
  Serial.begin(115200);
  delay(100);
  
  Serial.println("Countdown initialization...");
  
  // Initialize TFT screen
  tft.init();
  tft.setRotation(1); // Landscape mode
  
  // Initialize gauge geometry
  centerX = tft.width() / 2 - 1;
  centerY = tft.height() / 2 - 1;
  outerRadius = min(centerX, centerY) - 6;
  innerRadius = outerRadius - 12;
  
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

  // Initial spinner display
  drawSpinnerAnimation();
  
  // Display initial counter
  drawCircularGauge(counter, 60);
  
  Serial.println("Press button to decrement");
}

void loop() {
  // Read button state (LOW = pressed with pull-up)
  bool button_state = (digitalRead(BUTTON_PIN) == LOW);
  current_time = millis();
  
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
  
  // Update spinner animation
  drawSpinnerAnimation();
  
  delay(10); // Small delay to avoid CPU overload
}