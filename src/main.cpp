/*
  ============================================================
  SMART DIGITAL KEYCHAIN - hadiah ulang tahun Erika (14 Nov)
  ------------------------------------------------------------
  ESP32-C3 SuperMini + TFT (ILI9341 320x240 / ST7789 240x280)

  Animasi  : TFT_RoboEyes (turunan FluxGarage RoboEyes yang
             menggambar langsung ke TFT, tanpa framebuffer)
  Setting  : pin, tanggal ultah, WiFi, dan deep sleep tetap
             seperti konfigurasi awal -> lihat src/config.h

  Alur: bangun -> sinkron waktu (sekali) -> animasi mata ->
        tidur (deep sleep), bangun lagi saat tombol disentuh.
  ============================================================
*/

#include <Arduino.h>
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <math.h>

#include "config.h"

#if defined(USE_WOKWI_LCD)
#include <Adafruit_ILI9341.h>
#else
#include <Adafruit_ST7789.h>
#endif

#include <WiFi.h>
#include <time.h>
#include <sys/time.h>
#include "esp_sleep.h"
#include "driver/gpio.h"

// Library RoboEyes memakai makro DEFAULT = 0, sedangkan Arduino.h sudah
// mendefinisikan DEFAULT = 1. Lepas dulu supaya tidak bentrok, lalu setelah
// include makro itu dibuang dan diganti nama sendiri (MOOD_DASAR).
#undef DEFAULT
#include "TFT_RoboEyes.h"
#undef DEFAULT
#define MOOD_DASAR 0

// Pose wajah lengkap. Mata cuma punya 4 mood dari library
// (DEFAULT / TIRED / ANGRY / HAPPY), jadi tiap pose di sini memetakan
// mood mata + bentuk mulut, alis, dan animasi tambahannya sendiri.
enum PoseWajah : uint8_t {
  WAJAH_KALEM = 0,  // santai, senyum tipis
  WAJAH_SENANG,     // senyum lebar + ketawa
  WAJAH_MARAH,      // cemberut + kelopak miring
  WAJAH_LESU,       // mata lesu
  WAJAH_SEDIH,      // alis sedih (ujung dalam naik) + cemberut kecil
  WAJAH_MALU,       // mulut kecil + mata menunduk
  WAJAH_BINGUNG,    // mata goyang + alis asimetris
  WAJAH_NGANTUK     // mata lesu + menguap
};

// ===================== LAYAR =====================
#if defined(USE_WOKWI_LCD)
Adafruit_ILI9341 tft(&SPI, PIN_TFT_DC, PIN_TFT_CS, PIN_TFT_RST);
#else
Adafruit_ST7789 tft(PIN_TFT_CS, PIN_TFT_DC, PIN_TFT_RST);
#endif

TFTRoboEyes<decltype(tft)> mata(tft);

// ===================== STATE =====================
int lebarLayar = 0;
int tinggiLayar = 0;

// geometri mata (dihitung dari ukuran layar)
int mataLebar = 36;
int mataTinggi = 42;
int mataRadius = 10;
int mataJarak = 12;
int pangkalX = 0;   // posisi netral mata kiri
int pangkalY = 0;

bool modeUltah = false;
uint16_t warnaMata = WARNA_MATA_BIASA;

// geometri wajah (mulut & alis) - digambar manual, bukan dari library
int mulutX = 0, mulutY = 0, mulutLebar = 78, mulutTinggi = 24;
int alisKiriX = 0, alisKananX = 0, alisY = 0;
int alisLebar = 44, alisTebal = 6;

// Kanvas kecil (sprite) untuk mulut & alis. Digambar di RAM lalu dikirim
// sekaligus ke layar -> tidak ada lagi kedip "mati-nyala" walau library
// RoboEyes menimpa area di bawah mata tiap frame.
GFXcanvas16 *kanvasMulut = nullptr;
GFXcanvas16 *kanvasAlis  = nullptr;
int mulutKX = 0, mulutKY = 0, mulutKW = 0, mulutKH = 0;
int alisKX = 0, alisKY = 0, alisKW = 0, alisKH = 0;
// Tinggi yang benar-benar dikirim ke layar (bisa lebih pendek dari kanvas,
// supaya kanvas tidak menutupi mata / teks ucapan).
int mulutKTinggi = 0, alisKTinggi = 0;

// Semua gerakan wajah pakai nilai float yang dihaluskan (easing),
// jadi tidak ada lagi lompatan pose atau mulut buka-tutup on/off.
float bukaTarget = 0, bukaSekarang = 0;                  // 0 tutup .. 1 mangap
float lengkungTarget = 0.35f, lengkungSekarang = 0.35f;  // + senyum, - cemberut
float lebarTarget = 1.0f, lebarSekarang = 1.0f;
float bulatTarget = 0, bulatSekarang = 0;                // 1 = mulut bulat (kaget)
float nafasSekarang = 0.0f;                              // napas halus mulut
float alisMiringTarget = 0, alisMiringSekarang = 0;
float alisLengkungTarget = 0, alisLengkungSekarang = 0;
float alisNaikTarget[2] = {0, 0}, alisNaikSekarang[2] = {0, 0};
float skalaAlis = 1.0f;  // pengecil gerak alis kalau ruang atas sempit

unsigned long wajahTerakhir = 0;
unsigned long mulutRenderTerakhir = 0, alisRenderTerakhir = 0;
bool mulutKotor = true, alisKotor = true;

// ekspresi wajah
uint8_t moodWajah = WAJAH_KALEM;  // pose mulut & alis yang aktif
bool alisKiriNaik = false;        // dipakai saat mata "curious"
unsigned long tertawaSampai = 0;
unsigned long kejutanSampai = 0;
unsigned long kedipSampai = 0;    // alis ikut turun sedikit saat mata berkedip
unsigned long menguapMulai = 0, menguapSampai = 0;  // animasi menguap

// Target pandangan + posisi halusnya. Dipakai alis supaya ikut arah mata.
int tatapanDX = 0, tatapanDY = 0;
float tatapanSekarangX = 0, tatapanSekarangY = 0;
unsigned long tatapanTerakhir = 0;

// Memori RTC: tetap tersimpan selama baterai terpasang walau deep sleep
RTC_DATA_ATTR bool waktuSudahSinkron = false;
RTC_DATA_ATTR time_t waktuSinkronTerakhir = 0;
RTC_DATA_ATTR uint16_t hitungBangun = 0;
RTC_NOINIT_ATTR uint32_t magicSimulasiWokwi;
RTC_NOINIT_ATTR bool statusSinkronWokwi;
RTC_NOINIT_ATTR time_t waktuSimpanWokwi;

// ===================== UTIL LAYAR =====================
void tulisTengah(const char *teks, int y, uint8_t ukuran, uint16_t warna) {
  int16_t x1 = 0, y1 = 0;
  uint16_t w = 0, h = 0;
  tft.setTextSize(ukuran);
  tft.getTextBounds(teks, 0, 0, &x1, &y1, &w, &h);
  tft.setTextColor(warna);
  tft.setCursor((lebarLayar - (int)w) / 2 - x1, y - y1);
  tft.print(teks);
}

