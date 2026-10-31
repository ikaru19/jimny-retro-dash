# Jimny Cyber Dash

Layar digital untuk dashboard Suzuki Jimny.
Versi v0.4: animasi boot, menu, jam, dan inclinometer (MPU-6050), dengan 3 tema:
**Clean** (bawaan), **Cyberpunk**, dan **80-an**, serta 2 bahasa: **Indonesia** (bawaan) dan **English**. Sudah dites di preview Mac, dan sudah berhasil di-compile untuk ESP32.

## Cara kerja paling enak (di Mac)

Buka folder ini di VS Code, lalu taruh jendela preview di samping kode.

| Mau apa | Caranya | Waktu |
|---|---|---|
| **Desain** (animasi, gambar, menu) | VS Code: `Cmd+Shift+P` → *Run Task* → **Preview Mac (auto)**. Atau di terminal: `./preview.sh watch menu` | ~2 detik setelah simpan |
| **Cek akhir di simulator ESP32** | `Cmd+Shift+B` (Build ESP32), lalu `Cmd+Shift+P` → **Wokwi: Start Simulator** | ~16 detik |
| **Upload ke ESP32 asli** | *Run Task* → **Upload ke ESP32 asli** | |

Alur hariannya:
1. Jalankan **Preview Mac (auto)** sekali. Jendela OLED kuning muncul dan selalu di atas.
2. Edit `sketch.ino` lalu simpan (`Cmd+S`). Preview build ulang dan restart sendiri.
   Kalau ada error, preview lama tetap jalan dan errornya muncul di tab *Problems* VS Code.
3. Sesekali, atau sebelum beli/pasang hardware, cek di **Wokwi VS Code**. Simulatornya sama
   dengan Wokwi online, tapi compile di Mac, jadi tidak antre.

Mulai dari layar tertentu (supaya tidak menunggu animasi boot):
`./preview.sh watch kemiringan` (pilihan: `boot`, `menu`, `jam`, `kemiringan`, `gmeter`, `setelan`).

**Tes animasi boot per tema:**
- Dari perintah, tambahkan nama tema: `./preview.sh watch boot clean`, `./preview.sh watch boot cyber`,
  atau `./preview.sh watch boot 80an` (tema: `clean`, `cyber`, `80an`, `3d`). Temanya sekalian tersimpan.
- Atau di jendela preview: ganti tema di SETELAN → TEMA, lalu tekan **K**. Program diulang dari awal,
  seperti ACC dimatikan lalu dinyalakan, jadi animasi boot tema itu tampil. Klik knob saat boot = lewati.

Tema, bahasa, dan kecerahan tersimpan di `desktop/build/nvs.txt` (tiruan memori ESP32). Jam yang disetel lewat ATUR JAM
disimpan di `desktop/build/rtc.txt` (tiruan RTC; selisih dari jam Mac). Hapus file itu untuk kembali ke bawaan.
Coba tanpa RTC: `./desktop/build/jimny-preview --no-rtc` (jalankan dari folder `desktop`).

### Tombol di jendela preview

| Tombol | Fungsi |
|---|---|
| ← / → atau scroll mouse | Putar knob |
| Spasi / Enter | Klik knob (tahan = tahan) |
| A / D | Mobil miring kiri / kanan (25°/detik selama ditahan) |
| W / S | Mobil nanjak / turun |
| R | Mobil kembali datar |
| B / G | Rem / gas (selama ditahan) |
| Z / X | Belok kiri / kanan (selama ditahan, untuk G-meter) |
| J | Jalan rusak 2 detik |
| K | Boot ulang (ACC mati lalu nyala lagi). Setelan dan jam tetap tersimpan |
| P | Screenshot ke `desktop/build/` |
| T | Selalu di atas: on/off |
| M | Buzzer bisu: on/off (suara buzzer keluar dari speaker Mac) |
| Q / Esc | Keluar |

Sensor di preview adalah MPU-6050 virtual. Gyro dan accelerometernya bergerak bersamaan
seperti di mobil asli, lengkap dengan sedikit bias dan getaran mesin. Jadi tidak ada
masalah slider "Rotation" seperti di Wokwi.

