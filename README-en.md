# Jimny Retro Dash

**English** | [한국어](README.md)

A digital display for the Suzuki Jimny dashboard, running on an ESP32 with a 128×64 OLED.
Version v0.4: boot animation, menu, clock, inclinometer (MPU-6050), G-meter, buzzer feedback, and iPhone
"now playing", notifications, and calls over Bluetooth, with 4 themes:
**Clean** (default), **Cyberpunk**, **80s**, and **3D**, and 2 languages: **Indonesian** (default) and **English**.
Tested in a Mac preview and compiled for the ESP32.

## Workflow

| Goal | How | Time |
|---|---|---|
| **Final check in the ESP32 simulator** | `./build-esp32.sh`, then in VS Code: `Cmd+Shift+P` → **Wokwi: Start Simulator** | ~16 s |
| **Upload to a real ESP32** | `./build-esp32.sh upload` | |
| **Test one part at a time** | `./test.sh 01` … `./test.sh 05` (see [Component tests](#component-tests)) | |
| **Control the real dash from the Mac** | `./remote.py` (see [USB remote](#usb-remote)) | |

Both scripts use [arduino-cli](https://arduino.github.io/arduino-cli/) with the `esp32:esp32` core and the
`U8g2` library (`arduino-cli lib install U8g2`). `build-esp32.sh` uses the **Huge APP** partition scheme
(3 MB for the program, no OTA), because Bluetooth and the Korean/Japanese/Chinese fonts do not fit in the default 1.2 MB.

Day to day, the design work (animations, drawings, menus) happens in a Mac preview that rebuilds and
restarts about 2 seconds after each save of `sketch.ino`. The Mac preview (`preview.sh` and `desktop/`)
is **not published in this repo yet**.

### First time in Wokwi for VS Code
Wokwi needs a free license (personal use): `Cmd+Shift+P` → **Wokwi: Request a new License**,
then sign in in the browser. Only once.

### Files

| File | Contents |
|---|---|
| `sketch.ino` | Main code |
| `tests/` | 5 small test programs, one per component |
| `test.sh` | Uploads a test and opens the Serial Monitor |
| `build-esp32.sh` | Compile / upload for the ESP32 |
| `remote.py` | Control the dash from the Mac keyboard over USB |
| `diagram.json` | Circuit for Wokwi (online and VS Code) |
| `wokwi.toml` | Connects Wokwi for VS Code to the compiled firmware |
| `libraries.txt` | Libraries for Wokwi online |
| `README.md` | This README in Korean |

## Component tests

Build the prototype one part at a time and test each part before adding the next.
`./test.sh` lists the tests; `./test.sh 03` uploads test 03 and opens the Serial Monitor (exit with Ctrl+C).
`./test.sh monitor` opens only the Serial Monitor.

| Test | Checks | Expected result |
|---|---|---|
| `01-blink` | ESP32 is alive and accepts uploads | Blue LED blinks, "Hello from ESP32!" every second |
| `02-i2c-scan` | Devices on the I2C bus | `0x3C` = display, `0x69` = MPU-6050 (`0x68` means AD0 is not wired to 3V3) |
| `03-display` | Display | "HELLO JIMNY", a seconds counter, and a moving dot |
| `04-knob` | KY-040 knob | Number goes up/down when turning, click counter goes up when pressing |
| `05-tilt` | MPU-6050 | Roll and pitch in degrees, plus a horizon line that stays level |

## Run in Wokwi online

1. Open https://wokwi.com/projects/new/esp32
2. `sketch.ino` tab: delete its contents and paste `sketch.ino` from this repo.
3. `diagram.json` tab: delete its contents and paste `diagram.json`.
4. **Library Manager** tab > **+** button > search `U8g2` > click it.
5. Click the green ▶ button. Wait for the compile (the free queue can take 20–60 seconds).

Note: Wokwi only has the SSD1306 display, so swap the two display lines at the top of `sketch.ino`.
Wokwi online also uses the default 1.2 MB partition, which is now too small. If the compile fails,
use Wokwi for VS Code with `./build-esp32.sh` instead.

## Using the knob

Screen names below are in English. With the default language (Indonesian) they read
JAM, KEMIRINGAN, SETELAN, and so on.

| Action | Result |
|---|---|
| Turn | Move through the menu. On the clock screen: back to the menu |
| Click | Enter / back. During boot: skip the animation |
| Hold 1 second | On the TILT screen: the current position becomes zero ("ZERO SET") |
| Turn (on TILT) | Reset the recorded MAX angles ("MAX RESET") |
| On SETTINGS | Turn = choose LANGUAGE / THEME / BACK, click = open |
| On LANGUAGE or THEME | Turn = choose (themes show a small preview), click = save. Text/style changes right away. Choose BACK to cancel |
| On SET CLOCK | Turn = change hour, click = go to minutes, turn = change minutes, click = save to the RTC. Hold = cancel |
| On BRIGHTNESS | Turn = change (the display dims/brightens right away), click = save, hold = cancel |
| On NIGHT MODE | Turn = choose OFF / 20 / 40 / 60%, click = save, hold = cancel |
| On ABOUT | Click or hold = back to SETTINGS (the song stops) |
| On G-METER | Click = back to the menu. Turn or hold = reset MAX |
| On 4WD | Turn = move the lever (2H / 4H / 4L), click = shift. Click on the current mode or hold = back |
| On RACE | Turn = steer. Click or hold = back to the menu |
| During the TILT! warning | Click = go straight to the TILT screen |
| On MUSIC (something plays on the iPhone) | Turn right = next song, turn left = previous song, click = play/pause, hold = menu |
| On a notification | Turn = scroll by hand (stops the auto scroll), click = close (the next one shows), hold = close all |
| On an incoming call | Turn = choose DECLINE / ANSWER, click = do it on the phone, hold = hide (the phone keeps ringing) |

In Wokwi: click the small arrows next to the knob to turn it, click the middle of the knob to press,
and keep the click held to "hold".

## Screen flow

Each screen's style depends on the theme (see **Themes** below). The flow is the same:

1. **Boot** → menu. Click to skip.
2. **Menu**: **CLOCK**, **TILT**, **G-METER**, **4WD**, **RACE**, **BATTERY** (coming soon), **SETTINGS**.
3. **SETTINGS** → **LANGUAGE** (Indonesian / English), **THEME** (Clean / Cyberpunk / 80s / 3D),
   **BRIGHTNESS** (20–100%, 5 levels), **NIGHT MODE**, **SET CLOCK**, or **ABOUT**. After saving, it returns to SETTINGS.
   **ABOUT** shows someone pedalling a bike, "by ikaru19", and plays a song on loop until you leave.
   Language, theme, and brightness are stored in ESP32 memory; the time is stored in the RTC.
4. **Idle for 20 seconds** on the menu, a "coming soon" screen, or SETTINGS → switches to **CLOCK** automatically.
   The TILT screen never switches by itself, because it is used while driving.
5. **CLOCK**: big clock. Turn or click to go back to the menu.
   While something plays on the iPhone, the clock screen becomes the **MUSIC** screen (see below).
6. **TILT**: like a factory inclinometer. Left = **side view** (PITCH, front of the car facing left),
   right = **rear view** (ROLL). The Jimny silhouette is taken straight from an SJ410 hardtop blueprint
   (same body as the JA11) and rotated to the angle, on top of ground that tilts with it.
   Big numbers sit at the bottom right of each panel. The solid ground line = the ground under the car,
   the dotted line = a level floor for reference. ROLL and PITCH numbers stay big so they read at a glance;
   above 25° they blink. **MAX** = the largest angle since the last reset.
   Real data comes from the MPU-6050 (**LIVE** tag); without the sensor it shows moving sample data (**DEMO**).
   ROLL + = right side down, PITCH + = uphill.

## Tilt warning, G-meter, night mode

**Tilt warning:** if the car tilts more than **30° (roll)** or **35° (pitch)**, a big **TILT!** warning
appears on any screen (blinking, with hazard stripes) plus the worst angle.
Click to go straight to the TILT screen; on that screen the warning is a blinking frame.
The warning clears once the angle stays 3° below the limit for 1 second (so it does not flicker at the
limit). It is only active when the sensor is connected and the gyro is calibrated.

**G-meter** (G-METER menu): a dot moves like a ball in a bowl. Braking = dot moves up, accelerating = down,
turning left = right, turning right = left. Outer ring 0.8 g, inner ring 0.4 g. The table on the right
shows the current value and MAX for braking, accelerating, and turning. It is computed from the
accelerometer minus gravity (using the car's angle from the tilt sensor), then smoothed so engine
vibration does not get in the way. Note: long braking or accelerating can shift the angle slightly,
so the G value can be off by ~0.05 g.

**Night mode** (SETTINGS → NIGHT MODE): from **18:00 to 06:00** the display dims automatically to the
chosen level (default 40%). At night, brightness is **capped** at the night level, even if BRIGHTNESS is set
higher. Choose OFF to disable it.

**4WD (just for fun):** a transfer-case lever on the left and the chassis seen from above on the right.
Shifting to 4H or 4L gives a "clunk" shake, a low double click from the buzzer, and an "ENGAGED" banner;
the front wheels fill in and the front driveshaft starts turning. While 4H/4L is engaged, a small badge
shows on the clock and tilt screens. It is only a display: it is not connected to the car and resets to 2H
at power-on.

**RACE (mini game):** an endless winding road in the style of 90s racing games, with the Jimny rear view
from the SJ410 blueprint. The goal is just to stay on the road: bends push the car outward, so keep turning
the knob against them. On the grass the car shakes, slows down, and the buzzer rumbles. The top shows
speed and distance. The track is `TRACK` (list of `{length, curve}`); feel: `RACE_STEER`, `RACE_PUSH`,
`RACE_BEND`. In the Mac preview, hold ← / → to keep steering.

**Buzzer:** every knob click gives a short "beep" (2700 Hz, 18 ms); holding the knob gives a lower one
(1500 Hz, 60 ms). While the tilt warning is shown, a "beep-beep ... beep-beep" alarm keeps sounding until
the car is safe again. Clicks during a screen transition make no sound (those clicks are ignored).

## Bluetooth: iPhone now playing, notifications, calls

The dash shows up on the iPhone as **JIMNY DASH**. Pair it once in **Settings → Bluetooth** (tap "Pair",
no PIN). After that the iPhone reconnects by itself a few seconds after the dash turns on.

- **No extra menu.** While something plays on the iPhone, the idle screen (CLOCK) becomes **MUSIC**:
  clock, a music visualizer in the theme's style (Clean = bars, Cyberpunk = LED segments with falling peaks,
  80s = analog L/R VU needles, 3D = wireframe blocks), title, and artist. The visualizer is only for looks (the
  phone does not send the audio); it sinks flat when paused. The header shows the phone's battery
  (**PHONE 74%**). After a pause it stays 30 seconds (so the knob can resume), then the normal clock comes back.
  Same when the phone disconnects.
- **Knob:** turn = next / previous song, click = play/pause, hold = menu.
- **All letters:** titles and artists show Latin (with accents), Korean, Japanese, and Chinese letters.
  Each line picks its own 12 px font from the letters in it. Long lines scroll. Emoji and very rare letters are left out.
- **iPhone clock:** each time the iPhone connects, the dash sets its clock (and the RTC, if there is one) from the
  iPhone's time, down to the second. So the clock is right even without an RTC.
- **Notifications:** every new iPhone notification pops up on top of any screen with a "ding-dong": app name and
  time on top, then the sender and the text (all letters, like the song titles). A long one scrolls by itself
  (3 s wait, then one line every 2 s), so it can be read while driving. It closes by itself 20 seconds later.
  If more arrive, "+1" shows in the corner. Notifications already on the phone when it connects are not shown.
- **Calls:** the caller's name with **DECLINE / ANSWER** and a ring on the buzzer. The screen closes by itself
  when the call is answered or ends on the phone.
- If no notifications show up: on the iPhone, open **Settings → Bluetooth → JIMNY DASH (i)** and turn on
  **Share System Notifications**.
- Uses the Apple Media Service (AMS), Apple Notification Center Service (ANCS), Battery Service, and Current Time
  Service over Bluetooth Low Energy. Sound still plays
  from the phone or the car stereo; the dash only shows and controls it. Android is not supported yet.
- In the Mac preview, a fake phone "connects" after 1.5 seconds and plays demo songs in several languages,
  then sends two demo notifications (8 s, 26 s) and a call (40 s).

## USB remote

Handy while the knob is not wired yet or broken. With the ESP32 plugged into the Mac, run `./remote.py`
(standard Python, no extra packages). Close the Serial Monitor first.

| Key | Does |
|---|---|
| ← / → (or A / D) | Turn the knob |
| Space / Enter | Click |
| H | Hold |
| Q | Quit |

It also shows what the ESP32 prints. Any serial monitor works too: send `L`, `R`, `C`, or `H`.

## Themes

Choose in **SETTINGS**. The choice is stored in ESP32 memory (NVS), so it is remembered after the car is
turned off. Sensor, knob, screen flow, and the boot are the same for all themes. Only the drawing style differs.

**Boot (all themes):** an 80s computer starting up. The CRT warms up, a POST beep, and the BIOS counts memory and
checks the real sensor and clock (OK or NONE). Then a short chiptune jingle (`SND_BOOT`, ~1.5 s) plays and the screen
follows it note by note: the Suzuki "S" is scanned in on the power-up sweep, the classic "Jimny" wordmark appears one
letter per note, and SUZUKI appears on the final "ta-da" (~4 s in total). The logos are traced to pixels from
Wikimedia Commons (`LOGO_S`, `LOGO_SUZUKI`, `LOGO_JIMNY`). They are Suzuki trademarks, used here for a personal dash.

| | Clean (default) | Cyberpunk | 80s | 3D |
|---|---|---|---|---|
| Menu | Text list + small icons | HUD carousel | Double-line box + big arrows, IBM PC font | Spinning 3D wireframe models inside a targeting reticle, flying stars, a ship passes now and then |
| Clock | Plain big digits | Rolling digits + seconds bar | 7-segment, unlit segments shown faintly, blinking colon | Gently swaying 3D block digits, changing digits flip over, spinning cube colon |
| Tilt | Jimny silhouette + finely shaded ground | Jimny silhouette + road-striped ground + scan line | Jimny silhouette + grid ground + 7-segment numbers | Jimny outline (vector style) + dotted ground |
| Screen change | Slide | Glitch | CRT shrinks to the center | Hyperspace jump (screen zooms in + star lines) |

Clean is the default because it is the easiest to read at a glance while driving.
The 3D theme is styled after 80s vector arcade games: everything is drawn from 3D lines computed live
(rotated, given perspective, then connected), not from pre-made images. Menu models: desk clock,
gyroscope ball, G disc, battery, gear. The ship is an original design (X-wing style).

**Burn-in protection:** an OLED that shows the same image for hours can keep a permanent ghost image.
So in every theme, the clock screen shifts 1 pixel each minute (slowly circling within a ±2 pixel box),
exactly when the minute changes, so it is not noticeable. The shift is limited to the empty space at the
edges, so the image is never cut off. Shift pattern: `SHIFT_X` and `SHIFT_Y`.

## How the sensor computes tilt

Similar to a factory inclinometer (which uses the ESC/ABS sensors), but without wheel speed data:
1. The **gyro** (rotation speed) is integrated continuously: fast response, not fooled by braking/accelerating.
2. The **accelerometer** (direction of gravity) slowly corrects the gyro so it does not drift.
3. If the accelerometer does not match 1 g or the gyro, that means a bump, braking, or accelerating.
   The correction is reduced so no "fake tilt" appears.
4. Braking/accelerating/turning rarely lasts more than 2 seconds. If the accelerometer and gyro disagree
   for longer than that, the gyro is probably drifting, so the accelerometer is slowly trusted again.
   This stops the number from jumping far if the gyro is slightly wrong.
5. The gyro's zero error is learned automatically while the car is still (the first time during the boot
   animation). Until it is ready, the tag at the top of the screen reads **GYRO CAL**.

Test results in Wokwi:
- Quick tilt at 30°/second → the display follows right away, with no lag.
- Real tilt to 30° → the display shows 30.7° (sensor 30.5°).
- Hard braking at 0.4 g for 1.5 seconds → PITCH moves only 2.9°. Using the accelerometer alone: ~22°.

The sensor is read ~70 times per second. To do that, the image is sent to the display in parts and the
sensor is read in between.

## Try the sensor in Wokwi

While the simulation runs, click the MPU-6050 module. In Wokwi the **Acceleration** (direction of gravity)
and **Rotation** (gyro) sliders are separate, while in a real car both change together:
- Keep all **Rotation = 0** at the start. Otherwise the gyro cannot calibrate.
- Move only **Acceleration** (Y = tilt left/right, X = uphill/downhill): the number moves to the new angle
  within a few seconds, because a change without gyro rotation is treated as a push.
- **Rotation is rotation speed (degrees per second), not an angle.** In a real car the gyro only reads a
  value while the car is *moving* into a tilt, then goes back to 0 once it sits still at that tilt.
  Leaving the Rotation slider at a fixed number means "the car keeps rolling forever", which never happens.
  The result is a number stuck around **1.5 × the Rotation value** (example: Rotation 5 → about 7.5°),
  because the gyro and accelerometer pull against each other.
- The right way to fake a tilt with the gyro: move Rotation briefly (for example 30 for 1 second),
  then back to 0, while moving Acceleration to the target angle.
- To act like a real car, move both together.

| To simulate | Acc X | Acc Y | Acc Z |
|---|---|---|---|
| Level | 0 | 0 | 1 |
| Tilt right ~30° | 0 | 0.5 | 0.87 |
| Uphill ~20° | 0.34 | 0 | 0.94 |

Above 25° the number blinks as a warning.

## Installing in the car

- Sensor axes used by the code: **X toward the front of the car, Y to the left, Z up**.
  If the module is mounted another way, swap the axes in `mpuUpdate()`.
- After mounting, park on level ground, open TILT, and **hold the knob for 1 second**.
- Let the car sit still for a few seconds after power-on so the gyro can calibrate.
- Not yet included compared to the factory version: correction using wheel speed. Very long braking or
  accelerating (more than a few seconds) can still shift the number slightly.

## Customizing

- Idle time before switching to the clock: `IDLE_TO_CLOCK_MS` (20000 = 20 seconds).
- Menu items: the `MENU` array.
- Default theme and language for an ESP32 that has never been set: `THEME_CLEAN` and `LANG_ID` in `loadSettings()`.
- Brightness levels: `BRIGHT_PERCENT` (number on screen) and `BRIGHT_CONTRAST` (OLED "contrast" value 0–255).
  An OLED cannot go fully dark even at contrast 0; the dimmest level depends on the display chip.
- All text in both languages lives in one table, `TEXTS` (search for `// ---------- 2-language text`). New text:
  add a name to `enum TextId` and one row to `TEXTS` in the same order, then use `tr(T_NAME)`.
  English text is kept short so it fits in 128 pixels.
- Code for each theme: search for `// ---------- CLEAN theme` and `// ---------- 80S theme`. Cyberpunk uses the
  original functions (`drawMenu`, `drawClock`, `drawTilt`). The selector is in `drawScreen()`.
- Boot: `drawBootPC()`. The jingle is `SND_BOOT`; if you change it, update the step numbers `BOOT_LETTER1`
  (first letter note) and `BOOT_FINAL` (last note) so the screen stays in sync.
- Glitch strength (Cyberpunk): `glitchFrame(amount, seed)`, `amount` from 0 to 1.
- Tilt warning limits: `ROLL_WARN` and `PITCH_WARN` (degrees).
- Night mode hours: `NIGHT_START_H` and `NIGHT_END_H`. Night level options: `NIGHT_OPTIONS`.
- G-meter scale: `G_FULL` (g at the outer ring).
- Buzzer sounds: `SND_CLICK`, `SND_HOLD`, `SND_WARN`, and the ABOUT song `SND_ABOUT` (list of `{tone Hz, length ms}`, tone 0 = silence).
  Buzzer pin: `PIN_BUZZER`.
- Jimny drawings on the tilt screen: `JIMNY_SIDE` (60×30) and `JIMNY_REAR` (40×41), one text line per
  pixel row (`#` = on). They can be edited pixel by pixel. Source: SJ410 hardtop blueprint from
  getoutlines.com (Creative Commons license).

## Hardware notes

- The code uses the **SH1106** chip, which most 1.3" OLED displays have. For a 0.96" SSD1306 display
  (or Wokwi), swap the two display lines at the top of `sketch.ino`.
- For a yellow look, buy the **yellow** OLED version. 2.42" size (SSD1309 chip):
  `U8G2_SSD1309_128X64_NONAME0_F_HW_I2C u8g2(U8G2_R0, U8X8_PIN_NONE);`
- The display, MPU-6050, and RTC share the same I2C bus (SDA 21, SCL 22). Their addresses must differ:
  display 0x3C, RTC 0x68, MPU-6050 **0x69**. Because the MPU-6050 also defaults to 0x68, **the MPU-6050 AD0
  pin must be wired to 3V3** to move it to 0x69. If you forget, the sensor and the clock clash.

### Buzzer
- Use a **PASSIVE** buzzer (can play different tones). An active buzzer plays only one tone and will sound odd.
  How to tell: an active buzzer usually has a sticker and a sealed bottom; a passive buzzer shows a green
  board underneath. Or connect it to 3V3: an active buzzer beeps right away, a passive one only clicks.
- For a desk prototype, a small piezo buzzer can go straight to GPIO 25 through a 100Ω resistor.
- In the car (loud enough over the engine), use an NPN transistor (S8050 / 2N2222):
  GPIO 25 → 1kΩ resistor → base; emitter to GND; buzzer between 3V3/5V and the collector; plus a reversed
  1N4148 diode across the buzzer if it is a magnetic type (not piezo).
- Wokwi uses `wokwi-buzzer`, and the sound plays in the browser.

### Clock (RTC DS3231, ZS-042 module)
- The time is read from the RTC at power-on, then synced again every minute. If no RTC is found, the clock
  starts from the compile time, and the SET CLOCK screen shows "NO RTC".
  When an iPhone connects over Bluetooth, the clock and the RTC are set from the iPhone's time.
- **Coin cell:** the ZS-042 module has a charging circuit for rechargeable cells. Use an **LIR2032**.
  If you use a **CR2032** (not rechargeable), **remove the 200Ω resistor** (or its diode) on the module,
  so the CR2032 is not "charged" (it can leak or swell).
- **Power:** the coin cell already keeps the clock running while the car is off (the DS3231 needs only ~3 µA).
  Recommended: power the module's VCC from the **ESP32 3V3**, like the other parts. If the RTC's VCC stays
  powered from BATT+ while the ESP32 is off, current can leak through the module's I2C pull-up resistors into
  the unpowered ESP32. If you still want BATT+ (12V), a regulator down to 3.3V is required, and the module's
  I2C pull-ups should be removed.
- Mount the module somewhere easy to reach, so the coin cell can be replaced.
- Wokwi has no DS3231, so `diagram.json` uses a **DS1307**. The time registers are the same, so the code is
  the same. Wokwi fills the DS1307 clock with the computer's time when the simulation starts.

## Wiring

| Part | Pin | ESP32 |
|---|---|---|
| OLED | SDA | GPIO 21 |
| OLED | SCL | GPIO 22 |
| OLED | VCC / GND | 3V3 / GND |
| MPU-6050 | SDA | GPIO 21 |
| MPU-6050 | SCL | GPIO 22 |
| MPU-6050 | VCC / GND | 3V3 / GND |
| MPU-6050 | **AD0** | **3V3** (address becomes 0x69) |
| RTC DS3231 | SDA | GPIO 21 |
| RTC DS3231 | SCL | GPIO 22 |
| RTC DS3231 | VCC / GND | 3V3 / GND (see the power note above) |
| KY-040 | CLK | GPIO 18 |
| KY-040 | DT | GPIO 19 |
| KY-040 | SW | GPIO 5 |
| KY-040 | + / GND | 3V3 / GND |
| Passive buzzer | + / − | GPIO 25 (through a resistor/transistor) / GND |
