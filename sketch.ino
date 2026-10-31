/*
  JIMNY CYBER DASH  v0.4
  Animasi boot + menu + jam + inclinometer MPU-6050, dengan 3 tema:
  CLEAN (bawaan), CYBERPUNK, dan 80-AN, serta 2 bahasa (Indonesia/English). Pilih di SETELAN.

  Board  : ESP32 DevKit V1
  Layar  : OLED 128x64 I2C (SSD1306 di Wokwi, biasanya SH1106 di layar 1.3" asli)
  Knob   : Rotary encoder KY-040
  Sensor : MPU-6050 (I2C 0x69, pin AD0 ke 3.3V)
  Jam    : RTC DS3231 (I2C 0x68), tetap jalan saat mobil mati (baterai koin)

  Knob:
    putar        = pilih menu
    klik         = masuk / kembali, klik saat boot = lewati
    tahan 1 dtk  = di layar kemiringan: jadikan posisi sekarang titik nol
    putar        = di layar kemiringan: reset rekaman sudut MAX
    SETELAN      = BAHASA (Indonesia/English) dan TEMA: putar untuk pilih, klik untuk simpan
    ATUR JAM     = putar ubah jam, klik pindah ke menit, klik lagi simpan ke RTC, tahan = batal
    KECERAHAN    = putar ubah terang layar (langsung terlihat), klik simpan, tahan = batal
    MODE MALAM   = 18:00-06:00 layar otomatis redup (MATI/20/40/60%)
    G-METER      = gaya rem/gas/belok; putar atau tahan = reset MAX
  Miring lebih dari batas (roll 30, pitch 35 derajat) = peringatan di layar apa pun.
  Diam 20 detik di menu = otomatis pindah ke jam.
*/

#include <Arduino.h>
#include <U8g2lib.h>
#include <Wire.h>
#include <Preferences.h>  // simpan setelan di memori ESP32 (NVS)
#include <math.h>

// ---------- Layar ----------
// Wokwi pakai SSD1306. Kalau layar 1.3" kamu ternyata SH1106, ganti baris ini dengan:
// U8G2_SH1106_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, U8X8_PIN_NONE);
U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, U8X8_PIN_NONE);

const int W = 128;
const int H = 64;

// ---------- Pin knob ----------
const int PIN_CLK = 18;
const int PIN_DT = 19;
const int PIN_SW = 5;
const int PIN_BUZZER = 25;  // buzzer PASIF (yang bisa beda nada), lewat transistor

// ---------- Tipe data (harus di atas semua fungsi, biar Arduino tidak bingung) ----------
enum Screen { SCR_BOOT, SCR_MENU, SCR_CLOCK, SCR_TILT, SCR_SOON, SCR_THEME, SCR_SETTINGS, SCR_LANG, SCR_SET_CLOCK, SCR_BRIGHT, SCR_GMETER, SCR_NIGHT };
enum Theme { THEME_CLEAN, THEME_CYBER, THEME_80S, THEME_3D, THEME_COUNT };
enum ButtonEvent { BTN_NONE, BTN_CLICK, BTN_HOLD };
enum Lang { LANG_ID, LANG_EN, LANG_COUNT };
struct Beep { uint16_t freq; uint16_t ms; };
struct Flip { char shown[4]; char prev[4]; uint32_t at; };  // satu angka jam yang bisa berputar balik (tema 3D)
struct V3 { float x, y, z; };                                // titik 3D
const int MESH_MAX_V = 64, MESH_MAX_E = 100;
struct Mesh { V3 v[MESH_MAX_V]; uint8_t e[MESH_MAX_E][2]; uint8_t nv, ne; };  // model kawat 3D (tema 3D)
struct Pose3 { float yaw, pitch, roll, tx, ty, tz, f, cx, cy; };              // posisi + arah model dan kamera  // satu bunyi buzzer: nada (Hz, 0 = diam) dan lamanya
// Semua teks di layar yang punya 2 bahasa (isinya di tabel TEXTS)
enum TextId {
  T_CLOCK, T_TILT, T_BATTERY, T_SETTINGS,
  T_LIVE, T_DEMO, T_GYRO_CAL, T_HOLD_ZERO, T_ZERO_SET, T_MAX_RESET,
  T_SOON, T_CLICK_BACK, T_TIME, T_SECONDS, T_TILT_SHORT,
  T_THEME, T_LANGUAGE, T_SAVED, T_CLICK_SET, T_BACK,
  T_THEME_CLEAN, T_THEME_CYBER, T_THEME_80S, T_THEME_3D,
  T_SET_CLOCK, T_HOUR, T_MINUTE, T_CLICK_NEXT, T_CLICK_SAVE, T_NO_RTC,
  T_BRIGHTNESS, T_HOLD_CANCEL,
  T_GMETER, T_BRAKE, T_ACCEL, T_TURN, T_NOW, T_TILT_WARN, T_CLICK_VIEW, T_NIGHT, T_OFF,
  T_COUNT
};

typedef void (*IconFn)(int cx, int cy, float r, float phase, float active);

struct MenuItem {
  TextId text;
  Screen target;
  IconFn icon;
};

// Titik pusat + rotasi + skala, untuk gambar yang bisa miring
struct Xf {
  float cx, cy, c, s, k;
};

struct RollDigit {
  char cur, prev;
  uint32_t changedAt;
};

// ---------- Teks 2 bahasa ----------
// Bahasa Inggris dibuat pendek supaya muat di layar 128 piksel.
Lang lang = LANG_ID;  // bawaan: Indonesia
const char* const LANG_NAMES[LANG_COUNT] = { "INDONESIA", "ENGLISH" };

const char* const TEXTS[][LANG_COUNT] = {
  { "JAM", "CLOCK" },                // T_CLOCK
  { "KEMIRINGAN", "TILT" },          // T_TILT
  { "VOLT AKI", "BATTERY" },         // T_BATTERY
  { "SETELAN", "SETTINGS" },         // T_SETTINGS
  { "LIVE", "LIVE" },                // T_LIVE
  { "DEMO", "DEMO" },                // T_DEMO
  { "KALIBRASI GYRO", "GYRO CAL" },  // T_GYRO_CAL
  { "TAHAN=NOL", "HOLD=ZERO" },      // T_HOLD_ZERO
  { "NOL DISET", "ZERO SET" },       // T_ZERO_SET
  { "MAX DIRESET", "MAX RESET" },    // T_MAX_RESET
  { "SEGERA HADIR", "COMING SOON" }, // T_SOON
  { "KLIK=KEMBALI", "CLICK=BACK" },  // T_CLICK_BACK
  { "WAKTU", "TIME" },               // T_TIME
  { "DETIK", "SEC" },                // T_SECONDS
  { "MIRING", "TILT" },              // T_TILT_SHORT
  { "TEMA", "THEME" },               // T_THEME
  { "BAHASA", "LANGUAGE" },          // T_LANGUAGE
  { "TERSIMPAN", "SAVED" },          // T_SAVED
  { "KLIK=PILIH", "CLICK=SET" },     // T_CLICK_SET
  { "KEMBALI", "BACK" },             // T_BACK
  { "CLEAN", "CLEAN" },              // T_THEME_CLEAN
  { "CYBERPUNK", "CYBERPUNK" },      // T_THEME_CYBER
  { "80-AN", "80S" },                // T_THEME_80S
  { "3D", "3D" },                    // T_THEME_3D
  { "ATUR JAM", "SET CLOCK" },       // T_SET_CLOCK
  { "JAM", "HOUR" },                 // T_HOUR
  { "MENIT", "MIN" },                // T_MINUTE
  { "KLIK=LANJUT", "CLICK=NEXT" },   // T_CLICK_NEXT
  { "KLIK=SIMPAN", "CLICK=SAVE" },   // T_CLICK_SAVE
  { "RTC TIDAK ADA", "NO RTC" },     // T_NO_RTC
  { "KECERAHAN", "BRIGHTNESS" },     // T_BRIGHTNESS
  { "TAHAN=BATAL", "HOLD=CANCEL" },  // T_HOLD_CANCEL
  { "G-METER", "G-METER" },          // T_GMETER
  { "REM", "BRAKE" },                // T_BRAKE
  { "GAS", "ACCEL" },                // T_ACCEL
  { "BELOK", "TURN" },               // T_TURN
  { "SKRG", "NOW" },                 // T_NOW
  { "MIRING!", "TILT!" },            // T_TILT_WARN
  { "KLIK=LIHAT", "CLICK=VIEW" },    // T_CLICK_VIEW
  { "MODE MALAM", "NIGHT MODE" },    // T_NIGHT
  { "MATI", "OFF" },                 // T_OFF
};
static_assert(sizeof(TEXTS) / sizeof(TEXTS[0]) == T_COUNT, "Tabel TEXTS harus punya 1 baris untuk tiap TextId");

const char* tr(TextId id) {
  return TEXTS[id][lang];
}

// ---------- Knob (interrupt) ----------
volatile int encSteps = 0;
volatile uint32_t encLastUs = 0;
portMUX_TYPE encMux = portMUX_INITIALIZER_UNLOCKED;

void IRAM_ATTR onEncoder() {
  uint32_t now = micros();
  if (now - encLastUs < 1500) return;  // buang getaran kontak
  encLastUs = now;
  portENTER_CRITICAL_ISR(&encMux);
  if (digitalRead(PIN_DT) == HIGH) encSteps++;
  else encSteps--;
  portEXIT_CRITICAL_ISR(&encMux);
}

int takeEncoderSteps() {
  portENTER_CRITICAL(&encMux);
  int s = encSteps;
  encSteps = 0;
  portEXIT_CRITICAL(&encMux);
  return s;
}

// Tombol juga pakai interrupt, jadi klik yang sangat cepat tetap terbaca
volatile bool swPressed = false;
volatile uint32_t swLastMs = 0;

void IRAM_ATTR onButton() {
  uint32_t now = millis();
  bool quiet = now - swLastMs > 50;  // perubahan dalam 50ms = getaran kontak
  swLastMs = now;
  if (quiet && digitalRead(PIN_SW) == LOW) swPressed = true;
}

// Klik dihitung saat tombol dilepas; ditahan >= 800ms jadi "tahan"
ButtonEvent readButton(uint32_t now) {
  static bool holding = false, holdFired = false;
  static uint32_t pressedAt = 0;
  if (swPressed) {
    swPressed = false;
    holding = true;
    holdFired = false;
    pressedAt = now;
  }
  if (!holding) return BTN_NONE;
  if (digitalRead(PIN_SW) == LOW) {
    if (!holdFired && now - pressedAt >= 800) {
      holdFired = true;
      return BTN_HOLD;
    }
    return BTN_NONE;
  }
  holding = false;
  return holdFired ? BTN_NONE : BTN_CLICK;
}

// ---------- Gambar dasar ----------
// Versi sendiri yang aman untuk koordinat minus (U8g2 pakai angka unsigned).
void px(int x, int y) {
  if (x >= 0 && x < W && y >= 0 && y < H) u8g2.drawPixel(x, y);
}

void hline(int x0, int x1, int y) {
  if (y < 0 || y >= H) return;
  if (x0 > x1) {
    int t = x0;
    x0 = x1;
    x1 = t;
  }
  if (x1 < 0 || x0 >= W) return;
  if (x0 < 0) x0 = 0;
  if (x1 >= W) x1 = W - 1;
  u8g2.drawHLine(x0, y, x1 - x0 + 1);
}

void vline(int x, int y0, int y1) {
  if (x < 0 || x >= W) return;
  if (y0 > y1) {
    int t = y0;
    y0 = y1;
    y1 = t;
  }
  if (y1 < 0 || y0 >= H) return;
  if (y0 < 0) y0 = 0;
  if (y1 >= H) y1 = H - 1;
  u8g2.drawVLine(x, y0, y1 - y0 + 1);
}

void box(int x, int y, int w, int h) {
  if (w <= 0 || h <= 0) return;
  for (int yy = y; yy < y + h; yy++) hline(x, x + w - 1, yy);
}

void frame(int x, int y, int w, int h) {
  if (w <= 1 || h <= 1) return;
  hline(x, x + w - 1, y);
  hline(x, x + w - 1, y + h - 1);
  vline(x, y + 1, y + h - 2);
  vline(x + w - 1, y + 1, y + h - 2);
}

void line(int x0, int y0, int x1, int y1) {
  // Penjaga: koordinat rusak (misal hasil NaN) jangan sampai bikin loop sangat panjang
  if (abs(x0) > 2000 || abs(y0) > 2000 || abs(x1) > 2000 || abs(y1) > 2000) return;
  int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
  int dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
  int err = dx + dy;
  while (true) {
    px(x0, y0);
    if (x0 == x1 && y0 == y1) break;
    int e2 = 2 * err;
    if (e2 >= dy) {
      err += dy;
      x0 += sx;
    }
    if (e2 <= dx) {
      err += dx;
      y0 += sy;
    }
  }
}

void circle(int cx, int cy, int r) {
  if (r <= 0) {
    px(cx, cy);
    return;
  }
  int x = r, y = 0, err = 1 - r;
  while (x >= y) {
    px(cx + x, cy + y);
    px(cx + y, cy + x);
    px(cx - y, cy + x);
    px(cx - x, cy + y);
    px(cx - x, cy - y);
    px(cx - y, cy - x);
    px(cx + y, cy - x);
    px(cx + x, cy - y);
    y++;
    if (err < 0) {
      err += 2 * y + 1;
    } else {
      x--;
      err += 2 * (y - x) + 1;
    }
  }
}

void disc(int cx, int cy, int r) {
  if (r <= 0) {
    px(cx, cy);
    return;
  }
  for (int dy = -r; dy <= r; dy++) {
    int dx = (int)(sqrtf((float)(r * r - dy * dy)) + 0.5f);
    hline(cx - dx, cx + dx, cy + dy);
  }
}

// Isi bentuk poligon (titik dalam float)
void fillPoly(const float* xs, const float* ys, int n) {
  float minY = ys[0], maxY = ys[0];
  for (int i = 1; i < n; i++) {
    if (ys[i] < minY) minY = ys[i];
    if (ys[i] > maxY) maxY = ys[i];
  }
  int y0 = max(0, (int)floorf(minY));
  int y1 = min(H - 1, (int)ceilf(maxY));
  for (int y = y0; y <= y1; y++) {
    float fy = y + 0.5f;
    float nodes[8];
    int cnt = 0;
    for (int i = 0, j = n - 1; i < n && cnt < 8; j = i++) {
      if ((ys[i] < fy && ys[j] >= fy) || (ys[j] < fy && ys[i] >= fy)) {
        nodes[cnt++] = xs[i] + (fy - ys[i]) / (ys[j] - ys[i]) * (xs[j] - xs[i]);
      }
    }
    for (int a = 1; a < cnt; a++) {
      float v = nodes[a];
      int b = a - 1;
      while (b >= 0 && nodes[b] > v) {
        nodes[b + 1] = nodes[b];
        b--;
      }
      nodes[b + 1] = v;
    }
    for (int k = 0; k + 1 < cnt; k += 2) {
      int xa = (int)ceilf(nodes[k] - 0.5f);
      int xb = (int)floorf(nodes[k + 1] - 0.5f);
      if (xb >= xa) hline(xa, xb, y);
    }
  }
}

Xf makeXf(float cx, float cy, float ang, float scale) {
  Xf t = { cx, cy, cosf(ang), sinf(ang), scale };
  return t;
}

float xfX(const Xf& t, float x, float y) {
  return t.cx + (x * t.c - y * t.s) * t.k;
}

float xfY(const Xf& t, float x, float y) {
  return t.cy + (x * t.s + y * t.c) * t.k;
}

void fillShape(const Xf& t, const float (*pts)[2], int n) {
  float xs[12], ys[12];  // maks 12 titik
  for (int i = 0; i < n; i++) {
    xs[i] = xfX(t, pts[i][0], pts[i][1]);
    ys[i] = xfY(t, pts[i][0], pts[i][1]);
  }
  fillPoly(xs, ys, n);
}

void discAt(const Xf& t, float x, float y, float r) {
  disc(lroundf(xfX(t, x, y)), lroundf(xfY(t, x, y)), lroundf(r * t.k));
}

void lineAt(const Xf& t, float x0, float y0, float x1, float y1) {
  line(lroundf(xfX(t, x0, y0)), lroundf(xfY(t, x0, y0)), lroundf(xfX(t, x1, y1)), lroundf(xfY(t, x1, y1)));
}

// ---------- Easing (biar gerakan halus, tidak kaku) ----------
float clamp01(float x) {
  return x < 0 ? 0 : (x > 1 ? 1 : x);
}

float easeOutCubic(float t) {
  t = clamp01(t);
  float u = 1 - t;
  return 1 - u * u * u;
}

float easeInOutCubic(float t) {
  t = clamp01(t);
  return t < 0.5f ? 4 * t * t * t : 1 - powf(-2 * t + 2, 3) / 2;
}

float easeOutBack(float t) {
  t = clamp01(t);
  const float c1 = 1.70158f, c3 = c1 + 1;
  return 1 + c3 * powf(t - 1, 3) + c1 * powf(t - 1, 2);
}

int wrap(int i, int n) {
  int r = i % n;
  return r < 0 ? r + n : r;
}

// Bar atas gaya HUD: label putih dengan ujung miring + garis dengan titik scan berjalan
void drawTopBar(const char* title, const char* right, uint32_t now) {
  u8g2.setFont(u8g2_font_5x7_tr);
  int tw = u8g2.getStrWidth(title) + 6;
  for (int y = 0; y < 9; y++) hline(0, tw + (8 - y) / 2, y);
  u8g2.setDrawColor(0);
  u8g2.drawStr(2, 7, title);
  u8g2.setDrawColor(1);
  if (right) {
    u8g2.setFont(u8g2_font_4x6_tr);
    u8g2.drawStr(W - 1 - u8g2.getStrWidth(right), 6, right);
  }
  hline(0, W - 1, 10);
  int sx = (now / 12) % (W + 40) - 20;
  u8g2.setDrawColor(0);
  hline(sx, sx + 6, 10);
  u8g2.setDrawColor(1);
  hline(sx + 2, sx + 4, 11);
}

// Tulis teks dengan batas area (clip). Kalau italic, tiap baris piksel digeser
// sedikit ke kanan makin ke atas, jadi hurufnya miring.
void drawText(int x, int base, const char* s, bool italic, int clipX1, int clipY0, int clipY1) {
  if (!italic) {
    u8g2.setClipWindow(0, clipY0, clipX1, clipY1);
    u8g2.drawStr(x, base, s);
    u8g2.setMaxClipWindow();
    return;
  }
  for (int y = max(clipY0, base - 22); y < min(clipY1, base + 5); y++) {
    u8g2.setClipWindow(0, y, clipX1, y + 1);
    u8g2.drawStr(x + (base - y) / 3, base, s);
  }
  u8g2.setMaxClipWindow();
}

// ---------- Gambar Jimny SJ410 (bisa diputar) ----------
// Proporsi diukur dari blueprint SJ410 hardtop 1982.
// Satuan: titik (0,0) = tengah bawah (di tanah), y minus = ke atas.

// Tampak belakang: tinggi 1.6, lebar bodi 1.46 (kabin tinggi dan sempit khas Jimny)
const float JEEP_REAR_TIRE_L[][2] = { { -0.71f, 0 }, { -0.49f, 0 }, { -0.49f, -0.33f }, { -0.71f, -0.33f } };
const float JEEP_REAR_TIRE_R[][2] = { { 0.49f, 0 }, { 0.71f, 0 }, { 0.71f, -0.33f }, { 0.49f, -0.33f } };
const float JEEP_REAR_BODY[][2] = { { -0.73f, -0.26f }, { 0.73f, -0.26f }, { 0.73f, -0.95f }, { -0.73f, -0.95f } };
const float JEEP_REAR_CABIN[][2] = { { -0.68f, -0.95f }, { -0.61f, -1.6f }, { 0.61f, -1.6f }, { 0.68f, -0.95f } };
const float JEEP_REAR_GLASS[][2] = { { -0.49f, -1.05f }, { -0.45f, -1.5f }, { 0.45f, -1.5f }, { 0.49f, -1.05f } };
const float JEEP_REAR_MIRROR_L[][2] = { { -0.84f, -1.07f }, { -0.68f, -1.07f }, { -0.68f, -1.23f }, { -0.84f, -1.23f } };
const float JEEP_REAR_MIRROR_R[][2] = { { 0.68f, -1.07f }, { 0.84f, -1.07f }, { 0.84f, -1.23f }, { 0.68f, -1.23f } };