**Batas preview Mac:** ini cuma tampilan dan logika. Tidak mengetes kabel, chip ESP32, atau
I2C asli. Kecepatan kirim gambar ke layar memang ditiru (~50ms per frame), tapi tetap cek
di Wokwi atau hardware asli sebelum dipasang.

### Pertama kali di Wokwi VS Code
Wokwi minta lisensi gratis (pemakaian pribadi): `Cmd+Shift+P` → **Wokwi: Request a new License**,
lalu login di browser. Cukup sekali.

### Isi folder

| File | Isi |
|---|---|
| `sketch.ino` | Kode utama, dipakai semua cara di atas |
| `diagram.json` | Rangkaian untuk Wokwi (online dan VS Code) |
| `wokwi.toml` | Penghubung Wokwi VS Code ke hasil compile |
| `preview.sh` | Preview Mac |
| `build-esp32.sh` | Compile / upload ESP32 |
| `desktop/` | Isi preview Mac: tiruan Arduino, layar, knob, MPU-6050 dan RTC virtual |
| `arsip/sketch-dengan-wifi.ino` | Versi dengan fitur WiFi (setel lewat HP + QR + jam internet). Disimpan untuk nanti, tidak dipakai di MVP |
| `arsip/sketch-kemiringan-2d.ino` | Versi kemiringan lama (gambar 2D poligon, jarum 80-an) |
| `arsip/sketch-kemiringan-3d.ino` | Percobaan kemiringan 3D (kurang mirip Jimny di layar sekecil ini) |

## Jalankan di Wokwi online

1. Buka https://wokwi.com/projects/new/esp32
2. Tab `sketch.ino`: hapus isinya, tempel isi `sketch.ino` dari folder ini.
3. Tab `diagram.json`: hapus isinya, tempel isi `diagram.json`.
4. Tab **Library Manager** > tombol **+** > cari `U8g2` > klik.
5. Klik tombol hijau ▶. Tunggu compile (antrean gratis bisa 20-60 detik).

## Cara pakai knob

| Aksi | Hasil |
|---|---|
| Putar | Pindah menu. Di layar jam: kembali ke menu |
| Klik | Masuk / kembali. Saat boot: lewati animasi |
| Tahan 1 detik | Di layar KEMIRINGAN: posisi sekarang jadi titik nol (muncul "NOL DISET") |
| Putar (di KEMIRINGAN) | Reset rekaman sudut MAX (muncul "MAX DIRESET") |
| Di SETELAN | Putar = pilih BAHASA / TEMA / KEMBALI, klik = buka |
| Di BAHASA atau TEMA | Putar = pilih (tema ada contoh kecilnya), klik = simpan. Teks/gaya langsung berubah. Pilih KEMBALI untuk batal |
| Di ATUR JAM | Putar = ubah jam, klik = pindah ke menit, putar = ubah menit, klik = simpan ke RTC. Tahan = batal |
| Di KECERAHAN | Putar = ubah (layar langsung ikut redup/terang), klik = simpan, tahan = batal |
| Di MODE MALAM | Putar = pilih MATI / 20 / 40 / 60%, klik = simpan, tahan = batal |
| Di G-METER | Klik = kembali ke menu. Putar atau tahan = reset MAX |
| Saat peringatan MIRING! | Klik = langsung ke layar KEMIRINGAN |

Di Wokwi: klik panah kecil di samping knob untuk memutar, klik tengah knob untuk tombol,
tahan klik untuk "tahan".

## Alur layar

Gaya tiap layar tergantung tema (lihat bagian **Tema** di bawah). Alurnya sama:

1. **Boot** → menu. Klik untuk melewati.
2. **Menu**: **JAM**, **KEMIRINGAN**, **G-METER**, **VOLT AKI** (segera hadir), **SETELAN**.
   Dalam bahasa Inggris: CLOCK, TILT, G-METER, BATTERY, SETTINGS.
3. **SETELAN** → **BAHASA** (Indonesia / English), **TEMA** (Clean / Cyberpunk / 80-an),
   **KECERAHAN** (20–100%, 5 tingkat), **MODE MALAM**, atau **ATUR JAM**. Setelah disimpan, kembali ke SETELAN.
   Bahasa, tema, dan kecerahan disimpan di memori ESP32; jam disimpan di RTC.
