// Tes 5: sensor miring MPU-6050 terbaca?
// Hasil: dua angka derajat. Miringkan breadboard ke kiri/kanan dan depan/belakang, angkanya ikut berubah.
// Ini versi sederhana (cuma accelerometer). Program utama lebih halus karena juga pakai gyro.

#include <U8g2lib.h>
#include <Wire.h>

U8G2_SH1106_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, U8X8_PIN_NONE);
// U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, U8X8_PIN_NONE);

const uint8_t MPU_ADDR = 0x69;  // 0x69 karena kaki AD0 disambung ke 3V3

bool mpuBaca(float& ax, float& ay, float& az) {
  Wire.beginTransmission(MPU_ADDR);
  Wire.write(0x3B);  // mulai dari data accelerometer X
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom(MPU_ADDR, (uint8_t)6) != 6) return false;
  int16_t x = (Wire.read() << 8) | Wire.read();
  int16_t y = (Wire.read() << 8) | Wire.read();
  int16_t z = (Wire.read() << 8) | Wire.read();
  ax = x / 16384.0f;  // 16384 = 1g pada setelan bawaan
  ay = y / 16384.0f;
  az = z / 16384.0f;
  return true;
}

void setup() {
  Serial.begin(115200);
  u8g2.setBusClock(400000);
  u8g2.begin();
  Wire.beginTransmission(MPU_ADDR);
  Wire.write(0x6B);  // bangunkan sensor (bawaannya tidur)
  Wire.write(0x00);
  Wire.endTransmission();
}

void loop() {
  float ax, ay, az;
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_6x10_tr);
  if (!mpuBaca(ax, ay, az)) {
    u8g2.drawStr(0, 20, "MPU-6050 TIDAK ADA");
    u8g2.drawStr(0, 40, "cek kabel & AD0->3V3");
    Serial.println("MPU-6050 tidak menjawab di 0x69");
  } else {
    // Arah sama dengan program utama: X ke depan mobil, Y ke kiri, Z ke atas
    float miring = atan2f(ay, az) * 57.3f;
    float nanjak = atan2f(-ax, sqrtf(ay * ay + az * az)) * 57.3f;
    Serial.printf("miring %.1f  nanjak %.1f\n", miring, nanjak);

    char s[24];
    snprintf(s, sizeof s, "MIRING %+5.1f", miring);
    u8g2.drawStr(0, 10, s);
    snprintf(s, sizeof s, "NANJAK %+5.1f", nanjak);
    u8g2.drawStr(0, 22, s);
    // garis cakrawala: tetap datar terhadap tanah walau breadboard dimiringkan
    float r = -miring / 57.3f;
    int cy = 44 + (int)constrain(nanjak, -18, 18);
    u8g2.drawLine(64 - 40 * cosf(r), cy - 40 * sinf(r), 64 + 40 * cosf(r), cy + 40 * sinf(r));
    u8g2.drawCircle(64, 44, 3);
  }
  u8g2.sendBuffer();
  delay(50);
}