void drawJeepRear(float gx, float gy, float ang, float scale) {
  Xf t = makeXf(gx, gy, ang, scale);
  lineAt(t, -0.55f, -0.18f, 0.55f, -0.18f);  // as roda
  fillShape(t, JEEP_REAR_TIRE_L, 4);
  fillShape(t, JEEP_REAR_TIRE_R, 4);
  fillShape(t, JEEP_REAR_BODY, 4);
  fillShape(t, JEEP_REAR_CABIN, 4);
  fillShape(t, JEEP_REAR_MIRROR_L, 4);
  fillShape(t, JEEP_REAR_MIRROR_R, 4);
  u8g2.setDrawColor(0);
  fillShape(t, JEEP_REAR_GLASS, 4);
  lineAt(t, -0.73f, -0.41f, 0.73f, -0.41f);  // garis bumper
  discAt(t, 0, -0.88f, 0.38f);               // celah di sekeliling ban serep
  if (scale >= 10) {
    discAt(t, -0.62f, -0.75f, 0.05f);  // lampu belakang
    discAt(t, 0.62f, -0.75f, 0.05f);
  }
  u8g2.setDrawColor(1);
  discAt(t, 0, -0.88f, 0.32f);  // ban serep di pintu belakang
  if (scale >= 20) {  // velg cuma kalau gambar besar, biar tidak ramai
    u8g2.setDrawColor(0);
    discAt(t, 0, -0.88f, 0.2f);
    u8g2.setDrawColor(1);
    discAt(t, 0, -0.88f, 0.1f);  // velg
  }
}

// ---------- Ikon menu (digambar pakai kode, jadi bisa dibesar-kecilkan) ----------
void iconClock(int cx, int cy, float r, float ph, float a) {
  int R = lroundf(r);
  circle(cx, cy, R);
  if (R >= 8) {
    px(cx, cy - R + 2);
    px(cx + R - 2, cy);
    px(cx, cy + R - 2);
    px(cx - R + 2, cy);
  }
  float mAng = -0.52f + ph * 4.0f;   // jarum menit
  float hAng = -2.62f + ph * 0.33f;  // jarum jam
  line(cx, cy, cx + lroundf(cosf(mAng) * r * 0.75f), cy + lroundf(sinf(mAng) * r * 0.75f));
  line(cx, cy, cx + lroundf(cosf(hAng) * r * 0.5f), cy + lroundf(sinf(hAng) * r * 0.5f));
  disc(cx, cy, 1);
}

void iconTilt(int cx, int cy, float r, float ph, float a) {
  float ang = sinf(ph * 2.5f) * 0.3f * a;
  float gy = cy + r * 0.65f;
  Xf t = makeXf(cx, gy, ang, r * 0.85f);
  lineAt(t, -1.35f, 0.05f, 1.35f, 0.05f);
  drawJeepRear(cx, gy, ang, r * 0.85f);
}

void iconBattery(int cx, int cy, float r, float ph, float a) {
  int w = lroundf(r * 1.7f), h = lroundf(r * 1.0f);
  int x0 = cx - w / 2, y0 = cy - h / 2 + 1;
  frame(x0, y0, w, h);
  int tw = max(2, (int)lroundf(r * 0.25f));
  box(x0 + w / 5, y0 - 2, tw, 2);
  box(x0 + w - w / 5 - tw, y0 - 2, tw, 2);
  int cells = a > 0.5f ? ((int)(ph * 3.0f) % 5) : 4;  // animasi ngecas
  int cw = (w - 4 - 3) / 4;
  for (int i = 0; i < cells; i++) box(x0 + 2 + i * (cw + 1), y0 + 2, cw, h - 4);
}

void iconGMeter(int cx, int cy, float r, float ph, float a) {
  int R = lroundf(r);
  circle(cx, cy, R);
  if (R >= 6) circle(cx, cy, R / 2);
  hline(cx - 1, cx + 1, cy);
  vline(cx, cy - 1, cy + 1);
  float dx = sinf(ph * 3.1f) * 0.55f * a, dy = cosf(ph * 2.3f) * 0.45f * a;  // titik bergoyang
  disc(cx + lroundf(dx * R), cy + lroundf(dy * R), max(1, R / 5));
}

void iconGear(int cx, int cy, float r, float ph, float a) {
  float rot = ph * 1.2f;
  disc(cx, cy, lroundf(r * 0.68f));
  int tw = max(1, (int)lroundf(r * 0.1f));
  for (int k = 0; k < 8; k++) {
    float ang = rot + k * (PI / 4);
    float c = cosf(ang), s = sinf(ang);
    for (int o = -tw; o <= tw; o++) {
      float ox = -s * o, oy = c * o;
      line(cx + lroundf(c * r * 0.5f + ox), cy + lroundf(s * r * 0.5f + oy),
           cx + lroundf(c * r * 0.95f + ox), cy + lroundf(s * r * 0.95f + oy));
    }
  }
  u8g2.setDrawColor(0);
  disc(cx, cy, max(1, (int)lroundf(r * 0.3f)));
  u8g2.setDrawColor(1);
}

const MenuItem MENU[] = {
  { T_CLOCK, SCR_CLOCK, iconClock },
  { T_TILT, SCR_TILT, iconTilt },
  { T_GMETER, SCR_GMETER, iconGMeter },
  { T_BATTERY, SCR_SOON, iconBattery },
  { T_SETTINGS, SCR_SETTINGS, iconGear },
};
const int MENU_COUNT = sizeof(MENU) / sizeof(MENU[0]);

// ---------- Status aplikasi ----------
Screen current = SCR_BOOT;
Screen nextScreen = SCR_BOOT;
bool transitioning = false;
uint32_t transStart = 0;
uint32_t screenStart = 0;
const uint32_t TRANS_HALF = 180;

int menuTarget = 0;
float menuPos = 0;
float iconPhase[MENU_COUNT];
float arrowBumpL = 0, arrowBumpR = 0;
uint32_t menuLastFrame = 0;
int soonItem = 0;
int menuSel = 0;  // menu yang sedang dipilih
uint8_t snapC[W * H / 8];  // salinan layar untuk efek (zoom, geser, garis tepi)
Flip flipD[4] = {};        // empat angka jam yang bisa berputar balik (tema 3D)
Mesh meshFighter = {}, meshTitle = {}, meshItem[5] = {};         // model 3D (tema 3D)
bool meshesReady = false;
int clockHandIdx = -1;  // titik pusat jarum di model jam menu
const int STARS = 28;
float starX[STARS], starY[STARS], starZ[STARS];
bool starsReady = false;
uint32_t starLast = 0;

Preferences prefs;
Theme theme = THEME_CLEAN;  // bawaan: paling mudah dibaca saat menyetir
int themePick = 0;          // pilihan di layar TEMA (THEME_COUNT = KEMBALI)
uint32_t themeSavedAt = 0;
int settingsPick = 0;       // baris di layar SETELAN
int langPick = 0;           // pilihan di layar BAHASA (LANG_COUNT = KEMBALI)
uint32_t langSavedAt = 0;
int setHour = 0, setMin = 0;  // isian di layar ATUR JAM
int setField = 0;             // 0 = sedang ubah jam, 1 = sedang ubah menit
uint32_t clockSavedAt = 0;

// Kecerahan layar: 5 tingkat. Nilai = "contrast" OLED (0..255), makin besar makin terang.
const uint8_t BRIGHT_PERCENT[] = { 20, 40, 60, 80, 100 };
const uint8_t BRIGHT_CONTRAST[] = { 1, 30, 80, 160, 255 };
const int BRIGHT_COUNT = sizeof(BRIGHT_PERCENT);
int bright = BRIGHT_COUNT - 1;  // bawaan: paling terang
int appliedBright = -1;         // tingkat yang sedang dipasang di layar

// Mode malam: jam 18:00-06:00 layar otomatis lebih redup.
// Pilihan: indeks ke BRIGHT_PERCENT, -1 = mati.
const int NIGHT_START_H = 18, NIGHT_END_H = 6;
const int NIGHT_OPTIONS[] = { -1, 0, 1, 2 };  // MATI, 20%, 40%, 60%
const int NIGHT_COUNT = sizeof(NIGHT_OPTIONS) / sizeof(NIGHT_OPTIONS[0]);
int nightOpt = 2;  // bawaan: malam 40%
int nightPick = 0;
uint32_t nightSavedAt = 0;
int brightPick = 0;             // pilihan di layar KECERAHAN
uint32_t brightSavedAt = 0;

uint32_t clockBaseSec = 0;
RollDigit clockDigits[4];

// ---------- BOOT (gaya cyberpunk) ----------
const uint32_t BOOT_POWER = 700;
const uint32_t BOOT_DECODE = 2400;
const uint32_t BOOT_DRIVE = 3200;
const uint32_t BOOT_HUD = 2600;
const uint32_t BOOT_TOTAL = BOOT_POWER + BOOT_DECODE + BOOT_DRIVE + BOOT_HUD;

// Angka acak yang selalu sama untuk input yang sama (biar acaknya terkendali)
uint32_t hash32(uint32_t x) {
  x ^= x >> 16;
  x *= 0x7feb352dUL;
  x ^= x >> 15;
  x *= 0x846ca68bUL;
  x ^= x >> 16;
  return x;
}

bool inWindow(uint32_t t, uint32_t start, uint32_t len) {
  return t >= start && t < start + len;
}

// Efek glitch langsung di memori layar.
// Memori OLED terbagi 8 pita mendatar setinggi 8 piksel; 1 byte = 8 piksel tegak.
void shiftBand(int band, int dx) {
  uint8_t* row = u8g2.getBufferPtr() + band * W;
  uint8_t tmp[W];
  for (int x = 0; x < W; x++) {
    int sx = x - dx;
    tmp[x] = (sx >= 0 && sx < W) ? row[sx] : 0;
  }
  memcpy(row, tmp, W);
}

void invertBand(int band, int x0, int x1) {
  uint8_t* row = u8g2.getBufferPtr() + band * W;
  for (int x = max(0, x0); x < min(W, x1); x++) row[x] ^= 0xFF;
}

// amount 0..1: makin besar makin rusak (pita bergeser, terbalik, hilang)
void glitchFrame(float amount, uint32_t seed) {
  if (amount <= 0) return;
  for (int b = 0; b < 8; b++) {
    uint32_t h = hash32(seed * 8 + b);
    if (h % 1000 < amount * 600) shiftBand(b, (int)((h >> 10) % 25) - 12);
    if ((h >> 20) % 1000 < amount * 160) {
      int x0 = (h >> 4) % W;
      invertBand(b, x0, x0 + 8 + (h >> 13) % 40);
    }
    if (amount > 0.7f && (h >> 8) % 1000 < (amount - 0.7f) * 1500) {
      memset(u8g2.getBufferPtr() + b * W, 0, W);
    }
  }
}

void drawCorners(int x0, int y0, int x1, int y1, int len) {
  hline(x0, x0 + len, y0);
  vline(x0, y0, y0 + len);
  hline(x1 - len, x1, y0);
  vline(x1, y0, y0 + len);
  hline(x0, x0 + len, y1);
  vline(x0, y1 - len, y1);
  hline(x1 - len, x1, y1);
  vline(x1, y1 - len, y1);
}

// 1. Nyala: sinyal rusak, "NO SIGNAL", lalu garis scan menyapu
void drawPowerOn(uint32_t t) {
  uint32_t seed = t / 33;
  if (t < 480) {
    int bars = 2 + hash32(seed) % 6;
    for (int i = 0; i < bars; i++) {
      uint32_t h = hash32(seed * 31 + i);
      int y = h % H, x0 = (h >> 8) % W, len = 10 + (h >> 16) % 90;
      hline(x0, x0 + len, y);
      if (h & 1) hline(x0 + 6, x0 + len / 2, y + 1);
    }
  }
  if (t > 180 && t < 520 && hash32(seed + 7) % 4) {
    u8g2.setFont(u8g2_font_5x7_tr);
    const char* s = "NO SIGNAL";
    int w = u8g2.getStrWidth(s);
    int x = (W - w) / 2;
    u8g2.setDrawColor(0);
    box(x - 5, 25, w + 10, 13);
    u8g2.setDrawColor(1);
    frame(x - 5, 25, w + 10, 13);
    u8g2.drawStr(x, 34, s);
  }
  if (t >= 520) {
    int y = (t - 520) * H / 180;
    hline(0, W - 1, y);
    for (int x = (t / 20) % 3; x < W; x += 3) px(x, y - 3);
    for (int x = (t / 20) % 5; x < W; x += 5) px(x, y - 6);
  }
  if (t > 150 && t < 520) glitchFrame(0.7f, seed);
}

// 2. Hujan kode, lalu tulisan JIMNY "dipecahkan" huruf per huruf
const char* HEX_CHARS = "0123456789ABCDEF";
const char* SCRAMBLE = "ABCDEFGHJKLMNPQRSTUVWXYZ0123456789#$%&@";

void drawRain(uint32_t t) {
  u8g2.setFont(u8g2_font_4x6_tr);
  for (int c = 0; c < 21; c++) {
    uint32_t hc = hash32(c * 977 + 13);
    float speed = 45 + hc % 70;  // piksel per detik
    int len = 3 + (hc >> 8) % 5;
    float span = H + len * 7 + 10;
    int head = (int)fmodf(t / 1000.0f * speed + (hc >> 4) % 97, span);
    int x = c * 6 + 1;
    for (int k = 0; k < len; k++) {
      int y = head - k * 7;
      if (y < 6 || y > H + 5) continue;
      char ch[2] = { HEX_CHARS[hash32(c * 131 + (y / 7) * 17 + t / 120) % 16], 0 };
      if (k == 0) {
        box(x - 1, y - 6, 6, 7);  // kepala hujan: kotak terang
        u8g2.setDrawColor(0);
        u8g2.drawStr(x, y, ch);
        u8g2.setDrawColor(1);
      } else if (k < len - 1 || (t / 70) % 2) {
        u8g2.drawStr(x, y, ch);
      }
    }
  }
}

void drawDecode(uint32_t t) {
  drawRain(t);
  if (t < 500) return;
  uint32_t lt = t - 500;

  const char* word = "JIMNY";
  u8g2.setFont(u8g2_font_helvB18_tr);
  int widths[5], total = 0;
  for (int i = 0; i < 5; i++) {
    char c[2] = { word[i], 0 };
    widths[i] = u8g2.getStrWidth(c);
    total += widths[i] + (i < 4 ? 3 : 0);
  }

  // Panel hitam yang terbuka di tengah hujan
  int pw = lroundf((total + 18) * easeOutBack(lt / 250.0f));
  int px0 = 64 - pw / 2;
  u8g2.setDrawColor(0);
  box(px0, 11, pw, 30);
  u8g2.setDrawColor(1);
  if (pw > 12) drawCorners(px0, 11, px0 + pw - 1, 40, 4);

  if (lt > 150) {
    int x = (W - total - 6) / 2;
    for (int i = 0; i < 5; i++) {
      uint32_t lockAt = 300 + i * 120;
      char c[2] = { lt >= lockAt ? word[i] : SCRAMBLE[hash32(i * 53 + lt / 45) % strlen(SCRAMBLE)], 0 };
      drawText(x, 35, c, true, W, 0, H);
      x += widths[i] + 3;
    }
  }

  // Katakana ジムニー (nama Jimny dalam huruf Jepang)
  if (lt > 1000) {
    const char* kana = "ジムニー";  // 4 huruf, masing-masing 3 byte UTF-8
    u8g2.setFont(u8g2_font_b10_t_japanese1);
    int n = min(4, (int)((lt - 1000) / 90) + 1);
    char buf[16];
    memcpy(buf, kana, n * 3);
    buf[n * 3] = 0;
    int kw = u8g2.getUTF8Width(kana);
    int kx = (W - kw) / 2;
    u8g2.setDrawColor(0);
    box(kx - 3, 43, kw + 6, 13);
    u8g2.setDrawColor(1);
    u8g2.drawUTF8(kx, 54, buf);
  }

  if (inWindow(lt, 950, 70) || inWindow(lt, 1250, 50)) glitchFrame(0.8f, t / 30);
}

// 3. Kota neon + matahari bergaris + jalan grid, Jimny datang dari cakrawala
const int HORIZON = 32;
const uint8_t CITY[][3] = {  // {x, lebar, tinggi}
  { 0, 6, 12 }, { 6, 8, 21 }, { 14, 5, 9 }, { 19, 9, 25 }, { 28, 6, 14 }, { 34, 7, 8 },
  { 86, 7, 10 }, { 93, 6, 18 }, { 99, 10, 27 }, { 109, 5, 12 }, { 114, 8, 21 }, { 122, 6, 9 },
};

void drawSun(int cx, int cy, int r, uint32_t t) {
  for (int dy = -r; dy <= 0; dy++) {
    int depth = dy + r;
    if (depth > r / 3 && (depth + (int)(t / 140)) % 5 < depth * 3 / r) continue;  // celah garis
    int dx = (int)sqrtf((float)(r * r - dy * dy));
    hline(cx - dx, cx + dx, cy + dy);
  }
}

void drawCity(uint32_t t) {
  for (int i = 0; i < 12; i++) {
    int x = CITY[i][0], w = CITY[i][1], h = CITY[i][2];
    int top = HORIZON - h;
    u8g2.setDrawColor(0);
    box(x, top, w, h);
    u8g2.setDrawColor(1);
    hline(x, x + w - 1, top);
    vline(x, top, HORIZON);
    vline(x + w - 1, top, HORIZON);
    for (int wy = top + 3; wy < HORIZON - 1; wy += 3) {
      for (int wx = x + 2; wx < x + w - 2; wx += 2) {
        uint32_t flick = ((wx + wy) % 7 == 0) ? t / 600 : 0;  // sebagian jendela nyala-mati
        if (hash32(i * 401 + wy * 13 + wx + flick) % 3 == 0) px(wx, wy);
      }
    }
    if (h > 20) {
      vline(x + w / 2, top - 4, top);  // antena
      if ((t / 300) % 2) px(x + w / 2, top - 5);
    }
  }
}

void drawGrid(uint32_t t, float speed) {
  hline(0, W - 1, HORIZON);
  float phase = fmodf(t / 1000.0f * speed, 1.0f);
  for (int i = 0; i < 3; i++) {
    float z = i + 1 - phase;  // jarak garis dari kamera, bergerak mendekat
    int y = HORIZON + 3 + lroundf(29.0f * powf(0.45f, z));
    if (y < H) hline(0, W - 1, y);
  }
  for (int k = -7; k <= 7; k++) line(64 + k * 9, HORIZON, 64 + k * 42, H + 8);
}

// Suzuki Jimny tampak depan: badan kotak, lampu bulat, gril 5 lubang
const float JF_BODY[][2] = { { -1.0f, -0.22f }, { 1.0f, -0.22f }, { 1.0f, -1.02f }, { -1.0f, -1.02f } };
const float JF_CABIN[][2] = { { -0.86f, -1.02f }, { -0.76f, -1.62f }, { 0.76f, -1.62f }, { 0.86f, -1.02f } };
const float JF_GLASS[][2] = { { -0.68f, -1.1f }, { -0.61f, -1.52f }, { 0.61f, -1.52f }, { 0.68f, -1.1f } };
const float JF_TIRE_L[][2] = { { -0.98f, 0.0f }, { -0.68f, 0.0f }, { -0.68f, -0.3f }, { -0.98f, -0.3f } };
const float JF_TIRE_R[][2] = { { 0.68f, 0.0f }, { 0.98f, 0.0f }, { 0.98f, -0.3f }, { 0.68f, -0.3f } };
const float JF_BUMPER[][2] = { { -1.07f, -0.2f }, { 1.07f, -0.2f }, { 1.07f, -0.36f }, { -1.07f, -0.36f } };

void outlineShape(const Xf& t, const float (*pts)[2], int n) {
  for (int i = 0; i < n; i++) {
    int j = (i + 1) % n;
    lineAt(t, pts[i][0], pts[i][1], pts[j][0], pts[j][1]);
  }
}