4. **Diam 20 detik** di menu, layar "segera hadir", atau SETELAN → otomatis pindah ke **JAM**.
   Layar KEMIRINGAN tidak pindah sendiri, karena dipakai saat menyetir.
5. **JAM**: jam besar. Putar atau klik untuk kembali ke menu.
6. **KEMIRINGAN**: seperti inclinometer bawaan mobil. Kiri = **tampak samping** (PITCH, depan mobil
   menghadap kiri), kanan = **tampak belakang** (ROLL). Siluet Jimny diambil langsung dari blueprint
   SJ410 hardtop (bodi sama dengan JA11) dan diputar sesuai sudut, di atas lereng tanah yang ikut miring.
   Angka besar di kanan-bawah tiap panel. Garis lantai tegas = tanah di
   bawah mobil, titik-titik = lantai datar sebagai acuan. Angka ROLL (kiri) dan PITCH (kanan) tetap
   besar supaya terbaca sekilas; lebih dari 25° angkanya berkedip. **MAX** = sudut terbesar sejak reset.
   Data asli dari MPU-6050 (tanda **LIVE**); tanpa sensor pakai data bergerak sendiri (**DEMO**).
   ROLL + = sisi kanan turun, PITCH + = nanjak.

## Peringatan miring, G-meter, mode malam

**Peringatan miring:** kalau mobil miring lebih dari **30° (roll)** atau **35° (pitch)**, di layar apa
pun muncul peringatan besar **MIRING!** (berkedip, dengan garis bahaya) plus sudut yang paling parah.
Klik untuk langsung ke layar KEMIRINGAN; di layar itu peringatannya berupa bingkai berkedip.
Peringatan hilang setelah sudut turun 3° di bawah batas selama 1 detik (supaya tidak berkedip-kedip
di batas). Hanya aktif kalau sensor tersambung dan gyro sudah terkalibrasi.

**G-meter** (menu G-METER): titik bergerak seperti bola di mangkuk. Rem = titik naik, gas = turun,
belok kiri = ke kanan, belok kanan = ke kiri. Lingkaran luar 0.8 g, lingkaran dalam 0.4 g. Tabel di
kanan: nilai sekarang dan MAX untuk rem, gas, dan belok. Dihitung dari accelerometer dikurangi gravitasi
(memakai sudut mobil dari sensor kemiringan), lalu diperhalus supaya getaran mesin tidak mengganggu.
Catatan: rem atau gas yang lama bisa menggeser sudut sedikit, jadi angka G bisa meleset ~0.05 g.

**Mode malam** (SETELAN → MODE MALAM): jam **18:00–06:00** layar otomatis redup ke tingkat yang
dipilih (bawaan 40%). Di malam hari, kecerahan tampil **paling tinggi** sebesar tingkat malam, walau
setelan KECERAHAN lebih terang. Pilih MATI untuk mematikan.

**Buzzer:** setiap klik knob bunyi "tit" pendek (2700 Hz, 18 ms), tahan knob bunyi lebih rendah
(1500 Hz, 60 ms). Selama peringatan miring tampil, alarm "tit-tit ... tit-tit" bunyi terus sampai mobil
aman lagi. Klik saat dalam putaran animasi pindah layar tidak bunyi (kliknya memang diabaikan).
Di preview Mac suaranya keluar dari speaker; tekan **M** untuk bisu. Tes tanpa jendela bisa
menulis setiap bunyi ke terminal dengan `--log-sound`.

## Tema

Pilih di **SETELAN**. Pilihan disimpan di memori ESP32 (NVS), jadi tetap diingat walau mobil dimatikan.
Sensor, knob, dan alur layar sama untuk semua tema. Yang beda cuma gaya gambarnya.

