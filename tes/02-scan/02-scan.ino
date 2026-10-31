// Tes 2: cari alat yang tersambung ke jalur I2C (SDA = GPIO 21, SCL = GPIO 22).
// Layar, sensor miring, dan jam (nanti) berbagi dua kabel yang sama. Tiap alat punya "alamat".
// Hasil yang benar setelah semua MVP terpasang:  0x3C = layar,  0x69 = MPU-6050.

#include <Wire.h>

const char* nama(uint8_t a) {
  switch (a) {
    case 0x3C:
    case 0x3D: return "layar OLED";
    case 0x68: return "MPU-6050, tapi kabel AD0 belum ke 3V3! (atau jam RTC)";
    case 0x69: return "MPU-6050 (benar)";
    case 0x57: return "memori di modul jam RTC";
    default: return "alat lain";
  }
}

void setup() {
  Serial.begin(115200);
  Wire.begin(21, 22);
}

void loop() {
  Serial.println("Mencari alat I2C...");
  int ketemu = 0;
  for (uint8_t a = 1; a < 127; a++) {
    Wire.beginTransmission(a);
    if (Wire.endTransmission() == 0) {
      Serial.printf("  ketemu 0x%02X = %s\n", a, nama(a));
      ketemu++;
    }
  }
  if (!ketemu) Serial.println("  tidak ada. Cek kabel SDA (21), SCL (22), VCC dan GND.");
  Serial.println();
  delay(2000);
}