// lights: 0 = mati, 1 = nyala, >1 = silau besar
void drawJimnyFront(float cx, float gy, float s, float lights, uint32_t t) {
  Xf T = makeXf(cx, gy, 0, s);
  fillShape(T, JF_TIRE_L, 4);
  fillShape(T, JF_TIRE_R, 4);
  u8g2.setDrawColor(0);
  fillShape(T, JF_BODY, 4);
  fillShape(T, JF_CABIN, 4);
  u8g2.setDrawColor(1);
  outlineShape(T, JF_BODY, 4);
  outlineShape(T, JF_CABIN, 4);
  outlineShape(T, JF_GLASS, 4);
  lineAt(T, -0.42f, -1.16f, -0.18f, -1.46f);  // kilap kaca
  lineAt(T, -0.28f, -1.16f, -0.04f, -1.46f);
  lineAt(T, -0.7f, -1.74f, 0.7f, -1.74f);  // rak atap
  lineAt(T, -0.6f, -1.62f, -0.6f, -1.74f);
  lineAt(T, 0.6f, -1.62f, 0.6f, -1.74f);
  for (int k = -2; k <= 2; k++) lineAt(T, k * 0.13f, -0.86f, k * 0.13f, -0.56f);  // gril
  fillShape(T, JF_BUMPER, 4);

  for (int side = -1; side <= 1; side += 2) {
    lineAt(T, side * 1.0f, -1.08f, side * 1.14f, -1.12f);  // spion
    int hx = lroundf(xfX(T, side * 0.64f, -0.7f)), hy = lroundf(xfY(T, side * 0.64f, -0.7f));
    int r = max(1, (int)lroundf(0.2f * s));
    if (lights > 0) disc(hx, hy, r);
    else circle(hx, hy, r);
    if (lights >= 1) {
      int L = r + 2 + lroundf(lights * s / 3);
      for (int a = 0; a < 8; a++) {
        float ang = a * PI / 4 + t * 0.002f;
        int len = (a % 2) ? L / 2 : L;
        line(hx + lroundf(cosf(ang) * (r + 2)), hy + lroundf(sinf(ang) * (r + 2)),
             hx + lroundf(cosf(ang) * len), hy + lroundf(sinf(ang) * len));
      }
    }
  }
}

void drawDrive(uint32_t t) {
  drawSun(64, HORIZON, 20, t);
  drawCity(t);
  drawGrid(t, 1.8f);

  float p = easeOutCubic(t / 2000.0f);
  float s = 3.0f + 18.0f * p;
  float gy = HORIZON + 1 + 29.0f * p + ((t / 110) % 2 ? 0.0f : 0.6f * p);  // goyang suspensi
  float lights = 0;
  if (t > 2100 && !inWindow(t, 2180, 90)) lights = 1;
  if (inWindow(t, 2400, 300)) lights = 1.0f + 2.0f * (1.0f - (t - 2400) / 300.0f);

  u8g2.setDrawColor(0);  // bayangan di bawah mobil
  box(lroundf(64 - s * 1.1f), lroundf(gy) - 1, lroundf(s * 2.2f), 3);
  u8g2.setDrawColor(1);
  drawJimnyFront(64, gy, s, lights, t);

  // HUD kecil di pojok
  u8g2.setFont(u8g2_font_4x6_tr);
  char spd[12];
  snprintf(spd, sizeof(spd), "SPD %03d", (int)(120 * (1 - p)));
  u8g2.drawStr(1, 6, spd);
  if ((t / 250) % 2) u8g2.drawStr(W - 1 - u8g2.getStrWidth("4WD"), 6, "4WD");

  if (t > 2900) glitchFrame((t - 2900) / 300.0f, t / 30);
}

// 4. Logo + HUD: bingkai mengunci, loading, ONLINE
void drawHud(uint32_t t) {
  for (int y = 4; y < H; y += 8) {
    for (int x = ((y / 8) % 2) * 4; x < W; x += 8) px(x, y);  // titik grid latar
  }

  float p = easeOutCubic(t / 450.0f);
  int x0 = lroundf(p * 12), x1 = W - 1 - lroundf(p * 12);
  int y0 = lroundf(p * 10), y1 = H - 1 - lroundf(p * 13);
  drawCorners(x0, y0, x1, y1, 6);

  u8g2.setFont(u8g2_font_helvB18_tr);
  int lw = u8g2.getStrWidth("JIMNY");
  int lx = (W - lw - 6) / 2;
  if (t > 150) {
    int rx = lx - 2 + lroundf(easeOutCubic((t - 150) / 450.0f) * (lw + 12));
    u8g2.setDrawColor(0);
    box(lx - 4, 13, lw + 14, 24);
    u8g2.setDrawColor(1);
    drawText(lx, 34, "JIMNY", true, max(0, rx), 0, H);
    if (rx < lx + lw + 8) vline(rx, 12, 38);  // garis scan
    // Bayangan bergeser saat glitch (pengganti efek RGB split)
    if (inWindow(t, 900, 60) || inWindow(t, 1500, 50) || inWindow(t, 2100, 90)) {
      u8g2.setDrawColor(2);
      drawText(lx - 3, 34, "JIMNY", true, W, 0, H);
      u8g2.setDrawColor(1);
    }
  }

  if (t > 600) {
    u8g2.setFont(u8g2_font_b10_t_japanese1);
    u8g2.drawUTF8(x0 + 4, 48, "ジムニー");
    u8g2.setFont(u8g2_font_4x6_tr);
    const char* tag = "SUZUKI 4X4";
    u8g2.drawStr(x1 - 3 - u8g2.getStrWidth(tag), 47, tag);
  }

  // Status + persen di atas
  float pct = easeInOutCubic((t - min(t, (uint32_t)300)) / 2000.0f);
  u8g2.setFont(u8g2_font_4x6_tr);
  if (pct < 1.0f) u8g2.drawStr(1, 6, "SYS://BOOT");
  else if ((t / 200) % 2) u8g2.drawStr(1, 6, "SYS://ONLINE");
  char pc[8];
  snprintf(pc, sizeof(pc), "%03d%%", (int)lroundf(pct * 100));
  u8g2.drawStr(W - 1 - u8g2.getStrWidth(pc), 6, pc);

  // Bar loading bersegmen
  frame(12, 55, 104, 6);
  int fill = lroundf(pct * 100);
  for (int x = 14; x < 14 + fill; x += 4) box(x, 57, min(3, 14 + fill - x), 2);

  if (inWindow(t, 900, 60) || inWindow(t, 2100, 90)) glitchFrame(0.5f, t / 30);
}

void drawBoot(uint32_t now) {
  uint32_t t = now - screenStart;
  if (t < BOOT_POWER) {
    drawPowerOn(t);
    return;
  }
  t -= BOOT_POWER;
  if (t < BOOT_DECODE) {
    drawDecode(t);
    return;
  }
  t -= BOOT_DECODE;
  if (t < BOOT_DRIVE) {
    drawDrive(t);
    return;
  }
  drawHud(t - BOOT_DRIVE);
}

// ---------- MENU UTAMA (gaya HUD cyberpunk) ----------
const uint32_t IDLE_TO_CLOCK_MS = 20000;  // diam selama ini di menu = pindah ke jam

uint32_t lastInputMs = 0;
uint32_t menuChangedAt = 0;
int lastSel = -1;

void drawChamferFrame(int x0, int y0, int x1, int y1, int c) {
  hline(x0 + c, x1 - c, y0);
  hline(x0 + c, x1 - c, y1);
  vline(x0, y0 + c, y1 - c);
  vline(x1, y0 + c, y1 - c);
  line(x0, y0 + c, x0 + c, y0);
  line(x1 - c, y0, x1, y0 + c);
  line(x0, y1 - c, x0 + c, y1);
  line(x1 - c, y1, x1, y1 - c);
}

// Label miring (jajaran genjang) putih dengan teks hitam
void drawTag(int cx, int yTop, int h, const char* text, int textW) {
  int x0 = cx - textW / 2 - 8;
  for (int y = yTop; y < yTop + h; y++) {
    int k = (yTop + h - 1 - y) / 2;
    hline(x0 + k, x0 + textW + 12 + k, y);
  }
  u8g2.setDrawColor(0);
  u8g2.drawStr(cx - textW / 2 + 3, yTop + h - 2, text);
  u8g2.setDrawColor(1);
}

// Gerak menu (posisi, ikon, panah). Dipakai semua tema.
void menuAnimate(uint32_t now) {
  float dt = (now - menuLastFrame) / 1000.0f;
  if (dt > 0.1f) dt = 0.1f;
  menuLastFrame = now;

  // Posisi carousel mengejar target dengan halus
  menuPos += (menuTarget - menuPos) * min(1.0f, dt * 12.0f);
  if (fabsf(menuTarget - menuPos) < 0.002f) {
    menuPos = menuTarget;
    int shift = menuTarget - wrap(menuTarget, MENU_COUNT);
    menuTarget -= shift;
    menuPos -= shift;
  }
  arrowBumpL = max(0.0f, arrowBumpL - dt * 5);
  arrowBumpR = max(0.0f, arrowBumpR - dt * 5);

  // Ikon hanya bergerak kalau sedang di tengah
  for (int i = 0; i < MENU_COUNT; i++) {
    float diff = fmodf(menuPos - i, (float)MENU_COUNT);
    if (diff < 0) diff += MENU_COUNT;
    float dist = min(diff, MENU_COUNT - diff);
    iconPhase[i] += dt * (1.0f - min(1.0f, dist));
  }

  menuSel = wrap((int)lroundf(menuPos), MENU_COUNT);
  if (menuSel != lastSel) {
    lastSel = menuSel;
    menuChangedAt = now;
  }
}

void drawMenu(uint32_t now) {
  int sel = menuSel;

  char idx[8];
  snprintf(idx, sizeof(idx), "%02d/%02d", sel + 1, MENU_COUNT);
  drawTopBar("SYS//MENU", idx, now);

  // Titik grid HUD di latar
  for (int y = 16; y < 50; y += 8) {
    for (int x = 2 + ((y / 8) % 2) * 4; x < W; x += 8) px(x, y);
  }

  // Area tengah dibersihkan dulu supaya ikon pilihan bersih
  const int CY = 30, GAP = 42;
  u8g2.setDrawColor(0);
  box(46, 13, 37, 35);
  u8g2.setDrawColor(1);

  int base = (int)floorf(menuPos);
  for (int slot = base - 2; slot <= base + 3; slot++) {
    float d = slot - menuPos;
    float ad = fabsf(d);
    if (ad >= 2.0f) continue;
    float r = ad < 1 ? 13.0f - 5.0f * ad : 8.0f - 3.0f * (ad - 1.0f);
    int idx2 = wrap(slot, MENU_COUNT);
    MENU[idx2].icon(64 + lroundf(d * GAP), CY, r, iconPhase[idx2], 1.0f - min(1.0f, ad));
  }

  // Bingkai sudut terpotong + garis scan yang turun membalik warna
  drawChamferFrame(46, 13, 82, 47, 5);
  int pulse = (now / 300) % 2;
  hline(43 - pulse, 44 - pulse, CY);
  hline(84 + pulse, 85 + pulse, CY);
  int sy = 14 + (now / 30) % 33;
  u8g2.setDrawColor(2);
  hline(47, 81, sy);
  u8g2.setDrawColor(1);

  // Garis titik penghubung ke ikon samping
  for (int x = 31; x < 43; x += 2) px(x, CY);
  for (int x = 87; x < 98; x += 2) px(x, CY);

  // Panah ganda, menyentak saat knob diputar
  int ax = 2 - lroundf(arrowBumpL * 2);
  for (int k = 0; k < 2; k++) {
    line(ax + 3 + k * 3, CY - 3, ax + k * 3, CY);
    line(ax + k * 3, CY, ax + 3 + k * 3, CY + 3);
  }
  int bx = W - 3 + lroundf(arrowBumpR * 2);
  for (int k = 0; k < 2; k++) {
    line(bx - 3 - k * 3, CY - 3, bx - k * 3, CY);
    line(bx - k * 3, CY, bx - 3 - k * 3, CY + 3);
  }

  // Nama menu: huruf acak yang "dipecahkan" kiri ke kanan
  const char* label = tr(MENU[sel].text);
  int len = strlen(label);
  uint32_t el = now - menuChangedAt;
  char buf[20];
  for (int i = 0; i < len && i < 19; i++) {
    bool locked = el >= (uint32_t)(60 + i * 35) || label[i] == ' ';
    buf[i] = locked ? label[i] : SCRAMBLE[hash32(i * 7 + now / 40) % strlen(SCRAMBLE)];
  }
  buf[min(len, 19)] = 0;
  u8g2.setFont(u8g2_font_helvB10_tr);
  drawTag(64, 51, 13, buf, u8g2.getStrWidth(label));

  if (el < 90) glitchFrame(0.35f, now / 30);
}

// ---------- JAM (RTC DS3231) ----------
// RTC tetap menghitung waktu walau mobil mati (dapat listrik dari BATT+ dan baterai koin).
// Register waktu DS3231 sama dengan DS1307 (yang dipakai di Wokwi), jadi kode ini jalan di keduanya.
const uint8_t RTC_ADDR = 0x68;
bool rtcOk = false;
uint32_t clockBaseMs = 0;  // kapan clockBaseSec terakhir diambil

uint8_t bcdToDec(uint8_t v) {
  return (v >> 4) * 10 + (v & 0x0F);
}

uint8_t decToBcd(uint8_t v) {
  return ((v / 10) << 4) | (v % 10);
}

// Baca jam dari RTC sebagai detik sejak tengah malam
bool rtcRead(uint32_t& secOfDay) {
  Wire.beginTransmission(RTC_ADDR);
  Wire.write(0x00);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom(RTC_ADDR, (uint8_t)3) != 3) return false;
  uint8_t s = bcdToDec(Wire.read() & 0x7F);
  uint8_t m = bcdToDec(Wire.read() & 0x7F);
  uint8_t hr = Wire.read();
  uint8_t h;
  if (hr & 0x40) h = bcdToDec(hr & 0x1F) % 12 + ((hr & 0x20) ? 12 : 0);  // RTC dalam mode 12 jam
  else h = bcdToDec(hr & 0x3F);
  if (s > 59 || m > 59 || h > 23) return false;
  secOfDay = h * 3600UL + m * 60UL + s;
  return true;
}

void rtcWrite(int h, int m) {
  Wire.beginTransmission(RTC_ADDR);
  Wire.write(0x00);
  Wire.write(0);               // detik = 0 (sekaligus menyalakan jam DS1307 yang berhenti)
  Wire.write(decToBcd(m));
  Wire.write(decToBcd(h));     // mode 24 jam
  Wire.endTransmission();
}

void initClock() {
  uint32_t s;
  rtcOk = rtcRead(s);
  if (!rtcOk) {
    // RTC tidak ketemu: mulai dari jam saat kode di-compile
    const char* t = __TIME__;  // "HH:MM:SS"
    s = ((t[0] - '0') * 10 + (t[1] - '0')) * 3600UL + ((t[3] - '0') * 10 + (t[4] - '0')) * 60UL +
        (t[6] - '0') * 10 + (t[7] - '0');
  }
  clockBaseSec = s;
  clockBaseMs = millis();
}

uint32_t clockSeconds() {
  uint32_t now = millis();
  uint32_t s;
  if (rtcOk && now - clockBaseMs > 60000 && rtcRead(s)) {  // samakan dengan RTC tiap menit
    clockBaseSec = s;
    clockBaseMs = now;
  }
  return (clockBaseSec + (now - clockBaseMs) / 1000) % 86400;
}

void setClock(int h, int m) {
  if (rtcOk) rtcWrite(h, m);
  clockBaseSec = h * 3600UL + m * 60UL;
  clockBaseMs = millis();
}

void clockDigitsNow(char out[4]) {
  uint32_t s = clockSeconds();
  int h = s / 3600, m = (s / 60) % 60;
  out[0] = '0' + h / 10;
  out[1] = '0' + h % 10;
  out[2] = '0' + m / 10;
  out[3] = '0' + m % 10;
}

void enterClock() {
  char d[4];
  clockDigitsNow(d);
  for (int i = 0; i < 4; i++) clockDigits[i] = { d[i], d[i], 0 };
}

// Angka berganti dengan efek gulung (seperti odometer)
void drawRollDigit(int x, int dw, const RollDigit& d, uint32_t now) {
  const int BASE = 50, TRAVEL = 26;
  float p = d.changedAt ? easeOutCubic((now - d.changedAt) / 400.0f) : 1.0f;
  char a[2] = { d.cur, 0 };
  if (p >= 1.0f) {
    u8g2.drawStr(x, BASE, a);
    return;
  }
  char b[2] = { d.prev, 0 };
  u8g2.setClipWindow(x, BASE - TRAVEL + 1, x + dw, BASE + 1);
  u8g2.drawStr(x, BASE + lroundf(p * TRAVEL), b);
  u8g2.drawStr(x, BASE - TRAVEL + lroundf(p * TRAVEL), a);
  u8g2.setMaxClipWindow();
}

void drawClock(uint32_t now) {
  drawTopBar("SYS//TIME", "SUZUKI JIMNY", now);

  char d[4];
  clockDigitsNow(d);
  bool changed = false;
  for (int i = 0; i < 4; i++) {
    if (d[i] != clockDigits[i].cur) {
      clockDigits[i].prev = clockDigits[i].cur;
      clockDigits[i].cur = d[i];
      clockDigits[i].changedAt = now;
    }
    if (clockDigits[i].changedAt && now - clockDigits[i].changedAt < 120) changed = true;
  }

  drawCorners(3, 15, W - 4, 52, 4);

  u8g2.setFont(u8g2_font_logisoso24_tn);
  int dw = u8g2.getStrWidth("0");
  const int COLON = 8, SEC_W = 12;
  int total = dw * 4 + 2 + COLON + 2 + SEC_W;
  int x = (W - total) / 2;
  int xs[4] = { x, x + dw + 1, x + 2 * dw + 2 + COLON, x + 3 * dw + 3 + COLON };
  for (int i = 0; i < 4; i++) drawRollDigit(xs[i], dw, clockDigits[i], now);

  // Titik dua berkedip
  int cx = x + 2 * dw + 1 + COLON / 2;
  if (millis() % 1000 < 500) {
    box(cx - 1, 32, 3, 3);
    box(cx - 1, 42, 3, 3);
  }

  // Detik kecil
  uint32_t s = clockSeconds();
  char sec[3];
  snprintf(sec, sizeof(sec), "%02d", (int)(s % 60));
  u8g2.setFont(u8g2_font_6x10_tr);
  u8g2.drawStr(x + 4 * dw + 5 + COLON, 34, sec);

  // Bar detik bersegmen: 30 segmen, 1 segmen = 2 detik
  int filled = (s % 60) / 2;
  for (int i = 0; i < 30; i++) {
    int bx = 4 + i * 4;
    if (i < filled) box(bx, 56, 3, 4);
    else if (i == filled && (now / 250) % 2) box(bx, 56, 3, 4);
    else px(bx + 1, 59);
  }

  // Garis scan lewat tiap 4 detik
  uint32_t ph = now % 4000;
  if (ph < 1600) {
    u8g2.setDrawColor(2);
    hline(4, W - 5, 16 + ph / 46);
    u8g2.setDrawColor(1);
  }

  if (changed) glitchFrame(0.4f, now / 30);
}

// ---------- Sensor kemiringan MPU-6050: gabungan gyro + accelerometer ----------
// Cara kerja (mirip inclinometer pabrikan, tapi tanpa data kecepatan roda):
//  1. Gyro dijumlahkan tiap baca: cepat dan tidak tertipu rem/gas, tapi lama-lama melenceng.
//  2. Accelerometer (arah gravitasi) pelan-pelan menarik hasil kembali ke sudut yang benar.
//  3. Kalau accelerometer tidak cocok dengan 1g atau dengan gyro, berarti ada guncangan,
//     rem, atau gas. Koreksinya dikurangi supaya tidak muncul "miring palsu".
//  4. Kesalahan nol gyro (bias) dipelajari ulang setiap kali mobil diam.
const uint8_t MPU_ADDR = 0x69;  // pin AD0 disambung ke 3.3V, karena 0x68 sudah dipakai RTC
const float ACC_LSB = 16384.0f;  // skala +-2g
const float GYRO_LSB = 131.0f;   // skala +-250 derajat/detik
const float FUSION_TAU = 1.5f;   // detik: makin besar makin percaya gyro

bool mpuOk = false;
float rollDeg = 0, pitchDeg = 0;    // roll + = sisi kanan turun, pitch + = nanjak
float rollZero = 0, pitchZero = 0;  // titik nol hasil kalibrasi
float maxRoll = 0, maxPitch = 0;    // sudut terbesar sejak reset (nilai mutlak)
float gLon = 0, gLat = 0;           // gaya G: + = gas / belok kiri, - = rem / belok kanan
float maxBrake = 0, maxAccel = 0, maxTurn = 0;
uint32_t gResetAt = 0;
uint32_t calibratedAt = 0, maxResetAt = 0;

float gyroBias[3] = { 0, 0, 0 };
float biasSum[3] = { 0, 0, 0 };
int biasCount = 0;
bool biasReady = false;
uint32_t stillSince = 0;
uint32_t lastImuUs = 0;
bool imuStarted = false;
float disagreeSec = 0;  // berapa lama accelerometer dan gyro tidak cocok

