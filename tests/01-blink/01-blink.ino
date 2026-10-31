// Test 1: is the ESP32 alive and can it take an upload?
// Result: the blue LED on the board blinks, and the Serial Monitor prints "Hello" every second.
// No wires needed, just the USB cable.

const int LED = 2;  // blue LED on the ESP32 DevKit (not every board has one)

void setup() {
  Serial.begin(115200);
  pinMode(LED, OUTPUT);
}

void loop() {
  digitalWrite(LED, HIGH);
  delay(500);
  digitalWrite(LED, LOW);
  delay(500);
  Serial.printf("Hello from ESP32! Second %lu\n", millis() / 1000);
}
