// Tes 4: knob KY-040 terbaca?
// Hasil: angka di layar naik saat knob diputar ke kanan, turun ke kiri. Tekan knob = jumlah klik naik.
// Pin dan cara baca sama dengan program utama (sketch.ino).

#include <U8g2lib.h>
#include <Wire.h>

U8G2_SH1106_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, U8X8_PIN_NONE);
// U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, U8X8_PIN_NONE);

const int PIN_CLK = 18;
const int PIN_DT = 19;
const int PIN_SW = 5;

// Putaran dibaca lewat interrupt: ESP32 langsung "dicolek" tiap knob berputar,
// jadi tidak ada putaran yang terlewat walau layar sedang digambar.
volatile int angka = 0;
volatile uint32_t putarTerakhirUs = 0;

void IRAM_ATTR onPutar() {
  uint32_t now = micros();
  if (now - putarTerakhirUs < 1500) return;  // buang getaran kontak
  putarTerakhirUs = now;
  if (digitalRead(PIN_DT) == HIGH) angka++;
  else angka--;
}

void setup() {
  Serial.begin(115200);
  pinMode(PIN_CLK, INPUT_PULLUP);
  pinMode(PIN_DT, INPUT_PULLUP);
  pinMode(PIN_SW, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(PIN_CLK), onPutar, FALLING);
  u8g2.setBusClock(400000);
  u8g2.begin();
}

void loop() {
  static int klik = 0, angkaLama = 0;
  static bool tombolLama = HIGH;

  bool tombol = digitalRead(PIN_SW);
  if (tombol == LOW && tombolLama == HIGH) {
    klik++;
    Serial.printf("klik! (total %d)\n", klik);
    delay(20);  // buang getaran tombol
  }
  tombolLama = tombol;

  if (angka != angkaLama) {
    angkaLama = angka;
    Serial.printf("putar: %d\n", angkaLama);
  }

  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_6x10_tr);
  u8g2.drawStr(0, 9, "PUTAR / TEKAN KNOB");
  u8g2.setFont(u8g2_font_logisoso28_tn);
  char s[12];
  snprintf(s, sizeof s, "%d", angkaLama);
  u8g2.drawStr(64 - u8g2.getStrWidth(s) / 2, 46, s);
  u8g2.setFont(u8g2_font_6x10_tr);
  snprintf(s, sizeof s, "klik: %d", klik);
  u8g2.drawStr(0, 63, s);
  if (tombol == LOW) u8g2.drawStr(80, 63, "DITEKAN");
  u8g2.sendBuffer();
}