void mpuWrite(uint8_t reg, uint8_t val) {
  Wire.beginTransmission(MPU_ADDR);
  Wire.write(reg);
  Wire.write(val);
  Wire.endTransmission();
}

bool mpuBegin() {
  Wire.beginTransmission(MPU_ADDR);
  if (Wire.endTransmission() != 0) return false;
  mpuWrite(0x6B, 0x01);  // bangunkan sensor, pakai jam dari gyro (lebih stabil)
  mpuWrite(0x1A, 0x03);  // filter bawaan ~44Hz, meredam getaran mesin
  mpuWrite(0x19, 0x04);  // 200 sampel per detik
  mpuWrite(0x1B, 0x00);  // gyro +-250 derajat/detik
  mpuWrite(0x1C, 0x00);  // accelerometer +-2g
  return true;
}

void resetMax(uint32_t now) {
  maxRoll = 0;
  maxPitch = 0;
  maxResetAt = now;
}

void mpuUpdate() {
  if (!mpuOk) return;
  Wire.beginTransmission(MPU_ADDR);
  Wire.write(0x3B);  // accelerometer X, Y, Z, suhu, lalu gyro X, Y, Z
  if (Wire.endTransmission(false) != 0) return;
  if (Wire.requestFrom(MPU_ADDR, (uint8_t)14) != 14) return;
  uint8_t b[14];
  for (int i = 0; i < 14; i++) b[i] = Wire.read();
  float a[3], g[3];
  for (int i = 0; i < 3; i++) {
    a[i] = (int16_t)((b[i * 2] << 8) | b[i * 2 + 1]) / ACC_LSB;
    g[i] = (int16_t)((b[8 + i * 2] << 8) | b[9 + i * 2]) / GYRO_LSB;
  }
  uint32_t nowUs = micros();
  float dt = (nowUs - lastImuUs) / 1000000.0f;
  lastImuUs = nowUs;
  if (dt > 0.1f) dt = 0.1f;

  // Arah sumbu: X ke depan mobil, Y ke kiri, Z ke atas. Kalau dipasang beda arah, tukar di sini.
  float ax = a[0], ay = a[1], az = a[2];
  float aNorm = sqrtf(ax * ax + ay * ay + az * az);

  // 1. Pelajari bias gyro saat diam: accelerometer ~1g dan gyro hampir tidak berputar
  uint32_t ms = millis();
  float stillLimit = biasReady ? 2.0f : 10.0f;  // derajat/detik
  bool still = fabsf(aNorm - 1.0f) < 0.05f;
  for (int i = 0; i < 3; i++) {
    if (fabsf(g[i] - gyroBias[i]) > stillLimit) still = false;
  }
  if (!still) {
    stillSince = 0;
    biasCount = 0;
    for (int i = 0; i < 3; i++) biasSum[i] = 0;
  } else {
    if (!stillSince) stillSince = ms;
    if (ms - stillSince > 300) {
      for (int i = 0; i < 3; i++) biasSum[i] += g[i];
      if (++biasCount >= (biasReady ? 400 : 60)) {
        for (int i = 0; i < 3; i++) {
          gyroBias[i] = biasSum[i] / biasCount;
          biasSum[i] = 0;
        }
        biasCount = 0;
        biasReady = true;
      }
    }
  }
  float gx = g[0] - gyroBias[0], gy = g[1] - gyroBias[1], gz = g[2] - gyroBias[2];

  // 2. Sudut menurut accelerometer (arah gravitasi)
  float accRoll = atan2f(ay, az) * RAD_TO_DEG;
  float accPitch = atan2f(ax, sqrtf(ay * ay + az * az)) * RAD_TO_DEG;
  if (!imuStarted) {
    rollDeg = accRoll;
    pitchDeg = accPitch;
    imuStarted = true;
    return;
  }

  // 3. Sudut menurut gyro: sudut sebelumnya + kecepatan putar x waktu
  //    (rumus sudut Euler, supaya tetap benar saat roll dan pitch terjadi bersamaan)
  float r = rollDeg * DEG_TO_RAD, p = pitchDeg * DEG_TO_RAD;
  float rollRate = gx - (gy * sinf(r) + gz * cosf(r)) * tanf(p);
  float pitchRate = -gy * cosf(r) + gz * sinf(r);
  float gyroRoll = rollDeg + rollRate * dt;
  float gyroPitch = pitchDeg + pitchRate * dt;

  // 4. Seberapa percaya accelerometer saat ini
  float trust = clamp01(1.0f - fabsf(aNorm - 1.0f) / 0.15f);  // guncangan/rem/gas kuat
  float diff = (fabsf(accRoll - gyroRoll) + fabsf(accPitch - gyroPitch)) / 6.0f;
  float gate = max(0.3f, 1.0f / (1.0f + diff * diff));  // beda jauh dari gyro = gaya dorong, bukan miring
  // Rem/gas/belok jarang lebih dari 2 detik. Kalau tidak cocok lebih lama, kemungkinan gyro
  // yang salah (bias), jadi pelan-pelan percaya accelerometer lagi. Tanpa ini, gyro yang
  // melenceng > 2 derajat/detik bisa bikin angka melonjak jauh.
  if (diff > 0.5f) disagreeSec += dt;
  else disagreeSec = max(0.0f, disagreeSec - 2 * dt);
  gate += (1.0f - gate) * clamp01((disagreeSec - 2.0f) / 2.0f);
  trust *= gate;
  float k = biasReady ? dt / (FUSION_TAU + dt) * trust : 0.2f;  // gyro belum siap: ikut accelerometer
  rollDeg = gyroRoll + k * (accRoll - gyroRoll);
  pitchDeg = gyroPitch + k * (accPitch - gyroPitch);

  // 5. Rekam sudut terbesar (dihitung dari titik nol)
  if (biasReady) {
    maxRoll = max(maxRoll, fabsf(rollDeg - rollZero));
    maxPitch = max(maxPitch, fabsf(pitchDeg - pitchZero));
  }

  // 6. Gaya G: accelerometer dikurangi gravitasi (menurut sudut mobil), lalu diperhalus
  //    supaya getaran mesin tidak bikin titik G-meter loncat-loncat
  float rr = rollDeg * DEG_TO_RAD, pp = pitchDeg * DEG_TO_RAD;
  float kg = dt / (0.15f + dt);
  gLon += ((ax - sinf(pp)) - gLon) * kg;              // + = gas, - = rem
  gLat += ((ay - cosf(pp) * sinf(rr)) - gLat) * kg;   // + = belok kiri (badan terdorong ke kanan)
  if (biasReady) {
    maxBrake = max(maxBrake, -gLon);
    maxAccel = max(maxAccel, gLon);
    maxTurn = max(maxTurn, fabsf(gLat));
  }
}

void resetGMax(uint32_t now) {
  maxBrake = maxAccel = maxTurn = 0;
  gResetAt = now;
}

// ---------- KEMIRINGAN ----------
// Sudut yang ditampilkan (dihitung dari titik nol)
void tiltValues(uint32_t now, float& roll, float& pitch) {
  if (mpuOk) {
    roll = rollDeg - rollZero;
    pitch = pitchDeg - pitchZero;
  } else {
    // Sensor tidak ketemu: pakai data demo yang bergerak sendiri
    float t = now / 1000.0f;
    roll = 18.0f * sinf(t * 0.9f) + 4.0f * sinf(t * 2.3f);
    pitch = 12.0f * sinf(t * 0.6f + 1.0f) + 3.0f * sinf(t * 1.7f);
  }
}

const char* tiltStatus(uint32_t now) {
  TextId tag = !mpuOk ? T_DEMO : (biasReady ? T_LIVE : T_GYRO_CAL);
  if (now - screenStart < 2500) tag = T_HOLD_ZERO;
  if (calibratedAt && now - calibratedAt < 1200) tag = T_ZERO_SET;
  else if (maxResetAt && now - maxResetAt < 1200) tag = T_MAX_RESET;
  return tr(tag);
}

// ---------- SEGERA HADIR ----------
void drawSoon(uint32_t now) {
  const MenuItem& it = MENU[soonItem];
  uint32_t t = now - screenStart;
  drawHeader(tr(it.text), tr(T_CLICK_BACK), now);
  float r = 18.0f * easeOutBack(t / 400.0f);
  if (r >= 2) it.icon(64, 31, r, t / 1000.0f, 1.0f);

  const char* msg = tr(T_SOON);
  u8g2.setFont(u8g2_font_6x10_tr);
  int mw = u8g2.getStrWidth(msg);
  int n = min((int)strlen(msg), (int)(t / 50));
  char buf[16];
  memcpy(buf, msg, n);
  buf[n] = 0;
  int x = (W - mw) / 2;
  u8g2.drawStr(x, 62, buf);
  if ((now / 300) % 2) box(x + u8g2.getStrWidth(buf) + 1, 54, 5, 9);
}

// ---------- TEMA ----------
// Tiga gaya tampilan. Sensor, knob, dan alur layar sama; yang beda cuma cara menggambar.
// Pilihan disimpan di memori NVS ESP32, jadi tetap diingat walau mobil dimatikan.
#define FONT_80S u8g2_font_pxplusibmcga_8r  // huruf kotak 8x8 khas PC IBM tahun 80-an

const char* themeName(Theme t) {
  return tr((TextId)(T_THEME_CLEAN + t));
}

void loadSettings() {
  prefs.begin("jimny", false);
  uint8_t v = prefs.getUChar("theme", THEME_CLEAN);
  theme = v < THEME_COUNT ? (Theme)v : THEME_CLEAN;
  v = prefs.getUChar("lang", LANG_ID);
  lang = v < LANG_COUNT ? (Lang)v : LANG_ID;
  v = prefs.getUChar("bright", BRIGHT_COUNT - 1);
  bright = v < BRIGHT_COUNT ? v : BRIGHT_COUNT - 1;
  v = prefs.getUChar("night", 2);
  nightOpt = v < NIGHT_COUNT ? v : 2;
  applyBrightness(bright);
}

void applyBrightness(int level) {
  u8g2.setContrast(BRIGHT_CONTRAST[level]);
  appliedBright = level;
}

void saveNight(int opt) {
  nightOpt = opt;
  prefs.putUChar("night", (uint8_t)opt);
}

bool isNight() {
  uint32_t h = clockSeconds() / 3600;
  return h >= NIGHT_START_H || h < NIGHT_END_H;
}

// Dipanggil tiap putaran loop: pasang kecerahan siang/malam (tidak saat sedang disetel)
void updateAutoBrightness() {
  if (current == SCR_BRIGHT) return;
  int lv = NIGHT_OPTIONS[nightOpt];
  int want = (lv >= 0 && isNight()) ? min(bright, lv) : bright;
  if (want != appliedBright) applyBrightness(want);
}

const char* nightText(int opt, char* buf) {
  if (NIGHT_OPTIONS[opt] < 0) return tr(T_OFF);
  snprintf(buf, 6, "%d%%", BRIGHT_PERCENT[NIGHT_OPTIONS[opt]]);
  return buf;
}

void saveBrightness(int level) {
  bright = level;
  applyBrightness(level);
  prefs.putUChar("bright", (uint8_t)level);
}

void saveLang(Lang l) {
  lang = l;
  prefs.putUChar("lang", (uint8_t)l);
}

void saveTheme(Theme t) {
  theme = t;
  prefs.putUChar("theme", (uint8_t)t);
}

void clockHHMM(char out[6]) {
  char d[4];
  clockDigitsNow(d);
  out[0] = d[0];
  out[1] = d[1];
  out[2] = ':';
  out[3] = d[2];
  out[4] = d[3];
  out[5] = 0;
}

// Pola titik 4x4 untuk efek "muncul pelan" (OLED cuma bisa nyala atau mati)
const uint8_t BAYER4[4][4] = { { 0, 8, 2, 10 }, { 12, 4, 14, 6 }, { 3, 11, 1, 9 }, { 15, 7, 13, 5 } };

// Hapus sebagian piksel di kotak ini. level 0 = hilang semua, 1 = utuh.
void ditherBox(int x0, int y0, int x1, int y1, float level) {
  if (level >= 1) return;
  int th = (int)(clamp01(level) * 16);
  u8g2.setDrawColor(0);
  for (int y = max(0, y0); y <= min(H - 1, y1); y++) {
    for (int x = max(0, x0); x <= min(W - 1, x1); x++) {
      if (BAYER4[y & 3][x & 3] >= th) u8g2.drawPixel(x, y);
    }
  }
  u8g2.setDrawColor(1);
}

// Judul layar sesuai tema
void drawHeader(const char* title, const char* right, uint32_t now) {
  if (theme == THEME_CYBER) {
    drawTopBar(title, right, now);
    return;
  }
  if (theme == THEME_CLEAN) {
    u8g2.setFont(u8g2_font_5x7_tr);
    u8g2.drawStr(1, 7, title);
    if (right) u8g2.drawStr(W - 1 - u8g2.getStrWidth(right), 7, right);
    hline(0, W - 1, 10);
    return;
  }
  if (theme == THEME_3D) {  // tulisan tebal, garis bawah punya tebal (arsir)
    u8g2.setFont(u8g2_font_helvB08_tr);
    u8g2.drawStr(1, 8, title);
    u8g2.setFont(u8g2_font_5x7_tr);
    if (right) u8g2.drawStr(W - 1 - u8g2.getStrWidth(right), 8, right);
    hline(0, W - 1, 10);
    dotLine(1, 11, W - 1, 11);
    return;
  }
  u8g2.setFont(FONT_80S);  // 80-an: huruf kotak + garis ganda
  u8g2.drawStr(0, 8, title);
  u8g2.setFont(u8g2_font_5x7_tr);
  if (right) u8g2.drawStr(W - 1 - u8g2.getStrWidth(right), 8, right);
  hline(0, W - 1, 10);
  hline(0, W - 1, 12);
}

// Geser seluruh layar ke kanan (dx plus) atau kiri (dx minus)
void shiftAll(int dx) {
  for (int b = 0; b < 8; b++) shiftBand(b, dx);
}

// Efek TV tabung: gambar menyusut jadi garis di tengah, lalu jadi titik.
// a = 0 normal, a = 1 tinggal titik.
void crtEffect(float a) {
  if (a <= 0) return;
  static uint8_t src[W * H / 8];
  uint8_t* buf = u8g2.getBufferPtr();
  memcpy(src, buf, sizeof(src));
  memset(buf, 0, sizeof(src));
  if (a < 0.6f) {
    float sy = max(0.02f, 1.0f - a / 0.6f);  // sisa tinggi gambar
    for (int y = 0; y < H; y++) {
      int fy = (int)floorf(31.5f + (y - 31.5f) / sy);
      if (fy < 0 || fy >= H) continue;
      for (int x = 0; x < W; x++) {
        if (src[x + (fy / 8) * W] & (1 << (fy % 8))) buf[x + (y / 8) * W] |= 1 << (y % 8);
      }
    }
    if (sy < 0.15f) hline(0, W - 1, 31);  // garis terang
  } else {
    int half = lroundf((1.0f - (a - 0.6f) / 0.4f) * 64);
    if (half > 0) hline(64 - half, 63 + half, 31);
    disc(64, 31, 1);  // titik terang terakhir
  }
}

// ---------- Tema CLEAN: polos, mudah dibaca sekilas saat menyetir ----------
const uint32_t BOOT_CLEAN_TOTAL = 2200;

void drawBootClean(uint32_t t) {
  u8g2.setFont(u8g2_font_5x7_tr);
  const char* brand = "S U Z U K I";
  u8g2.drawStr((W - u8g2.getStrWidth(brand)) / 2, 14, brand);
  ditherBox(0, 6, W - 1, 15, easeInOutCubic((t - min(t, (uint32_t)400)) / 1000.0f));

  // Logo JIMNY muncul pelan
  u8g2.setFont(u8g2_font_helvB18_tr);
  const char* logo = "JIMNY";
  int lw = u8g2.getStrWidth(logo);
  int lx = (W - lw) / 2;
  u8g2.drawStr(lx, 40, logo);
  ditherBox(lx - 1, 18, lx + lw + 1, 41, easeInOutCubic(t / 1200.0f));

  // Garis tipis melebar dari tengah
  int half = lroundf(easeOutCubic((t - min(t, (uint32_t)500)) / 900.0f) * 40);
  if (half > 0) hline(64 - half, 64 + half, 47);
}

void drawMenuClean(uint32_t now) {
  char hm[6];
  clockHHMM(hm);
  drawHeader("MENU", hm, now);

  // Bingkai pilihan meluncur halus; di ujung daftar ikut berputar ke atas
  const int TOP = 13, ROW = 10;
  float p = fmodf(menuPos, (float)MENU_COUNT);
  if (p < 0) p += MENU_COUNT;
  int fy = TOP + lroundf(p * ROW);
  u8g2.setClipWindow(0, TOP, W, H);
  for (int k = 0; k < 2; k++) {
    int y = fy - k * MENU_COUNT * ROW;
    frame(1, y, W - 2, ROW);
    u8g2.setDrawColor(0);  // sudut membulat
    px(1, y);
    px(W - 2, y);
    px(1, y + ROW - 1);
    px(W - 2, y + ROW - 1);
    u8g2.setDrawColor(1);
  }
  u8g2.setMaxClipWindow();

  for (int i = 0; i < MENU_COUNT; i++) {
    int y = TOP + i * ROW;
    bool on = i == menuSel;
    MENU[i].icon(10, y + 5, 4.0f, iconPhase[i], on ? 1.0f : 0.0f);
    u8g2.setFont(on ? u8g2_font_helvB08_tr : u8g2_font_helvR08_tr);
    u8g2.drawStr(20, y + 8, tr(MENU[i].text));
    if (on) u8g2.drawStr(W - 9, y + 8, ">");
  }
}

void drawClockClean(uint32_t now) {
  char hm[6];
  clockHHMM(hm);
  u8g2.setFont(u8g2_font_logisoso42_tn);
  u8g2.drawStr((W - u8g2.getStrWidth(hm)) / 2, 53, hm);
}

// Perpindahan: layar lama keluar ke kiri, layar baru masuk dari kanan
void drawSlideTransition(uint32_t tt, uint32_t half) {
  if (tt < half) shiftAll(-lroundf(easeInOutCubic(tt / (float)half) * W));
  else shiftAll(lroundf((1.0f - easeOutCubic((tt - half) / (float)half)) * W));
}

// ---------- Tema 80-AN: matahari bergaris, krom, 7-segmen, jarum analog ----------
const uint32_t BOOT80_CRT = 600, BOOT80_SUN = 1000, BOOT80_LOGO = 900, BOOT80_HOLD = 1500;
const uint32_t BOOT80_TOTAL = BOOT80_CRT + BOOT80_SUN + BOOT80_LOGO + BOOT80_HOLD;

// Matahari terbenam: setengah lingkaran atas, bagian bawahnya dipotong garis-garis hitam
// yang makin tebal mendekati cakrawala
void drawStripedSun(int cx, int cy, int r, int horizon) {
  for (int dy = -r; dy <= 0; dy++) {
    int y = cy + dy;
    if (y > horizon || y < 0) continue;
    int d = -dy;  // jarak di atas pusat
    if (d <= r * 0.55f && (d + 2) % 4 < (d < r / 4 ? 2 : 1)) continue;  // garis hitam
    int dx = (int)(sqrtf((float)(r * r - dy * dy)) + 0.5f);
    hline(cx - dx, cx + dx, y);
  }
}

// Lantai grid yang bergerak mendekat
void drawSynthGrid(int cx, int horizon, int bottom, int spread, uint32_t t) {
  hline(0, W - 1, horizon);
  for (int k = -8; k <= 8; k++) line(cx + k * spread / 6, horizon, cx + k * spread, bottom + 20);
  float ph = fmodf(t / 700.0f, 1.0f);
  for (int i = 0; i < 5; i++) {
    float z = (i + ph) / 5.0f;
    int y = horizon + lroundf((bottom - horizon) * z * z);
    if (y > horizon && y <= bottom) hline(0, W - 1, y);
  }
}

// Tulisan krom: tebal miring, garis hitam di tengah, garis-garis di bagian bawah
void drawChrome(int x, int base, const char* s) {
  u8g2.setFont(u8g2_font_helvB18_tr);
  int w = u8g2.getStrWidth(s) + 8;
  drawText(x, base, s, true, W, 0, H);
  u8g2.setDrawColor(0);
  int x1 = min(W - 1, x + w);
  hline(x, x1, base - 8);
  for (int y = base - 5; y <= base; y += 2) hline(x, x1, y);
  u8g2.setDrawColor(1);
}