| | Clean (bawaan) | Cyberpunk | 80-an | 3D |
|---|---|---|---|---|
| Boot | Logo JIMNY muncul pelan (~2 dtk) | Sinyal rusak, hujan kode, kota neon (~9 dtk) | TV menyala, matahari bergaris terbit, SUZUKI krom (~4 dtk) | Keluar hyperspace, pesawat datang berguling lalu lewat, tulisan JIMNY 3D (~4 dtk) |
| Menu | Daftar teks + ikon kecil | Carousel HUD | Kotak bergaris ganda + panah besar, font PC IBM | Model kawat 3D berputar di dalam bidikan, bintang terbang, pesawat lewat sesekali |
| Jam | Angka besar polos | Angka gulung + bar detik | 7-segmen, segmen mati tampil samar, titik dua berkedip | Angka balok 3D bergoyang pelan, angka yang berganti berputar balik (flip), titik dua kubus berputar |
| Kemiringan | Siluet Jimny + tanah diarsir halus | Siluet Jimny + tanah bergaris jalan + garis scan | Siluet Jimny + tanah grid + angka 7-segmen | Garis tepi Jimny (gaya vektor) + tanah titik-titik |
| Pindah layar | Geser | Glitch | TV tabung menyusut ke tengah | Lompat hyperspace (layar membesar + garis bintang) |

Clean jadi bawaan karena paling mudah dibaca sekilas saat menyetir.
Tema 3D bergaya game arcade vektor 80-an: semua digambar dari garis 3D yang dihitung langsung
(diputar, diberi perspektif, lalu disambung), bukan gambar jadi. Model menu: jam meja, bola giroskop,
piringan G, aki, roda gigi. Pesawatnya desain sendiri (sayap X).

**Anti burn-in:** OLED yang menampilkan gambar sama berjam-jam bisa meninggalkan bayangan permanen.
Jadi di semua tema, layar jam digeser 1 piksel tiap menit (berputar pelan dalam kotak ±2 piksel),
tepat saat angka menit berganti, jadi tidak terasa. Geseran dibatasi ruang kosong di pinggir, jadi
gambar tidak pernah terpotong. Pola geser: `SHIFT_X` dan `SHIFT_Y`.

## Cara sensor menghitung miring

Mirip inclinometer pabrikan (yang memakai sensor sistem ESC/ABS), tapi tanpa data kecepatan roda:
1. **Gyro** (kecepatan putar) dijumlahkan terus: respons cepat, tidak tertipu rem/gas.
2. **Accelerometer** (arah gravitasi) pelan-pelan mengoreksi supaya gyro tidak melenceng (drift).
3. Kalau accelerometer tidak cocok dengan 1g atau dengan gyro, itu tandanya ada guncangan, rem,
   atau gas. Koreksinya dikurangi supaya tidak muncul "miring palsu".
4. Rem/gas/belok jarang lebih dari 2 detik. Kalau accelerometer dan gyro tidak cocok lebih lama
   dari itu, kemungkinan gyro yang melenceng, jadi accelerometer dipercaya lagi pelan-pelan.
   Ini mencegah angka melonjak jauh kalau gyro salah sedikit.
5. Kesalahan nol gyro dipelajari otomatis saat mobil diam (pertama kali saat animasi boot).
   Selama belum siap, tanda di atas layar tertulis **KALIBRASI GYRO**.

Hasil tes di Wokwi:
- Miring cepat 30°/detik → layar langsung ikut, tanpa jeda.
- Miring sungguhan ke 30° → layar menunjukkan 30.7° (sensor 30.5°).
- Rem keras 0.4g selama 1.5 detik → PITCH cuma bergeser 2.9°. Kalau pakai accelerometer saja: ~22°.

Sensor dibaca ~70x per detik. Caranya, gambar dikirim ke layar per bagian dan sensor dibaca di sela-selanya.

## Coba sensor di Wokwi

Saat simulasi jalan, klik modul MPU-6050. Di Wokwi slider **Acceleration** (arah gravitasi) dan
**Rotation** (gyro) terpisah, padahal di mobil asli keduanya berubah bersamaan:
- Biarkan semua **Rotation = 0** saat mulai. Kalau tidak, gyro tidak bisa kalibrasi.
- Geser **Acceleration** saja (Y = miring kiri/kanan, X = nanjak/turun): angka bergerak
  ke sudut baru dalam beberapa detik, karena perubahan tanpa putaran gyro dianggap dorongan.
- **Rotation itu kecepatan putar (derajat per detik), bukan sudut.** Di mobil asli, gyro cuma
  bernilai saat mobil *sedang* bergerak miring, lalu kembali 0 saat sudah diam miring.
  Kalau slider Rotation dibiarkan di angka tertentu, artinya "mobil terus berguling tanpa henti",
  yang tidak pernah terjadi. Hasilnya angka tertahan di sekitar **1.5 x nilai Rotation**
  (contoh Rotation 5 → sekitar 7.5°), karena gyro dan accelerometer saling tarik.
