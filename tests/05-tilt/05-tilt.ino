// Test 5: is the MPU-6050 tilt sensor read correctly?
// Result: two angles in degrees. Tilt the breadboard left/right and front/back, and the numbers follow.
// This is a simple version (accelerometer only). The main program is smoother because it also uses the gyro.

#include <U8g2lib.h>
#include <Wire.h>

U8G2_SH1106_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, U8X8_PIN_NONE);
// U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, U8X8_PIN_NONE);

const uint8_t MPU_ADDR = 0x69;  // 0x69 because the AD0 pin is wired to 3V3

bool mpuRead(float& ax, float& ay, float& az) {
  Wire.beginTransmission(MPU_ADDR);
  Wire.write(0x3B);  // start at accelerometer X
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom(MPU_ADDR, (uint8_t)6) != 6) return false;
  int16_t x = (Wire.read() << 8) | Wire.read();
  int16_t y = (Wire.read() << 8) | Wire.read();
  int16_t z = (Wire.read() << 8) | Wire.read();
  ax = x / 16384.0f;  // 16384 = 1g at the default setting
  ay = y / 16384.0f;
  az = z / 16384.0f;
  return true;
}

void setup() {
  Serial.begin(115200);
  u8g2.setBusClock(400000);
  u8g2.begin();
  Wire.beginTransmission(MPU_ADDR);
  Wire.write(0x6B);  // wake the sensor (it starts asleep)
  Wire.write(0x00);
  Wire.endTransmission();
}

void loop() {
  float ax, ay, az;
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_6x10_tr);
  if (!mpuRead(ax, ay, az)) {
    u8g2.drawStr(0, 20, "NO MPU-6050");
    u8g2.drawStr(0, 40, "check wires, AD0->3V3");
    Serial.println("MPU-6050 not answering at 0x69");
  } else {
    // Same axes as the main program: X toward the front of the car, Y to the left, Z up
    float roll = atan2f(ay, az) * 57.3f;
    float pitch = atan2f(-ax, sqrtf(ay * ay + az * az)) * 57.3f;
    Serial.printf("roll %.1f  pitch %.1f\n", roll, pitch);

    char s[24];
    snprintf(s, sizeof s, "ROLL  %+5.1f", roll);
    u8g2.drawStr(0, 10, s);
    snprintf(s, sizeof s, "PITCH %+5.1f", pitch);
    u8g2.drawStr(0, 22, s);
    // horizon line: stays level with the ground while the breadboard tilts
    float r = -roll / 57.3f;
    int cy = 44 + (int)constrain(pitch, -18, 18);
    u8g2.drawLine(64 - 40 * cosf(r), cy - 40 * sinf(r), 64 + 40 * cosf(r), cy + 40 * sinf(r));
    u8g2.drawCircle(64, 44, 3);
  }
  u8g2.sendBuffer();
  delay(50);
}