void drawBoot80(uint32_t t) {
  const int HZ = 44;
  float rise = easeOutCubic((t - min(t, BOOT80_CRT)) / (float)BOOT80_SUN);
  drawStripedSun(64, HZ + lroundf((1.0f - rise) * 18), 16, HZ);
  drawSynthGrid(64, HZ, H - 1, 26, t);

  uint32_t tl = BOOT80_CRT + BOOT80_SUN - 200;
  if (t > tl) {
    // SUZUKI krom meluncur dari kiri, lalu ada kilau lewat
    u8g2.setFont(u8g2_font_helvB18_tr);
    int w = u8g2.getStrWidth("SUZUKI") + 6;
    float lp = easeOutBack((t - tl) / (float)BOOT80_LOGO);
    int x = (W - w) / 2 - lroundf((1.0f - lp) * 150);
    drawChrome(x, 25, "SUZUKI");
    uint32_t gt = t - tl;
    if (gt > 1000 && gt < 1500) {
      int gx = x + (gt - 1000) * (w + 24) / 500;
      u8g2.setDrawColor(0);
      for (int k = 0; k < 3; k++) line(gx + k, 5, gx + k - 10, 26);
      u8g2.setDrawColor(1);
    }
    // JIMNY diketik di depan matahari
    const char* sub = "J I M N Y";
    int n = constrain((int)((gt - min(gt, (uint32_t)900)) / 90), 0, (int)strlen(sub));
    char buf[12];
    memcpy(buf, sub, n);
    buf[n] = 0;
    u8g2.setFont(u8g2_font_5x7_tr);
    u8g2.setDrawColor(2);
    u8g2.drawStr((W - u8g2.getStrWidth(sub)) / 2, 40, buf);
    u8g2.setDrawColor(1);
  }
  if (t < BOOT80_CRT) crtEffect(1.0f - easeOutCubic(t / (float)BOOT80_CRT));  // TV dinyalakan
}

void drawArrow80(int x, int cy, int dir, float bump) {
  int ox = x + lroundf(dir * bump * 3);
  float xs[3] = { (float)ox, (float)ox, (float)(ox + dir * 9) };
  float ys[3] = { (float)(cy - 9), (float)(cy + 9), (float)cy };
  fillPoly(xs, ys, 3);
}

void drawMenu80(uint32_t now) {
  char idx[8];
  snprintf(idx, sizeof(idx), "%d/%d", menuSel + 1, MENU_COUNT);
  drawHeader("MENU", idx, now);

  // Kotak bergaris ganda; isinya bergeser saat knob diputar
  const int X0 = 17, Y0 = 16, X1 = 110, Y1 = 63;
  frame(X0, Y0, X1 - X0 + 1, Y1 - Y0 + 1);
  frame(X0 + 2, Y0 + 2, X1 - X0 - 3, Y1 - Y0 - 3);
  u8g2.setClipWindow(X0 + 4, Y0 + 4, X1 - 3, Y1 - 3);
  int base = (int)floorf(menuPos);
  for (int slot = base; slot <= base + 1; slot++) {
    int cx = 64 + lroundf((slot - menuPos) * 90);
    int i = wrap(slot, MENU_COUNT);
    MENU[i].icon(cx, 33, 9.0f, iconPhase[i], 1.0f);
    u8g2.setFont(FONT_80S);
    const char* label = tr(MENU[i].text);
    u8g2.drawStr(cx - u8g2.getStrWidth(label) / 2, 57, label);
  }
  u8g2.setMaxClipWindow();
  drawArrow80(12, 40, -1, arrowBumpL);
  drawArrow80(115, 40, 1, arrowBumpR);
}

// Angka 7-segmen buatan sendiri. Segmen yang mati tetap terlihat samar, seperti jam lama.
//  aaa
// f   b
//  ggg
// e   c
//  ddd
const uint8_t SEG7[10] = { 0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07, 0x7F, 0x6F };  // bit0 = a ... bit6 = g

void seg7Poly(int x, int y, int w, int h, float t, int s, float* xs, float* ys) {
  float ht = t / 2, mid = y + h / 2.0f;
  if (s == 0 || s == 3 || s == 6) {  // mendatar: a, d, g
    float yy = s == 0 ? y : (s == 3 ? y + h - t : mid - ht);
    float px6[6] = { x + 1.0f, x + 1 + ht, x + w - 1 - ht, x + w - 1.0f, x + w - 1 - ht, x + 1 + ht };
    float py6[6] = { yy + ht, yy, yy, yy + ht, yy + t, yy + t };
    memcpy(xs, px6, sizeof(px6));
    memcpy(ys, py6, sizeof(py6));
  } else {  // tegak: b, c, e, f
    bool right = s == 1 || s == 2, upper = s == 1 || s == 5;
    float xx = right ? x + w - t : x;
    float y0 = upper ? y : mid, y1 = upper ? mid : y + h;
    float px6[6] = { xx + ht, xx + t, xx + t, xx + ht, xx, xx };
    float py6[6] = { y0 + 1, y0 + 1 + ht, y1 - 1 - ht, y1 - 1, y1 - 1 - ht, y0 + 1 + ht };
    memcpy(xs, px6, sizeof(px6));
    memcpy(ys, py6, sizeof(py6));
  }
}

// ghost = tampilkan segmen mati samar (bagus di angka besar, membingungkan di angka kecil)
void drawSeg7(int x, int y, int w, int h, float t, int digit, bool ghost) {
  float xs[6], ys[6];
  for (int s = 0; s < 7 && ghost; s++) {  // segmen mati: titik-titik jarang
    if (SEG7[digit] & (1 << s)) continue;
    seg7Poly(x, y, w, h, t, s, xs, ys);
    fillPoly(xs, ys, 6);
    float bx0 = xs[0], bx1 = xs[0], by0 = ys[0], by1 = ys[0];
    for (int i = 1; i < 6; i++) {
      bx0 = min(bx0, xs[i]);
      bx1 = max(bx1, xs[i]);
      by0 = min(by0, ys[i]);
      by1 = max(by1, ys[i]);
    }
    u8g2.setDrawColor(0);
    for (int py = (int)by0; py <= (int)by1; py++) {
      for (int pxx = (int)bx0; pxx <= (int)bx1; pxx++) {
        if (pxx % 2 || py % 3) px(pxx, py);
      }
    }
    u8g2.setDrawColor(1);
  }
  for (int s = 0; s < 7; s++) {
    if (!(SEG7[digit] & (1 << s))) continue;
    seg7Poly(x, y, w, h, t, s, xs, ys);
    fillPoly(xs, ys, 6);
  }
}

void drawClock80(uint32_t now) {
  drawHeader(tr(T_TIME), "JIMNY", now);
  char d[4];
  clockDigitsNow(d);
  const int DW = 22, DH = 38, Y = 16;
  const int xs[4] = { 8, 34, 72, 98 };
  for (int i = 0; i < 4; i++) drawSeg7(xs[i], Y, DW, DH, 5, d[i] - '0', true);
  if (millis() % 1000 < 500) {  // titik dua berkedip
    box(62, Y + 9, 4, 4);
    box(62, Y + 25, 4, 4);
  }
  char sec[12];
  snprintf(sec, sizeof(sec), "%s %02d", tr(T_SECONDS), (int)(clockSeconds() % 60));
  u8g2.setFont(u8g2_font_5x7_tr);
  u8g2.drawStr((W - u8g2.getStrWidth(sec)) / 2, 63, sec);
}

// ---------- Tema 3D: bantuan gambar dan jam ----------
const uint32_t BOOT3D_TOTAL = 4300;
const uint32_t FLIP_MS = 500;  // lama satu angka jam berputar balik

bool pxAt(const uint8_t* b, int x, int y) {
  if (x < 0 || x >= W || y < 0 || y >= H) return false;
  return b[x + (y / 8) * W] & (1 << (y % 8));
}

void snapScreen(uint8_t* dst) {
  memcpy(dst, u8g2.getBufferPtr(), W * H / 8);
}

// Semua yang digambar sejak snapshot 'before' dijadikan garis tepi saja (bagian dalamnya dikosongkan),
// supaya mirip gambar vektor
void outlineNew(const uint8_t* before, int x0, int x1) {
  static uint8_t shape[W * H / 8];
  uint8_t* buf = u8g2.getBufferPtr();
  for (int i = 0; i < W * H / 8; i++) shape[i] = buf[i] & ~before[i];
  u8g2.setDrawColor(0);
  for (int y = 0; y < H; y++) {
    for (int x = max(0, x0); x <= min(W - 1, x1); x++) {
      if (pxAt(shape, x, y) && pxAt(shape, x - 1, y) && pxAt(shape, x + 1, y) && pxAt(shape, x, y - 1) && pxAt(shape, x, y + 1)) px(x, y);
    }
  }
  u8g2.setDrawColor(1);
}

// Garis putus-putus kotak catur
void dotLine(int x0, int y0, int x1, int y1) {
  int n = max(abs(x1 - x0), abs(y1 - y0));
  for (int i = 0; i <= n; i++) {
    int x = n ? x0 + (x1 - x0) * i / n : x0, y = n ? y0 + (y1 - y0) * i / n : y0;
    if (((x + y) & 1) == 0) px(x, y);
  }
}

// Catat angka; kalau berubah, mulai animasi berputar balik
void flipSet(Flip& f, const char* v, uint32_t now) {
  if (!f.shown[0]) {
    strncpy(f.shown, v, 3);
    strcpy(f.prev, f.shown);
    f.at = 0;
    return;
  }
  if (strcmp(f.shown, v) == 0) return;
  strcpy(f.prev, f.shown);
  strncpy(f.shown, v, 3);
  f.at = now;
}

float flipProgress(const Flip& f, uint32_t now, uint32_t ms) {
  return f.at ? clamp01((now - f.at) / (float)ms) : 1.0f;
}

// Angka 7-segmen dari balok 3D. Titik tengahnya di (0,0,0) supaya bisa diputar di tempat.
// Muka depan diisi penuh (mudah dibaca), sisi tebalnya garis yang menjauh ke belakang.
const float SEG_BOX[7][4] = {  // x0, y0, x1, y1 tiap segmen a..g, angka 0.8 x 1.4
  { 0.1f, 1.23f, 0.7f, 1.4f }, { 0.63f, 0.75f, 0.8f, 1.35f }, { 0.63f, 0.05f, 0.8f, 0.65f }, { 0.1f, 0, 0.7f, 0.17f },
  { 0, 0.05f, 0.17f, 0.65f },  { 0, 0.75f, 0.17f, 1.35f },    { 0.1f, 0.615f, 0.7f, 0.785f },
};

void drawDigit3D(int digit, const Pose3& p) {
  const float D = 0.15f;  // setengah tebal
  for (int pass = 0; pass < 2; pass++) {  // 0 = sisi tebal semua segmen dulu, 1 = muka depan di atasnya
    for (int s = 0; s < 7; s++) {
      if (!(SEG7[digit] & (1 << s))) continue;
      float x0 = SEG_BOX[s][0] - 0.4f, y0 = SEG_BOX[s][1] - 0.7f, x1 = SEG_BOX[s][2] - 0.4f, y1 = SEG_BOX[s][3] - 0.7f;
      float fx[4], fy[4], bx[4], by[4];
      bool ok = true;
      for (int k = 0; k < 4; k++) {
        float x = (k == 1 || k == 2) ? x1 : x0, y = k < 2 ? y1 : y0;
        ok &= project3D(V3{ x, y, -D }, p, fx[k], fy[k]);
        ok &= project3D(V3{ x, y, D }, p, bx[k], by[k]);
      }
      if (!ok) continue;
      if (pass == 0) {
        for (int k = 0; k < 4; k++) {
          line(lroundf(bx[k]), lroundf(by[k]), lroundf(bx[(k + 1) % 4]), lroundf(by[(k + 1) % 4]));
          line(lroundf(fx[k]), lroundf(fy[k]), lroundf(bx[k]), lroundf(by[k]));
        }
      } else {
        u8g2.setDrawColor(0);  // garis hitam tipis di sekeliling muka depan, biar terpisah dari sisi tebal
        for (int k = 0; k < 4; k++) line(lroundf(fx[k]) + 1, lroundf(fy[k]) + 1, lroundf(fx[(k + 1) % 4]) + 1, lroundf(fy[(k + 1) % 4]) + 1);
        u8g2.setDrawColor(1);
        fillPoly(fx, fy, 4);
        for (int k = 0; k < 4; k++) line(lroundf(fx[k]), lroundf(fy[k]), lroundf(fx[(k + 1) % 4]), lroundf(fy[(k + 1) % 4]));
      }
    }
  }
}

// Jam 3D: angka balok 3D melayang di antara bintang dan bergoyang pelan. Saat angka berganti,
// angka itu berputar ke belakang seperti papan flip, lalu angka baru berputar masuk.
// Titik dua = dua kubus kecil berputar. Detik di bawah, di dalam bidikan.
void drawClock3D(uint32_t now) {
  static Mesh cube;
  meshesInit();
  if (!cube.nv) meshBox(cube, -0.1f, -0.1f, -0.1f, 0.1f, 0.1f, 0.1f);
  starsUpdate(now, 0.8f);
  starsDraw(0);

  char d[4];
  clockDigitsNow(d);
  const float POS[4] = { -1.95f, -0.9f, 0.9f, 1.95f };
  const float Z = 3.1f, TY = 0.15f, F = 66;
  float yaw = sinf(now / 1500.0f) * 0.12f;  // seluruh jam bergoyang pelan
  for (int i = 0; i < 4; i++) {
    char v[2] = { d[i], 0 };
    flipSet(flipD[i], v, now);
    float p = flipProgress(flipD[i], now, FLIP_MS);
    int digit = flipD[i].shown[0] - '0';
    float flip = 0;
    if (p < 0.5f) {  // angka lama berputar menjauh (atas ke belakang)
      digit = flipD[i].prev[0] - '0';
      flip = easeInOutCubic(p * 2) * HALF_PI;
    } else if (p < 1) {  // angka baru berputar masuk dari bawah
      flip = -(1 - easeOutCubic((p - 0.5f) * 2)) * HALF_PI;
    }
    Pose3 pose = { yaw, flip, 0, POS[i] * cosf(yaw), TY, Z - POS[i] * sinf(yaw), F, 64, 30 };
    drawDigit3D(digit, pose);
  }
  float spin = now / 500.0f;
  drawMesh(cube, spin, 0.6f, 0, 0, TY + 0.3f, Z, F, 64, 30);
  drawMesh(cube, -spin, 0.6f, 0, 0, TY - 0.3f, Z, F, 64, 30);

  char sec[4];
  snprintf(sec, sizeof(sec), "%02d", (int)(clockSeconds() % 60));
  u8g2.setFont(u8g2_font_5x7_tr);
  u8g2.setDrawColor(0);
  box(52, 53, 24, 11);
  u8g2.setDrawColor(1);
  u8g2.drawStr(64 - u8g2.getStrWidth(sec) / 2, 62, sec);
  drawCorners(53, 54, 74, 63, 3);
}

// ---------- Tema 3D: gaya game arcade vektor 80-an (model kawat 3D) ----------
// Gambar 3D sederhana: titik-titik model diputar, diberi perspektif, lalu disambung garis.
// Kamera melihat ke arah +z. Model: x kanan, y atas, z menjauh dari kamera.

int meshV(Mesh& m, float x, float y, float z) {
  for (int i = 0; i < m.nv; i++) {  // titik yang sama dipakai bersama
    if (fabsf(m.v[i].x - x) < 0.001f && fabsf(m.v[i].y - y) < 0.001f && fabsf(m.v[i].z - z) < 0.001f) return i;
  }
  if (m.nv >= MESH_MAX_V) return MESH_MAX_V - 1;
  m.v[m.nv] = V3{ x, y, z };
  return m.nv++;
}

void meshE(Mesh& m, int a, int b) {
  if (m.ne >= MESH_MAX_E) return;
  m.e[m.ne][0] = a;
  m.e[m.ne][1] = b;
  m.ne++;
}

// Lingkaran n titik. axis: 0 = menghadap kamera (bidang XY), 1 = mendatar (XZ), 2 = samping (YZ)
int meshRing(Mesh& m, int n, float r, int axis, float off) {
  int first = m.nv;
  for (int i = 0; i < n; i++) {
    float a = i * TWO_PI / n, c = cosf(a) * r, s = sinf(a) * r;
    if (axis == 0) m.v[m.nv] = V3{ c, s, off };
    else if (axis == 1) m.v[m.nv] = V3{ c, off, s };
    else m.v[m.nv] = V3{ off, c, s };
    m.nv++;
  }
  for (int i = 0; i < n; i++) meshE(m, first + i, first + (i + 1) % n);
  return first;
}

void meshBox(Mesh& m, float x0, float y0, float z0, float x1, float y1, float z1) {
  int c[8];
  for (int i = 0; i < 8; i++) c[i] = meshV(m, i & 1 ? x1 : x0, i & 2 ? y1 : y0, i & 4 ? z1 : z0);
  const uint8_t E[12][2] = { { 0, 1 }, { 2, 3 }, { 4, 5 }, { 6, 7 }, { 0, 2 }, { 1, 3 }, { 4, 6 }, { 5, 7 }, { 0, 4 }, { 1, 5 }, { 2, 6 }, { 3, 7 } };
  for (int i = 0; i < 12; i++) meshE(m, c[E[i][0]], c[E[i][1]]);
}

void meshLine(Mesh& m, float x0, float y0, float z0, float x1, float y1, float z1) {
  meshE(m, meshV(m, x0, y0, z0), meshV(m, x1, y1, z1));
}

// Pesawat tempur sayap X (buatan sendiri): badan panjang, 4 sayap, meriam di tiap ujung sayap
void buildFighter(Mesh& m) {
  const float SX[4] = { 1, -1, -1, 1 }, SY[4] = { 1, 1, -1, -1 };
  int nose = meshV(m, 0, 0, 2.2f), f[4], r[4];
  for (int k = 0; k < 4; k++) f[k] = meshV(m, 0.18f * SX[k], 0.15f * SY[k], 0.8f);
  for (int k = 0; k < 4; k++) r[k] = meshV(m, 0.25f * SX[k], 0.22f * SY[k], -1.2f);
  for (int k = 0; k < 4; k++) {
    meshE(m, nose, f[k]);
    meshE(m, f[k], f[(k + 1) % 4]);
    meshE(m, r[k], r[(k + 1) % 4]);
    meshE(m, f[k], r[k]);
    int a = meshV(m, 0.2f * SX[k], 0.16f * SY[k], -0.3f), b = meshV(m, 0.25f * SX[k], 0.2f * SY[k], -1.1f);
    int c = meshV(m, 1.6f * SX[k], 0.75f * SY[k], -1.1f), d = meshV(m, 1.6f * SX[k], 0.75f * SY[k], -0.5f);
    int g = meshV(m, 1.6f * SX[k], 0.75f * SY[k], 1.3f);
    meshE(m, a, b);
    meshE(m, b, c);
    meshE(m, c, d);
    meshE(m, d, a);
    meshE(m, d, g);
  }
  int cp = meshV(m, 0, 0.32f, 0.2f);  // kaca kokpit
  meshE(m, cp, f[0]);
  meshE(m, cp, f[1]);
  meshE(m, cp, r[0]);
  meshE(m, cp, r[1]);
}

// Tulisan JIMNY dari garis, punya tebal (depan z=0, belakang z=0.3)
void buildTitle(Mesh& m) {
  // Tiap huruf: garis (x0, y0, x1, y1) di kotak 0.8 x 1.2. Huruf diakhiri 99.
  const float G[] = {
    0.1f, 1.2f, 0.8f, 1.2f, 0.6f, 1.2f, 0.6f, 0.25f, 0.6f, 0.25f, 0.4f, 0, 0.4f, 0, 0.15f, 0, 0.15f, 0, 0, 0.25f, 99,  // J
    0.1f, 1.2f, 0.7f, 1.2f, 0.4f, 1.2f, 0.4f, 0, 0.1f, 0, 0.7f, 0, 99,                                                  // I
    0, 0, 0, 1.2f, 0, 1.2f, 0.4f, 0.55f, 0.4f, 0.55f, 0.8f, 1.2f, 0.8f, 1.2f, 0.8f, 0, 99,                              // M
    0, 0, 0, 1.2f, 0, 1.2f, 0.8f, 0, 0.8f, 0, 0.8f, 1.2f, 99,                                                           // N
    0, 1.2f, 0.4f, 0.6f, 0.8f, 1.2f, 0.4f, 0.6f, 0.4f, 0.6f, 0.4f, 0, 99,                                               // Y
  };
  int letter = 0, n = sizeof(G) / sizeof(G[0]);
  for (int i = 0; i < n;) {
    if (G[i] == 99) {
      letter++;
      i++;
      continue;
    }
    float ox = letter * 1.1f - 2.6f, oy = -0.6f;
    float x0 = G[i] + ox, y0 = G[i + 1] + oy, x1 = G[i + 2] + ox, y1 = G[i + 3] + oy;
    meshLine(m, x0, y0, 0, x1, y1, 0);
    meshLine(m, x0, y0, 0.3f, x1, y1, 0.3f);
    meshLine(m, x0, y0, 0, x0, y0, 0.3f);
    meshLine(m, x1, y1, 0, x1, y1, 0.3f);
    i += 4;
  }
}

