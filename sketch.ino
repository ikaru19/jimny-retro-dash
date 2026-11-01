/*
  JIMNY RETRO DASH  v0.4
  Boot animation + menu + clock + MPU-6050 inclinometer, with 4 themes:
  CLEAN (default), CYBERPUNK, 80S, and 3D, plus 2 languages (Indonesian/English). Choose in SETTINGS.

  Board  : ESP32 DevKit V1
  Display: 128x64 I2C OLED (SSD1306 in Wokwi, usually SH1106 on a real 1.3" screen)
  Knob   : KY-040 rotary encoder
  Sensor : MPU-6050 (I2C 0x69, AD0 pin to 3.3V)
  Clock  : DS3231 RTC (I2C 0x68), keeps running with the car off (coin cell)

  Knob:
    turn         = select a menu item
    click        = enter / back, click during boot = skip
    hold 1 s     = on the tilt screen: set the current position as zero
    turn         = on the tilt screen: reset the recorded MAX angles
    SETTINGS     = LANGUAGE (Indonesian/English) and THEME: turn to choose, click to save
    SET CLOCK    = turn to change the hour, click to move to minutes, click again to save to the RTC, hold = cancel
    BRIGHTNESS   = turn to change screen brightness (shown live), click = save, hold = cancel
    NIGHT MODE   = 18:00-06:00 the screen dims automatically (OFF/20/40/60%)
    G-METER      = braking/acceleration/cornering force; turn or hold = reset MAX
  Tilting past the limit (roll 30, pitch 35 degrees) = warning on any screen.
  20 seconds idle in the menu = switch to the clock automatically.
*/

#include <Arduino.h>
#include <U8g2lib.h>
#include <Wire.h>
#include <Preferences.h>  // stores settings in ESP32 memory (NVS)
#include <math.h>

// ---------- Display ----------
// The real 1.3" screen is an SH1106. Wokwi only has the SSD1306: for Wokwi, swap the two lines
// (the picture there is then shifted 2 pixels, which is harmless).
U8G2_SH1106_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, U8X8_PIN_NONE);
// U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, U8X8_PIN_NONE);

const int W = 128;
const int H = 64;

// ---------- Knob pins ----------
const int PIN_CLK = 18;
const int PIN_DT = 19;
const int PIN_SW = 5;
const int PIN_BUZZER = 25;  // PASSIVE buzzer (can play different tones), driven through a transistor

// ---------- Data types (must come before all functions so Arduino does not get confused) ----------
enum Screen { SCR_BOOT, SCR_MENU, SCR_CLOCK, SCR_TILT, SCR_SOON, SCR_THEME, SCR_SETTINGS, SCR_LANG, SCR_SET_CLOCK, SCR_BRIGHT, SCR_GMETER, SCR_NIGHT, SCR_4WD, SCR_RACE, SCR_ABOUT, SCR_MUSIC };
enum Theme { THEME_CLEAN, THEME_CYBER, THEME_80S, THEME_3D, THEME_COUNT };
enum ButtonEvent { BTN_NONE, BTN_CLICK, BTN_HOLD };
enum Lang { LANG_ID, LANG_EN, LANG_COUNT };
enum DriveMode { DRIVE_2H, DRIVE_4H, DRIVE_4L, DRIVE_COUNT };  // 4WD screen (just for fun: not wired to the car)
struct Beep { uint16_t freq; uint16_t ms; };
struct Flip { char shown[4]; char prev[4]; uint32_t at; };  // one clock digit that can flip over (3D theme)
struct V3 { float x, y, z; };                                // 3D point
const int MESH_MAX_V = 64, MESH_MAX_E = 100;
struct Mesh { V3 v[MESH_MAX_V]; uint8_t e[MESH_MAX_E][2]; uint8_t nv, ne; };  // 3D wireframe model (3D theme)
struct TrackPart { float len, curve; };                   // one stretch of the race track (curve: + = right)
struct Pose3 { float yaw, pitch, roll, tx, ty, tz, f, cx, cy; };              // model and camera position + orientation  // one buzzer beep: tone (Hz, 0 = silent) and its duration
// All on-screen text that has 2 languages (contents are in the TEXTS table)
enum TextId {
  T_CLOCK, T_TILT, T_BATTERY, T_SETTINGS,
  T_LIVE, T_DEMO, T_GYRO_CAL, T_HOLD_ZERO, T_ZERO_SET, T_MAX_RESET,
  T_SOON, T_CLICK_BACK, T_TIME, T_SECONDS, T_TILT_SHORT,
  T_THEME, T_LANGUAGE, T_SAVED, T_CLICK_SET, T_BACK,
  T_THEME_CLEAN, T_THEME_CYBER, T_THEME_80S, T_THEME_3D,
  T_SET_CLOCK, T_HOUR, T_MINUTE, T_CLICK_NEXT, T_CLICK_SAVE, T_NO_RTC,
  T_BRIGHTNESS, T_HOLD_CANCEL,
  T_GMETER, T_BRAKE, T_ACCEL, T_TURN, T_NOW, T_TILT_WARN, T_CLICK_VIEW, T_NIGHT, T_OFF,
  T_4WD, T_ENGAGED, T_RACE, T_OFFROAD, T_ABOUT, T_MUSIC,
  T_COUNT
};

typedef void (*IconFn)(int cx, int cy, float r, float phase, float active);

struct MenuItem {
  TextId text;
  Screen target;
  IconFn icon;
};

// Center point + rotation + scale, for drawings that can tilt
struct Xf {
  float cx, cy, c, s, k;
};

struct RollDigit {
  char cur, prev;
  uint32_t changedAt;
};

// ---------- 2-language text ----------
// English is kept short so it fits on the 128-pixel screen.
Lang lang = LANG_ID;  // default: Indonesian
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
  { "4WD", "4WD" },                  // T_4WD
  { "AKTIF", "ENGAGED" },            // T_ENGAGED
  { "BALAP", "RACE" },               // T_RACE
  { "KELUAR JALUR", "OFF ROAD" },    // T_OFFROAD
  { "TENTANG", "ABOUT" },            // T_ABOUT
  { "MUSIK", "MUSIC" },              // T_MUSIC
};
static_assert(sizeof(TEXTS) / sizeof(TEXTS[0]) == T_COUNT, "TEXTS table must have 1 row for each TextId");

const char* tr(TextId id) {
  return TEXTS[id][lang];
}

// ---------- Knob (interrupt) ----------
volatile int encSteps = 0;
volatile uint32_t encLastUs = 0;
portMUX_TYPE encMux = portMUX_INITIALIZER_UNLOCKED;

