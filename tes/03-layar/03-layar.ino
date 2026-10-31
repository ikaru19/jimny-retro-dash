// Tes 3: layar OLED menyala?
// Hasil: tulisan "HALO JIMNY", angka detik, dan titik kecil yang berjalan di bawah.

#include <U8g2lib.h>
#include <Wire.h>

// Layar 1.3" biasanya pakai chip SH1106.
// Kalau gambar bergeser 2 titik atau ada garis acak di pinggir, coba baris SSD1306 (hapus // di depannya,
// lalu beri // di depan baris SH1106).
U8G2_SH1106_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, U8X8_PIN_NONE);
// U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, U8X8_PIN_NONE);

void setup() {
  Serial.begin(115200);
  u8g2.setBusClock(400000);
  u8g2.begin();
  Serial.println("Tes layar jalan. Kalau layar tetap gelap, cek kabel layar.");
}

void loop() {
  u8g2.clearBuffer();
  u8g2.drawFrame(0, 0, 128, 64);
  u8g2.setFont(u8g2_font_helvB12_tr);
  u8g2.drawStr(14, 26, "HALO JIMNY");
  u8g2.setFont(u8g2_font_6x10_tr);
  char s[24];
  snprintf(s, sizeof s, "detik ke-%lu", millis() / 1000);
  u8g2.drawStr(30, 44, s);
  u8g2.drawBox(4 + (millis() / 20) % 118, 54, 4, 4);
  u8g2.sendBuffer();
}