// Model 3D untuk tiap menu
void buildMenuMesh(Mesh& m, TextId item) {
  if (item == T_CLOCK) {  // jam meja: cincin depan-belakang + jarum (jarum diisi waktu sekarang saat digambar)
    int a = meshRing(m, 12, 1.0f, 0, -0.2f), b = meshRing(m, 12, 1.0f, 0, 0.2f);
    for (int i = 0; i < 12; i += 3) meshE(m, a + i, b + i);
    for (int i = 0; i < 12; i += 3) meshLine(m, cosf(i * TWO_PI / 12) * 0.8f, sinf(i * TWO_PI / 12) * 0.8f, -0.2f, cosf(i * TWO_PI / 12), sinf(i * TWO_PI / 12), -0.2f);
    clockHandIdx = m.nv;
    meshE(m, meshV(m, 0, 0, -0.21f), meshV(m, 0, 0.5f, -0.22f));  // jarum jam
    meshE(m, clockHandIdx, meshV(m, 0.7f, 0, -0.23f));          // jarum menit
  } else if (item == T_TILT) {  // bola giroskop: tiga cincin
    meshRing(m, 12, 1.0f, 0, 0);
    meshRing(m, 12, 1.0f, 1, 0);
    meshRing(m, 12, 1.0f, 2, 0);
    meshLine(m, -1.3f, 0, 0, 1.3f, 0, 0);  // poros
  } else if (item == T_GMETER) {  // piringan G dengan dua cincin, silang, dan penanda
    int a = meshRing(m, 12, 1.0f, 1, 0);
    meshRing(m, 12, 0.5f, 1, 0);
    meshE(m, a, a + 6);
    meshE(m, a + 3, a + 9);
    int t = meshV(m, 0.35f, 0.4f, 0.25f);
    int c[4];
    for (int k = 0; k < 4; k++) c[k] = meshV(m, 0.35f + (k & 1 ? 0.12f : -0.12f), 0, 0.25f + (k & 2 ? 0.12f : -0.12f));
    const int ORD[4] = { 0, 1, 3, 2 };
    for (int k = 0; k < 4; k++) {
      meshE(m, t, c[k]);
      meshE(m, c[ORD[k]], c[ORD[(k + 1) % 4]]);
    }
  } else if (item == T_BATTERY) {  // aki dengan dua kutub, tanda + dan -
    meshBox(m, -0.8f, -0.5f, -0.4f, 0.8f, 0.4f, 0.4f);
    meshBox(m, -0.55f, 0.4f, -0.1f, -0.3f, 0.6f, 0.1f);
    meshBox(m, 0.3f, 0.4f, -0.1f, 0.55f, 0.6f, 0.1f);
    meshLine(m, -0.55f, 0, -0.41f, -0.25f, 0, -0.41f);
    meshLine(m, -0.4f, -0.15f, -0.41f, -0.4f, 0.15f, -0.41f);
    meshLine(m, 0.25f, 0, -0.41f, 0.55f, 0, -0.41f);
  } else {  // roda gigi 6 gigi, tebal, dengan lubang tengah
    for (int side = 0; side < 2; side++) {
      float z = side ? 0.25f : -0.25f;
      int first = m.nv;
      for (int j = 0; j < 6; j++) {
        float a = j * TWO_PI / 6, d = 0.35f, d2 = 0.17f;
        m.v[m.nv++] = V3{ cosf(a - d) * 0.72f, sinf(a - d) * 0.72f, z };
        m.v[m.nv++] = V3{ cosf(a - d2) * 1.0f, sinf(a - d2) * 1.0f, z };
        m.v[m.nv++] = V3{ cosf(a + d2) * 1.0f, sinf(a + d2) * 1.0f, z };
        m.v[m.nv++] = V3{ cosf(a + d) * 0.72f, sinf(a + d) * 0.72f, z };
      }
      for (int i = 0; i < 24; i++) meshE(m, first + i, first + (i + 1) % 24);
      if (side) {
        for (int i = 1; i < 24; i += 4) meshE(m, first + i, first - 24 + i);
        for (int i = 2; i < 24; i += 4) meshE(m, first + i, first - 24 + i);
      }
    }
    int h0 = meshRing(m, 6, 0.3f, 0, -0.25f), h1 = meshRing(m, 6, 0.3f, 0, 0.25f);
    for (int i = 0; i < 6; i += 2) meshE(m, h0 + i, h1 + i);
  }
}

void meshesInit() {
  if (meshesReady) return;
  meshesReady = true;
  buildFighter(meshFighter);
  buildTitle(meshTitle);
  for (int i = 0; i < MENU_COUNT && i < 5; i++) buildMenuMesh(meshItem[i], MENU[i].text);
}

// Putar satu titik (guling, angguk, lalu belok), geser, lalu beri perspektif ke layar.
// Hasil false = titik di belakang kamera atau terlalu jauh di luar layar.
bool project3D(const V3& v, const Pose3& p, float& X, float& Y) {
  float cyw = cosf(p.yaw), syw = sinf(p.yaw), cp = cosf(p.pitch), sp = sinf(p.pitch), cr = cosf(p.roll), sr = sinf(p.roll);
  float x1 = v.x * cr - v.y * sr, y1 = v.x * sr + v.y * cr, z1 = v.z;  // guling
  float y2 = y1 * cp - z1 * sp, z2 = y1 * sp + z1 * cp;                // angguk
  float x3 = x1 * cyw + z2 * syw, z3 = -x1 * syw + z2 * cyw;           // belok
  float Z = z3 + p.tz;
  if (Z <= 0.3f) return false;
  X = p.cx + p.f * (x3 + p.tx) / Z;
  Y = p.cy - p.f * (y2 + p.ty) / Z;
  return fabsf(X) < 1000 && fabsf(Y) < 1000;
}

// Gambar model kawat 3D. Sudut dalam radian, posisi dalam satuan model, f = fokus kamera (piksel).
void drawMesh(const Mesh& m, float yaw, float pitch, float roll, float tx, float ty, float tz, float f, int cx, int cy) {
  int16_t sx[MESH_MAX_V], sy[MESH_MAX_V];
  bool ok[MESH_MAX_V];
  Pose3 p = { yaw, pitch, roll, tx, ty, tz, f, (float)cx, (float)cy };
  for (int i = 0; i < m.nv; i++) {
    float X, Y;
    ok[i] = project3D(m.v[i], p, X, Y);
    sx[i] = (int16_t)lroundf(X);
    sy[i] = (int16_t)lroundf(Y);
  }
  for (int i = 0; i < m.ne; i++) {
    int a = m.e[i][0], b = m.e[i][1];
    if (ok[a] && ok[b]) line(sx[a], sy[a], sx[b], sy[b]);
  }
}

// Bintang 3D yang terbang mendekat. streak > 0: digambar sebagai garis (makin panjang makin cepat)
void starsUpdate(uint32_t now, float speed) {
  float dt = starLast ? min(0.1f, (now - starLast) / 1000.0f) : 0;
  starLast = now;
  for (int i = 0; i < STARS; i++) {
    if (!starsReady || starZ[i] < 0.5f) {
      starX[i] = random(-300, 300) / 100.0f;
      starY[i] = random(-160, 160) / 100.0f;
      starZ[i] = starsReady ? starZ[i] + 10 : 0.5f + random(0, 1000) / 100.0f;
    }
    starZ[i] -= speed * dt;
  }
  starsReady = true;
}

void starsDraw(float streak) {
  for (int i = 0; i < STARS; i++) {
    if (starZ[i] < 0.5f) continue;
    int x = 64 + lroundf(60 * starX[i] / starZ[i]), y = 32 - lroundf(60 * starY[i] / starZ[i]);
    if (streak > 0) {
      float z2 = starZ[i] + streak;
      line(x, y, 64 + lroundf(60 * starX[i] / z2), 32 - lroundf(60 * starY[i] / z2));
    } else {
      px(x, y);
      if (starZ[i] < 2) px(x + 1, y);  // bintang dekat lebih besar
    }
  }
}

// Boot: keluar dari hyperspace, pesawat datang sambil berguling lalu lewat di atas kamera,
// kemudian tulisan JIMNY 3D datang dari jauh dan berdiri menghadap ke depan.
void drawBoot3D(uint32_t t) {
  meshesInit();
  float warp = 1 - clamp01(t / 900.0f);
  starsUpdate(millis(), 2 + warp * 40);
  starsDraw(warp * 1.5f);

  if (t > 300 && t < 2700) {
    float p = (t - 300) / 2400.0f;
    float e = easeOutCubic(min(1.0f, p / 0.7f));
    float z = 40 - 36 * e, x = 0, y = -0.3f, pitch = 0;
    if (p > 0.7f) {  // tarik ke atas lalu lewat di atas kamera
      float q = (p - 0.7f) / 0.3f;
      z = 4 - 3.6f * q;
      y = -0.3f + 2.2f * q * q;
      x = 1.2f * q;
      pitch = 0.7f * q;
    }
    drawMesh(meshFighter, PI, pitch, e * TWO_PI, x, y, z, 60, 64, 32);  // hidung menghadap kamera, berguling 1x
  }

  if (t > 2300) {
    float q = easeOutCubic((t - 2300) / 1200.0f);
    drawMesh(meshTitle, 0, -1.2f * (1 - q), 0, 0, 0.1f, 30 - 26.8f * q, 60, 64, 30);
  }
  if (t > 3300) {
    u8g2.setFont(u8g2_font_6x10_tr);
    const char* brand = "S U Z U K I";
    u8g2.drawStr((W - u8g2.getStrWidth(brand)) / 2, 61, brand);
    ditherBox(0, 50, W - 1, H - 1, (t - 3300) / 500.0f);
  }
}

// Menu: model 3D menu berputar di tengah bidikan. Putar knob = model lama terbang menjauh ke samping,
// model baru masuk dari sisi lain. Bintang di belakang makin cepat saat berpindah.
// Sesekali pesawat lewat di kejauhan.
void drawMenu3D(uint32_t now) {
  meshesInit();
  float moving = fabsf(menuTarget - menuPos);
  starsUpdate(now, 1.5f + moving * 20);
  starsDraw(moving > 0.05f ? min(1.0f, moving) * 0.8f : 0);

  uint32_t fp = now % 11000;  // pesawat lewat
  if (fp < 3000) {
    float t = fp / 3000.0f;
    drawMesh(meshFighter, 1.25f, 0.15f, -0.5f, -9 + 18 * t, 1.6f - 1.2f * t, 16, 60, 64, 32);
  }

  // Jarum jam ikut waktu sekarang
  for (int i = 0; i < MENU_COUNT && i < 5; i++) {
    if (MENU[i].text != T_CLOCK || clockHandIdx < 0) continue;
    uint32_t s = clockSeconds();
    float ah = HALF_PI - ((s / 3600) % 12 + (s / 60) % 60 / 60.0f) * TWO_PI / 12, am = HALF_PI - (s / 60) % 60 * TWO_PI / 60;
    meshItem[i].v[clockHandIdx + 1] = V3{ cosf(ah) * 0.5f, sinf(ah) * 0.5f, -0.22f };
    meshItem[i].v[clockHandIdx + 2] = V3{ cosf(am) * 0.75f, sinf(am) * 0.75f, -0.23f };
  }

  const int CX = 64, CY = 34;
  int base = (int)floorf(menuPos);
  for (int slot = base; slot <= base + 1; slot++) {
    float d = slot - menuPos;
    if (fabsf(d) >= 1) continue;
    int i = wrap(slot, MENU_COUNT);
    float spin = now / 1000.0f * 1.3f + i * 1.7f;
    float yaw = MENU[i].text == T_CLOCK ? sinf(spin) * 0.7f : spin;  // jam cuma goyang, biar jarumnya kelihatan
    float roll = MENU[i].text == T_TILT ? sinf(spin * 1.3f) * 0.4f : 0;
    float pitch = MENU[i].text == T_GMETER ? 0.55f : 0.3f;
    drawMesh(meshItem[i], yaw, pitch, roll - d * 0.9f, d * 4.5f, 0, 3.0f + fabsf(d) * 6, 60, CX, CY);
  }

  // Bidikan: menyempit dan berkedip "terkunci" saat berhenti
  int g = 22 + lroundf(min(1.0f, moving) * 14);
  drawCorners(CX - g - 4, CY - g + 2, CX + g + 4, CY + g - 2, 5);
  if (moving < 0.02f && (now / 400) % 2) {
    hline(CX - 3, CX - 1, CY);
    hline(CX + 1, CX + 3, CY);
  }

  u8g2.setDrawColor(0);  // bersihkan atas dan bawah untuk judul dan nama menu
  box(0, 0, W, 12);
  box(0, 54, W, 10);
  u8g2.setDrawColor(1);
  char hm[6];
  clockHHMM(hm);
  drawHeader("MENU", hm, now);
  u8g2.setFont(u8g2_font_helvB08_tr);
  const char* label = tr(MENU[menuSel].text);
  u8g2.drawStr((W - u8g2.getStrWidth(label)) / 2, 63, label);
  u8g2.setFont(u8g2_font_5x7_tr);
  u8g2.drawStr(2, 63, "<");
  u8g2.drawStr(W - 6, 63, ">");
}

// Perpindahan layar: lompat hyperspace. Layar lama membesar dan memudar dengan garis bintang,
// lalu layar baru muncul dari tengah.
void zoomScreen(float sc) {
  if (fabsf(sc - 1) < 0.001f) return;
  uint8_t* buf = u8g2.getBufferPtr();
  snapScreen(snapC);
  memset(buf, 0, W * H / 8);
  for (int y = 0; y < H; y++) {
    int sy = (int)lroundf(31.5f + (y - 31.5f) / sc);
    for (int x = 0; x < W; x++) {
      if (pxAt(snapC, (int)lroundf(63.5f + (x - 63.5f) / sc), sy)) buf[x + (y / 8) * W] |= 1 << (y % 8);
    }
  }
}

void warpLines(float a) {
  if (a <= 0.01f) return;
  for (int k = 0; k < 22; k++) {
    float ang = k * TWO_PI / 22 + (k % 3) * 0.09f;
    float r1 = 6 + a * 40 + (k % 4) * 5, r2 = r1 + 4 + a * 70;
    line(64 + lroundf(cosf(ang) * r1), 32 + lroundf(sinf(ang) * r1 * 0.6f), 64 + lroundf(cosf(ang) * r2),
         32 + lroundf(sinf(ang) * r2 * 0.6f));
  }
}

void drawHyperspace(uint32_t tt, uint32_t half) {
  if (tt < half) {
    float a = tt / (float)half;
    a = a * a;
    zoomScreen(1 + 2.5f * a);
    ditherBox(0, 0, W - 1, H - 1, 1 - a);
    warpLines(a);
  } else {
    float b = 1 - easeOutCubic((tt - half) / (float)half);
    zoomScreen(1 - 0.8f * b);
    warpLines(b);
  }
}


// ---------- KEMIRINGAN: siluet Jimny dari blueprint ----------
// Seperti inclinometer bawaan mobil: kiri tampak samping (PITCH, depan mobil di kiri),
// kanan tampak belakang (ROLL). Siluet diambil dari blueprint SJ410 hardtop (bodi sama dengan JA11)
// lalu diperkecil. Tiap baris: '#' = piksel nyala. Boleh diedit langsung untuk mengubah detail.
const char* const JIMNY_SIDE[] = {
  "..................................##################........",
  "........................##############################......",
  "........................#######......##################.....",
  "........................#............##............####.....",
  ".......................#.............##............####.....",
  "......................#..............##............####.....",
  "......................#..............##.............###.....",
  ".....................#.#.............##.............#######.",
  ".....................###.............##.............#######.",
  "....................#.##.............##.............#######.",
  "...................##..####################################.",
  "......###############..####################################.",
  "...########################################################.",
  "...########################################################.",
  "...#########################################################",
  "..##########################################################",
  "..##########################################################",
  "..##########################################################",
  "..#####.......############################......###########.",
  "######.######..##########################.######..#######...",
  "#####.###..###..########################.###..###..######...",
  ".###..##.###.##.#######################..##.###.##.#####....",
  "..##.##.#####.#..######################.##.#######.####.....",
  "..##.#####.##.##.######################.####..##.##.##......",
  ".....####..##.##.##.................###.####..##.##.........",
  ".....##.#####.##........................##.#####.##.........",
  ".....##.####.##.........................##.####.##..........",
  "......###...###..........................###...###..........",
  ".......#######............................#######...........",
  ".........###...............................####.............",
};
const char* const JIMNY_REAR[] = {
  "..................####..................",
  "........#########################.......",
  ".......##########################.......",
  "......############################......",
  "......####.....................###......",
  "......###......................###......",
  ".....####.......................###.....",
  ".....###........................###.....",
  ".....###........................###.....",
  ".###.###........................###.###.",
  "########........................########",
  "########............######......########",
  "########...........#########....########",
  ".########.........###....####..########.",
  "...#############.###.####.###..######...",
  "...############..##.######.###.######...",
  "....###########.###.#######.##.#####....",
  "....###########.######..###.##.######...",
  "......#########.######..###.##.###..#...",
  "...#..#########.###.##########.###..#...",
  "..##..##########.##.######.###.###..##..",
  "..##..##########.###..##..###.####..##..",
  "..##..###########.##########..####..##..",
  "..##..############.########..#####..##..",
  "..##..#############..####...######..##..",
  "........................................",
  "..####################################..",
  "..####################################..",
  "..######........................######..",
  "..######........................######..",
  "..######.........######.........######..",
  "..####################################..",
  "..####################################..",
  "..######.........######.........######..",
  "..######........................######..",
  "..######........................######..",
  "..######........................######..",
  "..######........................######..",
  "..######........................######..",
  "..######........................######..",
  "...#####........................#####...",
};

const int SIDE_W = 60, SIDE_H = 30, REAR_W = 40, REAR_H = 41;

// Gambar bitmap teks diputar 'ang' (radian, + = searah jarum jam di layar) di sekitar titik
// pivot bitmap (pivX, pivY). Titik pivot jatuh di layar (sx, sy).
void drawBitmapRot(const char* const* rows, int bw, int bh, float pivX, float pivY, int sx, int sy, float ang) {
  float c = cosf(ang), s = sinf(ang);
  int r = (int)ceilf(hypotf(max(pivX, bw - pivX), max(pivY, bh - pivY))) + 1;
  for (int y = -r; y <= r; y++) {
    for (int x = -r; x <= r; x++) {
      float u = x * c + y * s + pivX, v = -x * s + y * c + pivY;  // titik layar diputar balik ke bitmap
      int iu = (int)floorf(u), iv = (int)floorf(v);
      if (iu >= 0 && iu < bw && iv >= 0 && iv < bh && rows[iv][iu] == '#') px(sx + x, sy + y);
    }
  }
}

// Lereng tanah di bawah mobil (garis tebal + isian sesuai tema), hanya di kolom x0..x1
void drawSlope(int x0, int x1, int sx, int sy, float ang, uint32_t now) {
  float c = cosf(ang), s = sinf(ang);
  int t = now / 80;
  for (int y = 0; y < H; y++) {
    for (int x = x0; x <= x1; x++) {
      float dx = x - sx, dy = y - sy;
      float v = -dx * s + dy * c;  // seberapa jauh di bawah permukaan tanah
      if (v < 0) continue;
      float u = dx * c + dy * s;  // posisi sepanjang tanah
      bool on;
      if (v < 1.6f) on = true;                                               // permukaan tanah
      else if (theme == THEME_CLEAN) on = BAYER4[y & 3][x & 3] < 3;          // arsiran halus
      else if (theme == THEME_CYBER) on = (((int)floorf(u - v) + t) % 5) == 0;  // garis miring berjalan
      else if (theme == THEME_3D) on = ((int)floorf(u) + 64) % 6 == 0 && (int)v % 4 == 0;  // titik-titik grid
      else on = ((int)v % 4 == 0) || (((int)floorf(u) + 64) % 8 == 0);          // grid 80-an
      if (on) px(x, y);
    }
  }
  for (int x = x0 + 1; x <= x1; x += 3) px(x, sy);  // acuan datar
}

