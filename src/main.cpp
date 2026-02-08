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
#define CELEBRATION_DURATION 10000UL // 10s Love then Reset
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
  buttonStrip.setBrightness(150); // Lower brightness for softness
  bodyStrip.setBrightness(150);
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

// Helper for soft colors
uint32_t colorSoftPink(int brightness) {
  // R=255, G=100, B=120 gives a lovely rose pink
  return bodyStrip.Color((255*brightness)/255, (100*brightness)/255, (120*brightness)/255);
}

uint32_t colorDeepRed(int brightness) {
  return bodyStrip.Color((255*brightness)/255, (20*brightness)/255, (30*brightness)/255);
}

void animBoot() {
  u8g2.clearBuffer(); u8g2.sendBuffer();
  
  // 1. Soft Pink Flow (Body)
  for(int i=0; i<ACTIVE_LED_COUNT; i++) {
    // Gentle wash
    bodyStrip.setPixelColor(i, bodyStrip.Color(180, 50, 80)); 
    bodyStrip.show();
    delay(40);
  }

  // 2. Button Wakeup Animation (Requested)
  // Pulse the middle then expand to Red/Green
  for(int b=0; b<200; b+=5) {
      buttonStrip.setPixelColor(1, buttonStrip.Color(b, b/2, b/2)); // White-ish center
      buttonStrip.show();
      delay(5);
  }
  buttonStrip.setPixelColor(1, 0); // Clear center

  // Fade in the interaction buttons (Left=Red, Right=Green)
  for(int b=0; b<255; b+=5) {
    buttonStrip.setPixelColor(0, buttonStrip.Color(b, 0, 0)); // LED 0 -> RED
    buttonStrip.setPixelColor(2, buttonStrip.Color(0, b, 0)); // LED 2 -> GREEN
    buttonStrip.show();
    delay(5);
  }
  
  oledGeneric("WILL YOU BE MY", "VALENTINE ?");
}

void animShutdown() {
  oledGeneric("Goodnight...", "<3");
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
  
  // 1. BODY STRIP: SOFT ROMANCE
  if (currentState == STATE_CELEBRATION) {
      // ROMANCE PULSE: Flow between Deep Red and Soft Pink
      float wave = 0.5 + 0.5 * sin(now / 1000.0 * PI); // Slow wave 0.0 to 1.0
      
      for(int i=0; i<ACTIVE_LED_COUNT; i++) {
          // Phase shift for flowing effect
          float localWave = 0.5 + 0.5 * sin((now / 800.0 * PI) + (i * 0.5));
          
          int r = 255;
          int g = 20 + (80 * localWave);  // vary green to move from red to pink
          int b = 30 + (90 * localWave);  // vary blue to soften
          
          bodyStrip.setPixelColor(i, bodyStrip.Color(r, g, b));
      }
  } else {
    // CANDLELIGHT BREATHING (IDLE)
    // Very subtle warm pink/peach
    for(int i=0; i<ACTIVE_LED_COUNT; i++) {
      int offset = i * 40; 
      float breathe = (exp(sin((now - offset) / 2500.0 * PI)) - 0.36787944) * 108.0;
      int val = map(breathe, 0, 255, 20, 100);
      
      // Warm Pink: R=High, G=Low-Mid, B=Low
      bodyStrip.setPixelColor(i, bodyStrip.Color(val, val/4, val/3)); 
    }
  }
  // Force unused OFF
  for(int i=ACTIVE_LED_COUNT; i<PHYSICAL_LED_COUNT; i++) bodyStrip.setPixelColor(i, 0);

  // 2. BUTTON STRIP
  // Gentle pulse variables
  int softPulse = 80 + (int)(sin(now / 800.0) * 60); // 20 to 140
  int panicPulse = 100 + (int)(sin(now / 150.0) * 100); 

  buttonStrip.clear();

  switch (currentState) {
    case STATE_SWAP_MODE:
      // Rapid Swap (Keep strict timing for trick, but softer colors)
      if ((now / 150) % 2 == 0) {
         buttonStrip.setPixelColor(0, buttonStrip.Color(0, 200, 50)); // Soft Green
         buttonStrip.setPixelColor(2, buttonStrip.Color(200, 0, 50)); // Soft Red
      } else {
         buttonStrip.setPixelColor(0, buttonStrip.Color(200, 0, 50)); 
         buttonStrip.setPixelColor(2, buttonStrip.Color(0, 200, 50)); 
      }
      break;

    case STATE_FINAL_PLEA:
      // Both Green, pulsing fast but soft green
      buttonStrip.setPixelColor(0, buttonStrip.Color(0, panicPulse, 50)); 
      buttonStrip.setPixelColor(2, buttonStrip.Color(0, panicPulse, 50));
      break;

    case STATE_CELEBRATION:
      // Heartbeat Green (Not strobe)
      // Pulse between bright green and soft white-green
      {
        int winPulse = 100 + (int)(sin(now / 300.0) * 155); // fast but smooth
        if (winPulse < 0) winPulse = 0;
        buttonStrip.fill(buttonStrip.Color(winPulse/4, 200, winPulse/4));
      }
      break;

    case STATE_FAIR_RIGHT:
      // LED 0 -> RED, LED 2 -> GREEN (Soft)
      buttonStrip.setPixelColor(0, buttonStrip.Color(softPulse, 0, 0)); 
      buttonStrip.setPixelColor(2, buttonStrip.Color(0, softPulse, 0)); 
      break;

    default: // IDLE
      // LED 0 -> RED, LED 2 -> GREEN (Soft)
      buttonStrip.setPixelColor(0, buttonStrip.Color(softPulse, 0, 0)); 
      buttonStrip.setPixelColor(2, buttonStrip.Color(0, softPulse, 0)); 
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
      oledGeneric("SHE SAID YES!", "(FINALLY!)", "<3 <3 <3");
      stateStartTime = now;
    }
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
    else if (currentState == STATE_SWAP_MODE) {
      oledGeneric("You pressed YES!"); 
      buttonStrip.clear();
      // Trick Visuals
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