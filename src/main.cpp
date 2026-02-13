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
int maxCounter = 60;  // Maximum value for the gauge (configurable)
bool button_pressed = false;
bool counter_changed = false;  // Track if counter changed during button press
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
const uint32_t initial_hold_delay = 500;  // 500ms before auto-repeat
const uint32_t repeat_delay = 150;        // 150ms between each decrement

// Config mode constants
const uint32_t config_debounce = 200;     // Debounce delay for config mode
const uint32_t config_hold_to_save = 2000; // Hold 2s to save in config mode

// Flash data validation limits
const int min_counter_value = 0;
const int max_counter_value = 999;        // Maximum value for counter (config and validation)

// Counter initialisation value (set by build script)
#ifndef INIT_COUNTER
#define INIT_COUNTER 60
#endif

#ifndef INIT_MAX_COUNTER
#define INIT_MAX_COUNTER 60
#endif

#ifndef BUILD_ID
#define BUILD_ID 0
#endif

struct FlashData {
  uint32_t build_id;
  int32_t counter;
  int32_t maxCounter;  // Maximum value for the gauge
};

// Function to read counter from flash
bool readCounterFromFlash(int &counter, int &maxCounter) {
  FlashData data;
  memcpy(&data, flash_target_contents, sizeof(data));
  
  // Check if this is the same build (not a fresh upload)
  if (data.build_id == BUILD_ID && 
      data.counter >= min_counter_value && data.counter <= max_counter_value) {
    
    counter = (int)data.counter;
    
    // Load maxCounter with validation
    if (data.maxCounter >= min_counter_value && data.maxCounter <= max_counter_value) {
      maxCounter = (int)data.maxCounter;
    } else {
      maxCounter = INIT_MAX_COUNTER;  // Fallback to default if invalid
    }
    
    return true;  // Normal boot
  }
  
  // Fresh upload or invalid data
  counter = INIT_COUNTER;
  maxCounter = INIT_MAX_COUNTER;
  return false;  // First boot
}

// Function to save counter to flash
void saveCounterToFlash(int value, int maxValue) {
  FlashData data;
  data.build_id = BUILD_ID;
  data.counter = value;
  data.maxCounter = maxValue;
  
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
  Serial.print(value);
  Serial.print(" / Max: ");
  Serial.println(maxValue);
}

