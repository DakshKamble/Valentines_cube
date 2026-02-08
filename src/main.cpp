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

// ================= TIMING & LOGIC =================
#define INACTIVITY_TIMEOUT   30000UL // 30s Sleep
#define CELEBRATION_DURATION 10000UL // 10s Party then Reset
#define DEBOUNCE_DELAY       50

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

// Button Debounce Variables
unsigned long lastDebounceTime = 0;
bool btnYesStable = HIGH;
bool btnNoStable = HIGH;
bool lastBtnYesReading = HIGH;
bool lastBtnNoReading = HIGH;

// Messages for the initial "No" presses
const char* NO_MESSAGES[] = { "Really?", "Are you sure?", "Think again!" };
const int TRIGGER_COUNT = 4; 

// ================= HARD RESET =================
void forceHardReset() {
  buttonStrip.begin();
  bodyStrip.begin();
  buttonStrip.clear();
  bodyStrip.clear();
  buttonStrip.show();
  bodyStrip.show();
  delay(20); 
  buttonStrip.setBrightness(200);
  bodyStrip.setBrightness(200);
}

// ================= DISPLAY HELPERS =================
void oledGeneric(const char* l1, const char* l2 = NULL, const char* l3 = NULL) {
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_ncenB08_tr);
  
  int w;
  if (l1) { w = u8g2.getStrWidth(l1); u8g2.drawStr((128-w)/2, l2?25:36, l1); }
  if (l2) { w = u8g2.getStrWidth(l2); u8g2.drawStr((128-w)/2, 45, l2); }
  if (l3) { w = u8g2.getStrWidth(l3); u8g2.drawStr((128-w)/2, 60, l3); }
  
  u8g2.sendBuffer();
}

// ================= ANIMATIONS =================
void animBoot() {
  u8g2.clearBuffer(); u8g2.sendBuffer();
  
  // Sexy Wipe (Pink)
  for(int i=0; i<ACTIVE_LED_COUNT; i++) {
    bodyStrip.setPixelColor(i, bodyStrip.Color(150, 0, 80)); 
    bodyStrip.show();
    delay(30);
  }
  
  // Buttons Fade In - Left=Green(YES), Right=Red(NO)
  for(int b=0; b<255; b+=10) {
    buttonStrip.setPixelColor(0, buttonStrip.Color(0, b, 0)); // Green
    buttonStrip.setPixelColor(2, buttonStrip.Color(b, 0, 0)); // Red
    buttonStrip.show();
    delay(5);
  }
  
  oledGeneric("WILL YOU BE MY", "VALENTINE ?");
}

void animShutdown() {
  oledGeneric("Goodnight...", "<3");
  for(int i=0; i<ACTIVE_LED_COUNT; i++) {
    bodyStrip.setPixelColor(random(ACTIVE_LED_COUNT), 0);
    bodyStrip.show();
    delay(30);
  }
  forceHardReset();
  u8g2.clearDisplay();
}