// Backlight: PWM hanya di hardware asli, Wokwi tetap on/off digital
void aturBacklight(uint8_t persen) {
#if defined(USE_WOKWI_LCD)
  digitalWrite(PIN_TFT_BLK, persen > 0 ? HIGH : LOW);
#else
  if (persen == 0) {
    analogWrite(PIN_TFT_BLK, 0);
    digitalWrite(PIN_TFT_BLK, LOW);
  } else {
    analogWrite(PIN_TFT_BLK, (uint16_t)persen * 255 / 100);
  }
#endif
}

void mulaiLayar() {
  pinMode(PIN_TFT_BLK, OUTPUT);
  digitalWrite(PIN_TFT_BLK, HIGH);

  SPI.begin(PIN_TFT_SCK, -1, PIN_TFT_MOSI, PIN_TFT_CS);

#if defined(USE_WOKWI_LCD)
  tft.begin(40000000);
#else
  tft.init(240, 280);
  tft.setSPISpeed(40000000);
  analogWriteFrequency(PIN_TFT_BLK, 5000);
#endif

  tft.setRotation(ORIENTASI_LAYAR);
  tft.fillScreen(WARNA_LATAR);
  lebarLayar = tft.width();
  tinggiLayar = tft.height();

  // Nyalakan layar bertahap supaya tidak "ngejeglek" saat bangun dari tidur.
  // (Di Wokwi backlight cuma on/off digital, jadi cukup langsung dinyalakan.)
#if defined(USE_WOKWI_LCD)
  aturBacklight(KECERAHAN_LAYAR);
#else
  aturBacklight(0);
  for (int b = 0; b <= KECERAHAN_LAYAR; b += 3) {
    aturBacklight((uint8_t)b);
    delay(8);
  }
  aturBacklight(KECERAHAN_LAYAR);
#endif
}

// ===================== WAKTU =====================
bool perluSinkron() {
  if (waktuSudahSinkron) return false;
  return (hitungBangun == 1);
}

void sinkronWaktuJikaPerlu() {
  if (!perluSinkron()) return;

  tulisTengah("Syncing Time...", tinggiLayar / 2 - 8, 2, WARNA_TEKS);

  WiFi.persistent(false);  // jangan tulis NVS tiap connect (hemat flash)
  WiFi.setTxPower(WIFI_POWER_8_5dBm);  // daya pancar rendah, cukup untuk NTP
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  int cobaWifi = 0;
  while (WiFi.status() != WL_CONNECTED && cobaWifi < 20) {
    delay(400);
    cobaWifi++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    configTime(25200, 0, "pool.ntp.org");  // WIB (UTC+7)
    struct tm timeinfo;
    int retry = 0;
    while (!getLocalTime(&timeinfo, 500) && retry < 6) retry++;
    if (retry < 6) {
      waktuSudahSinkron = true;
      waktuSinkronTerakhir = time(nullptr);
      statusSinkronWokwi = true;
      waktuSimpanWokwi = waktuSinkronTerakhir;
    }
  }

  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
  delay(80);
}

bool cekUltah() {
  if (SIMULASI_HARI_ULTAH) return true;

  struct tm timeinfo;
  if (!getLocalTime(&timeinfo, 100)) return false;

  int bulanSekarang   = timeinfo.tm_mon + 1;
  int tanggalSekarang = timeinfo.tm_mday;
  return (tanggalSekarang == TARGET_TANGGAL && bulanSekarang == TARGET_BULAN);
}

// ===================== DEKORASI ULTAH =====================
void gambarHati(int x, int y, int r, uint16_t warna) {
  tft.fillCircle(x - r + 1, y, r, warna);
  tft.fillCircle(x + r - 1, y, r, warna);
  tft.fillTriangle(x - (r * 2) + 1, y + 2,
                   x + (r * 2) - 1, y + 2,
                   x, y + (r * 2) + 2, warna);
}

void detakHati(bool besar) {
  const int r = besar ? 7 : 5;
  const int xs[2] = {22, lebarLayar - 22};
  const int ys[2] = {24, 24};

  for (int i = 0; i < 2; i++) {
    tft.fillRect(xs[i] - 18, ys[i] - 12, 36, 30, WARNA_LATAR);
    gambarHati(xs[i], ys[i], r, besar ? WARNA_HATI_TERANG : WARNA_HATI_LEMBUT);
  }
}

void tulisUcapanUltah() {
  tulisTengah("HAPPY BIRTHDAY", tinggiLayar - 40, 2, WARNA_TEKS);
  tulisTengah("ERIKA!",          tinggiLayar - 20, 2, WARNA_AKSEN);
}

// ===================== WAJAH: MULUT & ALIS =====================
// Library RoboEyes cuma menggambar mata. Mulut + alis digambar sendiri biar
// ekspresinya lebih hidup: ikut mood, ikut ketawa, dan bereaksi saat disentuh.

// Kurva "lonceng": 0 di ujung, 1 di tengah -> dipakai untuk lengkung mulut/alis
inline float lonceng(float t) {
  return 1.0f - (2.0f * t - 1.0f) * (2.0f * t - 1.0f);
}

// Campur dua warna RGB565 (t = 0 -> a, t = 1 -> b)
uint16_t campurWarna(uint16_t a, uint16_t b, float t) {
  const int ar = (a >> 11) & 0x1F, ag = (a >> 5) & 0x3F, ab = a & 0x1F;
  const int br = (b >> 11) & 0x1F, bg = (b >> 5) & 0x3F, bb = b & 0x1F;
  const int r = ar + (int)((br - ar) * t);
  const int g = ag + (int)((bg - ag) * t);
  const int bl = ab + (int)((bb - ab) * t);
  return (uint16_t)((r << 11) | (g << 5) | bl);
}

// State render terakhir, buat tahu kapan perlu digambar ulang
float bukaRender = -1, lengkungRender = -1, lebarRender = -1, nafasRender = -1;
float bulatRender = -1;
float alisMiringRender = -1, alisLengkungRender = -1;
float alisNaikRender[2] = {-1, -1};