// Function to draw smooth circular gauge
void drawCircularGauge(int value, int maxValue, bool updateText = true) {
  static bool gauge_initialized = false;
  static int last_angle = -1;
  static uint16_t last_gauge_color = TFT_GREEN;
  static uint32_t last_blink_time = 0;
  static bool blink_state = false;
  const uint32_t blink_interval = 500; // Blink every 500ms

  // Calculate gauge angle (0-300 degrees)
  int angle = (int)((float)value / maxValue * 300.0f);

  // Determine gauge color based on percentage of maxValue
  uint16_t inverseColor = TFT_DARKGREY; // Color for the unfilled portion of the gauge
  uint16_t gaugeColor;
  float percentage = (float)value / maxValue * 100.0f;
  
  if (value == 0) {
    gaugeColor = TFT_RED;
  } else if (percentage <= 20.0f) {
    gaugeColor = TFT_RED;
  } else if (percentage <= 40.0f) {
    gaugeColor = TFT_ORANGE;
  } else if (percentage <= 60.0f) {
    gaugeColor = TFT_GREENYELLOW;
  } else {
    gaugeColor = TFT_GREEN;
  }

  // Handle blinking for value == 0
  if (value == 0) {
    if (current_time - last_blink_time >= blink_interval) {
      last_blink_time = current_time;
      blink_state = !blink_state;
      
      // Draw blinking gauge
      uint16_t blinkColor = blink_state ? TFT_RED : TFT_DARKGREY;
      tft.drawSmoothArc(centerX, centerY, outerRadius, innerRadius, 30, 330, blinkColor, bgColor, false);
    }
  }

  bool colorChanged = (gaugeColor != last_gauge_color);
  bool resetNeeded = (!gauge_initialized || angle > last_angle);

  if (resetNeeded) {
    tft.fillScreen(bgColor);
    tft.drawSmoothArc(centerX, centerY, outerRadius + 5, outerRadius + 3, 0, 360, TFT_WHITE, bgColor, false);
    
    // Special case for value == 0: full red gauge
    if (value == 0) {
      tft.drawSmoothArc(centerX, centerY, outerRadius, innerRadius, 30, 330, TFT_RED, bgColor, false);
    } else {
      tft.drawSmoothArc(centerX, centerY, outerRadius, innerRadius, 30, 330, gaugeColor, bgColor, false);
      if (angle < 300) {
        tft.drawSmoothArc(centerX, centerY, outerRadius, innerRadius, 30 + angle, 330, inverseColor, bgColor, false);
      }
    }

    gauge_initialized = true;
    last_gauge_color = gaugeColor;
  } else if (angle < last_angle) {
    // Redraw complete gray arc to avoid antialiasing artifacts
    if (value == 0) {
      // Special case: draw full red gauge when reaching 0
      tft.drawSmoothArc(centerX, centerY, outerRadius, innerRadius, 30, 330, TFT_RED, bgColor, false);
    } else if (angle < 300) {
      tft.drawSmoothArc(centerX, centerY, outerRadius, innerRadius, 30 + angle, 330, inverseColor, bgColor, false);
    }
  }
  
  // Handle color change without full screen refresh
  if (colorChanged && gauge_initialized && value != 0) {
    tft.drawSmoothArc(centerX, centerY, outerRadius, innerRadius, 30, 30 + angle, gaugeColor, bgColor, false);
    last_gauge_color = gaugeColor;
  }

  // Update text only if requested
  if (updateText) {
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
  }

  last_angle = angle;
}

