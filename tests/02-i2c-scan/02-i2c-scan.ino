// Test 2: find the devices on the I2C bus (SDA = GPIO 21, SCL = GPIO 22).
// The display, tilt sensor, and (later) clock share the same two wires. Each device has an "address".
// Correct result once all MVP parts are wired:  0x3C = display,  0x69 = MPU-6050.

#include <Wire.h>

const char* deviceName(uint8_t addr) {
  switch (addr) {
    case 0x3C:
    case 0x3D: return "OLED display";
    case 0x68: return "MPU-6050, but AD0 is not wired to 3V3! (or the RTC clock)";
    case 0x69: return "MPU-6050 (correct)";
    case 0x57: return "memory chip on the RTC module";
    default: return "other device";
  }
}

void setup() {
  Serial.begin(115200);
  Wire.begin(21, 22);
}

void loop() {
  Serial.println("Scanning I2C...");
  int found = 0;
  for (uint8_t addr = 1; addr < 127; addr++) {
    Wire.beginTransmission(addr);
    if (Wire.endTransmission() == 0) {
      Serial.printf("  found 0x%02X = %s\n", addr, deviceName(addr));
      found++;
    }
  }
  if (!found) Serial.println("  nothing. Check the SDA (21), SCL (22), VCC and GND wires.");
  Serial.println();
  delay(2000);
}
