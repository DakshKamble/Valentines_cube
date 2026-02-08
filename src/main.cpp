#include <Adafruit_NeoPixel.h>
#include <Wire.h>
#include <U8g2lib.h>
#include "esp_sleep.h"

// ================= OLED =================
U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, U8X8_PIN_NONE);

// ================= WS2812B =================
#define LED_COUNT 30

#define BUTTON_STRIP_PIN D9
#define BODY_STRIP_PIN   D8

Adafruit_NeoPixel buttonStrip(LED_COUNT, BUTTON_STRIP_PIN, NEO_GRB + NEO_KHZ800);
Adafruit_NeoPixel bodyStrip(LED_COUNT, BODY_STRIP_PIN, NEO_GRB + NEO_KHZ800);

// ================= BUTTONS =================
#define BTN_YES_PIN D1     // WAKE BUTTON
#define BTN_NO_PIN  D2

// GPIO number for ESP32-C3 deep sleep
#define BTN_YES_GPIO GPIO_NUM_3   // D1 on XIAO ESP32-C3

// ================= SLEEP =================
#define INACTIVITY_TIMEOUT 10000UL  // 10 seconds
#define DEBOUNCE_DELAY 50           // 50ms debounce

RTC_DATA_ATTR int bootCount = 0;

// ================= STATE =================
bool yesPressed = false;
bool idleDrawn = false;
int noCount = 0;
bool showingWrongChoice = false;
bool showingSpecialAnimation = false;
bool showingFinalMessage = false;
bool waitingForFinalButtonPress = false;

// Fooled sequence states
enum FooledState {
  NOT_FOOLED,           // Not in the fooled sequence
  WAITING_FOR_PRESS,    // Showing "How about now?" with swapping LEDs
  FOOLED_YOU,           // Showing "Fooled you!" with green button
  FAIR_RIGHT            // Showing "Fair right? Valentine now?"
};

FooledState fooledState = NOT_FOOLED;
bool fooledButtonWasYes = false; // Tracks which button was pressed in the fooled sequence

unsigned long yesStartTime = 0;
unsigned long lastActivityTime = 0;
unsigned long lastYesButtonTime = 0;
unsigned long lastNoButtonTime = 0;
unsigned long wrongChoiceStartTime = 0;
unsigned long lastLedSwapTime = 0;
unsigned long fooledSequenceStartTime = 0;

// ================= NO MESSAGES =================
// Array of funny messages to display when the NO button is pressed
// These messages create a flow, with each one getting more insistent
const char* NO_MESSAGES[] = {
  "Really? Try again!",
  "Are you sure?",
  "Think harder!",
  "How about now?",  // Special message with LED animation
  "Wrong answer!",
  "Not an option!",
  ":("            // Sad emoji as the final message
};

// Fooled sequence messages
const char* FOOLED_MESSAGE = "You pressed GREEN!";
const char* FOOLED_QUESTION_LINE1 = "Fair right?";
const char* FOOLED_QUESTION_LINE2 = "Be my valentine now?";

// Index of the special message that triggers LED animation
#define SPECIAL_MESSAGE_INDEX 3

// Index of the final sad emoji message
#define FINAL_MESSAGE_INDEX 6

// Number of messages in the array
const int NO_MESSAGES_COUNT = 7;

#define WRONG_CHOICE_DISPLAY_TIME 2000  // Show wrong choice for 2 seconds

// Button states
bool lastYesState = HIGH;
bool lastNoState = HIGH;

// ==================================================

void clearAllStrips() {
  buttonStrip.clear();
  bodyStrip.clear();
  buttonStrip.show();
  bodyStrip.show();
}

// ================= OLED =================

void oledValentineMessage() {
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_ncenB08_tr);
  u8g2.drawStr(0, 24, "WILL YOU BE MY");
  u8g2.drawStr(0, 48, "VALENTINE ?");
  u8g2.sendBuffer();
}