// ---- MULUT ----
// Digambar ke kanvas di RAM, lalu dikirim ke layar dalam SATU blit.
// Dulu: hapus kotak -> gambar garis per kolom (puluhan transaksi SPI)
//       = kelihatan kedip mati-nyala.
// Sekarang: gambar di RAM (cepat) -> satu kiriman = mulus.
void renderMulut() {
  if (!kanvasMulut) return;
  GFXcanvas16 &g = *kanvasMulut;
  if (mulutKTinggi > 0)
    g.fillRect(0, 0, mulutKW, mulutKTinggi, WARNA_LATAR);
  else
    g.fillScreen(WARNA_LATAR);

  const uint16_t isi = campurWarna(WARNA_LATAR, warnaMata, 0.20f);
  const int cx = mulutKW / 2;
  const int cy = mulutY - mulutKY;  // garis sudut mulut di dalam kanvas
  const int tebal = max(4, (int)(mulutTinggi * 0.22f));
  const int lebar = max(10, (int)(mulutLebar * lebarSekarang));
  const int x0 = cx - lebar / 2;
  const int x1 = x0 + lebar - 1;

  // dip = kedalaman lengkung (positif = senyum), + napas halus.
  // Cemberut dibuat lebih dangkal supaya busurnya tidak nyaris menyentuh mata.
  const float skalaLengkung = (lengkungSekarang >= 0.0f) ? 0.80f : 0.55f;
  float dip = lengkungSekarang * mulutTinggi * skalaLengkung + nafasSekarang;
  float bukaPx = bukaSekarang * mulutTinggi;

  // Ruang yang tersedia di atas garis mulut. Semua bentuk (cemberut, mulut
  // bulat) dibatasi ke ruang ini -> tidak pernah kepotong tepi kanvas.
  const float ruangAtas = (float)max(2, cy - tebal);
  if (dip < -ruangAtas) dip = -ruangAtas;
  float bulatPx = bulatSekarang * mulutTinggi * 0.55f;
  if (bulatPx > ruangAtas) bulatPx = ruangAtas;

  // Jaga bibir bawah jangan sampai lewat tepi bawah kanvas (mis. ketawa lebar
  // di layar 240 px). Kalau tidak muat, bukaannya yang dikurangi, bukan
  // dipotong -> bentuk mulut tetap utuh.
  const float ruangBawah = (float)max(6, mulutKTinggi - cy - tebal);
  if (dip + bukaPx + bulatPx > ruangBawah) {
    bukaPx -= (dip + bukaPx + bulatPx) - ruangBawah;
    if (bukaPx < 0.0f) bukaPx = 0.0f;
  }

  if (bukaPx < 2.0f) {
    // mulut tertutup: satu busur tebal. Ketebalannya ikut naik sedikit saat
    // mulai membuka -> peralihan ke mulut mangap tidak "pop".
    const int tebalTutup = tebal + (int)(bukaPx * 1.5f);
    for (int x = x0; x <= x1; x++) {
      const float n = lonceng((float)(x - x0) / (float)(lebar - 1));
      const int yy = cy + (int)(dip * n);
      g.drawFastVLine(x, yy - tebalTutup / 2, tebalTutup, warnaMata);
    }
    return;
  }

  // mulut terbuka: isi gelap + bibir atas & bawah -> mangap yang halus.
  // bulatPx membuat bibir atas ikut melengkung ke atas -> bentuk "O".
  for (int x = x0; x <= x1; x++) {
    const float n = lonceng((float)(x - x0) / (float)(lebar - 1));
    const int yAtas  = cy + (int)(dip * n - bulatPx * n);
    const int yBawah = cy + (int)((dip + bukaPx) * n + bulatPx * n);
    if (yBawah > yAtas) g.drawFastVLine(x, yAtas, yBawah - yAtas + 1, isi);
    g.drawFastVLine(x, yAtas - tebal / 2, tebal, warnaMata);
    g.drawFastVLine(x, yBawah - tebal / 2 + 1, tebal, warnaMata);
  }
}

void blitMulut() {
  if (!kanvasMulut) return;
  tft.drawRGBBitmap(mulutKX, mulutKY, kanvasMulut->getBuffer(), mulutKW,
                    mulutKTinggi > 0 ? mulutKTinggi : mulutKH);
}

// ---- ALIS ----
// arah +1 = alis kiri (ujung dalam di kanan), -1 = alis kanan (ujung dalam
// di kiri). Semua parameter float -> alis bergerak mulus, tidak lompat.
void gambarAlisSatu(Adafruit_GFX &g, int xMulai, int yDasar, int arah,
                    float naik) {
  for (int i = 0; i < alisLebar; i++) {
    const float t = (float)i / (float)(alisLebar - 1);
    const float n = lonceng(t);
    const float miring = (t - 0.5f) * 2.0f;
    const float turun = alisMiringSekarang * miring * arah * (alisLebar * 0.16f);
    const float lengkung = -alisLengkungSekarang * n * (alisLebar * 0.24f);
    const int yy = yDasar + (int)(turun + lengkung - naik);
    g.drawFastVLine(xMulai + i, yy - alisTebal / 2, alisTebal, warnaMata);
  }
}

void renderAlis() {
  if (!kanvasAlis) return;
  GFXcanvas16 &g = *kanvasAlis;
  if (alisKTinggi > 0)
    g.fillRect(0, 0, alisKW, alisKTinggi, WARNA_LATAR);
  else
    g.fillScreen(WARNA_LATAR);

  const int cy = alisY - alisKY;  // garis dasar alis di dalam kanvas
  gambarAlisSatu(g, alisKiriX - alisKX, cy, 1, alisNaikSekarang[0]);
  gambarAlisSatu(g, alisKananX - alisKX, cy, -1, alisNaikSekarang[1]);
}

void blitAlis() {
  if (!kanvasAlis) return;
  tft.drawRGBBitmap(alisKX, alisKY, kanvasAlis->getBuffer(), alisKW,
                    alisKTinggi > 0 ? alisKTinggi : alisKH);
}

void tandaiMulutTergambar() {
  bukaRender = bukaSekarang;
  lengkungRender = lengkungSekarang;
  lebarRender = lebarSekarang;
  bulatRender = bulatSekarang;
  nafasRender = nafasSekarang;
  mulutKotor = false;
}

void tandaiAlisTergambar() {
  alisMiringRender = alisMiringSekarang;
  alisLengkungRender = alisLengkungSekarang;
  alisNaikRender[0] = alisNaikSekarang[0];
  alisNaikRender[1] = alisNaikSekarang[1];
  alisKotor = false;
}

void gambarWajahLangsung() {
  renderMulut();
  blitMulut();
  tandaiMulutTergambar();
  renderAlis();
  blitAlis();
  tandaiAlisTergambar();
}