// Satu panel: lereng + siluet mobil yang diputar
void drawTiltCar(bool side, float deg, uint32_t now) {
  int x0 = side ? 0 : 65, x1 = side ? 63 : W - 1;
  int sx = side ? 32 : 96, sy = 44;  // dasar mobil di atas kotak angka
  // Diputar per derajat bulat (sama dengan angka di layar), jadi saat datar gambar benar-benar lurus
  float ang = roundf(constrain(deg, -45.0f, 45.0f)) * DEG_TO_RAD;
  u8g2.setClipWindow(x0, 0, x1 + 1, H);
  drawSlope(x0, x1, sx, sy, ang, now);
  if (theme == THEME_3D) snapScreen(snapC);
  // Mobil berdiri di atas lereng: titik bawah tengah bitmap = titik pivot
  if (side) drawBitmapRot(JIMNY_SIDE, SIDE_W, SIDE_H, SIDE_W / 2.0f, SIDE_H, sx, sy, ang);
  else drawBitmapRot(JIMNY_REAR, REAR_W, REAR_H, REAR_W / 2.0f, REAR_H, sx, sy, ang);
  if (theme == THEME_3D) outlineNew(snapC, x0, x1);  // mobil jadi garis tepi (gaya vektor)
  u8g2.setMaxClipWindow();
}

// Label kecil + MAX di kiri-bawah panel, dengan latar hitam
void drawTiltLabel(int x, const char* name, const char* maxText) {
  u8g2.setFont(u8g2_font_4x6_tr);
  int w = max(u8g2.getStrWidth(name), u8g2.getStrWidth(maxText));
  u8g2.setDrawColor(0);
  box(x, maxText[0] ? 50 : 57, w + 3, maxText[0] ? 14 : 7);
  u8g2.setDrawColor(1);
  if (maxText[0]) {
    u8g2.drawStr(x + 1, 56, name);
    u8g2.drawStr(x + 1, 63, maxText);
  } else {
    u8g2.drawStr(x + 1, 63, name);
  }
}

// Angka derajat besar. Lebih dari 25 derajat: berkedip terbalik warna.
void drawTiltNumber(int x, int base, bool alignRight, float deg, uint32_t now) {
  char buf[8];
  snprintf(buf, sizeof(buf), "%+d\xb0", (int)lroundf(deg));
  int w = u8g2.getStrWidth(buf);
  int asc = u8g2.getAscent();
  int x0 = alignRight ? x - w : x;
  bool blink = fabsf(deg) > 25 && (now / 250) % 2;
  u8g2.setDrawColor(blink ? 1 : 0);  // latar: hitam biasa, putih saat berkedip
  box(x0 - 2, base - asc - 2, w + 4, asc + 4);
  u8g2.setDrawColor(blink ? 0 : 1);
  u8g2.drawStr(x0, base, buf);
  u8g2.setDrawColor(1);
}

// Angka 7-segmen kecil bertanda (tema 80-an)
void drawSeg7Number(int x, int y, bool alignRight, float deg, uint32_t now) {
  int v = (int)lroundf(deg);
  char digits[4];
  snprintf(digits, sizeof(digits), "%d", abs(v));
  int n = strlen(digits);
  const int DW = 9, DH = 16, GAP = 2;
  int w = 7 + n * (DW + GAP);
  int x0 = alignRight ? x - w : x;
  u8g2.setDrawColor(0);  // latar hitam supaya tidak tertimpa garis lantai
  box(x0 - 2, y - 2, w + 4, DH + 4);
  u8g2.setDrawColor(1);
  if (fabsf(deg) > 25 && (now / 250) % 2) return;  // berkedip
  hline(x0, x0 + 4, y + DH / 2);                       // tanda minus / plus
  if (v >= 0) vline(x0 + 2, y + DH / 2 - 2, y + DH / 2 + 2);
  for (int i = 0; i < n; i++) drawSeg7(x0 + 7 + i * (DW + GAP), y, DW, DH, 2, digits[i] - '0', false);
}

void drawTilt(uint32_t now) {
  float roll, pitch;
  tiltValues(now, roll, pitch);
  const char* st = tiltStatus(now);
  bool showMax = mpuOk && biasReady;
  char mr[12] = "", mp[12] = "";
  if (showMax) {
    snprintf(mr, sizeof(mr), "MAX %d", (int)lroundf(maxRoll));
    snprintf(mp, sizeof(mp), "MAX %d", (int)lroundf(maxPitch));
  }

  drawTiltCar(true, pitch, now);
  drawTiltCar(false, roll, now);
  vline(64, 0, H - 1);
  if (theme == THEME_80S) vline(62, 0, H - 1);  // garis ganda khas 80-an

  drawTiltLabel(0, "PITCH", mp);
  drawTiltLabel(66, "ROLL", mr);
  if (theme == THEME_80S) {
    drawSeg7Number(61, 47, true, pitch, now);
    drawSeg7Number(W - 1, 47, true, roll, now);
  } else {
    u8g2.setFont(theme == THEME_CYBER ? u8g2_font_helvB12_tf : u8g2_font_logisoso16_tf);
    drawTiltNumber(61, 63, true, pitch, now);
    drawTiltNumber(W - 1, 63, true, roll, now);
  }

  if (theme == THEME_CYBER) {
    drawCorners(1, 1, 62, 62, 4);
    drawCorners(66, 1, W - 2, 62, 4);
    u8g2.setDrawColor(2);
    hline(0, W - 1, (now / 20) % H);  // garis scan
    u8g2.setDrawColor(1);
    if (calibratedAt && now - calibratedAt < 100) glitchFrame(0.4f, now / 30);
  }

  if (strcmp(st, tr(T_LIVE)) != 0) {  // status (kalibrasi, tahan=nol, dll) di tengah atas
    u8g2.setFont(u8g2_font_4x6_tr);
    int w = u8g2.getStrWidth(st);
    u8g2.setDrawColor(0);
    box(64 - w / 2 - 2, 0, w + 4, 8);
    u8g2.setDrawColor(1);
    u8g2.drawStr(64 - w / 2, 6, st);
  }
}

// ---------- G-METER ----------
// Titik bergerak seperti bola di mangkuk: rem = naik (badan terdorong ke depan),
// gas = turun, belok kiri = ke kanan. Lingkaran luar = 0.8 g, lingkaran dalam = 0.4 g.
const float G_FULL = 0.8f;
const int G_TRAIL = 14;
int8_t gTrailX[G_TRAIL], gTrailY[G_TRAIL];
int gTrailPos = 0;
uint32_t gTrailAt = 0;

void drawGRow(int y, TextId label, float val, float maxV, bool showMax) {
  char buf[8];
  u8g2.setFont(u8g2_font_4x6_tr);
  u8g2.drawStr(67, y, tr(label));
  snprintf(buf, sizeof(buf), "%.2f", val);
  u8g2.setFont(u8g2_font_5x7_tr);
  u8g2.drawStr(108 - u8g2.getStrWidth(buf), y, buf);
  if (showMax) {
    snprintf(buf, sizeof(buf), "%.2f", maxV);
    u8g2.setFont(u8g2_font_4x6_tr);
    u8g2.drawStr(W - 1 - u8g2.getStrWidth(buf), y, buf);
  }
}

void drawGMeter(uint32_t now) {
  float lon = gLon, lat = gLat;
  if (!mpuOk) {  // tanpa sensor: data demo
    float t = now / 1000.0f;
    lon = 0.35f * sinf(t * 0.9f);
    lat = 0.45f * sinf(t * 1.3f + 1.0f);
  }
  const char* right = !mpuOk ? tr(T_DEMO) : (gResetAt && now - gResetAt < 1200 ? tr(T_MAX_RESET) : "g");
  drawHeader(tr(T_GMETER), right, now);

  const int CX = 32, CY = 38, R = 24;
  // Lingkaran skala
  if (theme == THEME_80S) {
    circle(CX, CY, R);
    circle(CX, CY, R / 2);
    for (int k = 0; k < 8; k++) {  // jari-jari radar
      float a = k * PI / 4;
      line(CX + lroundf(cosf(a) * (R / 2 + 2)), CY + lroundf(sinf(a) * (R / 2 + 2)), CX + lroundf(cosf(a) * (R - 2)),
           CY + lroundf(sinf(a) * (R - 2)));
    }
  } else {
    circle(CX, CY, R);
    for (int k = 0; k < 24; k++) {  // lingkaran dalam putus-putus
      float a = k * TWO_PI / 24;
      px(CX + lroundf(cosf(a) * R / 2), CY + lroundf(sinf(a) * R / 2));
    }
    for (int d = -R + 3; d <= R - 3; d += 3) {
      px(CX + d, CY);
      px(CX, CY + d);
    }
    if (theme == THEME_CYBER) drawCorners(CX - R - 3, CY - R - 1, CX + R + 3, CY + R + 1, 4);
  }

  // Posisi titik (dibatasi di dalam lingkaran)
  float dx = lat / G_FULL, dy = lon / G_FULL;  // rem (lon minus) = titik naik
  float d = sqrtf(dx * dx + dy * dy);
  if (d > 1) {
    dx /= d;
    dy /= d;
  }
  int bx = CX + lroundf(dx * (R - 3)), by = CY + lroundf(dy * (R - 3));
  if (now - gTrailAt > 70) {  // jejak titik
    gTrailAt = now;
    gTrailX[gTrailPos] = bx - CX;
    gTrailY[gTrailPos] = by - CY;
    gTrailPos = (gTrailPos + 1) % G_TRAIL;
  }
  for (int i = 0; i < G_TRAIL; i++) px(CX + gTrailX[i], CY + gTrailY[i]);
  if (theme == THEME_80S) {
    box(bx - 2, by - 2, 5, 5);
  } else if (theme == THEME_3D) {  // bola dengan bayangan di bawahnya
    for (int yy = -3; yy <= 3; yy++) {
      for (int xx = -3; xx <= 3; xx++) {
        if (xx * xx + yy * yy <= 9 && ((bx + xx + by + yy) & 1) == 0) px(bx + 2 + xx, by + 2 + yy);
      }
    }
    u8g2.setDrawColor(0);
    disc(bx, by, 4);
    u8g2.setDrawColor(1);
    disc(bx, by, 3);
    u8g2.setDrawColor(0);
    px(bx - 1, by - 1);  // kilau
    u8g2.setDrawColor(1);
  } else {
    disc(bx, by, 2);
    if (theme == THEME_CYBER) {
      u8g2.setDrawColor(2);
      hline(CX - R, CX + R, by);
      vline(bx, CY - R, CY + R);
      u8g2.setDrawColor(1);
    }
  }

  // Tabel: sekarang dan MAX
  bool showMax = mpuOk && biasReady;
  u8g2.setFont(u8g2_font_4x6_tr);
  u8g2.drawStr(108 - u8g2.getStrWidth(tr(T_NOW)), 22, tr(T_NOW));
  u8g2.drawStr(W - 1 - u8g2.getStrWidth("MAX"), 22, "MAX");
  hline(66, W - 1, 24);
  drawGRow(34, T_BRAKE, max(0.0f, -lon), maxBrake, showMax);
  drawGRow(45, T_ACCEL, max(0.0f, lon), maxAccel, showMax);
  drawGRow(56, T_TURN, fabsf(lat), maxTurn, showMax);
}

// ---------- MODE MALAM (SETELAN) ----------
void drawNight(uint32_t now) {
  bool saved = nightSavedAt && now - nightSavedAt < 1500;
  drawHeader(tr(T_NIGHT), nullptr, now);
  // Bulan sabit
  disc(22, 33, 11);
  u8g2.setDrawColor(0);
  disc(27, 29, 10);
  u8g2.setDrawColor(1);
  px(40, 20);
  px(44, 30);
  px(37, 44);

  char buf[8];
  u8g2.setFont(u8g2_font_logisoso22_tr);
  u8g2.drawStr(52, 40, nightText(nightPick, buf));
  u8g2.setFont(u8g2_font_4x6_tr);
  char hours[16];
  snprintf(hours, sizeof(hours), "%02d:00-%02d:00", NIGHT_START_H, NIGHT_END_H);
  u8g2.drawStr(52, 50, hours);

  u8g2.setFont(u8g2_font_5x7_tr);
  u8g2.drawStr(0, 63, saved ? tr(T_SAVED) : tr(T_CLICK_SAVE));
  if (!saved) u8g2.drawStr(W - u8g2.getStrWidth(tr(T_HOLD_CANCEL)), 63, tr(T_HOLD_CANCEL));
}

// ---------- PERINGATAN MIRING (di semua layar) ----------
const float ROLL_WARN = 30, PITCH_WARN = 35;  // derajat; turun 3 derajat di bawahnya = aman lagi
bool tiltWarn = false;
uint32_t tiltSafeSince = 0;

void updateTiltWarning(uint32_t now) {
  if (!mpuOk || !biasReady || current == SCR_BOOT) {
    tiltWarn = false;
    return;
  }
  float r = fabsf(rollDeg - rollZero), p = fabsf(pitchDeg - pitchZero);
  if (!tiltWarn) {
    if (r > ROLL_WARN || p > PITCH_WARN) {
      tiltWarn = true;
      tiltSafeSince = 0;
    }
  } else if (r < ROLL_WARN - 3 && p < PITCH_WARN - 3) {
    if (!tiltSafeSince) tiltSafeSince = now;
    if (now - tiltSafeSince > 1000) tiltWarn = false;  // aman 1 detik baru hilang (tidak berkedip di batas)
  } else {
    tiltSafeSince = 0;
  }
}

// ---------- BUZZER ----------
// Bunyi = daftar {nada Hz, lama ms}. 2700 Hz kira-kira nada paling keras buzzer pasif kecil.
// Satu bunyi pendek pakai lama dari tone() (tepat), urutan bunyi dijalankan per frame (~50ms).
const Beep SND_CLICK[] = { { 2700, 18 } };                                        // klik knob
const Beep SND_HOLD[] = { { 1500, 60 } };                                         // tahan knob
const Beep SND_WARN[] = { { 3000, 150 }, { 0, 90 }, { 3000, 150 }, { 0, 500 } };  // miring, diulang
#define SND(a) a, (int)(sizeof(a) / sizeof(a[0]))
const Beep* sndSeq = nullptr;
int sndLen = 0, sndIdx = 0;
bool sndRepeat = false;
uint32_t sndStepEnd = 0;

void soundStep(uint32_t now) {
  const Beep& b = sndSeq[sndIdx];
  if (b.freq) tone(PIN_BUZZER, b.freq, b.ms);
  else noTone(PIN_BUZZER);
  sndStepEnd = now + b.ms;
}

void playSound(const Beep* seq, int len, bool repeat, uint32_t now) {
  sndSeq = seq;
  sndLen = len;
  sndIdx = 0;
  sndRepeat = repeat;
  soundStep(now);
}

void stopSound() {
  noTone(PIN_BUZZER);
  sndSeq = nullptr;
}

void updateSound(uint32_t now) {
  if (sndSeq == SND_WARN && !tiltWarn) stopSound();
  if (!sndSeq && tiltWarn) playSound(SND(SND_WARN), true, now);  // alarm jalan terus selama miring
  if (!sndSeq || (int32_t)(now - sndStepEnd) < 0) return;
  if (++sndIdx >= sndLen) {
    if (!sndRepeat) {
      sndSeq = nullptr;
      return;
    }
    sndIdx = 0;
  }
  soundStep(now);
}

void drawTiltWarning(uint32_t now) {
  bool flash = (now / 300) % 2;
  if (current == SCR_TILT) {  // di layar kemiringan: cukup bingkai berkedip
    if (flash) {
      frame(0, 0, W, H);
      frame(1, 1, W - 2, H - 2);
    }
    return;
  }
  // Layar lain ditimpa penuh, warna bergantian
  u8g2.setDrawColor(flash ? 1 : 0);
  box(0, 0, W, H);
  u8g2.setDrawColor(flash ? 0 : 1);
  for (int x = -8; x < W + 8; x += 8) {  // garis bahaya miring di atas dan bawah
    for (int k = 0; k < 3; k++) {
      line(x + k, 7, x + k + 7, 0);
      line(x + k, H - 1, x + k + 7, H - 8);
    }
  }
  float r = rollDeg - rollZero, p = pitchDeg - pitchZero;
  bool rollWorse = fabsf(r) / ROLL_WARN >= fabsf(p) / PITCH_WARN;
  u8g2.setFont(u8g2_font_helvB18_tr);
  const char* big = tr(T_TILT_WARN);
  drawText((W - u8g2.getStrWidth(big)) / 2 - (theme == THEME_CYBER ? 3 : 0), 33, big, theme == THEME_CYBER, W, 8, H - 8);
  char buf[20];
  snprintf(buf, sizeof(buf), "%s %+d\xb0", rollWorse ? "ROLL" : "PITCH", (int)lroundf(rollWorse ? r : p));
  u8g2.setFont(u8g2_font_6x10_tf);
  u8g2.drawStr((W - u8g2.getStrWidth(buf)) / 2, 45, buf);
  u8g2.setFont(u8g2_font_4x6_tr);
  u8g2.drawStr((W - u8g2.getStrWidth(tr(T_CLICK_VIEW))) / 2, 54, tr(T_CLICK_VIEW));
  u8g2.setDrawColor(1);
  if (theme == THEME_CYBER && (now / 90) % 7 == 0) glitchFrame(0.3f, now / 30);
}

// ---------- Pilih tema (SETELAN) ----------
// Gambar contoh kecil tiap tema, supaya kelihatan bedanya sebelum disimpan
void drawThemeThumb(Theme t, int x0, int y0, int x1, int y1, uint32_t now) {
  frame(x0, y0, x1 - x0 + 1, y1 - y0 + 1);
  u8g2.setClipWindow(x0 + 1, y0 + 1, x1, y1);
  int cx = (x0 + x1) / 2, cy = (y0 + y1) / 2;
  if (t == THEME_CLEAN) {
    char hm[6];
    clockHHMM(hm);
    u8g2.setFont(u8g2_font_logisoso16_tn);
    u8g2.drawStr(cx - u8g2.getStrWidth(hm) / 2, cy + 3, hm);
    hline(cx - 18, cx + 18, cy + 8);
    u8g2.setFont(u8g2_font_4x6_tr);
    u8g2.drawStr(x0 + 4, y1 - 4, tr(T_CLOCK));
    u8g2.drawStr(x1 - 8, y1 - 4, ">");
  } else if (t == THEME_CYBER) {
    for (int y = y0 + 4; y < y1; y += 6) {
      for (int x = x0 + 3; x < x1; x += 6) px(x, y);
    }
    u8g2.setDrawColor(0);
    box(cx - 13, cy - 15, 27, 25);
    u8g2.setDrawColor(1);
    drawChamferFrame(cx - 13, cy - 15, cx + 13, cy + 9, 4);
    iconClock(cx, cy - 3, 8, now / 1000.0f, 1);
    int sy = cy - 14 + (now / 40) % 23;
    u8g2.setDrawColor(2);
    hline(cx - 12, cx + 12, sy);
    u8g2.setDrawColor(1);
    u8g2.setFont(u8g2_font_4x6_tr);
    drawTag(cx, y1 - 9, 8, tr(T_CLOCK), u8g2.getStrWidth(tr(T_CLOCK)));
  } else if (t == THEME_3D) {
    meshesInit();
    for (int i = 0; i < 14; i++) px(x0 + 3 + (i * 37) % (x1 - x0 - 6), y0 + 3 + (i * 23) % (y1 - y0 - 6));  // bintang
    drawMesh(meshFighter, now / 1000.0f * 1.4f, 0.35f, sinf(now / 700.0f) * 0.3f, 0, 0, 5.5f, 60, cx, cy);
  } else {
    int hz = cy + 5;
    drawStripedSun(cx, hz, 13, hz);
    for (int k = -4; k <= 4; k++) line(cx + k * 4, hz, cx + k * 18, y1 + 6);
    float ph = fmodf(now / 700.0f, 1.0f);
    hline(x0, x1, hz);
    for (int i = 0; i < 3; i++) {
      float z = (i + ph) / 3.0f;
      hline(x0, x1, hz + lroundf((y1 - hz) * z * z));
    }
  }
  u8g2.setMaxClipWindow();
}

