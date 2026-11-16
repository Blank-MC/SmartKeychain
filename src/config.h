#pragma once

// ============================================================
//  SMART KEYCHAIN - konfigurasi terpusat
//  Ubah di sini saja, main.cpp tidak perlu disentuh.
// ============================================================

// ------------------------------------------------------------
//  PILIH LAYAR
//    didefinisikan  -> ILI9341 320x240 (Wokwi / modul SPI)
//    dikomentari    -> ST7789  240x280 (hardware asli)
// ------------------------------------------------------------
#define USE_WOKWI_LCD

// ------------------------------------------------------------
//  ORIENTASI TAMPILAN (rotation Adafruit GFX)
//    0 = 240 x 320  (berdiri / portrait) <-- native ILI9341
//    1 = 320 x 240  (rebah / landscape)
//  Pakai 0 supaya driver menggambar di koordinat asli panel
//  (240x320) -> background pasti menutup seluruh layar.
// ------------------------------------------------------------
#define ORIENTASI_LAYAR 1

// Pin SPI (sama untuk ILI9341 & ST7789)
#define PIN_TFT_CS    7
#define PIN_TFT_DC    2
#define PIN_TFT_RST   3
#define PIN_TFT_MOSI  6
#define PIN_TFT_SCK   4
#define PIN_TFT_BLK   5

// Tombol / sensor sentuh (aktif HIGH, dipakai juga sebagai wake pin)
// GPIO_NUM_1 = pin 1, sekaligus tipe yang benar untuk driver gpio ESP-IDF
#define PIN_TOUCH     GPIO_NUM_1

// ------------------------------------------------------------
//  TANGGAL ULANG TAHUN
// ------------------------------------------------------------
#define SIMULASI_HARI_ULTAH  false
#define TARGET_TANGGAL       14
#define TARGET_BULAN         11

// ------------------------------------------------------------
//  WIFI UNTUK SINKRON WAKTU (NTP)
//  Ganti ke hotspot HP saat dipakai di hardware asli.
// ------------------------------------------------------------
#define WIFI_SSID     "Wokwi-GUEST"
#define WIFI_PASSWORD ""

// ------------------------------------------------------------
//  DURASI
// ------------------------------------------------------------
#define DURASI_NYALA  10000UL   // lama animasi tampil per bangun (ms)

// ------------------------------------------------------------
//  WARNA (RGB565, aman untuk ILI9341 maupun ST7789)
// ------------------------------------------------------------
#define WARNA_LATAR        0x0884   // biru malam (8,16,32)
#define WARNA_MATA_BIASA   0x07FF   // cyan (0,255,248)
#define WARNA_MATA_ULTAH   0xFB56   // pink lembut (248,170,176)
#define WARNA_TEKS         0xFFFF   // putih
#define WARNA_AKSEN        0xFED4   // krem hangat (255,220,160)
#define WARNA_HATI_TERANG  0xF800   // merah
#define WARNA_HATI_LEMBUT  0xFB56   // pink

// ------------------------------------------------------------
//  HEMAT DAYA
// ------------------------------------------------------------
#define KECERAHAN_LAYAR    55     // backlight (%) khusus hardware asli (PWM)
#define FREKUENSI_CPU_MHZ  80     // 80 MHz cukup untuk animasi + WiFi
#define RESYNC_HARI_KE     0      // sinkron NTP hanya saat pertama nyala
#define PERCOBAAN_SINKRON  20     // kalau WiFi gagal, coba lagi tiap N kali bangun
#define LED_ONBOARD_PIN    8      // LED biru ESP32-C3 SuperMini (aktif LOW), -1 = tidak ada
#define DEBUG_SERIAL       0      // 1 = aktifkan Serial (buat debug), 0 = hemat daya

// ------------------------------------------------------------
//  MODE GAMBAR LANGSUNG UNTUK TFT
//  (library RoboEyes default-nya butuh framebuffer OLED)
// ------------------------------------------------------------
#define ROBOEYES_TFT_MODE 1
