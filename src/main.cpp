#include <Adafruit_NeoPixel.h>
#include <Wire.h>
#include <U8g2lib.h>
#include "esp_sleep.h"

// ================= OLED =================
U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, U8X8_PIN_NONE);

// ================= HARDWARE SETTINGS =================
#define PHYSICAL_LED_COUNT 30 
#define ACTIVE_LED_COUNT   9 
#define BUTTON_LED_COUNT   3

// PINS
#define BUTTON_STRIP_PIN D9
#define BODY_STRIP_PIN   D8
#define BTN_YES_PIN      D1
#define BTN_NO_PIN       D2
#define BTN_YES_GPIO     GPIO_NUM_3 

Adafruit_NeoPixel buttonStrip(BUTTON_LED_COUNT, BUTTON_STRIP_PIN, NEO_GRB + NEO_KHZ800);
Adafruit_NeoPixel bodyStrip(PHYSICAL_LED_COUNT, BODY_STRIP_PIN, NEO_GRB + NEO_KHZ800);

// ================= TIMING =================
#define INACTIVITY_TIMEOUT   30000UL // 30s Sleep
#define CELEBRATION_DURATION 10000UL // 10s Love then Reset
#define DEBOUNCE_DELAY       50

// ================= TEXT CONFIGURATION (EDIT HERE) =================
// 1. BOOT SCREEN
const char* MSG_BOOT_1 = "Gargi, will you be";
const char* MSG_BOOT_2 = "my Valentine? ❤︎⁠♡❤︎⁠";

// 2. IDLE MODE (ESCALATING "NO" RESPONSES)
const char* NO_RESPONSES[] = { 
  "Really?", 
  "Are you sure?", 
  "Think again!" 
};
const int TRIGGER_COUNT = 4; // Trick happens on 4th press

// 3. SWAP TRICK MODE
const char* MSG_TRICK_PROMPT = "How about now?";
const char* MSG_TRICK_REVEAL = "You pressed YES!";

// 4. FAIR RIGHT MODE
const char* MSG_FAIR_1 = "Finally!";
const char* MSG_FAIR_2 = "You have to be my valentine now";

// 5. FINAL PLEA MODE (If they say No to Fair Right)
const char* MSG_PLEA_1 = "PRETTY PLEASE??";
const char* MSG_PLEA_2 = "I promise I'm";
const char* MSG_PLEA_3 = "worth it! <3";

// 6. VICTORY MESSAGES
// Standard Win (Immediate Yes)
const char* MSG_WIN_STD_1 = "SHE SAID YES!";
const char* MSG_WIN_STD_2 = "HAPPY VALENTINE";
const char* MSG_WIN_STD_3 = "<3 <3 <3";

// Final Win (After begging)
const char* MSG_WIN_FINAL_1 = "SHE SAID YES!";
const char* MSG_WIN_FINAL_2 = "(FINALLY!)";
const char* MSG_WIN_FINAL_3 = "<3 <3 <3";

// 7. SLEEP
const char* MSG_SLEEP_1 = "Goodnight...";
const char* MSG_SLEEP_2 = "<3";


// ================= STATE MACHINE =================
enum AppState {
  STATE_IDLE,          // "Will you be my Valentine?"
  STATE_SWAP_MODE,     // Trick mode
  STATE_FAIR_RIGHT,    // "Fair right?" (Yes/No available)
  STATE_FINAL_PLEA,    // "Pretty please??" (Force Win)
  STATE_CELEBRATION    // "She said YES!"
};

AppState currentState = STATE_IDLE;

// Logic Variables
int noCount = 0;
unsigned long lastActivityTime = 0;
unsigned long stateStartTime = 0; 
bool isTrickReveal = false; 

// Button Debounce Variables
unsigned long lastDebounceTime = 0;
bool btnYesStable = HIGH;
bool btnNoStable = HIGH;
bool lastBtnYesReading = HIGH;
bool lastBtnNoReading = HIGH;

// Forward declaration needed so Typewriter can animate LEDs
void updateLEDs(); 

// ================= HARD RESET =================
void forceHardReset() {
  buttonStrip.begin();
  bodyStrip.begin();
  buttonStrip.clear();
  bodyStrip.clear();
  buttonStrip.show();
  bodyStrip.show();
  delay(20); 
  buttonStrip.setBrightness(150); // Soft brightness
  bodyStrip.setBrightness(150);
}