- Cara benar meniru miring pakai gyro: geser Rotation sebentar (misalnya 30 selama 1 detik),
  lalu kembalikan ke 0, sambil Acceleration digeser ke sudut tujuan.
- Supaya mirip mobil asli, geser keduanya bersamaan.

| Mau simulasi | Acc X | Acc Y | Acc Z |
|---|---|---|---|
| Datar | 0 | 0 | 1 |
| Miring kanan ~30° | 0 | 0.5 | 0.87 |
| Nanjak ~20° | 0.34 | 0 | 0.94 |

Lebih dari 25° angkanya berkedip sebagai peringatan.

## Pasang di mobil

- Arah sensor yang dipakai kode: **X ke depan mobil, Y ke kiri, Z ke atas**.
  Kalau modul dipasang dengan arah lain, tukar sumbunya di fungsi `mpuUpdate()`.
- Setelah dipasang, parkir di tempat datar, buka KEMIRINGAN, **tahan knob 1 detik**.
- Biarkan mobil diam beberapa detik setelah dinyalakan supaya gyro sempat kalibrasi.
- Yang belum ada dibanding versi pabrik: koreksi pakai kecepatan roda. Rem atau gas yang
  sangat lama (lebih dari beberapa detik) masih bisa menggeser angka sedikit.

## Ubah sendiri

- Lama diam sebelum pindah ke jam: `IDLE_TO_CLOCK_MS` (20000 = 20 detik).
- Isi menu: array `MENU`.
- Tema dan bahasa bawaan untuk ESP32 yang belum pernah disetel: `THEME_CLEAN` dan `LANG_ID` di `loadSettings()`.
- Tingkat kecerahan: `BRIGHT_PERCENT` (angka di layar) dan `BRIGHT_CONTRAST` (nilai "contrast" OLED 0–255).
  OLED tidak bisa gelap total walau contrast 0; tingkat paling redup tergantung chip layarnya.
  Kecerahan di preview Mac cuma tiruan (warna diredupkan).
- Semua teks 2 bahasa ada di satu tabel `TEXTS` (cari `// ---------- Teks 2 bahasa`). Teks baru: tambah
  nama di `enum TextId` dan satu baris di `TEXTS` dengan urutan yang sama, lalu pakai `tr(T_NAMA)`.
  Teks Inggris dibuat pendek supaya muat 128 piksel.
- Kode tiap tema: cari `// ---------- Tema CLEAN` dan `// ---------- Tema 80-AN`. Cyberpunk adalah
  fungsi lama (`drawBoot`, `drawMenu`, `drawClock`, `drawTilt`). Pemilihnya ada di `drawScreen()`.
- Lama boot: Cyberpunk `BOOT_POWER` ... `BOOT_HUD`, Clean `BOOT_CLEAN_TOTAL`, 80-an `BOOT80_*`.
- Kekuatan glitch (Cyberpunk): `glitchFrame(amount, seed)`, `amount` 0 sampai 1.
- Batas peringatan miring: `ROLL_WARN` dan `PITCH_WARN` (derajat).
- Jam mode malam: `NIGHT_START_H` dan `NIGHT_END_H`. Pilihan tingkat malam: `NIGHT_OPTIONS`.
- Skala G-meter: `G_FULL` (g di lingkaran luar).
- Bunyi buzzer: `SND_CLICK`, `SND_HOLD`, `SND_WARN` (daftar `{nada Hz, lama ms}`, nada 0 = diam).
  Pin buzzer: `PIN_BUZZER`.
- Gambar Jimny di layar kemiringan: `JIMNY_SIDE` (60×30) dan `JIMNY_REAR` (40×41), satu baris teks per
  baris piksel (`#` = nyala). Bisa diedit langsung per piksel. Sumber: blueprint SJ410 hardtop dari
  getoutlines.com (lisensi Creative Commons).

## Catatan hardware

- Layar OLED 1.3" biasanya pakai chip **SH1106**, bukan SSD1306.
  Kalau gambar acak/geser, ganti baris `U8G2_SSD1306_...` dengan
  `U8G2_SH1106_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, U8X8_PIN_NONE);`