void IRAM_ATTR onEncoder() {
  uint32_t now = micros();
  if (now - encLastUs < 1500) return;  // ignore contact bounce
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

// USB remote: the Mac (remote.py, or any serial monitor) can send L / R = turn, C = click, H = hold.
// Handy while the knob is not wired yet or its switch is broken.
ButtonEvent serialBtn = BTN_NONE;

void pollSerialInput() {
  while (Serial.available() > 0) {
    int c = Serial.read();
    int turn = (c == 'R' || c == 'r') ? 1 : (c == 'L' || c == 'l') ? -1 : 0;
    if (turn) {
      portENTER_CRITICAL(&encMux);
      encSteps += turn;
      portEXIT_CRITICAL(&encMux);
    } else if (c == 'C' || c == 'c') {
      serialBtn = BTN_CLICK;
    } else if (c == 'H' || c == 'h') {
      serialBtn = BTN_HOLD;
    }
  }
}

// The button also uses an interrupt, so very fast clicks are still caught
volatile bool swPressed = false;
volatile uint32_t swLastMs = 0;

void IRAM_ATTR onButton() {
  uint32_t now = millis();
  bool quiet = now - swLastMs > 50;  // a change within 50ms = contact bounce
  swLastMs = now;
  if (quiet && digitalRead(PIN_SW) == LOW) swPressed = true;
}

// A click counts when the button is released; held >= 800ms becomes a "hold"
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

// ---------- Basic drawing ----------
// Own versions that are safe for negative coordinates (U8g2 uses unsigned numbers).
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
  // Guard: broken coordinates (e.g. from NaN) must not cause a very long loop
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

// Fill a polygon (points as floats)
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
  float xs[12], ys[12];  // max 12 points
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

// ---------- Easing (for smooth, not stiff, motion) ----------
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

// HUD-style top bar: white label with a slanted end + line with a moving scan dot
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

// Draw text inside a clip area. If italic, each pixel row is shifted
// a little further right the higher it is, so the letters slant.
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

// ---------- Jimny SJ410 drawing (rotatable) ----------
// Proportions measured from a 1982 SJ410 hardtop blueprint.
// Units: point (0,0) = bottom center (on the ground), negative y = up.

// Rear view: height 1.6, body width 1.46 (the tall, narrow cabin typical of a Jimny)
const float JEEP_REAR_TIRE_L[][2] = { { -0.71f, 0 }, { -0.49f, 0 }, { -0.49f, -0.33f }, { -0.71f, -0.33f } };
const float JEEP_REAR_TIRE_R[][2] = { { 0.49f, 0 }, { 0.71f, 0 }, { 0.71f, -0.33f }, { 0.49f, -0.33f } };
const float JEEP_REAR_BODY[][2] = { { -0.73f, -0.26f }, { 0.73f, -0.26f }, { 0.73f, -0.95f }, { -0.73f, -0.95f } };
const float JEEP_REAR_CABIN[][2] = { { -0.68f, -0.95f }, { -0.61f, -1.6f }, { 0.61f, -1.6f }, { 0.68f, -0.95f } };
const float JEEP_REAR_GLASS[][2] = { { -0.49f, -1.05f }, { -0.45f, -1.5f }, { 0.45f, -1.5f }, { 0.49f, -1.05f } };
const float JEEP_REAR_MIRROR_L[][2] = { { -0.84f, -1.07f }, { -0.68f, -1.07f }, { -0.68f, -1.23f }, { -0.84f, -1.23f } };
const float JEEP_REAR_MIRROR_R[][2] = { { 0.68f, -1.07f }, { 0.84f, -1.07f }, { 0.84f, -1.23f }, { 0.68f, -1.23f } };

void drawJeepRear(float gx, float gy, float ang, float scale) {
  Xf t = makeXf(gx, gy, ang, scale);
  lineAt(t, -0.55f, -0.18f, 0.55f, -0.18f);  // axle
  fillShape(t, JEEP_REAR_TIRE_L, 4);
  fillShape(t, JEEP_REAR_TIRE_R, 4);
  fillShape(t, JEEP_REAR_BODY, 4);
  fillShape(t, JEEP_REAR_CABIN, 4);
  fillShape(t, JEEP_REAR_MIRROR_L, 4);
  fillShape(t, JEEP_REAR_MIRROR_R, 4);
  u8g2.setDrawColor(0);
  fillShape(t, JEEP_REAR_GLASS, 4);
  lineAt(t, -0.73f, -0.41f, 0.73f, -0.41f);  // bumper line
  discAt(t, 0, -0.88f, 0.38f);               // gap around the spare tire
  if (scale >= 10) {
    discAt(t, -0.62f, -0.75f, 0.05f);  // tail lights
    discAt(t, 0.62f, -0.75f, 0.05f);
  }
  u8g2.setDrawColor(1);
  discAt(t, 0, -0.88f, 0.32f);  // spare tire on the rear door
  if (scale >= 20) {  // rim only when drawn large, to avoid clutter
    u8g2.setDrawColor(0);
    discAt(t, 0, -0.88f, 0.2f);
    u8g2.setDrawColor(1);
    discAt(t, 0, -0.88f, 0.1f);  // rim
  }
}

// ---------- Menu icons (drawn in code, so they can be scaled) ----------
void iconClock(int cx, int cy, float r, float ph, float a) {
  int R = lroundf(r);
  circle(cx, cy, R);
  if (R >= 8) {
    px(cx, cy - R + 2);
    px(cx + R - 2, cy);
    px(cx, cy + R - 2);
    px(cx - R + 2, cy);
  }
  float mAng = -0.52f + ph * 4.0f;   // minute hand
  float hAng = -2.62f + ph * 0.33f;  // hour hand
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
  int cells = a > 0.5f ? ((int)(ph * 3.0f) % 5) : 4;  // charging animation
  int cw = (w - 4 - 3) / 4;
  for (int i = 0; i < cells; i++) box(x0 + 2 + i * (cw + 1), y0 + 2, cw, h - 4);
}

void iconGMeter(int cx, int cy, float r, float ph, float a) {
  int R = lroundf(r);
  circle(cx, cy, R);
  if (R >= 6) circle(cx, cy, R / 2);
  hline(cx - 1, cx + 1, cy);
  vline(cx, cy - 1, cy + 1);
  float dx = sinf(ph * 3.1f) * 0.55f * a, dy = cosf(ph * 2.3f) * 0.45f * a;  // wobbling dot
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

void icon4WD(int cx, int cy, float r, float ph, float a) {  // tyre with tread that rolls
  int R = lroundf(r);
  circle(cx, cy, R);
  if (R >= 6) circle(cx, cy, lroundf(r * 0.45f));
  disc(cx, cy, max(1, R / 5));
  int n = R >= 6 ? 10 : 4;
  for (int k = 0; k < n; k++) {
    float ang = ph * 2.0f + k * TWO_PI / n;
    float c = cosf(ang), s = sinf(ang);
    line(cx + lroundf(c * r * 0.7f), cy + lroundf(s * r * 0.7f), cx + lroundf(c * r), cy + lroundf(s * r));
  }
}

void iconWheel(int cx, int cy, float r, float ph, float a) {  // steering wheel that turns left and right
  int R = lroundf(r);
  circle(cx, cy, R);
  if (R >= 6) circle(cx, cy, R - 2);
  disc(cx, cy, max(1, R / 4));
  float turn = sinf(ph * 2.0f) * 0.6f * a;
  for (int k = 0; k < 3; k++) {  // three spokes: left, right, down
    float ang = turn + (k == 0 ? PI : k == 1 ? 0 : HALF_PI);
    line(cx, cy, cx + lroundf(cosf(ang) * r), cy + lroundf(sinf(ang) * r));
  }
}

const MenuItem MENU[] = {
  { T_CLOCK, SCR_CLOCK, iconClock },
  { T_TILT, SCR_TILT, iconTilt },
  { T_GMETER, SCR_GMETER, iconGMeter },
  { T_4WD, SCR_4WD, icon4WD },
  { T_RACE, SCR_RACE, iconWheel },
  { T_BATTERY, SCR_SOON, iconBattery },
  { T_SETTINGS, SCR_SETTINGS, iconGear },
};
const int MENU_COUNT = sizeof(MENU) / sizeof(MENU[0]);

// ---------- App state ----------
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
int menuSel = 0;  // currently selected menu item
DriveMode drive = DRIVE_2H;      // engaged mode on the 4WD screen (resets to 2H at power-on)
DriveMode driveFrom = DRIVE_2H;  // mode before the last shift
int drivePick = 0;               // lever position chosen with the knob
float driveLever = 0;            // drawn lever position, glides toward drivePick
uint32_t driveShiftAt = 0;       // when the last shift started (0 = not shifting)
float racePos = 0, raceSpeed = 0;  // race game: distance along the track and speed (track units, units/s)
float raceX = 0, raceLean = 0;     // car position across the road (0 = middle, +-1 = edge) and steering lean
float raceBg = 0;                  // mountain scroll (pixels)
uint32_t raceLast = 0, raceStart = 0, raceRumbleAt = 0;
uint8_t snapC[W * H / 8];  // screen copy for effects (zoom, slide, outline)
Flip flipD[4] = {};        // four clock digits that can flip over (3D theme)
Mesh meshFighter = {}, meshItem[MENU_COUNT] = {};  // 3D models (3D theme)
bool meshesReady = false;
int clockHandIdx = -1;  // hand pivot point in the menu clock model
const int STARS = 28;
float starX[STARS], starY[STARS], starZ[STARS];
bool starsReady = false;
uint32_t starLast = 0;

Preferences prefs;
Theme theme = THEME_CLEAN;  // default: easiest to read while driving
int themePick = 0;          // choice on the THEME screen (THEME_COUNT = BACK)
uint32_t themeSavedAt = 0;
int settingsPick = 0;       // row on the SETTINGS screen
int langPick = 0;           // choice on the LANGUAGE screen (LANG_COUNT = BACK)
uint32_t langSavedAt = 0;
int setHour = 0, setMin = 0;  // values on the SET CLOCK screen
int setField = 0;             // 0 = editing the hour, 1 = editing the minute
uint32_t clockSavedAt = 0;

// Screen brightness: 5 levels. Value = OLED "contrast" (0..255), higher is brighter.
const uint8_t BRIGHT_PERCENT[] = { 20, 40, 60, 80, 100 };
const uint8_t BRIGHT_CONTRAST[] = { 1, 30, 80, 160, 255 };
const int BRIGHT_COUNT = sizeof(BRIGHT_PERCENT);
int bright = BRIGHT_COUNT - 1;  // default: brightest
int appliedBright = -1;         // level currently applied to the screen

// Night mode: from 18:00 to 06:00 the screen is automatically dimmer.
// Options: index into BRIGHT_PERCENT, -1 = off.
const int NIGHT_START_H = 18, NIGHT_END_H = 6;
const int NIGHT_OPTIONS[] = { -1, 0, 1, 2 };  // OFF, 20%, 40%, 60%
const int NIGHT_COUNT = sizeof(NIGHT_OPTIONS) / sizeof(NIGHT_OPTIONS[0]);
int nightOpt = 2;  // default: 40% at night
int nightPick = 0;
uint32_t nightSavedAt = 0;
int brightPick = 0;             // choice on the BRIGHTNESS screen
uint32_t brightSavedAt = 0;

uint32_t clockBaseSec = 0;
RollDigit clockDigits[4];

// ---------- Shared effects: glitch, corner brackets ----------

// Random number that is always the same for the same input (keeps the randomness controlled)
uint32_t hash32(uint32_t x) {
  x ^= x >> 16;
  x *= 0x7feb352dUL;
  x ^= x >> 15;
  x *= 0x846ca68bUL;
  x ^= x >> 16;
  return x;
}

// Glitch effect applied directly to screen memory.
// OLED memory is split into 8 horizontal bands, 8 pixels tall; 1 byte = 8 vertical pixels.
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

// amount 0..1: higher means more damage (bands shift, invert, disappear)
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

// Letters for the Cyberpunk menu label "decoding" effect
const char* SCRAMBLE = "ABCDEFGHJKLMNPQRSTUVWXYZ0123456789#$%&@";

// ---------- MAIN MENU (cyberpunk HUD style) ----------
const uint32_t IDLE_TO_CLOCK_MS = 20000;  // idle this long in the menu = switch to the clock

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

// Slanted white label (parallelogram) with black text
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

// Menu motion (position, icons, arrows). Used by all themes.
void menuAnimate(uint32_t now) {
  float dt = (now - menuLastFrame) / 1000.0f;
  if (dt > 0.1f) dt = 0.1f;
  menuLastFrame = now;

  // Carousel position smoothly chases the target
  menuPos += (menuTarget - menuPos) * min(1.0f, dt * 12.0f);
  if (fabsf(menuTarget - menuPos) < 0.002f) {
    menuPos = menuTarget;
    int shift = menuTarget - wrap(menuTarget, MENU_COUNT);
    menuTarget -= shift;
    menuPos -= shift;
  }
  arrowBumpL = max(0.0f, arrowBumpL - dt * 5);
  arrowBumpR = max(0.0f, arrowBumpR - dt * 5);

  // Icons only animate while in the center
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

  // HUD grid dots in the background
  for (int y = 16; y < 50; y += 8) {
    for (int x = 2 + ((y / 8) % 2) * 4; x < W; x += 8) px(x, y);
  }

  // Clear the center area first so the selected icon is clean
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

  // Chamfered-corner frame + a descending scan line that inverts colors
  drawChamferFrame(46, 13, 82, 47, 5);
  int pulse = (now / 300) % 2;
  hline(43 - pulse, 44 - pulse, CY);
  hline(84 + pulse, 85 + pulse, CY);
  int sy = 14 + (now / 30) % 33;
  u8g2.setDrawColor(2);
  hline(47, 81, sy);
  u8g2.setDrawColor(1);

  // Dotted lines linking to the side icons
  for (int x = 31; x < 43; x += 2) px(x, CY);
  for (int x = 87; x < 98; x += 2) px(x, CY);

  // Double arrows that jolt when the knob turns
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

  // Menu name: scrambled letters "decoded" left to right
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

// ---------- CLOCK (RTC DS3231) ----------
// The RTC keeps time even with the car off (powered from BATT+ and a coin cell).
// DS3231 time registers match the DS1307 (used in Wokwi), so this code works on both.
const uint8_t RTC_ADDR = 0x68;
bool rtcOk = false;
uint32_t clockBaseMs = 0;  // when clockBaseSec was last read

uint8_t bcdToDec(uint8_t v) {
  return (v >> 4) * 10 + (v & 0x0F);
}

uint8_t decToBcd(uint8_t v) {
  return ((v / 10) << 4) | (v % 10);
}

// Read the time from the RTC as seconds since midnight
bool rtcRead(uint32_t& secOfDay) {
  Wire.beginTransmission(RTC_ADDR);
  Wire.write(0x00);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom(RTC_ADDR, (uint8_t)3) != 3) return false;
  uint8_t s = bcdToDec(Wire.read() & 0x7F);
  uint8_t m = bcdToDec(Wire.read() & 0x7F);
  uint8_t hr = Wire.read();
  uint8_t h;
  if (hr & 0x40) h = bcdToDec(hr & 0x1F) % 12 + ((hr & 0x20) ? 12 : 0);  // RTC in 12-hour mode
  else h = bcdToDec(hr & 0x3F);
  if (s > 59 || m > 59 || h > 23) return false;
  secOfDay = h * 3600UL + m * 60UL + s;
  return true;
}

void rtcWrite(int h, int m) {
  Wire.beginTransmission(RTC_ADDR);
  Wire.write(0x00);
  Wire.write(0);               // seconds = 0 (also restarts a halted DS1307 clock)
  Wire.write(decToBcd(m));
  Wire.write(decToBcd(h));     // 24-hour mode
  Wire.endTransmission();
}

void initClock() {
  uint32_t s;
  rtcOk = rtcRead(s);
  if (!rtcOk) {
    // RTC not found: start from the time the code was compiled
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
  if (rtcOk && now - clockBaseMs > 60000 && rtcRead(s)) {  // resync with the RTC every minute
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

// Digits change with a rolling effect (like an odometer)
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

  // Blinking colon
  int cx = x + 2 * dw + 1 + COLON / 2;
  if (millis() % 1000 < 500) {
    box(cx - 1, 32, 3, 3);
    box(cx - 1, 42, 3, 3);
  }

  // Small seconds
  uint32_t s = clockSeconds();
  char sec[3];
  snprintf(sec, sizeof(sec), "%02d", (int)(s % 60));
  u8g2.setFont(u8g2_font_6x10_tr);
  u8g2.drawStr(x + 4 * dw + 5 + COLON, 34, sec);

  // Segmented seconds bar: 30 segments, 1 segment = 2 seconds
  int filled = (s % 60) / 2;
  for (int i = 0; i < 30; i++) {
    int bx = 4 + i * 4;
    if (i < filled) box(bx, 56, 3, 4);
    else if (i == filled && (now / 250) % 2) box(bx, 56, 3, 4);
    else px(bx + 1, 59);
  }

  // Scan line passes every 4 seconds
  uint32_t ph = now % 4000;
  if (ph < 1600) {
    u8g2.setDrawColor(2);
    hline(4, W - 5, 16 + ph / 46);
    u8g2.setDrawColor(1);
  }

  if (changed) glitchFrame(0.4f, now / 30);
}

// ---------- MPU-6050 tilt sensor: gyro + accelerometer fusion ----------
// How it works (similar to a factory inclinometer, but without wheel-speed data):
//  1. The gyro is integrated on every read: fast and not fooled by braking/acceleration, but drifts over time.
//  2. The accelerometer (direction of gravity) slowly pulls the result back to the true angle.
//  3. If the accelerometer disagrees with 1g or with the gyro, there is a bump,
//     braking, or acceleration. The correction is reduced so no "false tilt" appears.
//  4. The gyro zero error (bias) is relearned every time the car is still.
const uint8_t MPU_ADDR = 0x69;  // AD0 pin tied to 3.3V, because 0x68 is already used by the RTC
const float ACC_LSB = 16384.0f;  // +-2g range
const float GYRO_LSB = 131.0f;   // +-250 degrees/second range
const float FUSION_TAU = 1.5f;   // seconds: higher trusts the gyro more

bool mpuOk = false;
float rollDeg = 0, pitchDeg = 0;    // roll + = right side down, pitch + = nose up
float rollZero = 0, pitchZero = 0;  // calibrated zero point
float maxRoll = 0, maxPitch = 0;    // largest angle since reset (absolute value)
float gLon = 0, gLat = 0;           // G force: + = accelerate / turn left, - = brake / turn right
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
float disagreeSec = 0;  // how long the accelerometer and gyro have disagreed

void mpuWrite(uint8_t reg, uint8_t val) {
  Wire.beginTransmission(MPU_ADDR);
  Wire.write(reg);
  Wire.write(val);
  Wire.endTransmission();
}

bool mpuBegin() {
  Wire.beginTransmission(MPU_ADDR);
  if (Wire.endTransmission() != 0) return false;
  mpuWrite(0x6B, 0x01);  // wake the sensor, use the gyro clock (more stable)
  mpuWrite(0x1A, 0x03);  // built-in ~44Hz filter, damps engine vibration
  mpuWrite(0x19, 0x04);  // 200 samples per second
  mpuWrite(0x1B, 0x00);  // gyro +-250 degrees/second
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
  Wire.write(0x3B);  // accelerometer X, Y, Z, temperature, then gyro X, Y, Z
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

  // Axes: X toward the front of the car, Y to the left, Z up. If mounted differently, swap them here.
  float ax = a[0], ay = a[1], az = a[2];
  float aNorm = sqrtf(ax * ax + ay * ay + az * az);

  // 1. Learn the gyro bias while still: accelerometer ~1g and gyro barely rotating
  uint32_t ms = millis();
  float stillLimit = biasReady ? 2.0f : 10.0f;  // degrees/second
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

  // 2. Angle from the accelerometer (direction of gravity)
  float accRoll = atan2f(ay, az) * RAD_TO_DEG;
  float accPitch = atan2f(ax, sqrtf(ay * ay + az * az)) * RAD_TO_DEG;
  if (!imuStarted) {
    rollDeg = accRoll;
    pitchDeg = accPitch;
    imuStarted = true;
    return;
  }

  // 3. Angle from the gyro: previous angle + rotation rate x time
  //    (Euler angle formula, so it stays correct when roll and pitch happen together)
  float r = rollDeg * DEG_TO_RAD, p = pitchDeg * DEG_TO_RAD;
  float rollRate = gx - (gy * sinf(r) + gz * cosf(r)) * tanf(p);
  float pitchRate = -gy * cosf(r) + gz * sinf(r);
  float gyroRoll = rollDeg + rollRate * dt;
  float gyroPitch = pitchDeg + pitchRate * dt;

  // 4. How much to trust the accelerometer right now
  float trust = clamp01(1.0f - fabsf(aNorm - 1.0f) / 0.15f);  // strong bump/braking/acceleration
  float diff = (fabsf(accRoll - gyroRoll) + fabsf(accPitch - gyroPitch)) / 6.0f;
  float gate = max(0.3f, 1.0f / (1.0f + diff * diff));  // far off from the gyro = push force, not tilt
  // Braking/acceleration/cornering rarely lasts more than 2 seconds. If they disagree longer, the gyro
  // is probably wrong (bias), so slowly trust the accelerometer again. Without this, a gyro
  // drifting > 2 degrees/second can make the reading jump far off.
  if (diff > 0.5f) disagreeSec += dt;
  else disagreeSec = max(0.0f, disagreeSec - 2 * dt);
  gate += (1.0f - gate) * clamp01((disagreeSec - 2.0f) / 2.0f);
  trust *= gate;
  float k = biasReady ? dt / (FUSION_TAU + dt) * trust : 0.2f;  // gyro not ready yet: follow the accelerometer
  rollDeg = gyroRoll + k * (accRoll - gyroRoll);
  pitchDeg = gyroPitch + k * (accPitch - gyroPitch);

  // 5. Record the largest angle (measured from the zero point)
  if (biasReady) {
    maxRoll = max(maxRoll, fabsf(rollDeg - rollZero));
    maxPitch = max(maxPitch, fabsf(pitchDeg - pitchZero));
  }

  // 6. G force: accelerometer minus gravity (based on the car's angle), then smoothed
  //    so engine vibration does not make the G-meter dot jump around
  float rr = rollDeg * DEG_TO_RAD, pp = pitchDeg * DEG_TO_RAD;
  float kg = dt / (0.15f + dt);
  gLon += ((ax - sinf(pp)) - gLon) * kg;              // + = accelerate, - = brake
  gLat += ((ay - cosf(pp) * sinf(rr)) - gLat) * kg;   // + = turn left (body pushed to the right)
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

// ---------- TILT ----------
// Displayed angle (measured from the zero point)
void tiltValues(uint32_t now, float& roll, float& pitch) {
  if (mpuOk) {
    roll = rollDeg - rollZero;
    pitch = pitchDeg - pitchZero;
  } else {
    // Sensor not found: use self-moving demo data
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

// ---------- COMING SOON ----------
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

// ---------- THEME ----------
// Four display styles. Sensor, knob, and screen flow are the same; only the drawing differs.
// The choice is saved in ESP32 NVS memory, so it is remembered even when the car is turned off.
#define FONT_80S u8g2_font_pxplusibmcga_8r  // blocky 8x8 font typical of 1980s IBM PCs

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

// Called on every loop pass: apply day/night brightness (not while it is being adjusted)
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

// 4x4 dot pattern for the "fade in" effect (OLED pixels can only be on or off)
const uint8_t BAYER4[4][4] = { { 0, 8, 2, 10 }, { 12, 4, 14, 6 }, { 3, 11, 1, 9 }, { 15, 7, 13, 5 } };

// Clear some of the pixels in this box. level 0 = all gone, 1 = intact.
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

// Screen title in the current theme's style
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
  if (theme == THEME_3D) {  // bold text, underline with depth (hatched)
    u8g2.setFont(u8g2_font_helvB08_tr);
    u8g2.drawStr(1, 8, title);
    u8g2.setFont(u8g2_font_5x7_tr);
    if (right) u8g2.drawStr(W - 1 - u8g2.getStrWidth(right), 8, right);
    hline(0, W - 1, 10);
    dotLine(1, 11, W - 1, 11);
    return;
  }
  u8g2.setFont(FONT_80S);  // 80s: blocky font + double line
  u8g2.drawStr(0, 8, title);
  u8g2.setFont(u8g2_font_5x7_tr);
  if (right) u8g2.drawStr(W - 1 - u8g2.getStrWidth(right), 8, right);
  hline(0, W - 1, 10);
  hline(0, W - 1, 12);
}

// Shift the whole screen right (positive dx) or left (negative dx)
void shiftAll(int dx) {
  for (int b = 0; b < 8; b++) shiftBand(b, dx);
}

// CRT TV effect: the picture shrinks to a line in the middle, then to a dot.
// a = 0 normal, a = 1 only a dot is left.
void crtEffect(float a) {
  if (a <= 0) return;
  static uint8_t src[W * H / 8];
  uint8_t* buf = u8g2.getBufferPtr();
  memcpy(src, buf, sizeof(src));
  memset(buf, 0, sizeof(src));
  if (a < 0.6f) {
    float sy = max(0.02f, 1.0f - a / 0.6f);  // remaining picture height
    for (int y = 0; y < H; y++) {
      int fy = (int)floorf(31.5f + (y - 31.5f) / sy);
      if (fy < 0 || fy >= H) continue;
      for (int x = 0; x < W; x++) {
        if (src[x + (fy / 8) * W] & (1 << (fy % 8))) buf[x + (y / 8) * W] |= 1 << (y % 8);
      }
    }
    if (sy < 0.15f) hline(0, W - 1, 31);  // bright line
  } else {
    int half = lroundf((1.0f - (a - 0.6f) / 0.4f) * 64);
    if (half > 0) hline(64 - half, 63 + half, 31);
    disc(64, 31, 1);  // last bright dot
  }
}

// ---------- CLEAN theme: plain, easy to read at a glance while driving ----------

void drawMenuClean(uint32_t now) {
  char hm[6];
  clockHHMM(hm);
  drawHeader("MENU", hm, now);

  // Selection frame glides smoothly; at the end of the list it wraps back to the top.
  // 5 rows fit on screen; with more items the list scrolls to keep the selection visible.
  const int TOP = 13, ROW = 10, ROWS = 5;
  float p = fmodf(menuPos, (float)MENU_COUNT);
  if (p < 0) p += MENU_COUNT;
  float scroll = constrain(p - (ROWS - 1) / 2.0f, 0.0f, (float)max(0, MENU_COUNT - ROWS));
  int fy = TOP + lroundf((p - scroll) * ROW);
  u8g2.setClipWindow(0, TOP, W, H);
  for (int k = 0; k < 2; k++) {
    int y = fy - k * MENU_COUNT * ROW;
    frame(1, y, W - 2, ROW);
    u8g2.setDrawColor(0);  // rounded corners
    px(1, y);
    px(W - 2, y);
    px(1, y + ROW - 1);
    px(W - 2, y + ROW - 1);
    u8g2.setDrawColor(1);
  }
  for (int i = 0; i < MENU_COUNT; i++) {
    int y = TOP + lroundf((i - scroll) * ROW);
    if (y < TOP - ROW || y > H) continue;
    bool on = i == menuSel;
    MENU[i].icon(10, y + 5, 4.0f, iconPhase[i], on ? 1.0f : 0.0f);
    u8g2.setFont(on ? u8g2_font_helvB08_tr : u8g2_font_helvR08_tr);
    u8g2.drawStr(20, y + 8, tr(MENU[i].text));
    if (on) u8g2.drawStr(W - 9, y + 8, ">");
  }
  u8g2.setMaxClipWindow();
}

void drawClockClean(uint32_t now) {
  char hm[6];
  clockHHMM(hm);
  u8g2.setFont(u8g2_font_logisoso42_tn);
  u8g2.drawStr((W - u8g2.getStrWidth(hm)) / 2, 53, hm);
}

// Transition: the old screen slides out to the left, the new screen slides in from the right
void drawSlideTransition(uint32_t tt, uint32_t half) {
  if (tt < half) shiftAll(-lroundf(easeInOutCubic(tt / (float)half) * W));
  else shiftAll(lroundf((1.0f - easeOutCubic((tt - half) / (float)half)) * W));
}

// ---------- 80S theme: striped sun, chrome, 7-segment, analog needle ----------

// Sunset: upper half circle, its lower part cut by black stripes
// that get thicker toward the horizon
void drawStripedSun(int cx, int cy, int r, int horizon) {
  for (int dy = -r; dy <= 0; dy++) {
    int y = cy + dy;
    if (y > horizon || y < 0) continue;
    int d = -dy;  // distance above the center
    if (d <= r * 0.55f && (d + 2) % 4 < (d < r / 4 ? 2 : 1)) continue;  // black stripe
    int dx = (int)(sqrtf((float)(r * r - dy * dy)) + 0.5f);
    hline(cx - dx, cx + dx, y);
  }
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

  // Double-lined box; its contents slide when the knob turns
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

// Home-made 7-segment digits. Unlit segments stay faintly visible, like an old clock.
//  aaa
// f   b
//  ggg
// e   c
//  ddd
const uint8_t SEG7[10] = { 0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07, 0x7F, 0x6F };  // bit0 = a ... bit6 = g

void seg7Poly(int x, int y, int w, int h, float t, int s, float* xs, float* ys) {
  float ht = t / 2, mid = y + h / 2.0f;
  if (s == 0 || s == 3 || s == 6) {  // horizontal: a, d, g
    float yy = s == 0 ? y : (s == 3 ? y + h - t : mid - ht);
    float px6[6] = { x + 1.0f, x + 1 + ht, x + w - 1 - ht, x + w - 1.0f, x + w - 1 - ht, x + 1 + ht };
    float py6[6] = { yy + ht, yy, yy, yy + ht, yy + t, yy + t };
    memcpy(xs, px6, sizeof(px6));
    memcpy(ys, py6, sizeof(py6));
  } else {  // vertical: b, c, e, f
    bool right = s == 1 || s == 2, upper = s == 1 || s == 5;
    float xx = right ? x + w - t : x;
    float y0 = upper ? y : mid, y1 = upper ? mid : y + h;
    float px6[6] = { xx + ht, xx + t, xx + t, xx + ht, xx, xx };
    float py6[6] = { y0 + 1, y0 + 1 + ht, y1 - 1 - ht, y1 - 1, y1 - 1 - ht, y0 + 1 + ht };
    memcpy(xs, px6, sizeof(px6));
    memcpy(ys, py6, sizeof(py6));
  }
}

// ghost = show unlit segments faintly (good on large digits, confusing on small ones)
void drawSeg7(int x, int y, int w, int h, float t, int digit, bool ghost) {
  float xs[6], ys[6];
  for (int s = 0; s < 7 && ghost; s++) {  // unlit segments: sparse dots
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
  if (millis() % 1000 < 500) {  // blinking colon
    box(62, Y + 9, 4, 4);
    box(62, Y + 25, 4, 4);
  }
  char sec[12];
  snprintf(sec, sizeof(sec), "%s %02d", tr(T_SECONDS), (int)(clockSeconds() % 60));
  u8g2.setFont(u8g2_font_5x7_tr);
  u8g2.drawStr((W - u8g2.getStrWidth(sec)) / 2, 63, sec);
}

// ---------- 3D theme: drawing helpers and clock ----------
const uint32_t FLIP_MS = 500;  // duration of one clock digit flip

bool pxAt(const uint8_t* b, int x, int y) {
  if (x < 0 || x >= W || y < 0 || y >= H) return false;
  return b[x + (y / 8) * W] & (1 << (y % 8));
}

void snapScreen(uint8_t* dst) {
  memcpy(dst, u8g2.getBufferPtr(), W * H / 8);
}

// Everything drawn since the 'before' snapshot is turned into an outline only (its inside is cleared),
// so it looks like vector graphics
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

// Checkerboard dashed line
void dotLine(int x0, int y0, int x1, int y1) {
  int n = max(abs(x1 - x0), abs(y1 - y0));
  for (int i = 0; i <= n; i++) {
    int x = n ? x0 + (x1 - x0) * i / n : x0, y = n ? y0 + (y1 - y0) * i / n : y0;
    if (((x + y) & 1) == 0) px(x, y);
  }
}

// Record the digit; if it changed, start the flip animation
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

// 7-segment digit built from 3D blocks. Centered at (0,0,0) so it can rotate in place.
// The front face is filled solid (easy to read); the depth sides are lines receding backward.
const float SEG_BOX[7][4] = {  // x0, y0, x1, y1 for each segment a..g, digit is 0.8 x 1.4
  { 0.1f, 1.23f, 0.7f, 1.4f }, { 0.63f, 0.75f, 0.8f, 1.35f }, { 0.63f, 0.05f, 0.8f, 0.65f }, { 0.1f, 0, 0.7f, 0.17f },
  { 0, 0.05f, 0.17f, 0.65f },  { 0, 0.75f, 0.17f, 1.35f },    { 0.1f, 0.615f, 0.7f, 0.785f },
};

void drawDigit3D(int digit, const Pose3& p) {
  const float D = 0.15f;  // half depth
  for (int pass = 0; pass < 2; pass++) {  // 0 = depth sides of all segments first, 1 = front faces on top
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
        u8g2.setDrawColor(0);  // thin black line around the front face, to separate it from the depth sides
        for (int k = 0; k < 4; k++) line(lroundf(fx[k]) + 1, lroundf(fy[k]) + 1, lroundf(fx[(k + 1) % 4]) + 1, lroundf(fy[(k + 1) % 4]) + 1);
        u8g2.setDrawColor(1);
        fillPoly(fx, fy, 4);
        for (int k = 0; k < 4; k++) line(lroundf(fx[k]), lroundf(fy[k]), lroundf(fx[(k + 1) % 4]), lroundf(fy[(k + 1) % 4]));
      }
    }
  }
}

// 3D clock: 3D block digits float among the stars and sway gently. When a digit changes,
// it flips backward like a flip board, then the new digit flips in.
// Colon = two small spinning cubes. Seconds at the bottom, inside the reticle.
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
  float yaw = sinf(now / 1500.0f) * 0.12f;  // the whole clock sways gently
  for (int i = 0; i < 4; i++) {
    char v[2] = { d[i], 0 };
    flipSet(flipD[i], v, now);
    float p = flipProgress(flipD[i], now, FLIP_MS);
    int digit = flipD[i].shown[0] - '0';
    float flip = 0;
    if (p < 0.5f) {  // old digit flips away (top going back)
      digit = flipD[i].prev[0] - '0';
      flip = easeInOutCubic(p * 2) * HALF_PI;
    } else if (p < 1) {  // new digit flips in from below
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

// ---------- 3D theme: 80s vector arcade game style (3D wireframe models) ----------
// Simple 3D drawing: model points are rotated, given perspective, then joined with lines.
// The camera looks along +z. Model: x right, y up, z away from the camera.

int meshV(Mesh& m, float x, float y, float z) {
  for (int i = 0; i < m.nv; i++) {  // identical points are shared
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

// Circle of n points. axis: 0 = facing the camera (XY plane), 1 = horizontal (XZ), 2 = sideways (YZ)
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

// X-wing fighter (home-made): long body, 4 wings, a cannon on each wing tip
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
  int cp = meshV(m, 0, 0.32f, 0.2f);  // cockpit canopy
  meshE(m, cp, f[0]);
  meshE(m, cp, f[1]);
  meshE(m, cp, r[0]);
  meshE(m, cp, r[1]);
}

// 3D model for each menu item
void buildMenuMesh(Mesh& m, TextId item) {
  if (item == T_CLOCK) {  // desk clock: front and back rings + hands (hands are set to the current time when drawn)
    int a = meshRing(m, 12, 1.0f, 0, -0.2f), b = meshRing(m, 12, 1.0f, 0, 0.2f);
    for (int i = 0; i < 12; i += 3) meshE(m, a + i, b + i);
    for (int i = 0; i < 12; i += 3) meshLine(m, cosf(i * TWO_PI / 12) * 0.8f, sinf(i * TWO_PI / 12) * 0.8f, -0.2f, cosf(i * TWO_PI / 12), sinf(i * TWO_PI / 12), -0.2f);
    clockHandIdx = m.nv;
    meshE(m, meshV(m, 0, 0, -0.21f), meshV(m, 0, 0.5f, -0.22f));  // hour hand
    meshE(m, clockHandIdx, meshV(m, 0.7f, 0, -0.23f));          // minute hand
  } else if (item == T_TILT) {  // gyroscope sphere: three rings
    meshRing(m, 12, 1.0f, 0, 0);
    meshRing(m, 12, 1.0f, 1, 0);
    meshRing(m, 12, 1.0f, 2, 0);
    meshLine(m, -1.3f, 0, 0, 1.3f, 0, 0);  // axis
  } else if (item == T_GMETER) {  // G dial with two rings, a cross, and a marker
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
  } else if (item == T_BATTERY) {  // car battery with two terminals, + and - signs
    meshBox(m, -0.8f, -0.5f, -0.4f, 0.8f, 0.4f, 0.4f);
    meshBox(m, -0.55f, 0.4f, -0.1f, -0.3f, 0.6f, 0.1f);
    meshBox(m, 0.3f, 0.4f, -0.1f, 0.55f, 0.6f, 0.1f);
    meshLine(m, -0.55f, 0, -0.41f, -0.25f, 0, -0.41f);
    meshLine(m, -0.4f, -0.15f, -0.41f, -0.4f, 0.15f, -0.41f);
    meshLine(m, 0.25f, 0, -0.41f, 0.55f, 0, -0.41f);
  } else if (item == T_4WD) {  // tyre: two sidewalls with tread across, a rim, and a hub with spokes
    int a = meshRing(m, 12, 1.0f, 0, -0.25f), b = meshRing(m, 12, 1.0f, 0, 0.25f);
    for (int i = 0; i < 12; i++) meshE(m, a + i, b + i);
    int rim = meshRing(m, 12, 0.6f, 0, -0.25f), hub = meshRing(m, 6, 0.2f, 0, -0.25f);
    for (int i = 0; i < 6; i++) meshE(m, hub + i, rim + i * 2);
  } else if (item == T_RACE) {  // steering wheel: thick rim, hub, three spokes
    int a = meshRing(m, 12, 1.0f, 0, -0.1f), b = meshRing(m, 12, 0.8f, 0, -0.1f);
    for (int i = 0; i < 12; i += 3) meshE(m, a + i, b + i);
    int hub = meshRing(m, 6, 0.25f, 0, 0.1f);
    meshE(m, hub, b + 0);      // right spoke
    meshE(m, hub + 3, b + 6);  // left spoke
    meshE(m, meshV(m, 0, -0.25f, 0.1f), b + 9);  // lower spoke
  } else {  // 6-tooth gear, with depth, with a center hole
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
  for (int i = 0; i < MENU_COUNT; i++) buildMenuMesh(meshItem[i], MENU[i].text);
}

// Rotate one point (roll, pitch, then yaw), translate it, then project it onto the screen with perspective.
// Returns false = the point is behind the camera or too far off screen.
bool project3D(const V3& v, const Pose3& p, float& X, float& Y) {
  float cyw = cosf(p.yaw), syw = sinf(p.yaw), cp = cosf(p.pitch), sp = sinf(p.pitch), cr = cosf(p.roll), sr = sinf(p.roll);
  float x1 = v.x * cr - v.y * sr, y1 = v.x * sr + v.y * cr, z1 = v.z;  // roll
  float y2 = y1 * cp - z1 * sp, z2 = y1 * sp + z1 * cp;                // pitch
  float x3 = x1 * cyw + z2 * syw, z3 = -x1 * syw + z2 * cyw;           // yaw
  float Z = z3 + p.tz;
  if (Z <= 0.3f) return false;
  X = p.cx + p.f * (x3 + p.tx) / Z;
  Y = p.cy - p.f * (y2 + p.ty) / Z;
  return fabsf(X) < 1000 && fabsf(Y) < 1000;
}

// Draw a 3D wireframe model. Angles in radians, position in model units, f = camera focal length (pixels).
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

// 3D stars flying toward the viewer. streak > 0: drawn as lines (longer means faster)
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
      if (starZ[i] < 2) px(x + 1, y);  // near stars are bigger
    }
  }
}

// Menu: the menu's 3D model spins in the middle of the reticle. Turning the knob = the old model flies off to the side,
// the new model enters from the other side. Background stars speed up during the switch.
// Now and then a fighter passes in the distance.
void drawMenu3D(uint32_t now) {
  meshesInit();
  float moving = fabsf(menuTarget - menuPos);
  starsUpdate(now, 1.5f + moving * 20);
  starsDraw(moving > 0.05f ? min(1.0f, moving) * 0.8f : 0);

  uint32_t fp = now % 11000;  // fighter flyby
  if (fp < 3000) {
    float t = fp / 3000.0f;
    drawMesh(meshFighter, 1.25f, 0.15f, -0.5f, -9 + 18 * t, 1.6f - 1.2f * t, 16, 60, 64, 32);
  }

  // Clock hands follow the current time
  for (int i = 0; i < MENU_COUNT; i++) {
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
    float yaw = MENU[i].text == T_CLOCK ? sinf(spin) * 0.7f : spin;  // the clock only sways, so its hands stay visible
    float roll = MENU[i].text == T_TILT ? sinf(spin * 1.3f) * 0.4f : 0;
    float pitch = MENU[i].text == T_GMETER ? 0.55f : 0.3f;
    if (MENU[i].text == T_4WD) {  // the tyre rolls on its axle, seen at an angle
      yaw = 0.5f + sinf(spin * 0.5f) * 0.25f;
      roll = -spin * 2.0f;
    }
    if (MENU[i].text == T_RACE) {  // the steering wheel turns left and right, facing the driver
      yaw = sinf(spin * 0.4f) * 0.3f;
      roll = sinf(spin * 1.5f) * 0.9f;
    }
    drawMesh(meshItem[i], yaw, pitch, roll - d * 0.9f, d * 4.5f, 0, 3.0f + fabsf(d) * 6, 60, CX, CY);
  }

  // Reticle: narrows and blinks "locked" when it stops
  int g = 22 + lroundf(min(1.0f, moving) * 14);
  drawCorners(CX - g - 4, CY - g + 2, CX + g + 4, CY + g - 2, 5);
  if (moving < 0.02f && (now / 400) % 2) {
    hline(CX - 3, CX - 1, CY);
    hline(CX + 1, CX + 3, CY);
  }

  u8g2.setDrawColor(0);  // clear the top and bottom for the title and menu name
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

// Screen transition: hyperspace jump. The old screen grows and fades with star streaks,
// then the new screen appears from the center.
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


// ---------- TILT: Jimny silhouettes from a blueprint ----------
// Like a factory car inclinometer: left is the side view (PITCH, car front on the left),
// right is the rear view (ROLL). Silhouettes are traced from an SJ410 hardtop blueprint (same body as the JA11)
// then scaled down. Each row: '#' = lit pixel. Edit directly to change details.
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

// ---------- Boot logos: Suzuki "S" emblem, SUZUKI wordmark, and the classic "Jimny" wordmark ----------
// Traced to pixels from the logos on Wikimedia Commons ("Suzuki Motor Corporation logo.svg",
// "Suzuki Jimny logo.svg"). Trademarks of Suzuki Motor Corporation, shown here for a personal dash.
const char* const LOGO_S[] = {
  "..........##..........",
  ".........####.........",
  "........#######.......",
  "......##########......",
  ".....#############....",
  "...#####.##########...",
  ".########..#########..",
  ".##########.##########",
  "..##########..#######.",
  "....##########........",
  ".....##########.......",
  ".......##########.....",
  "........##########....",
  "..######..##########..",
  "##########.##########.",
  ".##########.##########",
  "...##########.#####...",
  "....##############....",
  "......##########......",
  ".......#######........",
  ".........####.........",
  "..........##..........",
};
const char* const LOGO_SUZUKI[] = {
  "..########....###....####.###########.####....####.###....#####.####",
  ".##########..####....####.###########.####....####.####..#####..####",
  "#####.######.####....####.###########.####....####.####.#####...####",
  "#####...####.####....####......#####..####....####.#########....####",
  "#########....####....####.....#####...####....####.########.....####",
  ".##########..####....####....#####....####....####.#######......####",
  "...#########.####....####...#####.....####....####.########.....####",
  "#####...####.####....####..#####......####....####.#########....####",
  "######.#####.############.###########.############.####.#####...####",
  ".###########..##########..###########..##########..####..#####..####",
  "..########.....########...###########...########...###....#####.####",
};
const char* const LOGO_JIMNY[] = {
  "......##########..#########.............................................................................",
  "......##########..#########.............................................................................",
  ".......#########..#########.............................................................................",
  ".......#########........................................................................................",
  ".......##########.......................................................................................",
  ".......##########..########..########...#######..########..########....########...#######......#########",
  ".......##########..########..########..########.#########..########...#########..########......########.",
  ".......#########...########..########..########.########..#########..##########..########.....#########.",
  ".......#########..#########..########.##################..#########.###########..########....#########..",
  ".......#########..#########..###########################..########.############..#######.....#########..",
  ".......#########..#########..###########################..#####################..#######....#########...",
  "......##########..#########..###########################..#####################..#######....#########...",
  "......##########..#########..###########################..#####################..#######...#########....",
  "......##########..#########..###########################..#####################..#######..#########.....",
  "......##########..########...###########################..####################...#######..#########.....",
  "......##########..########..############################..###########.########..########.#########......",
  "......##########..########..#########.##################..##########..########..##################......",
  "......#########...########..########.#########.########..##########...########..#################.......",
  ".....##########..#########..########.########..########..#########....########..#################.......",
  ".....##########..#########..#######..########..########..#########....########..################........",
  "....###########..#########..######...#######...########..########.....########..################........",
  "....###########.......................................................................#########.........",
  "...###########.......................................................................#########..........",
  "...###########.......................................................................#########..........",
  "..###########.......................................................................#########...........",
  ".############......................................................................##########...........",
  ".###########.......................................................................#########............",
  "############......................................................................##########............",
};
const int LOGO_S_W = 22, LOGO_SUZUKI_W = 68, LOGO_SUZUKI_H = 11, LOGO_JIMNY_W = 104, LOGO_JIMNY_H = 28;
const int LOGO_JIMNY_LETTER_END[5] = { 17, 28, 57, 80, 104 };  // last column of J, i, m, n, y

// Draw a text bitmap ("#" = on) at (x, y), only columns before maxCol and rows before maxRow
void drawRows(const char* const* rows, int w, int h, int x, int y, int maxCol, int maxRow) {
  for (int r = 0; r < min(h, maxRow); r++)
    for (int c = 0; c < min(w, maxCol); c++)
      if (rows[r][c] == '#') px(x + c, y + r);
}

// Draw a text bitmap rotated by 'ang' (radians, + = clockwise on screen) around the bitmap
// pivot point (pivX, pivY). The pivot lands on screen at (sx, sy).
void drawBitmapRot(const char* const* rows, int bw, int bh, float pivX, float pivY, int sx, int sy, float ang) {
  drawBitmapScaled(rows, bw, bh, pivX, pivY, sx, sy, ang, 1.0f);
}

// Same, also scaled by 'scale' (0.5 = half size)
void drawBitmapScaled(const char* const* rows, int bw, int bh, float pivX, float pivY, int sx, int sy, float ang, float scale) {
  float c = cosf(ang) / scale, s = sinf(ang) / scale;
  int r = (int)ceilf(hypotf(max(pivX, bw - pivX), max(pivY, bh - pivY)) * scale) + 1;
  for (int y = -r; y <= r; y++) {
    for (int x = -r; x <= r; x++) {
      float u = x * c + y * s + pivX, v = -x * s + y * c + pivY;  // screen point rotated back into the bitmap
      int iu = (int)floorf(u), iv = (int)floorf(v);
      if (iu >= 0 && iu < bw && iv >= 0 && iv < bh && rows[iv][iu] == '#') px(sx + x, sy + y);
    }
  }
}

// Ground slope under the car (thick line + theme fill), only in columns x0..x1
void drawSlope(int x0, int x1, int sx, int sy, float ang, uint32_t now) {
  float c = cosf(ang), s = sinf(ang);
  int t = now / 80;
  for (int y = 0; y < H; y++) {
    for (int x = x0; x <= x1; x++) {
      float dx = x - sx, dy = y - sy;
      float v = -dx * s + dy * c;  // how far below the ground surface
      if (v < 0) continue;
      float u = dx * c + dy * s;  // position along the ground
      bool on;
      if (v < 1.6f) on = true;                                               // ground surface
      else if (theme == THEME_CLEAN) on = BAYER4[y & 3][x & 3] < 3;          // fine hatching
      else if (theme == THEME_CYBER) on = (((int)floorf(u - v) + t) % 5) == 0;  // moving diagonal lines
      else if (theme == THEME_3D) on = ((int)floorf(u) + 64) % 6 == 0 && (int)v % 4 == 0;  // grid dots
      else on = ((int)v % 4 == 0) || (((int)floorf(u) + 64) % 8 == 0);          // 80s grid
      if (on) px(x, y);
    }
  }
  for (int x = x0 + 1; x <= x1; x += 3) px(x, sy);  // level reference
}

// One panel: slope + rotated car silhouette
void drawTiltCar(bool side, float deg, uint32_t now) {
  int x0 = side ? 0 : 65, x1 = side ? 63 : W - 1;
  int sx = side ? 32 : 96, sy = 44;  // car base sits above the number box
  // Rotated in whole degrees (same as the number on screen), so the picture is perfectly straight when level
  float ang = roundf(constrain(deg, -45.0f, 45.0f)) * DEG_TO_RAD;
  u8g2.setClipWindow(x0, 0, x1 + 1, H);
  drawSlope(x0, x1, sx, sy, ang, now);
  if (theme == THEME_3D) snapScreen(snapC);
  // The car stands on the slope: bitmap bottom center = pivot point
  if (side) drawBitmapRot(JIMNY_SIDE, SIDE_W, SIDE_H, SIDE_W / 2.0f, SIDE_H, sx, sy, ang);
  else drawBitmapRot(JIMNY_REAR, REAR_W, REAR_H, REAR_W / 2.0f, REAR_H, sx, sy, ang);
  if (theme == THEME_3D) outlineNew(snapC, x0, x1);  // car becomes an outline (vector style)
  u8g2.setMaxClipWindow();
}

// Small label + MAX at the bottom left of the panel, on a black background
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

// Large degree number. Above 25 degrees: blinks with inverted colors.
void drawTiltNumber(int x, int base, bool alignRight, float deg, uint32_t now) {
  char buf[8];
  snprintf(buf, sizeof(buf), "%+d\xb0", (int)lroundf(deg));
  int w = u8g2.getStrWidth(buf);
  int asc = u8g2.getAscent();
  int x0 = alignRight ? x - w : x;
  bool blink = fabsf(deg) > 25 && (now / 250) % 2;
  u8g2.setDrawColor(blink ? 1 : 0);  // background: normally black, white while blinking
  box(x0 - 2, base - asc - 2, w + 4, asc + 4);
  u8g2.setDrawColor(blink ? 0 : 1);
  u8g2.drawStr(x0, base, buf);
  u8g2.setDrawColor(1);
}

// Small signed 7-segment number (80s theme)
void drawSeg7Number(int x, int y, bool alignRight, float deg, uint32_t now) {
  int v = (int)lroundf(deg);
  char digits[4];
  snprintf(digits, sizeof(digits), "%d", abs(v));
  int n = strlen(digits);
  const int DW = 9, DH = 16, GAP = 2;
  int w = 7 + n * (DW + GAP);
  int x0 = alignRight ? x - w : x;
  u8g2.setDrawColor(0);  // black background so the floor lines do not overlap it
  box(x0 - 2, y - 2, w + 4, DH + 4);
  u8g2.setDrawColor(1);
  if (fabsf(deg) > 25 && (now / 250) % 2) return;  // blink
  hline(x0, x0 + 4, y + DH / 2);                       // minus / plus sign
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
  if (theme == THEME_80S) vline(62, 0, H - 1);  // 80s-style double line

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
    hline(0, W - 1, (now / 20) % H);  // scan line
    u8g2.setDrawColor(1);
    if (calibratedAt && now - calibratedAt < 100) glitchFrame(0.4f, now / 30);
  }

  if (strcmp(st, tr(T_LIVE)) != 0) {  // status (calibrating, hold=zero, etc.) at the top center
    u8g2.setFont(u8g2_font_4x6_tr);
    int w = u8g2.getStrWidth(st);
    u8g2.setDrawColor(0);
    box(64 - w / 2 - 2, 0, w + 4, 8);
    u8g2.setDrawColor(1);
    u8g2.drawStr(64 - w / 2, 6, st);
  }
  drawDriveBadge(W - 13, 0);
}

// ---------- G-METER ----------
// The dot moves like a ball in a bowl: brake = up (body pushed forward),
// accelerate = down, turn left = right. Outer circle = 0.8 g, inner circle = 0.4 g.
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
  if (!mpuOk) {  // no sensor: demo data
    float t = now / 1000.0f;
    lon = 0.35f * sinf(t * 0.9f);
    lat = 0.45f * sinf(t * 1.3f + 1.0f);
  }
  const char* right = !mpuOk ? tr(T_DEMO) : (gResetAt && now - gResetAt < 1200 ? tr(T_MAX_RESET) : "g");
  drawHeader(tr(T_GMETER), right, now);

  const int CX = 32, CY = 38, R = 24;
  // Scale circles
  if (theme == THEME_80S) {
    circle(CX, CY, R);
    circle(CX, CY, R / 2);
    for (int k = 0; k < 8; k++) {  // radar spokes
      float a = k * PI / 4;
      line(CX + lroundf(cosf(a) * (R / 2 + 2)), CY + lroundf(sinf(a) * (R / 2 + 2)), CX + lroundf(cosf(a) * (R - 2)),
           CY + lroundf(sinf(a) * (R - 2)));
    }
  } else {
    circle(CX, CY, R);
    for (int k = 0; k < 24; k++) {  // dashed inner circle
      float a = k * TWO_PI / 24;
      px(CX + lroundf(cosf(a) * R / 2), CY + lroundf(sinf(a) * R / 2));
    }
    for (int d = -R + 3; d <= R - 3; d += 3) {
      px(CX + d, CY);
      px(CX, CY + d);
    }
    if (theme == THEME_CYBER) drawCorners(CX - R - 3, CY - R - 1, CX + R + 3, CY + R + 1, 4);
  }

  // Dot position (clamped inside the circle)
  float dx = lat / G_FULL, dy = lon / G_FULL;  // braking (negative lon) = dot moves up
  float d = sqrtf(dx * dx + dy * dy);
  if (d > 1) {
    dx /= d;
    dy /= d;
  }
  int bx = CX + lroundf(dx * (R - 3)), by = CY + lroundf(dy * (R - 3));
  if (now - gTrailAt > 70) {  // dot trail
    gTrailAt = now;
    gTrailX[gTrailPos] = bx - CX;
    gTrailY[gTrailPos] = by - CY;
    gTrailPos = (gTrailPos + 1) % G_TRAIL;
  }
  for (int i = 0; i < G_TRAIL; i++) px(CX + gTrailX[i], CY + gTrailY[i]);
  if (theme == THEME_80S) {
    box(bx - 2, by - 2, 5, 5);
  } else if (theme == THEME_3D) {  // ball with a shadow beneath it
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
    px(bx - 1, by - 1);  // highlight
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

  // Table: current and MAX
  bool showMax = mpuOk && biasReady;
  u8g2.setFont(u8g2_font_4x6_tr);
  u8g2.drawStr(108 - u8g2.getStrWidth(tr(T_NOW)), 22, tr(T_NOW));
  u8g2.drawStr(W - 1 - u8g2.getStrWidth("MAX"), 22, "MAX");
  hline(66, W - 1, 24);
  drawGRow(34, T_BRAKE, max(0.0f, -lon), maxBrake, showMax);
  drawGRow(45, T_ACCEL, max(0.0f, lon), maxAccel, showMax);
  drawGRow(56, T_TURN, fabsf(lat), maxTurn, showMax);
}

// ---------- 4WD (just for fun: the screen shifts, the car does not) ----------
// Left: transfer-case lever gate. Right: chassis seen from above; driven wheels are filled
// and driven shafts spin. Click = shift, with a "clunk" shake and a banner.
const char* const DRIVE_NAMES[DRIVE_COUNT] = { "2H", "4H", "4L" };
const uint32_t SHIFT_MS = 1600;  // shift animation, then back to the menu

// Driveshaft from y0 to y1; a moving dash pattern when it turns
void drawShaft(int x, int y0, int y1, bool turning, uint32_t now) {
  if (!turning) {
    for (int y = y0; y <= y1; y += 3) px(x, y);
    return;
  }
  int off = (now / 60) % 4;
  for (int y = y0; y <= y1; y++) {
    if ((y + off) % 4 != 0) {
      px(x - 1, y);
      px(x, y);
    }
  }
}

// Wheel seen from above. fill 0..1 = how far the drive has reached it (fills from the axle outward)
void drawWheel(int x, int y, float fill, bool frontAxleAbove) {
  const int WW = 7, WH = 11;
  frame(x, y, WW, WH);
  int fh = lroundf(fill * (WH - 2));
  if (fh <= 0) return;
  box(x + 1, frontAxleAbove ? y + WH - 1 - fh : y + 1, WW - 2, fh);
}

void draw4WD(uint32_t now) {
  drawHeader(tr(T_4WD), DRIVE_NAMES[drive], now);
  driveLever += (drivePick - driveLever) * 0.4f;
  uint32_t t = driveShiftAt ? now - driveShiftAt : SHIFT_MS;

  // Lever gate
  const int GX = 16, GY[DRIVE_COUNT] = { 22, 38, 54 };
  frame(GX - 3, GY[0] - 4, 7, GY[2] - GY[0] + 9);
  for (int i = 0; i < DRIVE_COUNT; i++) {
    hline(GX - 6, GX - 4, GY[i]);  // notch
    if (theme == THEME_80S) u8g2.setFont(FONT_80S);
    else u8g2.setFont(u8g2_font_helvB08_tr);
    int tx = GX + 9, ty = GY[i] + 4, tw = u8g2.getStrWidth(DRIVE_NAMES[i]);
    if (i == drive) {  // engaged: inverted label
      box(tx - 2, GY[i] - 6, tw + 4, 12);
      u8g2.setDrawColor(0);
      u8g2.drawStr(tx, ty, DRIVE_NAMES[i]);
      u8g2.setDrawColor(1);
    } else {
      u8g2.drawStr(tx, ty, DRIVE_NAMES[i]);
    }
  }
  float lp = driveLever;
  int ly = lroundf(GY[0] + lp * (GY[1] - GY[0]));
  u8g2.setDrawColor(0);
  disc(GX, ly, 6);
  u8g2.setDrawColor(1);
  disc(GX, ly, 5);
  u8g2.setDrawColor(0);
  circle(GX, ly, 2);  // knob top
  u8g2.setDrawColor(1);

  // Chassis from above (front at the top). The rear wheels are always driven.
  const int LX = 70, RX = 113, FY = 16, RY = 48, CX = 94;
  bool was4 = driveFrom != DRIVE_2H, is4 = drive != DRIVE_2H;
  float k = constrain(((int)t - 150) / 450.0f, 0.0f, 1.0f);  // front drive engaging/disengaging
  float front = was4 == is4 ? (is4 ? 1 : 0) : (is4 ? easeOutCubic(k) : 1 - easeOutCubic(k));
  hline(LX + 7, RX - 1, FY + 5);  // front axle
  hline(LX + 7, RX - 1, RY + 5);  // rear axle
  drawWheel(LX, FY, front, false);
  drawWheel(RX, FY, front, false);
  drawWheel(LX, RY, 1, true);
  drawWheel(RX, RY, 1, true);
  frame(CX - 5, 34, 11, 8);  // transfer case
  if (drive == DRIVE_4L) {
    u8g2.setFont(u8g2_font_4x6_tr);
    u8g2.drawStr(CX + 8, 41, "LOW");
  }
  drawShaft(CX, FY + 6, 33, front > 0.5f, now);
  drawShaft(CX, 42, RY + 4, true, now);
  if (theme == THEME_CYBER) drawCorners(66, 14, W - 1, H - 1, 4);

  // Shift: shake, then a banner
  if (driveShiftAt) {
    if (t < 220) {
      shiftAll((t / 40) % 2 ? 2 : -2);
      if (theme == THEME_CYBER) glitchFrame(0.35f, now / 25);
    }
    if (t > 450) {
      char msg[20];
      snprintf(msg, sizeof(msg), "%s %s", DRIVE_NAMES[drive], tr(T_ENGAGED));
      u8g2.setFont(theme == THEME_80S ? FONT_80S : u8g2_font_helvB08_tr);
      int w = u8g2.getStrWidth(msg) + 10;
      int x = 64 - w / 2;
      u8g2.setDrawColor(0);
      box(x - 2, 24, w + 4, 18);
      u8g2.setDrawColor(1);
      frame(x, 26, w, 14);
      if (theme == THEME_80S) frame(x - 2, 24, w + 4, 18);
      u8g2.drawStr(x + 5, 37, msg);
    }
  }
}

// Small "4H"/"4L" tag shown on other screens while 4WD is engaged
void drawDriveBadge(int x, int y) {
  if (drive == DRIVE_2H) return;
  u8g2.setFont(u8g2_font_4x6_tr);
  int w = u8g2.getStrWidth(DRIVE_NAMES[drive]);
  box(x, y, w + 4, 8);
  u8g2.setDrawColor(0);
  u8g2.drawStr(x + 2, y + 7, DRIVE_NAMES[drive]);
  u8g2.setDrawColor(1);
}

// ---------- RACE: endless winding road, 90s racing-game style. The goal is just to keep the Jimny on the road ----------
// Pseudo-3D: each screen row below the horizon is one distance ahead. The road center is shifted row by row
// by the track's curve at that distance, so bends appear ahead and sweep toward the car.
// Curves push the car outward; turn the knob to steer against it. Off the road the car shakes and slows down.
const TrackPart TRACK[] = {
  { 40, 0 }, { 30, 0.05f }, { 15, 0 }, { 35, -0.07f }, { 20, 0.03f }, { 25, -0.03f },
  { 30, 0.08f }, { 20, 0 }, { 25, -0.06f }, { 30, 0.04f }, { 15, -0.08f }, { 20, 0 },
};
const int RACE_HOR = 22;           // horizon row
const float RACE_ZK = 26;          // distance = RACE_ZK / rows below the horizon
const float RACE_CAMH = 0.65f;     // camera height: road half-width in pixels = rows below horizon / RACE_CAMH
const float RACE_VMAX = 12, RACE_VOFF = 4;  // top speed on the road and on the grass
const float RACE_STEER = 0.1f;     // road widths per knob step
const float RACE_PUSH = 0.6f;      // how hard curves push the car outward
const float RACE_BEND = 1.8f;      // how much bends are exaggerated on screen
const Beep SND_RUMBLE[] = { { 90, 40 } };  // wheels on the grass

// Curve at distance d along the track, eased between stretches
float trackCurve(float d) {
  static float total = 0;
  const int n = sizeof(TRACK) / sizeof(TRACK[0]);
  if (!total)
    for (int i = 0; i < n; i++) total += TRACK[i].len;
  d = fmodf(d, total);
  if (d < 0) d += total;
  for (int i = 0; i < n; i++) {
    if (d < TRACK[i].len) {
      float prev = TRACK[(i + n - 1) % n].curve, t = min(1.0f, d / 8.0f);
      return prev + (TRACK[i].curve - prev) * t * t * (3 - 2 * t);
    }
    d -= TRACK[i].len;
  }
  return 0;
}

void drawRace(uint32_t now) {
  float dt = min(0.1f, (now - raceLast) / 1000.0f);
  raceLast = now;
  bool off = fabsf(raceX) > 1.05f;
  bool started = now - raceStart > 900;  // "GO!" first
  float vTarget = !started ? 0 : off ? RACE_VOFF : RACE_VMAX;
  raceSpeed += (vTarget - raceSpeed) * min(1.0f, dt * (off ? 2.0f : 0.45f));
  float c0 = trackCurve(racePos);
  raceX = constrain(raceX - c0 * raceSpeed * RACE_PUSH * dt, -2.2f, 2.2f);
  racePos += raceSpeed * dt;
  raceBg += c0 * raceSpeed * dt * 30;
  raceLean *= 0.8f;
  if (off && raceSpeed > 2 && now - raceRumbleAt > 180) {
    raceRumbleAt = now;
    playSound(SND_RUMBLE, 1, false, now);
  }

  // Mountains on the horizon, scrolling sideways in bends
  for (int x = 0; x < W; x++) {
    float u = x + raceBg;
    int h = 7 + lroundf(4 * sinf(u * 0.07f) + 3 * sinf(u * 0.19f + 1) + 2 * sinf(u * 0.031f));
    px(x, RACE_HOR - h);
    for (int y = RACE_HOR - h + 2; y < RACE_HOR; y++)
      if (((x + y) & 3) == 0) px(x, y);
  }
  hline(0, W - 1, RACE_HOR);

  // Road, row by row from the car toward the horizon
  static int16_t rowX[H], rowW[H];
  float wx = 0, dwx = 0, zPrev = RACE_ZK / (H - 1 - RACE_HOR);
  for (int y = H - 1; y > RACE_HOR; y--) {
    float z = RACE_ZK / (y - RACE_HOR), dz = z - zPrev, s = (y - RACE_HOR) / RACE_CAMH;
    zPrev = z;
    dwx += trackCurve(racePos + z) * RACE_BEND * dz;
    wx += dwx * dz;
    int cx = 64 + lroundf((wx - raceX) * s), hw = lroundf(s);
    rowX[y] = cx;
    rowW[y] = hw;
    bool stripe = ((int)floorf((racePos + z) / 1.6f)) & 1;
    int rumble = max(1, hw / 7), mid = hw / 45;
    for (int x = 0; x < W; x++) {
      int ax = abs(x - cx);
      if (ax <= hw) {
        if ((ax > hw - rumble || ax <= mid) && stripe) px(x, y);  // red-white kerb and the center line
      } else if (stripe ? ((x + y) & 1) == 0 : ((x | y) & 1) == 0) {
        px(x, y);  // grass, in light and dark bands
      }
    }
  }

  // Roadside posts every 4 units, with a sign on the outside of bends
  for (int k = (int)ceilf(racePos / 4); k < racePos / 4 + 7; k++) {
    float z = k * 4 - racePos;
    if (z < 0.7f) continue;
    int y = RACE_HOR + lroundf(RACE_ZK / z);
    if (y >= H || y <= RACE_HOR) continue;
    float s = (y - RACE_HOR) / RACE_CAMH;
    int h = max(1, (int)lroundf(s * 0.5f));
    float cv = trackCurve(k * 4.0f);
    for (int side = -1; side <= 1; side += 2) {
      int x = rowX[y] + side * lroundf(s * 1.35f);
      vline(x, y - h, y);
      if (cv * side < -0.02f && h >= 3) {  // outside of the bend: chevron sign
        int sw = max(2, h / 2);
        box(x - sw, y - h - sw, sw * 2 + 1, sw);
      }
    }
  }

  // The Jimny (rear view from the blueprint), black outline first so it stands out
  int jy = H - 1 + (off && raceSpeed > 1 ? (int)((now / 50) % 2) : 0);
  float ang = raceLean * 0.04f;
  const float SC = 0.55f;
  u8g2.setDrawColor(0);
  for (int o = 0; o < 4; o++) drawBitmapScaled(JIMNY_REAR, REAR_W, REAR_H, REAR_W / 2.0f, REAR_H, 64 + (o & 1 ? 1 : -1), jy - (o & 2 ? 1 : 0), ang, SC);
  u8g2.setDrawColor(1);
  drawBitmapScaled(JIMNY_REAR, REAR_W, REAR_H, REAR_W / 2.0f, REAR_H, 64, jy, ang, SC);

  // Speed and distance
  char buf[16];
  u8g2.setFont(u8g2_font_5x7_tr);
  snprintf(buf, sizeof(buf), "%d", (int)lroundf(raceSpeed * 15));
  u8g2.setDrawColor(0);
  box(0, 0, 40, 8);
  box(W - 36, 0, 36, 8);
  u8g2.setDrawColor(1);
  u8g2.drawStr(1, 7, buf);
  u8g2.setFont(u8g2_font_4x6_tr);
  u8g2.drawStr(2 + 5 * strlen(buf), 7, "km/h");
  snprintf(buf, sizeof(buf), "%.1f km", racePos * 15 / 3600.0f);  // same scale as the km/h shown
  u8g2.setFont(u8g2_font_5x7_tr);
  u8g2.drawStr(W - 1 - u8g2.getStrWidth(buf), 7, buf);

  const char* msg = !started ? "READY" : now - raceStart < 1500 ? "GO!" : off && (now / 250) % 2 ? tr(T_OFFROAD) : nullptr;
  if (msg) {
    u8g2.setFont(u8g2_font_helvB08_tr);
    int w = u8g2.getStrWidth(msg);
    u8g2.setDrawColor(0);
    box(64 - w / 2 - 3, 9, w + 6, 12);
    u8g2.setDrawColor(1);
    u8g2.drawStr(64 - w / 2, 19, msg);
  }
}

// ---------- MUSIC: iPhone "Now Playing" over Bluetooth LE (Apple Media Service) ----------
// The dash advertises as "JIMNY DASH". After it is paired once in the iPhone's Bluetooth settings, the iPhone
// shares what is playing (title, artist, play state, position) and accepts play/pause/next/previous.
// There is no menu item: while an iPhone is connected, the clock screen becomes this Now Playing screen
// (big clock on top), and it goes back to the plain clock when the phone disconnects.
// The music keeps playing where it already plays (phone speaker, car radio); the dash only shows and controls it.
// In the Mac preview there is no Bluetooth: a demo track plays instead.
struct Music {
  bool connected = false;
  bool playing = false;
  char title[64] = "";
  char artist[48] = "";
  float elapsed = 0, duration = 0, rate = 1;  // seconds; rate 1 = normal speed
  uint32_t infoAt = 0;                         // millis() when 'elapsed' was reported
};
Music music;
portMUX_TYPE musicMux = portMUX_INITIALIZER_UNLOCKED;
enum { AMS_PLAY = 0, AMS_PAUSE = 1, AMS_TOGGLE = 2, AMS_NEXT = 3, AMS_PREV = 4, AMS_VOL_UP = 5, AMS_VOL_DOWN = 6 };

// Copy text from the phone, keeping only what our fonts can draw (ASCII); other letters become '?'
void musicCopyText(char* dst, size_t size, const uint8_t* src, size_t len) {
  size_t n = 0;
  for (size_t i = 0; i < len && n + 1 < size; i++) {
    uint8_t c = src[i];
    if (c < 0x80) dst[n++] = c >= 32 ? c : ' ';
    else if ((c & 0xC0) != 0x80) dst[n++] = '?';  // first byte of a multi-byte letter
  }
  dst[n] = 0;
}

#if defined(ESP_PLATFORM)
#include <BLEDevice.h>
#include <BLESecurity.h>

// Apple Media Service: the iPhone is the server, the dash reads from it over the same connection
struct Ams {
  static constexpr const char* SERVICE = "89D3502B-0F36-433A-8EF4-C502AD55F8DC";
  static constexpr const char* REMOTE = "9B3C81D8-57B1-4A8A-B8DF-0E56F7CA51C2";  // send play/pause/next
  static constexpr const char* UPDATE = "2F7CABCE-808D-411F-9A0C-BB92BA96C102";  // subscribe + receive updates
  static BLERemoteCharacteristic* remote;
  static BLEClient* client;
  static uint8_t peer[6];

  // Update = { entity, attribute, flags, value... }. Entity 0 = player, 2 = track.
  static void onUpdate(BLERemoteCharacteristic* c, uint8_t* d, size_t len, bool isNotify) {
    if (len < 3) return;
    uint8_t entity = d[0], attr = d[1];
    char buf[64];
    musicCopyText(buf, sizeof(buf), d + 3, len - 3);
    portENTER_CRITICAL(&musicMux);
    if (entity == 2 && attr == 0) strlcpy(music.artist, buf, sizeof(music.artist));
    if (entity == 2 && attr == 2) {
      strlcpy(music.title, buf, sizeof(music.title));
      music.elapsed = 0;
      music.infoAt = millis();
    }
    if (entity == 2 && attr == 3) music.duration = atof(buf);
    if (entity == 0 && attr == 1) {  // playback info: "state,rate,elapsed"; state 1 = playing
      float st = 0, rate = 0, el = 0;
      sscanf(buf, "%f,%f,%f", &st, &rate, &el);
      music.playing = st == 1;
      music.rate = rate > 0 ? rate : 1;
      music.elapsed = el;
      music.infoAt = millis();
    }
    portEXIT_CRITICAL(&musicMux);
  }

  // Runs once per connection (it waits on the radio, so not in the main loop)
  static void setupTask(void*) {
    vTaskDelay(pdMS_TO_TICKS(600));
    client = BLEDevice::createClient();
    if (client->connect(BLEAddress(peer))) {
      client->setMTU(185);  // long titles fit in one update
      BLERemoteService* svc = nullptr;
      for (int i = 0; i < 10 && !svc; i++) {  // the service shows up once pairing is done
        svc = client->getService(SERVICE);
        if (!svc) vTaskDelay(pdMS_TO_TICKS(1000));
      }
      BLERemoteCharacteristic* upd = svc ? svc->getCharacteristic(UPDATE) : nullptr;
      remote = svc ? svc->getCharacteristic(REMOTE) : nullptr;
      if (upd) {
        upd->registerForNotify(onUpdate);
        uint8_t track[] = { 2, 0, 2, 3 };  // track: artist, title, duration
        uint8_t player[] = { 0, 1 };       // player: playback info
        upd->writeValue(track, sizeof(track), true);
        upd->writeValue(player, sizeof(player), true);
        music.connected = true;
        Serial.println("Music: iPhone connected");
      } else {
        Serial.println("Music: no Apple Media Service (is this an iPhone, and paired?)");
      }
    }
    vTaskDelete(nullptr);
  }
};
BLERemoteCharacteristic* Ams::remote = nullptr;
BLEClient* Ams::client = nullptr;
uint8_t Ams::peer[6];

class AmsServerCallbacks : public BLEServerCallbacks {
  void onConnect(BLEServer* s, esp_ble_gatts_cb_param_t* p) override {
    memcpy(Ams::peer, p->connect.remote_bda, 6);
    Serial.println("Music: phone connected, asking for media info...");
    BLESecurity::startSecurity(p->connect.remote_bda);  // iPhone only shares media info over an encrypted link
    xTaskCreate(Ams::setupTask, "ams", 6144, nullptr, 1, nullptr);
  }
  void onDisconnect(BLEServer* s, esp_ble_gatts_cb_param_t* p) override {
    music.connected = false;
    Ams::remote = nullptr;
    Serial.println("Music: phone disconnected");
    BLEDevice::startAdvertising();  // ready for the next connection
  }
};

void musicBegin() {
  BLEDevice::init("JIMNY DASH");
  BLESecurity::setAuthenticationMode(true, false, true);  // bond, no PIN, secure connection
  BLESecurity::setCapability(ESP_IO_CAP_NONE);           // the iPhone just asks "Pair?"
  BLESecurity::setInitEncryptionKey(ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK);
  BLESecurity::setRespEncryptionKey(ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK);
  BLEServer* server = BLEDevice::createServer();
  server->setCallbacks(new AmsServerCallbacks());
  // Advertise that we want Apple Media Service; that is what makes the dash show up in the iPhone's Bluetooth list
  BLEAdvertisementData adv, scan;
  adv.setFlags(0x06);
  uint8_t solicit[18] = { 17, 0x15, 0xDC, 0xF8, 0x55, 0xAD, 0x02, 0xC5, 0xF4, 0x8E, 0x3A, 0x43, 0x36, 0x0F, 0x2B, 0x50, 0xD3, 0x89 };
  adv.addData((char*)solicit, sizeof(solicit));
  adv.setShortName("JIMNY");  // short name in the main packet (31-byte limit), full name in the scan response
  scan.setName("JIMNY DASH");
  BLEAdvertising* a = BLEDevice::getAdvertising();
  a->setAdvertisementData(adv);
  a->setScanResponseData(scan);
  BLEDevice::startAdvertising();
  Serial.printf("Music: Bluetooth ready, advertising as JIMNY DASH (%s)\n", BLEDevice::getAddress().toString().c_str());
}

void musicCommand(uint8_t cmd) {
  if (Ams::remote) Ams::remote->writeValue(cmd, true);
}

void musicUpdate(uint32_t now) {}
#else
// Mac preview: a demo track, so the screen can be designed without a phone
const char* const DEMO_TRACKS[][2] = { { "Hime Hime Suki Suki Daisuki", "Jimny Dash" }, { "Kira Kirarin", "Ikaru19" } };
int demoTrack = 0;

void musicBegin() {
  strlcpy(music.title, DEMO_TRACKS[0][0], sizeof(music.title));
  strlcpy(music.artist, DEMO_TRACKS[0][1], sizeof(music.artist));
  music.duration = 214;
  music.elapsed = 37;
  music.playing = true;
  music.infoAt = millis();
}

void musicUpdate(uint32_t now) {
  music.connected = now > 1500;  // "pairs" shortly after power-on
}

void musicCommand(uint8_t cmd) {
  uint32_t now = millis();
  float el = music.elapsed + (music.playing ? (now - music.infoAt) / 1000.0f : 0);
  if (cmd == AMS_TOGGLE) music.playing = !music.playing;
  if (cmd == AMS_NEXT || cmd == AMS_PREV) {
    demoTrack = (demoTrack + 1) % 2;
    strlcpy(music.title, DEMO_TRACKS[demoTrack][0], sizeof(music.title));
    strlcpy(music.artist, DEMO_TRACKS[demoTrack][1], sizeof(music.artist));
    el = 0;
  }
  music.elapsed = el;
  music.infoAt = now;
}
#endif

void drawMusic(uint32_t now) {
  Music m;
  portENTER_CRITICAL(&musicMux);
  m = music;
  portEXIT_CRITICAL(&musicMux);
  drawHeader(tr(T_MUSIC), m.connected ? "BT" : "--", now);

  // Big clock on the left, play/pause symbol on the right
  char hm[6];
  clockHHMM(hm);
  u8g2.setFont(u8g2_font_logisoso16_tn);
  u8g2.drawStr(2, 32, hm);
  int px0 = W - 16, py0 = 18;
  if (m.playing) {
    for (int i = 0; i <= 10; i++) vline(px0 + i, py0 + i * 6 / 10, py0 + 12 - i * 6 / 10);  // play triangle
  } else {
    box(px0, py0, 4, 13);
    box(px0 + 7, py0, 4, 13);
  }

  // Title: scrolls slowly when it is wider than the screen
  u8g2.setFont(u8g2_font_helvB08_tr);
  const char* title = m.title[0] ? m.title : "-";
  int tw = u8g2.getStrWidth(title);
  int tx = 2;
  if (tw > W - 4) {
    int span = tw + 30;
    tx = 2 - (int)((now / 40) % span);
    u8g2.setClipWindow(0, 34, W, 46);
    u8g2.drawStr(tx + span, 44, title);
  }
  u8g2.setClipWindow(0, 34, W, 46);
  u8g2.drawStr(tx, 44, title);
  u8g2.setMaxClipWindow();

  // Artist and time
  float el = m.elapsed + (m.playing ? (now - m.infoAt) / 1000.0f * m.rate : 0);
  if (m.duration > 0) el = min(el, m.duration);
  char t[16];
  snprintf(t, sizeof(t), "%d:%02d/%d:%02d", (int)el / 60, (int)el % 60, (int)m.duration / 60, (int)m.duration % 60);
  u8g2.setFont(u8g2_font_4x6_tr);
  int timeW = u8g2.getStrWidth(t);
  u8g2.drawStr(W - 1 - timeW, 54, t);
  u8g2.setFont(u8g2_font_5x7_tr);
  u8g2.setClipWindow(0, 46, W - timeW - 4, 56);
  u8g2.drawStr(2, 54, m.artist);
  u8g2.setMaxClipWindow();

  // Progress bar
  frame(2, 58, W - 4, 5);
  if (m.duration > 0) box(3, 59, lroundf((W - 6) * el / m.duration), 3);
  burnInShift();
}

// ---------- ABOUT (SETTINGS): someone pedalling a bike, "by ikaru19", and a song on loop ----------
// Knee position for a leg from hip (hx, hy) to foot (fx, fy), thigh and shin both 'len' long, knee pointing forward
void kneeAt(float hx, float hy, float fx, float fy, float len, float& kx, float& ky) {
  float dx = fx - hx, dy = fy - hy, d = sqrtf(dx * dx + dy * dy);
  d = constrain(d, 0.01f, len * 2 - 0.01f);
  float a = d / 2, h = sqrtf(max(0.0f, len * len - a * a));
  float mx = hx + dx * a / d, my = hy + dy * a / d;
  kx = mx + dy * h / d;  // the side toward the front of the bike
  ky = my - dx * h / d;
}

void drawBikeWheel(int cx, int cy, int r, float ang) {
  circle(cx, cy, r);
  circle(cx, cy, r - 1);
  for (int k = 0; k < 3; k++) {  // spokes turn with the wheel
    float a = ang + k * PI / 3;
    line(cx - lroundf(cosf(a) * (r - 2)), cy - lroundf(sinf(a) * (r - 2)), cx + lroundf(cosf(a) * (r - 2)), cy + lroundf(sinf(a) * (r - 2)));
  }
}

void drawAbout(uint32_t now) {
  drawHeader(tr(T_ABOUT), "v0.4", now);
  float t = now / 1000.0f;
  float crank = t * TWO_PI * 1.1f;  // pedalling speed

  // Ground moving past (the bike stays in the middle of the screen)
  const int GY = 52;
  hline(0, W - 1, GY);
  int off = (int)(t * 40) % 10;
  for (int x = -off; x < W; x += 10) hline(max(0, x), min(W - 1, x + 3), GY + 2);
  int hill = (int)(t * 8) % (W + 40);  // a small hill drifting slowly in the distance
  for (int x = 0; x < 30; x++) px(W + 10 - hill + x, GY - 6 + lroundf(fabsf(x - 15) * 0.4f));

  // Bike (facing right)
  const int RX = 46, FX = 80, WY = 45, WR = 7, CX = 61, CY = 46;
  drawBikeWheel(RX, WY, WR, crank * 2.2f);
  drawBikeWheel(FX, WY, WR, crank * 2.2f);
  line(RX, WY, CX, CY);   // chainstay
  line(RX, WY, 56, 33);   // seatstay
  line(56, 33, CX, CY);   // seat tube
  line(56, 33, 75, 32);   // top tube
  line(CX, CY, 75, 33);   // down tube
  line(75, 30, FX, WY);   // fork
  line(75, 30, 76, 27);   // stem
  hline(76, 79, 27);      // handlebar
  hline(53, 58, 32);      // saddle
  circle(CX, CY, 2);      // chainring

  // Rider: bobs a little with each pedal stroke
  float bob = sinf(crank * 2) * 0.5f;
  float hx = 56, hy = 30 + bob, sx = 66, sy = 21 + bob;
  line(lroundf(hx), lroundf(hy), lroundf(sx), lroundf(sy));  // back
  disc(lroundf(sx + 3), lroundf(sy - 4), 3);                  // head
  line(lroundf(sx), lroundf(sy), 72, lroundf(25 + bob));      // upper arm
  line(72, lroundf(25 + bob), 77, 27);                        // forearm to the handlebar
  for (int leg = 0; leg < 2; leg++) {
    float a = crank + leg * PI;
    float fx = CX + cosf(a) * 4, fy = CY + sinf(a) * 4, kx, ky;
    kneeAt(hx, hy, fx, fy, 9, kx, ky);
    line(lroundf(hx), lroundf(hy), lroundf(kx), lroundf(ky));
    line(lroundf(kx), lroundf(ky), lroundf(fx), lroundf(fy));
    hline(lroundf(fx) - 1, lroundf(fx) + 2, lroundf(fy));  // pedal
  }

  // Music notes floating up from the rider while the song plays
  for (int n = 0; n < 3; n++) {
    float p = fmodf(t * 0.6f + n / 3.0f, 1.0f);
    int nx = 74 + n * 9 + lroundf(sinf((t + n) * 3) * 2), ny = 30 - lroundf(p * 16);
    if (ny < 13) continue;
    disc(nx, ny, 1);
    vline(nx + 1, ny - 4, ny);
    px(nx + 2, ny - 4);
  }
  if (theme == THEME_CYBER) drawCorners(30, 12, 98, 56, 4);

  // Credit
  const char* by = "by ikaru19";
  u8g2.setFont(theme == THEME_80S ? FONT_80S : u8g2_font_helvB08_tr);
  u8g2.drawStr(64 - u8g2.getStrWidth(by) / 2, H - 1, by);
}

// ---------- NIGHT MODE (SETTINGS) ----------
void drawNight(uint32_t now) {
  bool saved = nightSavedAt && now - nightSavedAt < 1500;
  drawHeader(tr(T_NIGHT), nullptr, now);
  // Crescent moon
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

// ---------- TILT WARNING (on every screen) ----------
const float ROLL_WARN = 30, PITCH_WARN = 35;  // degrees; 3 degrees below this = safe again
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
    if (now - tiltSafeSince > 1000) tiltWarn = false;  // clears only after 1 second of being safe (no flicker at the limit)
  } else {
    tiltSafeSince = 0;
  }
}

// ---------- BUZZER ----------
// Sound = list of {tone Hz, duration ms}. 2700 Hz is roughly the loudest tone of a small passive buzzer.
// A single short beep uses tone()'s own duration (precise); sound sequences are stepped per frame (~50ms).
const Beep SND_CLICK[] = { { 2700, 18 } };                                        // knob click
const Beep SND_SHIFT[] = { { 180, 30 }, { 0, 70 }, { 140, 45 } };                 // 4WD lever "clunk-clunk"
const Beep SND_HOLD[] = { { 1500, 60 } };                                         // knob hold
const Beep SND_WARN[] = { { 3000, 150 }, { 0, 90 }, { 3000, 150 }, { 0, 500 } };  // tilt, repeated
// Boot jingle: short 80s-computer chiptune (original). A quick power-up sweep, five notes that spell
// out J-I-M-N-Y on screen, then a bright "ta-da". About 1.5 s.
const Beep SND_BOOT[] = {
  // { frequency (Hz), length (ms) }, { 0, gap (ms) }
  { 523, 40 }, { 659, 40 }, { 784, 40 }, { 1047, 40 }, { 0, 120 },  // power-up sweep C5 E5 G5 C6
  { 784, 110 }, { 0, 30 },                                         // J  G5
  { 1047, 110 }, { 0, 30 },                                        // I  C6
  { 1319, 110 }, { 0, 30 },                                        // M  E6
  { 1175, 110 }, { 0, 30 },                                        // N  D6
  { 1319, 110 }, { 0, 70 },                                        // Y  E6
  { 1568, 380 },                                                   // ta-da  G6
};
// ABOUT screen song, loops until you leave the screen
const Beep SND_ABOUT[] = {
  // { frequency (Hz), length (ms) }, { 0, gap (ms) }

  // Hime hime hime
  { 988, 380 }, { 0, 20 },  // B5, opening
  { 784, 180 }, { 0, 20 },  // G5
  { 880, 180 }, { 0, 20 },  // A5
  { 784, 580 }, { 0, 20 },  // G5
  { 880, 180 }, { 0, 20 },  // A5
  { 784, 580 }, { 0, 20 },  // G5

  // Suki suki daisuki
  { 784, 180 }, { 0, 20 },  // G5
  { 784, 180 }, { 0, 20 },  // G5
  { 740, 180 }, { 0, 20 },  // F#5
  { 784, 180 }, { 0, 20 },  // G5
  { 880, 180 }, { 0, 20 },  // A5
  { 988, 180 }, { 0, 20 },  // B5
  { 880, 180 }, { 0, 20 },  // A5
  { 784, 180 }, { 0, 20 },  // G5

  // Hime hime
  { 880, 180 }, { 0, 20 },  // A5
  { 784, 580 }, { 0, 20 },  // G5
  { 880, 180 }, { 0, 20 },  // A5
  { 784, 580 }, { 0, 20 },  // G5

  // Kira kirarin
  { 880, 180 }, { 0, 20 },  // A5
  { 784, 180 }, { 0, 20 },  // G5
  { 740, 180 }, { 0, 20 },  // F#5
  { 784, 180 }, { 0, 20 },  // G5
  { 880, 180 }, { 0, 20 },  // A5
  { 988, 380 }, { 0, 400 }, // B5, end of the phrase (longer gap before it loops)
};
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
  if (sndSeq == SND_ABOUT && (tiltWarn || current != SCR_ABOUT)) stopSound();  // song only on ABOUT, alarm first
  if (!sndSeq && tiltWarn) playSound(SND(SND_WARN), true, now);  // alarm keeps going while tilted
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
  if (current == SCR_TILT) {  // on the tilt screen: a blinking frame is enough
    if (flash) {
      frame(0, 0, W, H);
      frame(1, 1, W - 2, H - 2);
    }
    return;
  }
  // Other screens are fully covered, alternating colors
  u8g2.setDrawColor(flash ? 1 : 0);
  box(0, 0, W, H);
  u8g2.setDrawColor(flash ? 0 : 1);
  for (int x = -8; x < W + 8; x += 8) {  // diagonal hazard stripes at the top and bottom
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

// ---------- Theme picker (SETTINGS) ----------
// Small preview of each theme, so the difference is visible before saving
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
    for (int i = 0; i < 14; i++) px(x0 + 3 + (i * 37) % (x1 - x0 - 6), y0 + 3 + (i * 23) % (y1 - y0 - 6));  // stars
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

// One row of a choice list: text on the left, value on the right, inverted when selected.
// active = mark the option currently in use with a small box.
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

// SETTINGS: list of settings with their current values
const TextId SETTINGS_ITEMS[] = { T_LANGUAGE, T_THEME, T_BRIGHTNESS, T_NIGHT, T_SET_CLOCK, T_ABOUT, T_BACK };
const int SETTINGS_COUNT = sizeof(SETTINGS_ITEMS) / sizeof(SETTINGS_ITEMS[0]);

void drawSettings(uint32_t now) {
  drawHeader(tr(T_SETTINGS), nullptr, now);
  char hm[6];
  clockHHMM(hm);
  char pct[6];
  snprintf(pct, sizeof(pct), "%d%%", BRIGHT_PERCENT[bright]);
  char nb[6];
  const char* values[SETTINGS_COUNT] = { LANG_NAMES[lang], themeName(theme), pct, nightText(nightOpt, nb), hm, "v0.4", nullptr };
  // 6 rows fit; the list scrolls once the selection goes past them
  const int VISIBLE = (H - PICK_TOP) / PICK_ROW;
  int scroll = constrain(settingsPick - VISIBLE + 1, 0, max(0, SETTINGS_COUNT - VISIBLE));
  for (int i = 0; i < SETTINGS_COUNT; i++) {
    int y = PICK_TOP + (i - scroll) * PICK_ROW;
    if (y >= PICK_TOP && y + PICK_ROW <= H) drawPickRow(0, y, W, tr(SETTINGS_ITEMS[i]), values[i], i == settingsPick, false);
  }
}

// BRIGHTNESS: turn = change (the screen updates live), click = save, hold = cancel
void drawBrightness(uint32_t now) {
  bool saved = brightSavedAt && now - brightSavedAt < 1500;
  drawHeader(tr(T_BRIGHTNESS), nullptr, now);

  // Sun icon: the brighter it is, the longer the rays
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

  // 5-block bar
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

// SET CLOCK: turn = change the number, click = move to minutes, click again = save, hold = cancel
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
  if (!saved && (now / 300) % 2) box(setField == 0 ? x : xm, 50, dw, 2);  // blinking line under the number being edited

  u8g2.setFont(u8g2_font_5x7_tr);
  if (rtcOk) {
    u8g2.drawStr(x + (dw - u8g2.getStrWidth(tr(T_HOUR))) / 2, 61, tr(T_HOUR));
    u8g2.drawStr(xm + (dw - u8g2.getStrWidth(tr(T_MINUTE))) / 2, 61, tr(T_MINUTE));
  } else {
    u8g2.drawStr((W - u8g2.getStrWidth(tr(T_NO_RTC))) / 2, 61, tr(T_NO_RTC));  // the time only lasts until power off
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

// ---------- Anti burn-in (all clock screens) ----------
// The clock screen can stay up for hours without changing. OLED pixels that stay lit wear out over time
// and leave a permanent ghost image (burn-in). So the whole clock image is shifted by 1 pixel every
// minute, slowly cycling within a small box. The shift happens when the minute changes, so it goes unnoticed.
const int8_t SHIFT_X[8] = { 0, 1, 2, 1, 0, -1, -2, -1 };
const int8_t SHIFT_Y[8] = { 0, 1, 0, -1, 0, 1, 0, -1 };

// Shift the whole screen. Limited by the empty space at the edges, so the image is never cut off
// (e.g. header text flush right: shifting right is skipped, other shifts still apply).
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
  if (x1 < 0) return;  // empty screen
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

// ---------- Boot (same for every theme): an 80s computer starting up ----------
// CRT warms up, POST beep, BIOS checks the real hardware (sensor, clock), then the boot jingle (SND_BOOT)
// plays and the Suzuki and Jimny logos appear with it: the S on the sweep, one Jimny letter per note,
// SUZUKI on the final "ta-da". Click = skip.
const uint32_t BOOT_JINGLE_AT = 1500;  // jingle starts after the BIOS checks
const Beep SND_POST[] = { { 988, 160 } };  // the classic single "all OK" beep
// Step numbers in SND_BOOT: the first letter note, and the final note
const int BOOT_LETTER1 = 5, BOOT_FINAL = 15;
uint32_t bootDoneAt = 0;  // when the jingle finished (0 = not yet)

uint32_t bootDuration() {
  return bootDoneAt ? bootDoneAt - screenStart + 1000 : 8000;  // a moment to see the logo, or give up after 8 s
}

void drawStr80(int x, int y, const char* s) {
  u8g2.setFont(FONT_80S);
  u8g2.drawStr(x, y, s);
}

// Text on the left and a result on the right of one 8-pixel row
void bootRow(int row, const char* left, const char* right) {
  int y = 7 + row * 8;
  drawStr80(0, y, left);
  if (right) drawStr80(W - 1 - u8g2.getStrWidth(right), y, right);
}

void drawBootPC(uint32_t t) {
  static int stage = 0;
  uint32_t now = millis();
  if (t < 60) {
    stage = 0;
    bootDoneAt = 0;
  }
  if (stage == 0 && t >= 450) {
    playSound(SND(SND_POST), false, now);
    stage = 1;
  }
  if (stage == 1 && t >= BOOT_JINGLE_AT) {
    playSound(SND(SND_BOOT), false, now);
    stage = 2;
  }
  // How far the jingle is (step number); after it ends, past the last step
  int step = stage < 2 ? -1 : sndSeq == SND_BOOT ? sndIdx : 99;
  if (step == 99 && !bootDoneAt) bootDoneAt = now;

  if (t < 350) {  // CRT warms up, cursor blinking on a blank screen
    if ((t / 120) % 2) box(0, 5, 7, 2);
    crtEffect(1.0f - easeOutCubic(t / 350.0f));
    return;
  }
  if (step < 0) {  // BIOS checks
    bootRow(0, "JIMNY BIOS v0.4", nullptr);
    if (t >= 500) {  // memory test counting up
      char buf[17];
      snprintf(buf, sizeof(buf), "RAM %4dK", min(520, (int)((t - 500) * 520 / 500)));
      bootRow(1, buf, t >= 1000 ? "OK" : nullptr);
    }
    if (t >= 1050) bootRow(2, "MPU6050", t >= 1200 ? (mpuOk ? "OK" : "NONE") : nullptr);
    if (t >= 1200) bootRow(3, "RTC", t >= 1350 ? (rtcOk ? "OK" : "NONE") : nullptr);
    return;
  }

  // Power-up sweep: the Suzuki "S" is scanned in from the top, a bit per note
  const int SX = 64 - (LOGO_S_W + 4 + LOGO_SUZUKI_W) / 2, SY = 1;
  int sRows = step < BOOT_LETTER1 ? (step + 1) * LOGO_S_W / BOOT_LETTER1 : LOGO_S_W;
  drawRows(LOGO_S, LOGO_S_W, LOGO_S_W, SX, SY, LOGO_S_W, sRows);
  if (step < BOOT_LETTER1) {
    hline(SX - 4, SX + LOGO_S_W + 3, SY + sRows);  // scan line
    return;
  }
  // "Jimny": one letter per note
  int letters = constrain((step - BOOT_LETTER1) / 2 + 1, 0, 5);
  drawRows(LOGO_JIMNY, LOGO_JIMNY_W, LOGO_JIMNY_H, 64 - LOGO_JIMNY_W / 2, 32, LOGO_JIMNY_LETTER_END[letters - 1], LOGO_JIMNY_H);
  // "Ta-da": the SUZUKI wordmark next to the S, and a line under it all
  if (step >= BOOT_FINAL) {
    drawRows(LOGO_SUZUKI, LOGO_SUZUKI_W, LOGO_SUZUKI_H, SX + LOGO_S_W + 4, SY + (LOGO_S_W - LOGO_SUZUKI_H) / 2, LOGO_SUZUKI_W, LOGO_SUZUKI_H);
    hline(SX + LOGO_S_W + 4, SX + LOGO_S_W + 3 + LOGO_SUZUKI_W, SY + (LOGO_S_W + LOGO_SUZUKI_H) / 2 + 2);
  }
}

// ---------- Pick the drawing function for the theme ----------
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
  else if (tt < half) crtEffect(easeInOutCubic(tt / (float)half));  // TV switches off
  else crtEffect(1.0f - easeOutCubic((tt - half) / (float)half));   // then switches back on
}

// ---------- Screen transition (glitch gets worse, switch screen, then recover) ----------
void drawGlitchTransition(uint32_t tt, uint32_t now) {
  float a = 1.0f - fabsf((float)tt - TRANS_HALF) / TRANS_HALF;
  glitchFrame(a, now / 25);
}

void onEnter(Screen s, uint32_t now) {
  screenStart = now;
  if (s == SCR_ABOUT) playSound(SND(SND_ABOUT), true, now);
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
  if (s == SCR_RACE) {
    racePos = raceSpeed = raceX = raceLean = raceBg = 0;
    raceLast = raceStart = now;
  }
  if (s == SCR_4WD) {
    drivePick = drive;
    driveLever = drive;
    driveFrom = drive;
    driveShiftAt = 0;
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
  if (s == SCR_CLOCK && music.connected) s = SCR_MUSIC;  // phone connected: the clock screen shows what is playing
  nextScreen = s;
  transitioning = true;
  transStart = millis();
}

void drawScreen(Screen s, uint32_t now) {
  bool clean = theme == THEME_CLEAN, eighties = theme == THEME_80S, threeD = theme == THEME_3D;
  switch (s) {
    case SCR_BOOT:
      drawBootPC(now - screenStart);
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
      if (eighties) drawDriveBadge(0, 56);  // empty corners differ per theme
      else if (theme == THEME_CYBER) drawDriveBadge(50, 0);
      else drawDriveBadge(0, 0);
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
    case SCR_4WD: draw4WD(now); break;
    case SCR_RACE: drawRace(now); break;
    case SCR_ABOUT: drawAbout(now); break;
    case SCR_MUSIC: drawMusic(now); break;
  }
}

void handleInput(int steps, ButtonEvent btn, uint32_t now) {
  if (steps || btn != BTN_NONE) lastInputMs = now;
  if (tiltWarn && current != SCR_TILT) {  // tilt warning is showing: click = view tilt
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
      if (steps || btn == BTN_CLICK) goTo(SCR_MENU);  // turn or click = wake to the menu
      break;
    case SCR_GMETER:
      // Used while driving: does not switch to the clock on its own
      if (btn == BTN_CLICK) goTo(SCR_MENU);
      else if (steps || btn == BTN_HOLD) resetGMax(now);
      break;
    case SCR_MUSIC:
      // Turn = next / previous track, click = play/pause, hold = back to the menu. Stays on (it shows the clock).
      if (steps > 0) musicCommand(AMS_NEXT);
      else if (steps < 0) musicCommand(AMS_PREV);
      if (btn == BTN_CLICK) musicCommand(AMS_TOGGLE);
      else if (btn == BTN_HOLD) goTo(SCR_MENU);
      break;
    case SCR_ABOUT:
      if (btn != BTN_NONE) goTo(SCR_SETTINGS);  // the song keeps looping until you leave
      break;
    case SCR_RACE:
      if (steps) {
        raceX += steps * RACE_STEER;
        raceLean = constrain(raceLean + steps * 2, -6.0f, 6.0f);
      }
      if (btn != BTN_NONE) goTo(SCR_MENU);
      else if (idle) goTo(SCR_CLOCK);
      break;
    case SCR_4WD:
      if (driveShiftAt) {  // shifting: let the animation play, then back to the menu
        if (now - driveShiftAt > SHIFT_MS) goTo(SCR_MENU);
        break;
      }
      if (steps) drivePick = constrain(drivePick + steps, 0, DRIVE_COUNT - 1);
      if (btn == BTN_CLICK) {
        if (drivePick == drive) {
          goTo(SCR_MENU);  // nothing to shift
        } else {
          driveFrom = drive;
          drive = (DriveMode)drivePick;
          driveShiftAt = now;
          playSound(SND(SND_SHIFT), false, now);
        }
      } else if (btn == BTN_HOLD) {
        goTo(SCR_MENU);
      } else if (idle) {
        goTo(SCR_CLOCK);
      }
      break;
    case SCR_NIGHT:
      if (nightSavedAt) {  // already saved: show briefly, then go back
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
      // The tilt screen does not switch away on its own, because it is used while driving
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
      if (themeSavedAt) {  // just saved: show briefly, then go back to SETTINGS
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
        else if (item == T_ABOUT) goTo(SCR_ABOUT);
        else goTo(SCR_MENU);
      } else if (idle) {
        goTo(SCR_CLOCK);
      }
      break;
    case SCR_BRIGHT:
      if (brightSavedAt) {  // already saved: show briefly, then go back
        if (now - brightSavedAt > 700) goTo(SCR_SETTINGS);
        break;
      }
      if (steps) {
        brightPick = constrain(brightPick + steps, 0, BRIGHT_COUNT - 1);
        applyBrightness(brightPick);  // visible immediately
      }
      if (btn == BTN_CLICK) {
        saveBrightness(brightPick);
        brightSavedAt = now;
      } else if (btn == BTN_HOLD || idle) {
        applyBrightness(bright);  // cancel: revert to the saved brightness
        goTo(btn == BTN_HOLD ? SCR_SETTINGS : SCR_CLOCK);
      }
      break;
    case SCR_SET_CLOCK:
      if (clockSavedAt) {  // already saved: show briefly, then go back
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
        goTo(SCR_SETTINGS);  // hold = cancel
      } else if (idle) {
        goTo(SCR_CLOCK);
      }
      break;
    case SCR_LANG:
      if (langSavedAt) {  // text has switched language; show briefly, then go back
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

// Send the image to the screen 2 rows (16 pixels) at a time, reading the sensor in between.
// Sending 1 full screen takes ~50ms; without this the gyro is only read ~14x per second.
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
  pinMode(PIN_BUZZER, OUTPUT);  // silent at power-on (buzzer pin not floating)
  attachInterrupt(digitalPinToInterrupt(PIN_CLK), onEncoder, FALLING);
  attachInterrupt(digitalPinToInterrupt(PIN_SW), onButton, CHANGE);

  u8g2.setBusClock(400000);
  u8g2.begin();
  u8g2.setFontMode(1);  // transparent text, so it can be drawn over boxes

  mpuOk = mpuBegin();  // display and sensor share the same I2C bus
  Serial.println(mpuOk ? "MPU-6050: OK" : "MPU-6050: not found, using DEMO data");

  loadSettings();
  initClock();
  musicBegin();
  onEnter(SCR_BOOT, millis());
}

void loop() {
  uint32_t now = millis();
  pollSerialInput();
  // During a screen transition: knob turns are held (not dropped), but clicks are ignored
  // so a click meant for the old screen does not accidentally open something on the new one
  int steps = transitioning ? 0 : takeEncoderSteps();
  ButtonEvent btn = readButton(now);
  if (btn == BTN_NONE && serialBtn != BTN_NONE) btn = serialBtn;  // click/hold sent over USB
  serialBtn = BTN_NONE;
  if (transitioning) btn = BTN_NONE;
  if (btn == BTN_CLICK) playSound(SND(SND_CLICK), false, now);
  else if (btn == BTN_HOLD) playSound(SND(SND_HOLD), false, now);
  mpuUpdate();
  updateTiltWarning(now);
  updateSound(now);
  updateAutoBrightness();
  musicUpdate(now);
  if (!transitioning && current == SCR_CLOCK && music.connected) goTo(SCR_MUSIC);  // phone just connected
  if (!transitioning && current == SCR_MUSIC && !music.connected) goTo(SCR_CLOCK);  // phone went away
  if (!transitioning) handleInput(steps, btn, now);

  u8g2.clearBuffer();
  if (transitioning) {
    uint32_t tt = millis() - transStart;  // millis(), not now: transStart can be newer than now
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
