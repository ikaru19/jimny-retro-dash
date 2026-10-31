// Tes 1: apakah ESP32 hidup dan bisa di-upload?
// Hasil: lampu biru di papan berkedip, dan Serial Monitor menulis "Halo" tiap detik.
// Tidak perlu kabel apa pun, cukup colok USB.

const int LED = 2;  // lampu biru di papan ESP32 DevKit (tidak semua papan punya)

void setup() {
  Serial.begin(115200);
  pinMode(LED, OUTPUT);
}

void loop() {
  digitalWrite(LED, HIGH);
  delay(500);
  digitalWrite(LED, LOW);
  delay(500);
  Serial.printf("Halo dari ESP32! Detik ke-%lu\n", millis() / 1000);
}