void oledYesMessage() {
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_ncenB08_tr);
  u8g2.drawStr(0, 20, "SHE SAID YES!");
  u8g2.drawStr(0, 40, "HAPPY VALENTINE");
  u8g2.drawStr(0, 60, "<3 <3 <3");
  u8g2.sendBuffer();
}

void oledWrongChoice(int count) {
  // Ensure we don't exceed the number of messages
  // Don't use modulo to prevent repeating messages
  int messageIndex = min(count - 1, NO_MESSAGES_COUNT - 1);
  const char* message = NO_MESSAGES[messageIndex];
  
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_ncenB08_tr);
  
  // Calculate position to center the message
  int messageWidth = u8g2.getStrWidth(message);
  int messageX = (128 - messageWidth) / 2;
  
  // Draw the message centered on the display
  u8g2.drawStr(messageX, 36, message);
  
  u8g2.sendBuffer();
  
  // Check if this is the special message that triggers the LED animation
  showingSpecialAnimation = (messageIndex == SPECIAL_MESSAGE_INDEX);
  
  // Check if this is the final sad emoji message
  showingFinalMessage = (messageIndex == FINAL_MESSAGE_INDEX);
  
  if (showingSpecialAnimation) {
    // Initialize the LED swap timer
    lastLedSwapTime = millis();
    // Set the fooled state to waiting for button press
    fooledState = WAITING_FOR_PRESS;
  }
}

// Function to display the fooled message
void oledFooledMessage(bool showQuestion) {
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_ncenB08_tr);
  
  if (showQuestion) {
    // Display the question on two lines
    int line1Width = u8g2.getStrWidth(FOOLED_QUESTION_LINE1);
    int line2Width = u8g2.getStrWidth(FOOLED_QUESTION_LINE2);
    int line1X = (128 - line1Width) / 2;
    int line2X = (128 - line2Width) / 2;
    
    // Draw the two lines centered on the display
    u8g2.drawStr(line1X, 25, FOOLED_QUESTION_LINE1);
    u8g2.drawStr(line2X, 45, FOOLED_QUESTION_LINE2);
  } else {
    // Display the single line message
    int messageWidth = u8g2.getStrWidth(FOOLED_MESSAGE);
    int messageX = (128 - messageWidth) / 2;
    
    // Draw the message centered on the display
    u8g2.drawStr(messageX, 36, FOOLED_MESSAGE);
  }
  
  u8g2.sendBuffer();
}

// ================= LED STATES =================

void showIdleLEDs() {
  buttonStrip.clear();
  bodyStrip.clear();

  buttonStrip.setPixelColor(0, buttonStrip.Color(255, 0, 0)); // First LED (red)
  buttonStrip.setPixelColor(2, buttonStrip.Color(0, 255, 0)); // Third LED (green)

  bodyStrip.setPixelColor(0, bodyStrip.Color(0, 255, 0));
  bodyStrip.setPixelColor(2, bodyStrip.Color(255, 0, 0));

  buttonStrip.show();
  bodyStrip.show();
}

// Function to swap LED colors rapidly for the special animation
void swapButtonLEDColors() {
  static bool swapState = false;
  
  buttonStrip.clear();
  
  if (swapState) {
    buttonStrip.setPixelColor(0, buttonStrip.Color(0, 255, 0)); // First LED (green)
    buttonStrip.setPixelColor(2, buttonStrip.Color(255, 0, 0)); // Third LED (red)
  } else {
    buttonStrip.setPixelColor(0, buttonStrip.Color(255, 0, 0)); // First LED (red)
    buttonStrip.setPixelColor(2, buttonStrip.Color(0, 255, 0)); // Third LED (green)
  }
  
  buttonStrip.show();
  swapState = !swapState;
}

// Reset to normal button colors (first LED red, third LED green)
void resetButtonLEDs() {
  buttonStrip.clear();
  buttonStrip.setPixelColor(0, buttonStrip.Color(255, 0, 0)); // First LED (red)
  buttonStrip.setPixelColor(2, buttonStrip.Color(0, 255, 0)); // Third LED (green)
  buttonStrip.show();
}