// Non-blocking Animation Loop
void updateLEDs() {
  unsigned long now = millis();
  
  // 1. BODY STRIP
  if (currentState == STATE_CELEBRATION) {
      // Rainbow Party
      for(int i=0; i<ACTIVE_LED_COUNT; i++) {
          int pixelHue = (now * 256 / 20) + (i * 65536L / ACTIVE_LED_COUNT);
          bodyStrip.setPixelColor(i, bodyStrip.gamma32(bodyStrip.ColorHSV(pixelHue)));
      }
  } else {
    // Sexy Breathing (IDLE / INTERACTION)
    for(int i=0; i<ACTIVE_LED_COUNT; i++) {
      int offset = i * 20; 
      float localBreathe = (exp(sin((now - offset) / 2000.0 * PI)) - 0.36787944) * 108.0;
      int localVal = map(localBreathe, 0, 255, 10, 150);
      bodyStrip.setPixelColor(i, bodyStrip.Color(localVal, 0, localVal/3)); 
    }
  }
  // Force unused OFF
  for(int i=ACTIVE_LED_COUNT; i<PHYSICAL_LED_COUNT; i++) bodyStrip.setPixelColor(i, 0);

  // 2. BUTTON STRIP
  buttonStrip.clear();
  int pulse = 128 + (int)(sin(now / 500.0) * 80); 
  int fastPulse = 128 + (int)(sin(now / 150.0) * 120); 

  switch (currentState) {
    case STATE_SWAP_MODE:
      // Rapid Swap
      if ((now / 150) % 2 == 0) {
         buttonStrip.setPixelColor(0, buttonStrip.Color(0, 255, 0)); 
         buttonStrip.setPixelColor(2, buttonStrip.Color(255, 0, 0)); 
      } else {
         buttonStrip.setPixelColor(0, buttonStrip.Color(255, 0, 0)); 
         buttonStrip.setPixelColor(2, buttonStrip.Color(0, 255, 0)); 
      }
      break;

    case STATE_FINAL_PLEA:
      // Both Green, pulsing frantically
      buttonStrip.setPixelColor(0, buttonStrip.Color(0, fastPulse, 0)); 
      buttonStrip.setPixelColor(2, buttonStrip.Color(0, fastPulse, 0));
      break;

    case STATE_CELEBRATION:
      // Sparkle Green
      if ((now / 100) % 2 == 0) buttonStrip.fill(buttonStrip.Color(0, 255, 0));
      else buttonStrip.fill(buttonStrip.Color(0, 50, 0));
      break;

    case STATE_FAIR_RIGHT:
      // Normal colors (Green/Red) but pulsing
      buttonStrip.setPixelColor(0, buttonStrip.Color(0, pulse, 0)); 
      buttonStrip.setPixelColor(2, buttonStrip.Color(pulse, 0, 0)); 
      break;

    default: // IDLE
      buttonStrip.setPixelColor(0, buttonStrip.Color(0, pulse, 0)); // Left = Green
      buttonStrip.setPixelColor(2, buttonStrip.Color(pulse, 0, 0)); // Right = Red
      break;
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
  
  // --- 1. ROBUST INPUT READING ---
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

  // --- 2. GAME LOGIC ---
  if (btnPressed) {
    lastActivityTime = now; 

    // CLICKING DURING CELEBRATION -> MANUAL RESET
    if (currentState == STATE_CELEBRATION) {
      // Allow manual skip/reset
      if (now - stateStartTime > 500) { // Slight debounce on win state
        forceHardReset();
        currentState = STATE_IDLE;
        noCount = 0;
        animBoot(); // Calls OLED update and LED reset
      }
    }
    
    // FINAL PLEA -> WIN
    else if (currentState == STATE_FINAL_PLEA) {
      currentState = STATE_CELEBRATION;
      oledGeneric("SHE SAID YES!", "(FINALLY!)", "<3 <3 <3");
      stateStartTime = now;
    }
    
    // FAIR RIGHT -> WIN OR PLEA
    else if (currentState == STATE_FAIR_RIGHT) {
      if (isYesBtn) {
        currentState = STATE_CELEBRATION;
        oledGeneric("SHE SAID YES!", "HAPPY VALENTINE", "<3 <3 <3");
        stateStartTime = now;
      } else {
        currentState = STATE_FINAL_PLEA;
        oledGeneric("PRETTY PLEASE??", "I promise I'm", "worth it! <3");
      }
    }
    
    // SWAP MODE -> TRICK -> FAIR RIGHT
    else if (currentState == STATE_SWAP_MODE) {
      oledGeneric("You pressed YES!"); 
      
      buttonStrip.clear();
      if (isYesBtn) { 
        buttonStrip.setPixelColor(0, buttonStrip.Color(255, 0, 0)); 
        buttonStrip.setPixelColor(2, buttonStrip.Color(0, 255, 0));
      } else { 
        buttonStrip.setPixelColor(0, buttonStrip.Color(0, 255, 0));
        buttonStrip.setPixelColor(2, buttonStrip.Color(255, 0, 0)); 
      }
      buttonStrip.show();
      
      delay(2000); 
      
      currentState = STATE_FAIR_RIGHT;
      oledGeneric("Fair right?", "Be my valentine?");
    }
    
    // IDLE MODE
    else if (currentState == STATE_IDLE) {
      if (isYesBtn) {
        currentState = STATE_CELEBRATION;
        oledGeneric("SHE SAID YES!", "HAPPY VALENTINE", "<3 <3 <3");
        stateStartTime = now;
      } else {
        noCount++;
        if (noCount >= TRIGGER_COUNT) {
          currentState = STATE_SWAP_MODE;
          oledGeneric("How about now?");
        } else {
          oledGeneric(NO_MESSAGES[noCount - 1]);
        }
      }
    }
  }

  // --- 3. AUTO RESET LOGIC (THE FIX) ---
  // If celebration is done, AUTOMATICALLY return to IDLE
  if (currentState == STATE_CELEBRATION && (now - stateStartTime > CELEBRATION_DURATION)) {
      forceHardReset();      // Clear visual glitches
      noCount = 0;           // Reset game vars
      currentState = STATE_IDLE; // Reset State
      animBoot();            // Re-draw "Will you be my Valentine" OLED & Reset LEDs
      lastActivityTime = now; // Reset sleep timer so we don't sleep instantly
  }

  // Animation Update
  updateLEDs();

  // --- 4. DEEP SLEEP ---
  if (now - lastActivityTime >= INACTIVITY_TIMEOUT) {
    animShutdown();
    esp_deep_sleep_enable_gpio_wakeup(1ULL << BTN_YES_GPIO, ESP_GPIO_WAKEUP_GPIO_LOW);
    delay(100);
    esp_deep_sleep_start();
  }
  
  delay(10); 
}