// ================= DISPLAY HELPERS (TYPEWRITER) =================
void oledTypewriter(const char* l1, const char* l2 = NULL, const char* l3 = NULL) {
  u8g2.setFont(u8g2_font_ncenB08_tr);
  
  char buf1[32] = "";
  char buf2[32] = "";
  char buf3[32] = "";
  
  // --- TYPE LINE 1 ---
  if (l1) {
    int len = strlen(l1);
    for(int i=0; i<len; i++) {
      buf1[i] = l1[i];
      buf1[i+1] = '\0';
      
      u8g2.clearBuffer();
      int w = u8g2.getStrWidth(buf1);
      int x = (128-w)/2;
      int y = l2 ? 25 : 36;
      u8g2.drawStr(x, y, buf1);
      u8g2.drawStr(x + w + 1, y, "_"); 
      u8g2.sendBuffer();
      
      updateLEDs(); // KEEP ANIMATIONS ALIVE!
      delay(30 + random(30)); 
    }
  }

  // --- TYPE LINE 2 ---
  if (l2) {
    int len = strlen(l2);
    for(int i=0; i<len; i++) {
      buf2[i] = l2[i];
      buf2[i+1] = '\0';
      
      u8g2.clearBuffer();
      int w1 = u8g2.getStrWidth(l1); u8g2.drawStr((128-w1)/2, 25, l1);
      
      int w2 = u8g2.getStrWidth(buf2);
      int x = (128-w2)/2;
      u8g2.drawStr(x, 45, buf2);
      u8g2.drawStr(x + w2 + 1, 45, "_"); 
      u8g2.sendBuffer();
      updateLEDs();
      delay(30 + random(30));
    }
  }

  // --- TYPE LINE 3 ---
  if (l3) {
    int len = strlen(l3);
    for(int i=0; i<len; i++) {
      buf3[i] = l3[i];
      buf3[i+1] = '\0';
      
      u8g2.clearBuffer();
      int w1 = u8g2.getStrWidth(l1); u8g2.drawStr((128-w1)/2, 25, l1);
      int w2 = u8g2.getStrWidth(l2); u8g2.drawStr((128-w2)/2, 45, l2);
      
      int w3 = u8g2.getStrWidth(buf3);
      int x = (128-w3)/2;
      u8g2.drawStr(x, 60, buf3);
      u8g2.drawStr(x + w3 + 1, 60, "_"); 

      u8g2.sendBuffer();
      updateLEDs();
      delay(30 + random(30));
    }
  }

  // Final render
  u8g2.clearBuffer();
  if(l1) { int w = u8g2.getStrWidth(l1); u8g2.drawStr((128-w)/2, l2?25:36, l1); }
  if(l2) { int w = u8g2.getStrWidth(l2); u8g2.drawStr((128-w)/2, 45, l2); }
  if(l3) { int w = u8g2.getStrWidth(l3); u8g2.drawStr((128-w)/2, 60, l3); }
  u8g2.sendBuffer();
}

// ================= ANIMATIONS =================
void animBoot() {
  u8g2.clearBuffer(); u8g2.sendBuffer();
  
  // 1. Soft Pink Flow (Body)
  for(int i=0; i<ACTIVE_LED_COUNT; i++) {
    bodyStrip.setPixelColor(i, bodyStrip.Color(180, 50, 80)); 
    bodyStrip.show();
    delay(40);
  }

  // 2. Button Wakeup
  for(int b=0; b<200; b+=5) {
      buttonStrip.setPixelColor(1, buttonStrip.Color(b, b/2, b/2)); 
      buttonStrip.show();
      delay(5);
  }
  buttonStrip.setPixelColor(1, 0); 

  // Fade in buttons
  for(int b=0; b<255; b+=5) {
    buttonStrip.setPixelColor(0, buttonStrip.Color(b, 0, 0)); // RED
    buttonStrip.setPixelColor(2, buttonStrip.Color(0, b, 0)); // GREEN
    buttonStrip.show();
    delay(5);
  }
  
  oledTypewriter(MSG_BOOT_1, MSG_BOOT_2);
}