// Function to set LED colors for the fooled sequence
void setFooledButtonLEDs(bool yesButtonPressed, bool flipColors) {
  buttonStrip.clear();
  
  if (flipColors) {
    // For the "Fooled you" screen, INVERT the logic - pressed button is RED
    if (yesButtonPressed) {
      // YES button was pressed - First LED (yes) is RED, Third LED (no) is green
      buttonStrip.setPixelColor(0, buttonStrip.Color(255, 0, 0)); // First LED (red - pressed)
      buttonStrip.setPixelColor(2, buttonStrip.Color(0, 255, 0)); // Third LED (green)
    } else {
      // NO button was pressed - First LED (yes) is green, Third LED (no) is RED
      buttonStrip.setPixelColor(0, buttonStrip.Color(0, 255, 0)); // First LED (green)
      buttonStrip.setPixelColor(2, buttonStrip.Color(255, 0, 0)); // Third LED (red - pressed)
    }
  } else {
    // Back to default colors (first LED red, third LED green)
    buttonStrip.setPixelColor(0, buttonStrip.Color(255, 0, 0)); // First LED (red)
    buttonStrip.setPixelColor(2, buttonStrip.Color(0, 255, 0)); // Third LED (green)
  }
  
  buttonStrip.show();
}

// ================= ANIMATION =================

void valentinesAnimationStep() {
  static int brightness = 0;
  static int direction = 1;

  brightness += direction * 5;
  if (brightness >= 255) direction = -1;
  if (brightness <= 0) direction = 1;

  for (int i = 0; i < LED_COUNT; i++) {
    uint32_t c = buttonStrip.Color(255, brightness / 2, brightness);
    buttonStrip.setPixelColor(i, c);
    bodyStrip.setPixelColor(i, c);
  }

  buttonStrip.show();
  bodyStrip.show();
}

// ================= DEEP SLEEP (ESP32-C3 CORRECT) =================

void goToDeepSleep() {
  clearAllStrips();
  u8g2.clearDisplay();

  // Enable GPIO wakeup (ESP32-C3 style)
  esp_deep_sleep_enable_gpio_wakeup(
    1ULL << BTN_YES_GPIO,
    ESP_GPIO_WAKEUP_GPIO_LOW
  );

  delay(100);
  esp_deep_sleep_start();
}

// ==================================================

void setup() {
  Serial.begin(115200);
  delay(300);

  ++bootCount;
  Serial.println("Boot: " + String(bootCount));

  buttonStrip.begin();
  bodyStrip.begin();
  clearAllStrips();

  pinMode(BTN_YES_PIN, INPUT_PULLUP);
  pinMode(BTN_NO_PIN, INPUT_PULLUP);

  Wire.begin();
  u8g2.begin();

  oledValentineMessage();
  showIdleLEDs();
  idleDrawn = true;

  lastActivityTime = millis();
}

