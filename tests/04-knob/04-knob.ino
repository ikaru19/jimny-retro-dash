// Test 4: is the KY-040 knob read correctly?
// Result: the number on screen goes up when turning right and down when turning left. Pressing the knob counts clicks.
// Pins and reading method match the main program (sketch.ino).

#include <U8g2lib.h>
#include <Wire.h>

U8G2_SH1106_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, U8X8_PIN_NONE);
// U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, U8X8_PIN_NONE);

const int PIN_CLK = 18;
const int PIN_DT = 19;
const int PIN_SW = 5;

// Turning is read with an interrupt: the ESP32 gets "poked" on every knob step,
// so no step is missed even while the display is being drawn.
volatile int count = 0;
volatile uint32_t lastTurnUs = 0;

void IRAM_ATTR onTurn() {
  uint32_t now = micros();
  if (now - lastTurnUs < 1500) return;  // ignore contact bounce
  lastTurnUs = now;
  if (digitalRead(PIN_DT) == HIGH) count++;
  else count--;
}

void setup() {
  Serial.begin(115200);
  pinMode(PIN_CLK, INPUT_PULLUP);
  pinMode(PIN_DT, INPUT_PULLUP);
  pinMode(PIN_SW, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(PIN_CLK), onTurn, FALLING);
  u8g2.setBusClock(400000);
  u8g2.begin();
}

void loop() {
  static int clicks = 0, shownCount = 0;
  static bool lastButton = HIGH;

  bool button = digitalRead(PIN_SW);
  if (button == LOW && lastButton == HIGH) {
    clicks++;
    Serial.printf("click! (total %d)\n", clicks);
    delay(20);  // ignore button bounce
  }
  lastButton = button;

  if (count != shownCount) {
    shownCount = count;
    Serial.printf("turn: %d\n", shownCount);
  }

  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_6x10_tr);
  u8g2.drawStr(0, 9, "TURN / PRESS KNOB");
  u8g2.setFont(u8g2_font_logisoso28_tn);
  char s[12];
  snprintf(s, sizeof s, "%d", shownCount);
  u8g2.drawStr(64 - u8g2.getStrWidth(s) / 2, 46, s);
  u8g2.setFont(u8g2_font_6x10_tr);
  snprintf(s, sizeof s, "clicks: %d", clicks);
  u8g2.drawStr(0, 63, s);
  if (button == LOW) u8g2.drawStr(80, 63, "PRESSED");
  u8g2.sendBuffer();
}