void animShutdown() {
  oledTypewriter(MSG_SLEEP_1, MSG_SLEEP_2); 
  
  // Fade out softly
  for(int b=150; b>=0; b-=5) {
    for(int i=0; i<ACTIVE_LED_COUNT; i++) {
       bodyStrip.setPixelColor(i, bodyStrip.Color(b, 0, b/3));
    }
    buttonStrip.setBrightness(b);
    bodyStrip.show();
    buttonStrip.show();
    delay(20);
  }
  forceHardReset();
  u8g2.clearDisplay();
}

// Non-blocking Animation Loop
void updateLEDs() {
  unsigned long now = millis();
  
  // 1. BODY STRIP: SOFT ROMANCE (Always Active)
  if (currentState == STATE_CELEBRATION) {
      float wave = 0.5 + 0.5 * sin(now / 1000.0 * PI);
      for(int i=0; i<ACTIVE_LED_COUNT; i++) {
          float localWave = 0.5 + 0.5 * sin((now / 800.0 * PI) + (i * 0.5));
          int r = 255;
          int g = 20 + (80 * localWave);
          int b = 30 + (90 * localWave);
          bodyStrip.setPixelColor(i, bodyStrip.Color(r, g, b));
      }
  } else {
    // CANDLELIGHT BREATHING (IDLE)
    for(int i=0; i<ACTIVE_LED_COUNT; i++) {
      int offset = i * 40; 
      float breathe = (exp(sin((now - offset) / 2500.0 * PI)) - 0.36787944) * 108.0;
      int val = map(breathe, 0, 255, 20, 100);
      bodyStrip.setPixelColor(i, bodyStrip.Color(val, val/4, val/3)); 
    }
  }
  for(int i=ACTIVE_LED_COUNT; i<PHYSICAL_LED_COUNT; i++) bodyStrip.setPixelColor(i, 0);

  // 2. BUTTON STRIP
  if (!isTrickReveal) {
      int softPulse = 80 + (int)(sin(now / 800.0) * 60); 
      int panicPulse = 100 + (int)(sin(now / 150.0) * 100); 

      buttonStrip.clear();

      switch (currentState) {
        case STATE_SWAP_MODE:
          if ((now / 150) % 2 == 0) {
             buttonStrip.setPixelColor(0, buttonStrip.Color(0, 255, 0)); // Pure Green
             buttonStrip.setPixelColor(2, buttonStrip.Color(255, 0, 0)); // Pure Red
          } else {
             buttonStrip.setPixelColor(0, buttonStrip.Color(255, 0, 0)); // Pure Red
             buttonStrip.setPixelColor(2, buttonStrip.Color(0, 255, 0)); // Pure Green
          }
          break;

        case STATE_FINAL_PLEA:
          buttonStrip.setPixelColor(0, buttonStrip.Color(0, panicPulse, 50)); 
          buttonStrip.setPixelColor(2, buttonStrip.Color(0, panicPulse, 50));
          break;

        case STATE_CELEBRATION:
          {
            int winPulse = 100 + (int)(sin(now / 300.0) * 155); 
            if (winPulse < 0) winPulse = 0;
            buttonStrip.fill(buttonStrip.Color(winPulse/4, 200, winPulse/4));
          }
          break;

        case STATE_FAIR_RIGHT:
        default: // IDLE & FAIR RIGHT
          buttonStrip.setPixelColor(0, buttonStrip.Color(softPulse, 0, 0)); 
          buttonStrip.setPixelColor(2, buttonStrip.Color(0, softPulse, 0)); 
          break;
      }
  }

  bodyStrip.show();
  buttonStrip.show();
}

// ================= SETUP =================
void setup() {
  Serial.begin(115200);
  forceHardReset();

  pinMode(BTN_YES_PIN, INPUT_PULLUP);
  pinMode(BTN_NO_PIN, INPUT_PULLUP);

  Wire.begin();
  u8g2.begin();

  lastBtnYesReading = digitalRead(BTN_YES_PIN);
  lastBtnNoReading = digitalRead(BTN_NO_PIN);
  btnYesStable = lastBtnYesReading;
  btnNoStable = lastBtnNoReading;

  animBoot();
  lastActivityTime = millis();
}