// Semua nilai wajah dihaluskan ke targetnya tiap frame (frame-rate aware),
// lalu ditandai "kotor" kalau perubahannya sudah kelihatan.
void perbaruiWajah(unsigned long sekarang, bool kaget, bool tertawa) {
  unsigned long dt = (wajahTerakhir == 0) ? 16 : (sekarang - wajahTerakhir);
  if (dt > 80) dt = 16;  // abis delay panjang (mis. WiFi) -> jangan lompat
  wajahTerakhir = sekarang;
  const float aBuka = 1.0f - expf(-(float)dt / 70.0f);   // bukaan mulut
  const float aPose = 1.0f - expf(-(float)dt / 130.0f);  // bentuk & alis

  // --- Pose dasar: bentuk mulut + kemiringan/lengkung alis ---
  float tLengkung, tLebar, tMiring, tArch;
  switch (moodWajah) {
    case WAJAH_SENANG:  // senyum lebar, alis melengkung ke atas
      tLengkung = 1.00f;  tLebar = 1.03f; tMiring =  0.00f; tArch =  1.00f; break;
    case WAJAH_MARAH:   // cemberut, ujung dalam alis menukik
      tLengkung = -0.85f; tLebar = 0.92f; tMiring =  1.00f; tArch = -0.20f; break;
    case WAJAH_LESU:    // mulut hampir datar, alis jatuh di ujung luar
      tLengkung = 0.06f;  tLebar = 0.90f; tMiring = -0.90f; tArch = -0.25f; break;
    case WAJAH_SEDIH:   // cemberut kecil, ujung dalam alis naik (sad brows)
      tLengkung = -0.45f; tLebar = 0.86f; tMiring = -0.55f; tArch = -0.10f; break;
    case WAJAH_MALU:    // mulut kecil menguncup, alis melengkung lembut
      tLengkung = 0.75f;  tLebar = 0.62f; tMiring =  0.15f; tArch =  0.55f; break;
    case WAJAH_BINGUNG: // mulut miring, alis tidak simetris (diatur di bawah)
      tLengkung = 0.05f;  tLebar = 0.95f; tMiring =  0.35f; tArch = -0.05f; break;
    case WAJAH_NGANTUK: // mulut lemas, alis turun
      tLengkung = 0.10f;  tLebar = 0.88f; tMiring = -0.35f; tArch = -0.15f; break;
    default:            // kalem
      tLengkung = 0.35f;  tLebar = 1.00f; tMiring =  0.00f; tArch =  0.25f; break;
  }

  float tBuka = 0.0f, tBulat = 0.0f;

  if (kaget) {
    // Kaget: mulut terbuka bulat (O) + alis naik tinggi
    tBuka = 0.92f;
    tBulat = 1.00f;
    tLebar = 0.72f;
  } else if (menguapSampai && sekarang >= menguapMulai &&
             sekarang < menguapSampai) {
    // Menguap: bukaan mengikuti kurva lonceng (buka -> tahan -> tutup)
    const float p = (float)(sekarang - menguapMulai) /
                    (float)(menguapSampai - menguapMulai);
    const float nafas = sinf(p * 3.1415926f);
    tBuka = 0.90f * nafas;
    tBulat = 0.55f * nafas;
    tLebar = 0.88f - 0.06f * nafas;
  } else if (tertawa) {
    // Ketawa: bukaan mengikuti gelombang sinus (halus), bukan toggle
    const float fase = sinf((float)sekarang * 0.011f);
    tBuka = 0.55f + 0.45f * fase;
    tBulat = 0.10f + 0.10f * fase;
    tLebar *= 1.0f + 0.03f * fase;
  } else if (alisKiriNaik) {
    // Curious: mulut ikut terbuka sedikit ("hmm?") -> nyambung sama alis naik
    tBuka = 0.20f;
    tLebar *= 0.88f;
  }

  // --- Sinkron dengan mata ---
  // Kedip terdeteksi dari tinggi mata (library menutup mata sampai ~1 px).
  if (mata.eyeLheightCurrent < (int)(mataTinggi * 0.35f)) {
    kedipSampai = sekarang + 90;
  }
  const bool kedip = (kedipSampai && sekarang < kedipSampai);

  float tNaik0 = 0.0f, tNaik1 = 0.0f;
  if (kaget) { tNaik0 = 12.0f; tNaik1 = 12.0f; }
  else if (alisKiriNaik) { tNaik0 = 8.0f; }
  else if (moodWajah == WAJAH_BINGUNG) { tNaik0 = 8.0f; tNaik1 = -2.5f; }
  else if (moodWajah == WAJAH_SEDIH) { tNaik0 = 1.5f; tNaik1 = 1.5f; }
  if (menguapSampai && sekarang >= menguapMulai && sekarang < menguapSampai) {
    const float p = (float)(sekarang - menguapMulai) /
                    (float)(menguapSampai - menguapMulai);
    tNaik0 += 5.0f * sinf(p * 3.1415926f);
    tNaik1 += 5.0f * sinf(p * 3.1415926f);
  }

  // Alis mengikuti mata: lihat ke atas -> alis naik, ke bawah -> ikut turun,
  // melirik jauh -> sedikit terangkat, kedip -> turun sebentar,
  // mulut mangap -> alis ikut terangkat. Jadi terasa satu wajah, bukan tempelan.
  float ikutMata = -tatapanSekarangY * 0.45f;
  ikutMata += fabsf(tatapanSekarangX) * 0.06f;
  if (kedip) ikutMata -= 1.8f;
  ikutMata += bukaSekarang * 2.2f;
  ikutMata = constrain(ikutMata, -6.0f, 7.0f);

  // Getaran sangat halus dengan fase berbeda -> alis tidak pernah benar-benar diam
  tNaik0 += ikutMata + 0.8f * sinf((float)sekarang * 0.0019f);
  tNaik1 += ikutMata + 0.8f * sinf((float)sekarang * 0.0019f + 1.7f);

  // Kalau ruang di atas mata sempit (mis. mode ultah), gerak alis dikecilkan
  // supaya tetap utuh di layar, bukan kepotong.
  tNaik0 *= skalaAlis;
  tNaik1 *= skalaAlis;
  if (tArch > 0.0f) tArch *= skalaAlis;

  bukaTarget = tBuka;
  bulatTarget = tBulat;
  lengkungTarget = tLengkung;
  lebarTarget = tLebar;
  alisMiringTarget = tMiring;
  alisLengkungTarget = tArch;
  alisNaikTarget[0] = tNaik0;
  alisNaikTarget[1] = tNaik1;

  bukaSekarang         += (bukaTarget         - bukaSekarang) * aBuka;
  bulatSekarang        += (bulatTarget        - bulatSekarang) * aBuka;
  lengkungSekarang     += (lengkungTarget     - lengkungSekarang) * aPose;
  lebarSekarang        += (lebarTarget        - lebarSekarang) * aPose;
  alisMiringSekarang   += (alisMiringTarget   - alisMiringSekarang) * aPose;
  alisLengkungSekarang += (alisLengkungTarget - alisLengkungSekarang) * aPose;
  alisNaikSekarang[0]  += (alisNaikTarget[0]  - alisNaikSekarang[0]) * aPose;
  alisNaikSekarang[1]  += (alisNaikTarget[1]  - alisNaikSekarang[1]) * aPose;

  // Napas halus: mulut naik-turun ~1,6 px saat diam -> wajah terasa hidup
  nafasSekarang = (tertawa || kaget || menguapSampai) ? 0.0f
                                      : 1.6f * sinf((float)sekarang * 0.0017f);

  if (fabsf(bukaSekarang - bukaRender) > 0.008f ||
      fabsf(bulatSekarang - bulatRender) > 0.008f ||
      fabsf(lengkungSekarang - lengkungRender) > 0.010f ||
      fabsf(lebarSekarang - lebarRender) > 0.008f ||
      fabsf(nafasSekarang - nafasRender) > 0.05f) {
    mulutKotor = true;
  }
  if (fabsf(alisMiringSekarang - alisMiringRender) > 0.01f ||
      fabsf(alisLengkungSekarang - alisLengkungRender) > 0.01f ||
      fabsf(alisNaikSekarang[0] - alisNaikRender[0]) > 0.15f ||
      fabsf(alisNaikSekarang[1] - alisNaikRender[1]) > 0.15f) {
    alisKotor = true;
  }
}

