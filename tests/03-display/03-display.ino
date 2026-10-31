// Test 3: does the OLED display turn on?
// Result: "HELLO JIMNY", a seconds counter, and a small dot moving along the bottom.

#include <U8g2lib.h>
#include <Wire.h>

// 1.3" displays usually use the SH1106 chip.
// If the image is shifted by 2 pixels or there is noise along the edge, try the SSD1306 line (remove the //
// in front of it, then put // in front of the SH1106 line).
U8G2_SH1106_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, U8X8_PIN_NONE);
// U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, U8X8_PIN_NONE);

void setup() {
  Serial.begin(115200);
  u8g2.setBusClock(400000);
  u8g2.begin();
  Serial.println("Display test running. If the display stays dark, check the display wires.");
}

void loop() {
  u8g2.clearBuffer();
  u8g2.drawFrame(0, 0, 128, 64);
  u8g2.setFont(u8g2_font_helvB12_tr);
  u8g2.drawStr(64 - u8g2.getStrWidth("HELLO JIMNY") / 2, 26, "HELLO JIMNY");
  u8g2.setFont(u8g2_font_6x10_tr);
  char s[24];
  snprintf(s, sizeof s, "second %lu", millis() / 1000);
  u8g2.drawStr(64 - u8g2.getStrWidth(s) / 2, 44, s);
  u8g2.drawBox(4 + (millis() / 20) % 118, 54, 4, 4);
  u8g2.sendBuffer();
}