- Mau kuning seperti referensi: beli OLED versi **kuning**. Ukuran 2.42" (chip SSD1309):
  `U8G2_SSD1309_128X64_NONAME0_F_HW_I2C u8g2(U8G2_R0, U8X8_PIN_NONE);`
- Layar, MPU-6050, dan RTC berbagi jalur I2C yang sama (SDA 21, SCL 22). Alamatnya harus beda:
  layar 0x3C, RTC 0x68, MPU-6050 **0x69**. Karena MPU-6050 bawaannya juga 0x68, **pin AD0 MPU-6050
  wajib disambung ke 3V3** supaya pindah ke 0x69. Kalau lupa, sensor dan jam bentrok.

### Buzzer
- Pakai buzzer **PASIF** (bisa beda nada). Buzzer aktif cuma bisa satu nada dan akan bunyi aneh.
  Cara cek: buzzer aktif biasanya ada stiker dan bagian bawahnya tertutup; buzzer pasif bawahnya
  terlihat papan hijau. Atau colok ke 3V3: buzzer aktif langsung bunyi, pasif cuma "tek".
- Untuk prototipe di meja, buzzer piezo kecil boleh langsung ke GPIO 25 lewat resistor 100Ω.
- Untuk di mobil (biar cukup keras di atas suara mesin), pakai transistor NPN (S8050 / 2N2222):
  GPIO 25 → resistor 1kΩ → basis; emitor ke GND; buzzer antara 3V3/5V dan kolektor; plus dioda
  1N4148 terbalik di kaki buzzer kalau buzzernya jenis magnetik (bukan piezo).
- Di Wokwi pakai `wokwi-buzzer`, suaranya keluar dari browser.

### Jam (RTC DS3231, modul ZS-042)
- Jam dibaca dari RTC saat menyala, lalu disamakan lagi tiap menit. Kalau RTC tidak ketemu, jam mulai
  dari waktu compile, dan layar ATUR JAM menulis "RTC TIDAK ADA".
- **Baterai koin:** modul ZS-042 punya rangkaian pengisi untuk baterai isi ulang. Pakai **LIR2032**.
  Kalau pakai **CR2032** (tidak bisa diisi ulang), **lepas resistor 200Ω** (atau diodanya) di modul,
  supaya CR2032 tidak "dicas" (bisa bocor/menggembung).
- **Daya:** baterai koin sudah menjaga jam tetap jalan saat mobil mati (DS3231 cuma butuh ~3 µA).
  Saran saya: VCC modul dari **3V3 ESP32**, sama seperti komponen lain. Kalau VCC RTC diberi listrik
  terus dari BATT+ sementara ESP32 mati, listrik bisa bocor lewat resistor pull-up I2C di modul ke
  ESP32 yang sedang mati. Kalau tetap mau dari BATT+ (12V), wajib pakai regulator ke 3.3V, dan pull-up
  I2C di modul sebaiknya dilepas.
- Taruh modul di tempat yang gampang dijangkau, untuk ganti baterai koin.
- Di Wokwi tidak ada DS3231, jadi `diagram.json` memakai **DS1307**. Register jamnya sama, jadi kodenya
  sama. Wokwi mengisi jam DS1307 dengan jam komputer saat simulasi mulai.

## Wiring

| Komponen | Pin | ESP32 |
|---|---|---|
| OLED | SDA | GPIO 21 |
| OLED | SCL | GPIO 22 |
| OLED | VCC / GND | 3V3 / GND |
| MPU-6050 | SDA | GPIO 21 |
| MPU-6050 | SCL | GPIO 22 |
| MPU-6050 | VCC / GND | 3V3 / GND |
| MPU-6050 | **AD0** | **3V3** (alamat jadi 0x69) |
| RTC DS3231 | SDA | GPIO 21 |
| RTC DS3231 | SCL | GPIO 22 |
| RTC DS3231 | VCC / GND | 3V3 / GND (lihat catatan daya di atas) |
| KY-040 | CLK | GPIO 18 |
| KY-040 | DT | GPIO 19 |
| KY-040 | SW | GPIO 5 |
| KY-040 | + / GND | 3V3 / GND |
| Buzzer pasif | + / − | GPIO 25 (lewat resistor/transistor) / GND |