// ===================== MATA =====================
// Alokasi kanvas dilakukan sedini mungkin (sebelum WiFi hidup) supaya
// heap masih rapi. Ukurannya dihitung dari rumus yang SAMA dengan
// siapkanMata(), dan sengaja pakai ukuran mode biasa (0.26 / 1.15) karena
// selalu lebih besar dari mode ultah (0.24 / 1.14) -> tidak pernah
// realokasi setelah WiFi menyala.
void siapkanKanvasAwal() {
  const int dasar = min(lebarLayar, tinggiLayar);
  const int m   = (int)(dasar * 0.26f);         // mataLebar (versi maksimum)
  const int mt  = (int)(m * 1.15f);             // mataTinggi
  const int mj  = (int)(m * 0.34f);             // mataJarak
  const int al  = max(16, (int)(m * 0.62f));    // alisLebar
  const int ml  = (int)(m * 1.12f);             // mulutLebar
  const int mtg = (int)(mt * 0.30f);            // mulutTinggi

  const int kw = ml + 18;
  const int kh = 26 + mtg * 2 + 8;
  const int aw = m + mj + al + 24;
  const int ah = 54;

  if (!kanvasMulut) kanvasMulut = new GFXcanvas16(kw, kh);
  if (!kanvasAlis)  kanvasAlis  = new GFXcanvas16(aw, ah);
}

void siapkanMata() {
  const int dasar = min(lebarLayar, tinggiLayar);

  // Mata sedikit lebih kecil dari sebelumnya supaya ada ruang untuk mulut +
  // alis tanpa saling menimpa.
  mataLebar  = (int)(dasar * (modeUltah ? 0.24f : 0.26f));
  mataTinggi = (int)(mataLebar * (modeUltah ? 1.14f : 1.15f));
  mataRadius = (int)(mataLebar * 0.30f);
  mataJarak  = (int)(mataLebar * 0.34f);

  pangkalX = (lebarLayar - (mataLebar * 2 + mataJarak)) / 2;
  // Digeser naik 10 px di mode biasa: tinggi layar cuma 240 px, jadi mulut
  // butuh ruang di bawah. Hasil akhirnya wajah tetap pas di tengah layar.
  pangkalY = (tinggiLayar - mataTinggi) / 2 - (modeUltah ? 34 : 10);

  // --- posisi mulut & alis, dihitung dari posisi mata ---
  const int pusatKiri  = pangkalX + mataLebar / 2;
  const int pusatKanan = pangkalX + mataLebar + mataJarak + mataLebar / 2;

  alisLebar = max(16, (int)(mataLebar * 0.62f));
  alisTebal = max(4, (int)(mataLebar * 0.085f));
  alisKiriX  = pusatKiri - alisLebar / 2;
  alisKananX = pusatKanan - alisLebar / 2;
  // Alis ditaruh 26-28 px di atas mata; kalau ruang atasnya sempit (mode
  // ultah) posisinya dibatasi dan gerakannya ikut dikecilkan.
  alisY = max(18, pangkalY - (modeUltah ? 26 : 28));
  skalaAlis = constrain((alisY - 6.0f) / 28.0f, 0.40f, 1.0f);

  mulutLebar  = (int)(mataLebar * 1.12f);
  mulutTinggi = (int)(mataTinggi * 0.30f);
  mulutX = (lebarLayar - mulutLebar) / 2;
  mulutY = pangkalY + mataTinggi + (modeUltah ? 24 : 36);

  // --- kanvas (sprite) mulut & alis ---
  // Kanvas mulut digambar SETELAH mata, jadi tepi atasnya wajib di bawah
  // titik terendah mata: lirik ke bawah (12) + drift napas (2) + getaran
  // ketawa (5) + mata membesar saat bingung (7) = maksimal ~19 px. Mode
  // ultah cukup 8 px karena pose bingung tidak dipakai di sana.
  mulutKW = mulutLebar + 18;
  mulutKH = 26 + mulutTinggi * 2 + 8;
  mulutKX = (lebarLayar - mulutKW) / 2;
  mulutKY = max(mulutY - (modeUltah ? 20 : 22),
                pangkalY + mataTinggi + (modeUltah ? 8 : 24));
  // Di mode ultah sisakan ruang untuk tulisan "HAPPY BIRTHDAY ERIKA!".
  const int batasBawahMulut = modeUltah ? (tinggiLayar - 48) : (tinggiLayar - 6);
  mulutKTinggi = constrain(batasBawahMulut - mulutKY, 24, mulutKH);

  alisKW = (alisKananX + alisLebar) - alisKiriX + 24;
  alisKH = 54;
  alisKX = alisKiriX - 12;
  alisKY = max(0, alisY - 36);
  // Bawah kanvas alis berhenti 8 px di atas mata supaya tidak perlu menimpa
  // mata sama sekali (alis memang digambar lebih dulu).
  alisKTinggi = constrain((pangkalY - 8) - alisKY, 20, alisKH);

  // Kalau kanvas awal sudah pas ukurannya (normalnya begitu), pakai itu.
  if (!kanvasMulut || kanvasMulut->width() != mulutKW ||
      kanvasMulut->height() != mulutKH) {
    delete kanvasMulut;
    kanvasMulut = new GFXcanvas16(mulutKW, mulutKH);
  }
  if (!kanvasAlis || kanvasAlis->width() != alisKW ||
      kanvasAlis->height() != alisKH) {
    delete kanvasAlis;
    kanvasAlis = new GFXcanvas16(alisKW, alisKH);
  }

  mulutKotor = true;
  alisKotor = true;
  wajahTerakhir = 0;

  mata.setEyeSize(mataLebar, mataTinggi);
  mata.setBorderradius(mataRadius, mataRadius);
  mata.setSpacebetween(mataJarak);

  mata.setDisplayColors(WARNA_LATAR, warnaMata);
  mata.setAutoblinker(ON, 2, 4);   // kedip tiap 2-6 detik
  mata.setCuriosity(ON);
  mata.setIdleMode(OFF, 0, 0);     // tatapan diatur manual agar halus

  mata.begin(lebarLayar, tinggiLayar, 60);  // 60 fps

  // Mulai langsung di posisi netral (tanpa meluncur dari sudut layar)
  mata.eyeLx = mata.eyeLxNext = pangkalX;
  mata.eyeLy = mata.eyeLyNext = pangkalY;
  mata.eyeRx = mata.eyeRxNext = pangkalX + mataLebar + mataJarak;

  mata.setMood(modeUltah ? HAPPY : MOOD_DASAR);
}