// Satu baris daftar pilihan: teks kiri, nilai kanan, warna terbalik kalau sedang dipilih.
// active = tandai pilihan yang sedang dipakai dengan kotak kecil.
void drawPickRow(int x, int y, int w, const char* left, const char* right, bool on, bool active) {
  u8g2.setFont(u8g2_font_5x7_tr);
  if (on) box(x, y, w, 8);
  u8g2.setDrawColor(on ? 0 : 1);
  u8g2.drawStr(x + 3, y + 7, left);
  if (right) u8g2.drawStr(x + w - 3 - u8g2.getStrWidth(right), y + 7, right);
  if (active) box(x + w - 5, y + 3, 3, 3);
  u8g2.setDrawColor(1);
}

const int PICK_TOP = 13, PICK_ROW = 8;

void drawThemePicker(uint32_t now) {
  bool saved = themeSavedAt && now - themeSavedAt < 1500;
  drawHeader(tr(T_THEME), tr(saved ? T_SAVED : T_CLICK_SET), now);
  for (int i = 0; i <= THEME_COUNT; i++) {
    const char* name = i < THEME_COUNT ? themeName((Theme)i) : tr(T_BACK);
    drawPickRow(0, PICK_TOP + i * PICK_ROW, 54, name, nullptr, i == themePick, i == theme);
  }
  Theme show = themePick < THEME_COUNT ? (Theme)themePick : theme;
  drawThemeThumb(show, 58, 14, W - 1, H - 1, now);
}

// SETELAN: daftar setelan beserta nilainya sekarang
const TextId SETTINGS_ITEMS[] = { T_LANGUAGE, T_THEME, T_BRIGHTNESS, T_NIGHT, T_SET_CLOCK, T_BACK };
const int SETTINGS_COUNT = sizeof(SETTINGS_ITEMS) / sizeof(SETTINGS_ITEMS[0]);

void drawSettings(uint32_t now) {
  drawHeader(tr(T_SETTINGS), nullptr, now);
  char hm[6];
  clockHHMM(hm);
  char pct[6];
  snprintf(pct, sizeof(pct), "%d%%", BRIGHT_PERCENT[bright]);
  char nb[6];
  const char* values[SETTINGS_COUNT] = { LANG_NAMES[lang], themeName(theme), pct, nightText(nightOpt, nb), hm, nullptr };
  for (int i = 0; i < SETTINGS_COUNT; i++) {
    drawPickRow(0, PICK_TOP + i * PICK_ROW, W, tr(SETTINGS_ITEMS[i]), values[i], i == settingsPick, false);
  }
}

// KECERAHAN: putar = ubah (layar langsung ikut berubah), klik = simpan, tahan = batal
void drawBrightness(uint32_t now) {
  bool saved = brightSavedAt && now - brightSavedAt < 1500;
  drawHeader(tr(T_BRIGHTNESS), nullptr, now);

  // Ikon matahari: makin terang, sinarnya makin panjang
  const int SX = 22, SY = 32;
  disc(SX, SY, 5);
  int ray = 3 + brightPick * 2;
  for (int k = 0; k < 8; k++) {
    float a = k * PI / 4;
    line(SX + lroundf(cosf(a) * 8), SY + lroundf(sinf(a) * 8),
         SX + lroundf(cosf(a) * (8 + ray)), SY + lroundf(sinf(a) * (8 + ray)));
  }

  char buf[6];
  snprintf(buf, sizeof(buf), "%d%%", BRIGHT_PERCENT[brightPick]);
  u8g2.setFont(u8g2_font_logisoso22_tr);
  u8g2.drawStr(48, 42, buf);

  // Bar 5 kotak
  for (int i = 0; i < BRIGHT_COUNT; i++) {
    int x = 48 + i * 15;
    if (i <= brightPick) box(x, 47, 12, 5);
    else frame(x, 47, 12, 5);
  }

  u8g2.setFont(u8g2_font_5x7_tr);
  const char* hint = saved ? tr(T_SAVED) : tr(T_CLICK_SAVE);
  u8g2.drawStr(0, 63, hint);
  if (!saved) u8g2.drawStr(W - u8g2.getStrWidth(tr(T_HOLD_CANCEL)), 63, tr(T_HOLD_CANCEL));
}

// ATUR JAM: putar = ubah angka, klik = pindah ke menit, klik lagi = simpan, tahan = batal
void drawSetClock(uint32_t now) {
  bool saved = clockSavedAt && now - clockSavedAt < 1500;
  drawHeader(tr(T_SET_CLOCK), tr(saved ? T_SAVED : (setField == 0 ? T_CLICK_NEXT : T_CLICK_SAVE)), now);

  char hh[3], mm[3];
  snprintf(hh, sizeof(hh), "%02d", setHour);
  snprintf(mm, sizeof(mm), "%02d", setMin);
  u8g2.setFont(u8g2_font_logisoso32_tn);
  int dw = u8g2.getStrWidth("00"), cw = u8g2.getStrWidth(":");
  int x = (W - (2 * dw + cw + 6)) / 2;
  int xm = x + dw + cw + 6;
  u8g2.drawStr(x, 47, hh);
  u8g2.drawStr(x + dw + 3, 47, ":");
  u8g2.drawStr(xm, 47, mm);
  if (!saved && (now / 300) % 2) box(setField == 0 ? x : xm, 50, dw, 2);  // garis berkedip di angka yang diubah

  u8g2.setFont(u8g2_font_5x7_tr);
  if (rtcOk) {
    u8g2.drawStr(x + (dw - u8g2.getStrWidth(tr(T_HOUR))) / 2, 61, tr(T_HOUR));
    u8g2.drawStr(xm + (dw - u8g2.getStrWidth(tr(T_MINUTE))) / 2, 61, tr(T_MINUTE));
  } else {
    u8g2.drawStr((W - u8g2.getStrWidth(tr(T_NO_RTC))) / 2, 61, tr(T_NO_RTC));  // jam cuma berlaku sampai mati
  }
}

void drawLangPicker(uint32_t now) {
  bool saved = langSavedAt && now - langSavedAt < 1500;
  drawHeader(tr(T_LANGUAGE), tr(saved ? T_SAVED : T_CLICK_SET), now);
  for (int i = 0; i <= LANG_COUNT; i++) {
    const char* name = i < LANG_COUNT ? LANG_NAMES[i] : tr(T_BACK);
    drawPickRow(0, PICK_TOP + i * PICK_ROW, W, name, nullptr, i == langPick, i == lang);
  }
}

// ---------- Anti burn-in (semua layar jam) ----------
// Layar jam bisa tampil berjam-jam tanpa berubah. Piksel OLED yang menyala terus lama-lama aus
// dan meninggalkan bayangan permanen (burn-in). Jadi seluruh gambar jam digeser 1 piksel tiap
// menit, berputar pelan dalam kotak kecil. Geserannya bareng dengan angka menit berganti, jadi tidak terasa.
const int8_t SHIFT_X[8] = { 0, 1, 2, 1, 0, -1, -2, -1 };
const int8_t SHIFT_Y[8] = { 0, 1, 0, -1, 0, 1, 0, -1 };

// Geser seluruh layar. Dibatasi oleh ruang kosong di pinggir, jadi gambar tidak pernah terpotong
// (misalnya tulisan header yang mepet kanan: geser ke kanan dilewati, geser lain tetap jalan).
void shiftScreen(int dx, int dy) {
  if (!dx && !dy) return;
  uint8_t* buf = u8g2.getBufferPtr();
  int x0 = W, x1 = -1, y0 = H, y1 = -1;
  for (int y = 0; y < H; y++) {
    for (int x = 0; x < W; x++) {
      if (!pxAt(buf, x, y)) continue;
      x0 = min(x0, x);
      x1 = max(x1, x);
      y0 = min(y0, y);
      y1 = max(y1, y);
    }
  }
  if (x1 < 0) return;  // layar kosong
  dx = constrain(dx, -x0, W - 1 - x1);
  dy = constrain(dy, -y0, H - 1 - y1);
  if (!dx && !dy) return;
  snapScreen(snapC);
  memset(buf, 0, W * H / 8);
  for (int y = 0; y < H; y++) {
    for (int x = 0; x < W; x++) {
      if (pxAt(snapC, x - dx, y - dy)) buf[x + (y / 8) * W] |= 1 << (y % 8);
    }
  }
}

void burnInShift() {
  int k = (clockSeconds() / 60) % 8;
  shiftScreen(SHIFT_X[k], SHIFT_Y[k]);
}

// ---------- Pilih fungsi gambar sesuai tema ----------
uint32_t bootDuration() {
  if (theme == THEME_CLEAN) return BOOT_CLEAN_TOTAL;
  if (theme == THEME_80S) return BOOT80_TOTAL;
  if (theme == THEME_3D) return BOOT3D_TOTAL;
  return BOOT_TOTAL;
}

uint32_t transHalf() {
  if (theme == THEME_CLEAN) return 170;
  if (theme == THEME_80S) return 230;
  if (theme == THEME_3D) return 260;
  return TRANS_HALF;
}

void drawTransition(uint32_t tt, uint32_t now) {
  uint32_t half = transHalf();
  if (theme == THEME_CYBER) drawGlitchTransition(tt, now);
  else if (theme == THEME_CLEAN) drawSlideTransition(tt, half);
  else if (theme == THEME_3D) drawHyperspace(tt, half);
  else if (tt < half) crtEffect(easeInOutCubic(tt / (float)half));  // TV dimatikan
  else crtEffect(1.0f - easeOutCubic((tt - half) / (float)half));   // lalu dinyalakan lagi
}

// ---------- Perpindahan layar (glitch makin parah, ganti layar, lalu pulih) ----------
void drawGlitchTransition(uint32_t tt, uint32_t now) {
  float a = 1.0f - fabsf((float)tt - TRANS_HALF) / TRANS_HALF;
  glitchFrame(a, now / 25);
}

void onEnter(Screen s, uint32_t now) {
  screenStart = now;
  if (s == SCR_CLOCK) enterClock();
  if (s == SCR_THEME) {
    themePick = theme;
    themeSavedAt = 0;
  }
  if (s == SCR_NIGHT) {
    nightPick = nightOpt;
    nightSavedAt = 0;
  }
  if (s == SCR_BRIGHT) {
    brightPick = bright;
    brightSavedAt = 0;
  }
  if (s == SCR_SET_CLOCK) {
    uint32_t sec = clockSeconds();
    setHour = sec / 3600;
    setMin = (sec / 60) % 60;
    setField = 0;
    clockSavedAt = 0;
  }
  if (s == SCR_LANG) {
    langPick = lang;
    langSavedAt = 0;
  }
  if (s == SCR_MENU) {
    settingsPick = 0;
    menuLastFrame = now;
    lastSel = -1;
    lastInputMs = now;
  }
}

void goTo(Screen s) {
  if (transitioning) return;
  nextScreen = s;
  transitioning = true;
  transStart = millis();
}

void drawScreen(Screen s, uint32_t now) {
  bool clean = theme == THEME_CLEAN, eighties = theme == THEME_80S, threeD = theme == THEME_3D;
  switch (s) {
    case SCR_BOOT:
      if (clean) drawBootClean(now - screenStart);
      else if (eighties) drawBoot80(now - screenStart);
      else if (threeD) drawBoot3D(now - screenStart);
      else drawBoot(now);
      break;
    case SCR_MENU:
      menuAnimate(now);
      if (clean) drawMenuClean(now);
      else if (eighties) drawMenu80(now);
      else if (threeD) drawMenu3D(now);
      else drawMenu(now);
      break;
    case SCR_CLOCK:
      if (clean) drawClockClean(now);
      else if (eighties) drawClock80(now);
      else if (threeD) drawClock3D(now);
      else drawClock(now);
      burnInShift();
      break;
    case SCR_TILT: drawTilt(now); break;
    case SCR_SOON: drawSoon(now); break;
    case SCR_THEME: drawThemePicker(now); break;
    case SCR_SETTINGS: drawSettings(now); break;
    case SCR_LANG: drawLangPicker(now); break;
    case SCR_SET_CLOCK: drawSetClock(now); break;
    case SCR_BRIGHT: drawBrightness(now); break;
    case SCR_GMETER: drawGMeter(now); break;
    case SCR_NIGHT: drawNight(now); break;
  }
}

void handleInput(int steps, ButtonEvent btn, uint32_t now) {
  if (steps || btn != BTN_NONE) lastInputMs = now;
  if (tiltWarn && current != SCR_TILT) {  // peringatan miring sedang tampil: klik = lihat kemiringan
    if (btn == BTN_CLICK) goTo(SCR_TILT);
    return;
  }
  bool idle = now - lastInputMs > IDLE_TO_CLOCK_MS;

  switch (current) {
    case SCR_BOOT:
      if (btn == BTN_CLICK || now - screenStart >= bootDuration()) goTo(SCR_MENU);
      break;
    case SCR_MENU:
      if (steps) {
        menuTarget += steps;
        if (steps > 0) arrowBumpR = 1;
        else arrowBumpL = 1;
      }
      if (btn == BTN_CLICK) {
        int i = wrap(menuTarget, MENU_COUNT);
        soonItem = i;
        goTo(MENU[i].target);
      } else if (idle) {
        goTo(SCR_CLOCK);
      }
      break;
    case SCR_CLOCK:
      if (steps || btn == BTN_CLICK) goTo(SCR_MENU);  // putar atau klik = bangun ke menu
      break;
    case SCR_GMETER:
      // Dipakai saat menyetir: tidak pindah sendiri ke jam
      if (btn == BTN_CLICK) goTo(SCR_MENU);
      else if (steps || btn == BTN_HOLD) resetGMax(now);
      break;
    case SCR_NIGHT:
      if (nightSavedAt) {  // sudah disimpan: tunjukkan sebentar, lalu kembali
        if (now - nightSavedAt > 700) goTo(SCR_SETTINGS);
        break;
      }
      if (steps) nightPick = constrain(nightPick + steps, 0, NIGHT_COUNT - 1);
      if (btn == BTN_CLICK) {
        saveNight(nightPick);
        nightSavedAt = now;
      } else if (btn == BTN_HOLD) {
        goTo(SCR_SETTINGS);
      } else if (idle) {
        goTo(SCR_CLOCK);
      }
      break;
    case SCR_TILT:
      // Layar kemiringan tidak pindah sendiri, karena dipakai saat menyetir
      if (btn == BTN_HOLD) {
        rollZero = rollDeg;
        pitchZero = pitchDeg;
        calibratedAt = now;
        resetMax(now);
      } else if (btn == BTN_CLICK) {
        goTo(SCR_MENU);
      } else if (steps) {
        resetMax(now);
      }
      break;
    case SCR_THEME:
      if (themeSavedAt) {  // baru disimpan: tunjukkan sebentar, lalu kembali ke SETELAN
        if (now - themeSavedAt > 700) goTo(SCR_SETTINGS);
        break;
      }
      if (steps) themePick = wrap(themePick + steps, THEME_COUNT + 1);
      if (btn == BTN_CLICK) {
        if (themePick == THEME_COUNT) {
          goTo(SCR_SETTINGS);
        } else {
          saveTheme((Theme)themePick);
          themeSavedAt = now;
        }
      } else if (idle) {
        goTo(SCR_CLOCK);
      }
      break;
    case SCR_SETTINGS:
      if (steps) settingsPick = wrap(settingsPick + steps, SETTINGS_COUNT);
      if (btn == BTN_CLICK) {
        TextId item = SETTINGS_ITEMS[settingsPick];
        if (item == T_LANGUAGE) goTo(SCR_LANG);
        else if (item == T_THEME) goTo(SCR_THEME);
        else if (item == T_SET_CLOCK) goTo(SCR_SET_CLOCK);
        else if (item == T_BRIGHTNESS) goTo(SCR_BRIGHT);
        else if (item == T_NIGHT) goTo(SCR_NIGHT);
        else goTo(SCR_MENU);
      } else if (idle) {
        goTo(SCR_CLOCK);
      }
      break;
    case SCR_BRIGHT:
      if (brightSavedAt) {  // sudah disimpan: tunjukkan sebentar, lalu kembali
        if (now - brightSavedAt > 700) goTo(SCR_SETTINGS);
        break;
      }
      if (steps) {
        brightPick = constrain(brightPick + steps, 0, BRIGHT_COUNT - 1);
        applyBrightness(brightPick);  // langsung terlihat
      }
      if (btn == BTN_CLICK) {
        saveBrightness(brightPick);
        brightSavedAt = now;
      } else if (btn == BTN_HOLD || idle) {
        applyBrightness(bright);  // batal: kembali ke kecerahan yang tersimpan
        goTo(btn == BTN_HOLD ? SCR_SETTINGS : SCR_CLOCK);
      }
      break;
    case SCR_SET_CLOCK:
      if (clockSavedAt) {  // sudah disimpan: tunjukkan sebentar, lalu kembali
        if (now - clockSavedAt > 700) goTo(SCR_SETTINGS);
        break;
      }
      if (steps) {
        if (setField == 0) setHour = wrap(setHour + steps, 24);
        else setMin = wrap(setMin + steps, 60);
      }
      if (btn == BTN_CLICK) {
        if (setField == 0) {
          setField = 1;
        } else {
          setClock(setHour, setMin);
          clockSavedAt = now;
        }
      } else if (btn == BTN_HOLD) {
        goTo(SCR_SETTINGS);  // tahan = batal
      } else if (idle) {
        goTo(SCR_CLOCK);
      }
      break;
    case SCR_LANG:
      if (langSavedAt) {  // teks sudah berganti bahasa; tunjukkan sebentar, lalu kembali
        if (now - langSavedAt > 700) goTo(SCR_SETTINGS);
        break;
      }
      if (steps) langPick = wrap(langPick + steps, LANG_COUNT + 1);
      if (btn == BTN_CLICK) {
        if (langPick == LANG_COUNT) {
          goTo(SCR_SETTINGS);
        } else {
          saveLang((Lang)langPick);
          langSavedAt = now;
        }
      } else if (idle) {
        goTo(SCR_CLOCK);
      }
      break;
    default:
      if (btn == BTN_CLICK) goTo(SCR_MENU);
      else if (idle) goTo(SCR_CLOCK);
      break;
  }
}

// Kirim gambar ke layar per 2 baris (16 piksel), sambil baca sensor di sela-selanya.
// Kirim 1 layar penuh makan ~50ms; tanpa ini gyro cuma terbaca ~14x per detik.
void sendBufferWithImu() {
  for (int row = 0; row < 8; row += 2) {
    u8g2.updateDisplayArea(0, row, 16, 2);
    mpuUpdate();
  }
}

// ---------- Setup & loop ----------
void setup() {
  Serial.begin(115200);
  pinMode(PIN_CLK, INPUT_PULLUP);
  pinMode(PIN_DT, INPUT_PULLUP);
  pinMode(PIN_SW, INPUT_PULLUP);
  pinMode(PIN_BUZZER, OUTPUT);  // diam saat menyala (kaki buzzer tidak mengambang)
  attachInterrupt(digitalPinToInterrupt(PIN_CLK), onEncoder, FALLING);
  attachInterrupt(digitalPinToInterrupt(PIN_SW), onButton, CHANGE);

  u8g2.setBusClock(400000);
  u8g2.begin();
  u8g2.setFontMode(1);  // teks transparan, biar bisa ditimpa di atas kotak

  mpuOk = mpuBegin();  // layar dan sensor berbagi jalur I2C yang sama
  Serial.println(mpuOk ? "MPU-6050: OK" : "MPU-6050: tidak ketemu, pakai data DEMO");

  loadSettings();
  initClock();
  onEnter(SCR_BOOT, millis());
}

void loop() {
  uint32_t now = millis();
  // Saat animasi pindah layar: putaran ditahan dulu (tidak dibuang), tapi klik diabaikan
  // supaya klik untuk layar lama tidak nyasar membuka sesuatu di layar baru
  int steps = transitioning ? 0 : takeEncoderSteps();
  ButtonEvent btn = readButton(now);
  if (transitioning) btn = BTN_NONE;
  if (btn == BTN_CLICK) playSound(SND(SND_CLICK), false, now);
  else if (btn == BTN_HOLD) playSound(SND(SND_HOLD), false, now);
  mpuUpdate();
  updateTiltWarning(now);
  updateSound(now);
  updateAutoBrightness();
  if (!transitioning) handleInput(steps, btn, now);

  u8g2.clearBuffer();
  if (transitioning) {
    uint32_t tt = millis() - transStart;  // millis(), bukan now: transStart bisa lebih baru dari now
    uint32_t half = transHalf();
    if (tt >= half && current != nextScreen) {
      current = nextScreen;
      onEnter(current, now);
    }
    drawScreen(current, now);
    drawTransition(tt, now);
    if (tt >= 2 * half) transitioning = false;
  } else {
    drawScreen(current, now);
  }
  if (tiltWarn) drawTiltWarning(now);
  sendBufferWithImu();
}
