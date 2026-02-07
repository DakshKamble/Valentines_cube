#include <Adafruit_NeoPixel.h>
#include <Wire.h>
#include <U8g2lib.h>
#include "esp_sleep.h"

// ================= OLED =================
U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, U8X8_PIN_NONE);

// ================= WS2812B =================
#define LED_COUNT 30

#define BUTTON_STRIP_PIN D8
#define BODY_STRIP_PIN   D9

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

unsigned long yesStartTime = 0;
unsigned long lastActivityTime = 0;
unsigned long lastYesButtonTime = 0;
unsigned long lastNoButtonTime = 0;
unsigned long wrongChoiceStartTime = 0;

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
  char buf[24];
  sprintf(buf, "WRONG CHOICE %d", count);

  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_ncenB08_tr);
  u8g2.drawStr(0, 36, buf);
  u8g2.sendBuffer();
}

// ================= LED STATES =================

void showIdleLEDs() {
  buttonStrip.clear();
  bodyStrip.clear();

  buttonStrip.setPixelColor(0, buttonStrip.Color(255, 0, 0));
  buttonStrip.setPixelColor(2, buttonStrip.Color(0, 255, 0));

  bodyStrip.setPixelColor(0, bodyStrip.Color(0, 255, 0));
  bodyStrip.setPixelColor(2, bodyStrip.Color(255, 0, 0));

  buttonStrip.show();
  bodyStrip.show();
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
    if (currentYesState == LOW && !yesPressed) {
      yesPressed = true;
      idleDrawn = false;
      yesStartTime = now;
      lastActivityTime = now;
      oledYesMessage();
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
      noCount++;
      oledWrongChoice(noCount);
      lastActivityTime = now;
      idleDrawn = false;
      showingWrongChoice = true;
      wrongChoiceStartTime = now;
    }
  }
  lastNoState = currentNoState;

  // ================= WRONG CHOICE DISPLAY =================
  if (showingWrongChoice && (now - wrongChoiceStartTime >= WRONG_CHOICE_DISPLAY_TIME)) {
    showingWrongChoice = false;
    idleDrawn = false;
  }

  // ================= IDLE =================
  if (!idleDrawn && !showingWrongChoice) {
    oledValentineMessage();
    showIdleLEDs();
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