// Pasang pose wajah lengkap. Mata hanya punya 4 mood dari library, jadi
// tiap pose di sini memetakan mood mata + animasi tambahan + bentuk mulut/alis
// (bentuk mulut/alis dihaluskan oleh perbaruiWajah()).
void pakaiEkspresi(uint8_t pose) {
  mata.setHFlicker(OFF, 0);
  tertawaSampai = 0;
  kejutanSampai = 0;
  menguapMulai = 0;
  menguapSampai = 0;
  alisKiriNaik = false;

  moodWajah = pose;

  switch (pose) {
    case WAJAH_SENANG:  // senyum lebar + ketawa halus
      mata.setMood(HAPPY);
      mata.anim_laugh();
      tertawaSampai = millis() + 2400;
      break;

    case WAJAH_MARAH:  // cemberut + kelopak mata bergetar
      mata.setMood(ANGRY);
      mata.setHFlicker(ON, 2);
      break;

    case WAJAH_LESU:    // mata setengah tertutup
    case WAJAH_SEDIH:   // mata lesu + alis sedih
    case WAJAH_NGANTUK: // mata lesu, nanti menguap
      mata.setMood(TIRED);
      break;

    case WAJAH_BINGUNG:  // mata goyang kiri-kanan (library)
      mata.setMood(MOOD_DASAR);
      mata.anim_confused();
      break;

    case WAJAH_MALU:  // mata menunduk, mulut kecil menguncup
      mata.setMood(MOOD_DASAR);
      tatapanDY = 7;
      break;

    default:  // WAJAH_KALEM: netral, santai
      mata.setMood(MOOD_DASAR);
      break;
  }

  // Ngantuk: menguap dimulai agak telat supaya mata sempat sayu dulu.
  if (pose == WAJAH_NGANTUK) {
    menguapMulai  = millis() + 700;
    menguapSampai = menguapMulai + 2400;
  }

  // Pose mulut/alis tidak digambar di sini: nilai targetnya berubah,
  // lalu perbaruiWajah() menghaluskannya frame demi frame.
  mulutKotor = true;
  alisKotor = true;
}

// Target pandangan disimpan, lalu digerakkan perlahan (bukan lompat).
// Library RoboEyes sendiri menggerakkan mata dengan "setengah sisa jarak"
// per frame, jadi kalau targetnya dipindah mendadak hasilnya menyentak.
void aturTatapan() {
  // Pose tertentu punya arah pandang khas: malu menunduk, ngantuk sayu.
  if (moodWajah == WAJAH_MALU) {
    tatapanDX = random(-3, 4);
    tatapanDY = 7;
    return;
  }
  if (moodWajah == WAJAH_NGANTUK) {
    tatapanDX = random(-5, 6);
    tatapanDY = 4;
    return;
  }

  const int jangkauanX = modeUltah ? 7 : 16;
  const int jangkauanY = modeUltah ? 4 : 10;

  int dx = random(-jangkauanX, jangkauanX + 1);
  int dy = random(-jangkauanY, jangkauanY + 1);

  // Sesekali melirik lebih jauh biar terasa hidup
  if (!modeUltah && random(100) < 18) {
    dx = (random(2) ? 1 : -1) * jangkauanX * 2;
  }

  tatapanDX = dx;
  tatapanDY = dy;
}

// Geser pandangan + drift napas, semuanya dihaluskan per frame.
void perbaruiTatapan(unsigned long sekarang) {
  unsigned long dt = (tatapanTerakhir == 0) ? 16 : (sekarang - tatapanTerakhir);
  if (dt > 80) dt = 16;
  tatapanTerakhir = sekarang;
  const float a = 1.0f - expf(-(float)dt / 240.0f);  // time constant 240 ms

  tatapanSekarangX += ((float)tatapanDX - tatapanSekarangX) * a;
  tatapanSekarangY += ((float)tatapanDY - tatapanSekarangY) * a;

  const float driftX = 3.0f * sinf((float)sekarang * 0.0008f);
  const float driftY = 2.0f * sinf((float)sekarang * 0.0010f + 1.3f);

  mata.eyeLxNext = pangkalX + (int)lroundf(tatapanSekarangX + driftX);

  // Jangan biarkan mata turun sampai masuk area kanvas mulut: kanvas mulut
  // digambar SETELAH mata, jadi bagian mata yang kena pasti terhapus. Sisa
  // 14 px dipakai untuk getaran ketawa + pembesaran mata saat bingung yang
  // ditambahkan sendiri oleh library.
  int targetY = pangkalY + (int)lroundf(tatapanSekarangY + driftY);
  const int batasBawahMata = mulutKY - 14 - mataTinggi;
  if (targetY > batasBawahMata) targetY = batasBawahMata;
  mata.eyeLyNext = targetY;
}