// Configuration mode: allows user to set maxCounter
void configMode() {
  Serial.println("\n=== CONFIG MODE ===");
  Serial.println("Button held at startup - entering configuration mode");
  Serial.println("Press button to increment max counter value");
  
  maxCounter = 1;  // Start at 1
  bool config_button_pressed = false;
  uint32_t last_config_press = 0;
  
  // Display configuration screen
  tft.fillScreen(bgColor);
  tft.setTextDatum(MC_DATUM);
  tft.setFreeFont(&FreeSansBold18pt7b);
  tft.setTextColor(TFT_YELLOW, bgColor);
  tft.drawString("CONFIG", centerX, centerY - 40);
  
  // Wait for button release before starting
  while (digitalRead(BUTTON_PIN) == LOW) {
    delay(10);
  }
  delay(200);  // Extra delay after release
  
  // Display initial value
  char buffer[32];
  snprintf(buffer, sizeof(buffer), "Max: %d", maxCounter);
  tft.setTextColor(TFT_WHITE, bgColor);
  tft.drawString(buffer, centerX, centerY);
  
  tft.setTextColor(TFT_CYAN, bgColor);
  tft.setFreeFont(&FreeSans9pt7b);
  tft.drawString("Hold 2s to save", centerX, centerY + 40);
  
  Serial.print("Max counter: ");
  Serial.println(maxCounter);
  
  uint32_t hold_start_time = 0;
  bool is_holding = false;
  
  // Configuration loop
  while (true) {
    bool button_state = (digitalRead(BUTTON_PIN) == LOW);
    uint32_t now = millis();
    
    if (button_state) {
      if (!config_button_pressed) {
        // Button just pressed
        config_button_pressed = true;
        hold_start_time = now;
        is_holding = true;
      } else if (is_holding) {
        // Check if held for configured duration
        if (now - hold_start_time >= config_hold_to_save) {
          // Save and exit config mode
          Serial.println("\nSaving configuration...");
          counter = maxCounter;  // Reset counter to max value
          saveCounterToFlash(counter, maxCounter);
          Serial.print("Configuration saved - Max counter: ");
          Serial.println(maxCounter);
          
          // Show confirmation
          tft.fillScreen(bgColor);
          tft.setTextColor(TFT_GREEN, bgColor);
          tft.setFreeFont(&FreeSansBold18pt7b);
          tft.drawString("SAVED!", centerX, centerY);
          delay(1000);
          
          return;  // Exit config mode
        }
      }
    } else {
      // Button released
      if (config_button_pressed && is_holding) {
        uint32_t hold_duration = now - hold_start_time;
        
        if (hold_duration < config_hold_to_save) {
          // Short press: increment maxCounter
          if (now - last_config_press >= config_debounce) {
            last_config_press = now;
            maxCounter++;
            if (maxCounter > max_counter_value) {
              maxCounter = 1;
            }
            
            // Update display
            tft.fillRect(0, centerY - 20, tft.width(), 40, bgColor);
            tft.setTextColor(TFT_WHITE, bgColor);
            tft.setFreeFont(&FreeSansBold18pt7b);
            snprintf(buffer, sizeof(buffer), "Max: %d", maxCounter);
            tft.drawString(buffer, centerX, centerY);
            
            Serial.print("Max counter: ");
            Serial.println(maxCounter);
          }
        }
      }
      config_button_pressed = false;
      is_holding = false;
    }
    
    delay(10);
  }
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
    if (counter != 0) {
      if (end > 360) {
        // Draw arc in two parts
        spinnerNewSprite.drawSmoothArc(spriteCenter, spriteCenter, outerRadius + 5, outerRadius + 3, start, 360, TFT_BLUE, bgColor, true);
        spinnerNewSprite.drawSmoothArc(spriteCenter, spriteCenter, outerRadius + 5, outerRadius + 3, 0, end - 360, TFT_BLUE, bgColor, true);
      } else {
        spinnerNewSprite.drawSmoothArc(spriteCenter, spriteCenter, outerRadius + 5, outerRadius + 3, start, end, TFT_BLUE, bgColor, true);
      }
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
  
  // Check if button is held at startup (enter config mode)
  delay(100);  // Small delay to stabilize button reading
  if (digitalRead(BUTTON_PIN) == LOW) {
    // Button held at startup - enter config mode
    configMode();
    // After config mode, continue with normal initialization
  }
  
  // Load counter from flash
  readCounterFromFlash(counter, maxCounter);
  
  Serial.print("Counter loaded: ");
  Serial.print(counter);
  Serial.print(" / Max: ");
  Serial.println(maxCounter);

  // Initial spinner display
  drawSpinnerAnimation();
  
  // Display initial counter with current value
  drawCircularGauge(counter, maxCounter);
  
  // Decrement counter for next boot
  counter--;
  if (counter < 0) {
    counter = maxCounter;
  }
  
  // Save decremented counter for next boot
  saveCounterToFlash(counter, maxCounter);
  
  Serial.print("Counter saved for next boot: ");
  Serial.println(counter);
  
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
        counter = maxCounter;
      }
      counter_changed = true;  // Mark that counter has changed
      
      Serial.print("Counter: ");
      Serial.println(counter);
      drawCircularGauge(counter, maxCounter);
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
            counter = maxCounter;
          }
          counter_changed = true;  // Mark that counter has changed
          
          Serial.print("Counter (auto): ");
          Serial.println(counter);
          drawCircularGauge(counter, maxCounter);
        }
      }
    }
  } else {
    // Button released
    if (button_pressed && counter_changed) {
      // Save to flash only when button is released after changes
      saveCounterToFlash(counter, maxCounter);
      counter_changed = false;
    }
    button_pressed = false;
  }
  
  // Update spinner animation
  drawSpinnerAnimation();
  
  // Handle blinking when counter is at 0 (without updating text)
  if (counter == 0) {
    drawCircularGauge(counter, maxCounter, false);
  }
  
  delay(10); // Small delay to avoid CPU overload
}