// ================= LOOP =================
void loop() {
  unsigned long now = millis();
  
  // --- 1. INPUT READING ---
  bool readYes = digitalRead(BTN_YES_PIN);
  bool readNo = digitalRead(BTN_NO_PIN);
  bool btnPressed = false;
  bool isYesBtn = false; 

  if (readYes != lastBtnYesReading || readNo != lastBtnNoReading) {
    lastDebounceTime = now;
  }
  lastBtnYesReading = readYes;
  lastBtnNoReading = readNo;

  if ((now - lastDebounceTime) > DEBOUNCE_DELAY) {
    if (readYes != btnYesStable) {
      btnYesStable = readYes;
      if (btnYesStable == LOW) { btnPressed = true; isYesBtn = true; }
    }
    if (readNo != btnNoStable) {
      btnNoStable = readNo;
      if (btnNoStable == LOW) { btnPressed = true; isYesBtn = false; }
    }
  }

  // --- 2. LOGIC ---
  if (btnPressed) {
    lastActivityTime = now; 

    if (currentState == STATE_CELEBRATION) {
      if (now - stateStartTime > 500) { 
        forceHardReset();
        currentState = STATE_IDLE;
        noCount = 0;
        animBoot(); 
      }
    }
    else if (currentState == STATE_FINAL_PLEA) {
      currentState = STATE_CELEBRATION;
      oledTypewriter(MSG_WIN_FINAL_1, MSG_WIN_FINAL_2, MSG_WIN_FINAL_3);
      stateStartTime = now;
    }
    else if (currentState == STATE_FAIR_RIGHT) {
      if (isYesBtn) {
        currentState = STATE_CELEBRATION;
        oledTypewriter(MSG_WIN_STD_1, MSG_WIN_STD_2, MSG_WIN_STD_3);
        stateStartTime = now;
      } else {
        currentState = STATE_FINAL_PLEA;
        oledTypewriter(MSG_PLEA_1, MSG_PLEA_2, MSG_PLEA_3);
      }
    }
    else if (currentState == STATE_SWAP_MODE) {
      // IMMEDIATE UPDATE BEFORE TEXT STARTS
      isTrickReveal = true; 
      
      buttonStrip.clear();
      if (isYesBtn) { 
        buttonStrip.setPixelColor(0, buttonStrip.Color(255, 0, 0)); 
        buttonStrip.setPixelColor(2, buttonStrip.Color(0, 255, 0));
      } else { 
        buttonStrip.setPixelColor(0, buttonStrip.Color(0, 255, 0));
        buttonStrip.setPixelColor(2, buttonStrip.Color(255, 0, 0)); 
      }
      buttonStrip.show(); // FORCE SHOW NOW

      oledTypewriter(MSG_TRICK_REVEAL); 
      
      delay(2000); 
      isTrickReveal = false; 
      currentState = STATE_FAIR_RIGHT;
      oledTypewriter(MSG_FAIR_1, MSG_FAIR_2);
    }
    else if (currentState == STATE_IDLE) {
      if (isYesBtn) {
        currentState = STATE_CELEBRATION;
        oledTypewriter(MSG_WIN_STD_1, MSG_WIN_STD_2, MSG_WIN_STD_3);
        stateStartTime = now;
      } else {
        noCount++;
        if (noCount >= TRIGGER_COUNT) {
          currentState = STATE_SWAP_MODE;
          oledTypewriter(MSG_TRICK_PROMPT);
        } else {
          oledTypewriter(NO_RESPONSES[noCount - 1]);
        }
      }
    }
  }

  // --- 3. AUTO RESET ---
  if (currentState == STATE_CELEBRATION && (now - stateStartTime > CELEBRATION_DURATION)) {
      forceHardReset();      
      noCount = 0;           
      currentState = STATE_IDLE; 
      animBoot();            
      lastActivityTime = now; 
  }

  updateLEDs();

  // --- 4. SLEEP ---
  if (now - lastActivityTime >= INACTIVITY_TIMEOUT) {
    animShutdown();
    esp_deep_sleep_enable_gpio_wakeup(1ULL << BTN_YES_GPIO, ESP_GPIO_WAKEUP_GPIO_LOW);
    delay(100);
    esp_deep_sleep_start();
  }
  
  delay(10); 
}