// ===================== ANIMASI UTAMA =====================
void jalankanAnimasi() {
  const unsigned long mulai = millis();

  unsigned long jadwalTatapan   = 0;
  unsigned long jadwalEkspresi  = mulai + 2600;
  unsigned long selesaiEkspresi = 0;
  unsigned long jadwalKetawa    = mulai + 1300;
  unsigned long jadwalHati      = 0;
  bool hatiBesar = false;

  bool tombolSebelum = (digitalRead(PIN_TOUCH) == HIGH);
  unsigned long tombolTerakhir = 0;
  uint8_t siklusTombol = 0;

  // Urutan pose saat tombol ditekan: semua ekspresi, berputar.
  const uint8_t urutanTombol[] = {WAJAH_SENANG, WAJAH_MARAH,  WAJAH_MALU,
                                  WAJAH_BINGUNG, WAJAH_SEDIH, WAJAH_LESU,
                                  WAJAH_NGANTUK, WAJAH_KALEM};

  while (millis() - mulai < DURASI_NYALA) {
    const unsigned long sekarang = millis();

    // --- Tatapan: target acak tiap 1,6-3,4 detik, geraknya dihaluskan ---
    if (sekarang >= jadwalTatapan) {
      aturTatapan();
      jadwalTatapan = sekarang + random(1600, 3400);
    }
    perbaruiTatapan(sekarang);

    // --- Kembali ke pose dasar setelah ekspresi selesai ---
    if (selesaiEkspresi && sekarang >= selesaiEkspresi) {
      pakaiEkspresi(modeUltah ? WAJAH_SENANG : WAJAH_KALEM);
      selesaiEkspresi = 0;
      jadwalEkspresi  = sekarang + random(1800, 4200);
    }

    // --- Ekspresi otomatis (hari biasa), dipilih berbobot ---
    // Total 100: kalem/idle 18%, sisanya 7 pose + "penasaran".
    if (!modeUltah && !selesaiEkspresi && sekarang >= jadwalEkspresi) {
      const int r = random(100);
      unsigned long durasi = 0;

      if (r < 14) {                       // senang / ketawa
        pakaiEkspresi(WAJAH_SENANG);
        durasi = 2400;
      } else if (r < 24) {                // bingung
        pakaiEkspresi(WAJAH_BINGUNG);
        durasi = 2200;
      } else if (r < 34) {                // lesu
        pakaiEkspresi(WAJAH_LESU);
        durasi = 2000;
      } else if (r < 43) {                // sedih
        pakaiEkspresi(WAJAH_SEDIH);
        durasi = 2200;
      } else if (r < 51) {                // marah
        pakaiEkspresi(WAJAH_MARAH);
        durasi = 1800;
      } else if (r < 60) {                // malu
        pakaiEkspresi(WAJAH_MALU);
        durasi = 2000;
      } else if (r < 68) {                // ngantuk + menguap
        pakaiEkspresi(WAJAH_NGANTUK);
        durasi = 3000;
      } else if (r < 82) {                // penasaran: satu mata menyipit
        pakaiEkspresi(WAJAH_KALEM);
        mata.anim_curious();
        alisKiriNaik = true;              // alis kiri ikut terangkat
        if (random(100) < 45) mata.blink(false, true);  // sesekali kedip satu mata
        durasi = 1900;
      }

      if (durasi) {
        selesaiEkspresi = sekarang + durasi;
      } else {
        // Diam sejenak (cuma napas + lirikan) -> terasa hidup, tidak robotik
        jadwalEkspresi = sekarang + random(1400, 2600);
      }
    }

    // --- Mode ultah: ketawa + hati berdenyut ---
    if (modeUltah) {
      if (sekarang >= jadwalKetawa) {
        mata.anim_laugh();
        tertawaSampai = sekarang + 1500;  // mulut ikut buka-tutup
        jadwalKetawa = sekarang + 2400;
      }
      if (sekarang >= jadwalHati) {
        detakHati(hatiBesar);
        hatiBesar = !hatiBesar;
        jadwalHati = sekarang + 280;
      }
    }

    // --- Tombol: ganti ekspresi saat disentuh lagi ---
    const bool tombolSekarang = (digitalRead(PIN_TOUCH) == HIGH);
    if (tombolSekarang && !tombolSebelum && (sekarang - tombolTerakhir > 250)) {
      tombolTerakhir = sekarang;
      pakaiEkspresi(urutanTombol[siklusTombol % 8]);
      siklusTombol++;
      selesaiEkspresi = sekarang + 2500;
      kejutanSampai = sekarang + 520;  // mulut mangap + alis naik sebentar
      jadwalHati = 0;  // hati langsung ikut berdenyut lagi di mode ultah
    }
    tombolSebelum = tombolSekarang;

    // --- Wajah: hitung pose mulut + alis tiap frame (float, dihaluskan) ---
    const bool kaget   = (kejutanSampai && sekarang < kejutanSampai);
    const bool tertawa = (tertawaSampai && sekarang < tertawaSampai);

    perbaruiWajah(sekarang, kaget, tertawa);

    // Alis digambar SEBELUM mata. Kalau kanvas alis menyentuh tepi atas mata,
    // mata yang digambar sesudahnya menimpanya -> tidak ada tepi mata terhapus.
    if (alisKotor && (sekarang - alisRenderTerakhir >= 20)) {
      renderAlis();
      blitAlis();
      tandaiAlisTergambar();
      alisRenderTerakhir = sekarang;
    }

    // --- Mata: library menggambar frame baru ---
    const unsigned long timerLama = mata.fpsTimer;
    mata.update();
    const bool frameBaru = (mata.fpsTimer != timerLama);

    // Library selalu menimpa area di bawah mata tiap frame, jadi mulut
    // digambar ulang tepat setelahnya. Blit cuma satu kiriman -> mulus.
    if (frameBaru || (mulutKotor && (sekarang - mulutRenderTerakhir >= 20))) {
      renderMulut();
      blitMulut();
      tandaiMulutTergambar();
      mulutRenderTerakhir = sekarang;
    }

    delay(1);
  }
}

// ===================== TIDUR =====================
// Kirim perintah sleep ke kontroller layar supaya arus turun drastis.
void tidurkanLayar() {
#if defined(USE_WOKWI_LCD)
  // ILI9341: DISPOFF (0x28) lalu SLPIN (0x10) lewat SPI mentah
  SPI.beginTransaction(SPISettings(8000000, MSBFIRST, SPI_MODE0));

  digitalWrite(PIN_TFT_DC, LOW);
  digitalWrite(PIN_TFT_CS, LOW);
  SPI.transfer(ILI9341_DISPOFF);
  digitalWrite(PIN_TFT_CS, HIGH);

  delay(10);

  digitalWrite(PIN_TFT_DC, LOW);
  digitalWrite(PIN_TFT_CS, LOW);
  SPI.transfer(ILI9341_SLPIN);
  digitalWrite(PIN_TFT_CS, HIGH);
  digitalWrite(PIN_TFT_DC, HIGH);

  SPI.endTransaction();
#else
  tft.enableDisplay(false);
  tft.enableSleep(true);
#endif

  aturBacklight(0);
  delay(20);
}

// Kunci level pin supaya tidak melayang (nggak bocor arus) selama deep sleep.
void kunciPinTidur() {
  pinMode(PIN_TFT_CS, OUTPUT);
  pinMode(PIN_TFT_DC, OUTPUT);
  pinMode(PIN_TFT_RST, OUTPUT);
  pinMode(PIN_TFT_MOSI, OUTPUT);
  pinMode(PIN_TFT_SCK, OUTPUT);
  pinMode(PIN_TFT_BLK, OUTPUT);

  digitalWrite(PIN_TFT_CS, HIGH);   // layar tidak terpilih
  digitalWrite(PIN_TFT_RST, HIGH);  // tidak sedang di-reset
  digitalWrite(PIN_TFT_DC, LOW);
  digitalWrite(PIN_TFT_MOSI, LOW);
  digitalWrite(PIN_TFT_SCK, LOW);
  digitalWrite(PIN_TFT_BLK, LOW);   // backlight mati total

  gpio_hold_en((gpio_num_t)PIN_TFT_CS);
  gpio_hold_en((gpio_num_t)PIN_TFT_RST);
  gpio_hold_en((gpio_num_t)PIN_TFT_DC);
  gpio_hold_en((gpio_num_t)PIN_TFT_MOSI);
  gpio_hold_en((gpio_num_t)PIN_TFT_SCK);
  gpio_hold_en((gpio_num_t)PIN_TFT_BLK);
  // Catatan: gpio_deep_sleep_hold_en() sengaja TIDAK dipakai. Kalau semua
  // GPIO di-hold, pin sentuh (GPIO1) bisa ikut terkunci dan gagal jadi
  // sumber wake. Per-pin hold di atas sudah cukup untuk pin TFT.
}