void loop() {
  unsigned long now = millis();

  // ================= YES =================
  bool currentYesState = digitalRead(BTN_YES_PIN);
  
  // Handle button debouncing
  if (currentYesState != lastYesState && (now - lastYesButtonTime) > DEBOUNCE_DELAY) {
    lastYesButtonTime = now;
    if (currentYesState == LOW) {
      // Handle button press based on current fooled state
      switch (fooledState) {
        case WAITING_FOR_PRESS:
          // User pressed YES during "How about now?" screen
          fooledState = FOOLED_YOU;
          fooledButtonWasYes = true;
          fooledSequenceStartTime = now;
          oledFooledMessage(false); // Show "Fooled you!"
          setFooledButtonLEDs(true, true); // Yes button pressed, flip colors
          showingSpecialAnimation = false; // Stop the LED swapping
          lastActivityTime = now;
          break;
          
        case FOOLED_YOU:
          // User pressed YES during "Fooled you!" screen
          fooledState = FAIR_RIGHT;
          oledFooledMessage(true); // Show "Fair right? Valentine now?"
          setFooledButtonLEDs(true, false); // Yes button pressed, normal colors
          lastActivityTime = now;
          break;
          
        case FAIR_RIGHT:
          // User pressed YES to the Valentine question after being fooled
          fooledState = NOT_FOOLED;
          yesPressed = true;
          idleDrawn = false;
          yesStartTime = now;
          lastActivityTime = now;
          oledYesMessage();
          break;
          
        default:
          // Normal yes button behavior
          if (!yesPressed) {
            yesPressed = true;
            idleDrawn = false;
            yesStartTime = now;
            lastActivityTime = now;
            oledYesMessage();
          }
          break;
      }
    }
  }
  lastYesState = currentYesState;

  if (yesPressed) {
    valentinesAnimationStep();

    if (now - yesStartTime >= 5000) {
      yesPressed = false;
      noCount = 0;
      clearAllStrips();
      oledValentineMessage();
      showIdleLEDs();
      idleDrawn = true;
      lastActivityTime = now;
    }

    delay(30);
    return;
  }

  // ================= NO =================
  bool currentNoState = digitalRead(BTN_NO_PIN);
  
  // Handle button debouncing
  if (currentNoState != lastNoState && (now - lastNoButtonTime) > DEBOUNCE_DELAY) {
    lastNoButtonTime = now;
    if (currentNoState == LOW) {
      // Handle button press based on current fooled state
      switch (fooledState) {
        case WAITING_FOR_PRESS:
          // User pressed NO during "How about now?" screen
          fooledState = FOOLED_YOU;
          fooledButtonWasYes = false;
          fooledSequenceStartTime = now;
          oledFooledMessage(false); // Show "Fooled you!"
          setFooledButtonLEDs(false, true); // No button pressed, flip colors
          showingSpecialAnimation = false; // Stop the LED swapping
          lastActivityTime = now;
          break;
          
        case FOOLED_YOU:
          // User pressed NO during "Fooled you!" screen
          fooledState = FAIR_RIGHT;
          oledFooledMessage(true); // Show "Fair right? Valentine now?"
          setFooledButtonLEDs(false, false); // No button pressed, normal colors
          lastActivityTime = now;
          break;
          
        case FAIR_RIGHT:
          // User pressed NO to the Valentine question after being fooled
          // Continue with the next no message
          fooledState = NOT_FOOLED;
          noCount++;
          oledWrongChoice(noCount);
          lastActivityTime = now;
          idleDrawn = false;
          showingWrongChoice = true;
          
          // If we're showing the final message, set the flag to wait for another button press
          if (showingFinalMessage) {
            waitingForFinalButtonPress = true;
          }
          break;
          
        default:
          // Check if we're on the final message and waiting for a button press
          if (waitingForFinalButtonPress) {
            // Return to the main Valentine screen
            waitingForFinalButtonPress = false;
            showingWrongChoice = false;
            showingFinalMessage = false;
            idleDrawn = false;
          } else {
            // Normal flow - increment count and show next message
            noCount++;
            oledWrongChoice(noCount);
            lastActivityTime = now;
            idleDrawn = false;
            showingWrongChoice = true;
            
            // If we're showing the final message, set the flag to wait for another button press
            if (showingFinalMessage) {
              waitingForFinalButtonPress = true;
            }
          }
          break;
      }
    }
  }
  lastNoState = currentNoState;

  // ================= WRONG CHOICE DISPLAY =================
  if (showingWrongChoice) {
    // Handle special animation if showing the special message
    if (showingSpecialAnimation && (now - lastLedSwapTime >= 100)) { // Swap every 100ms
      swapButtonLEDColors();
      lastLedSwapTime = now;
    }
    
    // Only auto-return to main screen if we're not in the new flow
    // This keeps the message displayed until another button press
  }
  
  // ================= IDLE =================
  if (!idleDrawn && !showingWrongChoice && fooledState == NOT_FOOLED) {
    oledValentineMessage();
    resetButtonLEDs();
    idleDrawn = true;
  }


  // ================= SLEEP =================
  if (now - lastActivityTime >= INACTIVITY_TIMEOUT) {
    Serial.println("Entering deep sleep");
    goToDeepSleep();
  }

  // Small delay for stability without affecting button response
  delay(10);
}