void masukModeTidur() {
  // Animasi tidur: mata jadi berat -> dua kedipan lambat -> terpejam ->
  // wajah rileks -> backlight meredup sampai mati.
  tertawaSampai = 0;
  kejutanSampai = 0;
  menguapMulai = 0;
  menguapSampai = 0;
  alisKiriNaik = false;
  tatapanDX = 0;
  tatapanDY = 2;

  // Satu frame wajah lengkap (alis -> mata -> mulut). Urutannya sama dengan
  // jalankanAnimasi() supaya tidak ada bagian yang saling menghapus.
  auto satuFrame = [](unsigned long sekarang) {
    perbaruiWajah(sekarang, false, false);
    if (alisKotor) {
      renderAlis();
      blitAlis();
      tandaiAlisTergambar();
    }
    mata.update();
    renderMulut();
    blitMulut();
    tandaiMulutTergambar();
  };

  // Matikan kedip otomatis: kalau tidak, library bisa membuka mata lagi
  // tepat saat wajah sedang diredupkan.
  mata.setAutoblinker(OFF);
  mata.setIdleMode(OFF, 0, 0);
  mata.setHFlicker(OFF, 0);

  moodWajah = WAJAH_NGANTUK;
  mata.setMood(TIRED);

  // 1) Mata berat: dua kedipan lambat, jeda makin panjang.
  for (int k = 0; k < 2; k++) {
    mata.close();
    for (int i = 0; i < 9; i++) {
      satuFrame(millis());
      delay(34);
    }
    delay(120);
    mata.open();
    for (int i = 0; i < 8; i++) {
      satuFrame(millis());
      delay(30);
    }
    delay(140 + k * 160);
  }

  // 2) Terpejam total, perlahan.
  mata.close();
  for (int i = 0; i < 14; i++) {
    satuFrame(millis());
    delay(26);
  }

  // 3) Wajah rileks (mulut datar, alis turun) -> ekspresi damai.
  moodWajah = WAJAH_KALEM;
  mata.setMood(MOOD_DASAR);
  for (int i = 0; i < 14; i++) {
    satuFrame(millis());
    delay(22);
  }

  // 4) Redupkan layar bertahap. Di Wokwi backlight on/off, di hardware asli
  //    PWM-nya benar-benar turun halus sampai gelap.
  for (int b = KECERAHAN_LAYAR; b >= 0; b -= 2) {
    aturBacklight((uint8_t)b);
    delay(12);
  }
  aturBacklight(0);
  delay(60);

  tidurkanLayar();
  kunciPinTidur();

  // Tunggu tombol/sensor dilepas, TAPI jangan sampai nyangkut selamanya
  // kalau pin terbaca HIGH terus (mis. tombol nyangkut / bounce).
  unsigned long mulaiLepas = millis();
  while (gpio_get_level(PIN_TOUCH) == 1 && millis() - mulaiLepas < 3000) {
    delay(20);
  }
  delay(120);

  // Pin sentuh disiapkan PERSIS sebelum tidur. Di ESP32-C3, GPIO0/GPIO1
  // input-nya nonaktif setelah reset, jadi harus di-set ulang di sini
  // supaya benar-benar bisa membangunkan chip.
  gpio_set_direction(PIN_TOUCH, GPIO_MODE_INPUT);
  gpio_pullup_dis(PIN_TOUCH);
  gpio_pulldown_en(PIN_TOUCH);

#if defined(USE_WOKWI_LCD)
  // Wokwi sering tidak memodelkan deep-sleep + wake GPIO dengan benar.
  // Di sini "tidur" ditiru: layar sudah gelap, tunggu sentuhan lalu restart
  // chip (efeknya sama seperti bangun dari deep sleep).
#if DEBUG_SERIAL
  Serial.println("Wokwi: tidur. Klik SENTUH untuk bangun...");
  Serial.flush();
#endif
  while (gpio_get_level(PIN_TOUCH) == 0) delay(20);
  delay(150);
  waktuSimpanWokwi = time(nullptr);
  statusSinkronWokwi = waktuSudahSinkron;
  ESP.restart();
#else
  // Hardware asli: deep sleep, bangun saat pin sentuh jadi HIGH.
  esp_deep_sleep_enable_gpio_wakeup(1ULL << PIN_TOUCH, ESP_GPIO_WAKEUP_GPIO_HIGH);

#if DEBUG_SERIAL
  Serial.println("Masuk Deep Sleep stabil. Klik SENTUH untuk bangun...");
  Serial.flush();
#endif
  delay(100);
  esp_deep_sleep_start();
#endif
}

// ===================== SETUP =====================
void setup() {
#if DEBUG_SERIAL
  Serial.begin(115200);
#endif

  // CPU lebih lambat = arus lebih kecil (80 MHz masih cukup untuk WiFi)
  setCpuFrequencyMhz(FREKUENSI_CPU_MHZ);

  // Seed acak supaya pola tatapan/kedip beda tiap bangun
  randomSeed(esp_random());

  // Lepas kunci pin dari sesi tidur sebelumnya
  gpio_deep_sleep_hold_dis();
  gpio_hold_dis((gpio_num_t)PIN_TFT_CS);
  gpio_hold_dis((gpio_num_t)PIN_TFT_RST);
  gpio_hold_dis((gpio_num_t)PIN_TFT_DC);
  gpio_hold_dis((gpio_num_t)PIN_TFT_MOSI);
  gpio_hold_dis((gpio_num_t)PIN_TFT_SCK);
  gpio_hold_dis((gpio_num_t)PIN_TFT_BLK);

#if LED_ONBOARD_PIN >= 0
  // LED biru bawaan board dimatikan (aktif LOW)
  pinMode(LED_ONBOARD_PIN, OUTPUT);
  digitalWrite(LED_ONBOARD_PIN, HIGH);
#endif

  gpio_reset_pin(PIN_TOUCH);
  gpio_set_direction(PIN_TOUCH, GPIO_MODE_INPUT);
  gpio_pulldown_en(PIN_TOUCH);

  #if defined(USE_WOKWI_LCD)
  if (esp_reset_reason() == ESP_RST_SW && magicSimulasiWokwi == 0xCAFE1411) {
    waktuSudahSinkron = statusSinkronWokwi;
    hitungBangun = 1;
    if (waktuSudahSinkron && waktuSimpanWokwi > 1600000000) {
      struct timeval tv = { .tv_sec = waktuSimpanWokwi, .tv_usec = 0 };
      settimeofday(&tv, nullptr);
    }
  } else {
    magicSimulasiWokwi = 0xCAFE1411;
    statusSinkronWokwi = false;
    waktuSimpanWokwi = 0;
  }
  #endif

  hitungBangun++;

  mulaiLayar();

  // Kanvas sprite dialokasikan sebelum WiFi supaya heap tidak terpecah
  siapkanKanvasAwal();

  // Sinkron waktu kalau perlu (sekali di awal, lalu tiap beberapa hari)
  sinkronWaktuJikaPerlu();

  modeUltah  = cekUltah();
  warnaMata  = modeUltah ? WARNA_MATA_ULTAH : WARNA_MATA_BIASA;

  siapkanMata();

  if (modeUltah) {
    tulisUcapanUltah();
    detakHati(false);
  }

  // Wajah awal: pasang pose langsung (tanpa meluncur dari pose netral)
  moodWajah = modeUltah ? WAJAH_SENANG : WAJAH_KALEM;
  bukaSekarang = 0.0f;
  lengkungSekarang = modeUltah ? 1.00f : 0.40f;
  lebarSekarang = modeUltah ? 1.02f : 1.00f;
  alisMiringSekarang = 0.0f;
  alisLengkungSekarang = modeUltah ? 1.00f : 0.25f;
  alisNaikSekarang[0] = 0.0f;
  alisNaikSekarang[1] = 0.0f;
  nafasSekarang = 0.0f;
  gambarWajahLangsung();

  jalankanAnimasi();
  masukModeTidur();
}

void loop() {}
