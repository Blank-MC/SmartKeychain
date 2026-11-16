# Smart Digital Keychain — Erika (14 November)

Gantungan kunci digital: disentuh → layar nyala → main 1 animasi 10 detik →
backlight mati → deep sleep. Di tanggal **14 November** animasinya otomatis
berganti jadi tema ulang tahun.

## Struktur file

```
src/
├── config.h             pinout, palet, DURASI_NYALA, tanggal ultah, helper gambar
├── animations_daily.h   6 animasi hari biasa
├── animations_bday.h    2 animasi ulang tahun
└── main.cpp             power management, RTC memory, NTP, randomizer
backup/legacy_main_v1.cpp.txt   versi lama (arsip, tidak dikompilasi)
```

## Pinout

| Fungsi | GPIO |
|---|---|
| TFT SCK | 4 |
| TFT MOSI | 6 |
| TFT CS | 7 |
| TFT DC | 2 |
| TFT RST | 3 |
| TFT BLK (backlight) | 5 |
| TOUCH (TTP223 / pushbutton) | 1 |

## Pilih layar

Buka `src/config.h`:

```cpp
#define USE_WOKWI_LCD   // aktif      -> ILI9341 320x240 (simulasi Wokwi)
// #define USE_WOKWI_LCD  // dikomentari -> ST7789 240x240 (hardware asli)
```

Semua koordinat animasi dihitung dari titik tengah (`ax()` / `ay()`), jadi
layout otomatis pas di kedua ukuran layar.

## Build & jalankan

```powershell
# build
& "$env:USERPROFILE\.platformio\penv\Scripts\platformio.exe" run -d D:\SmartKeychain

# upload ke board
& "$env:USERPROFILE\.platformio\penv\Scripts\platformio.exe" run -d D:\SmartKeychain -t upload

# monitor serial
& "$env:USERPROFILE\.platformio\penv\Scripts\platformio.exe" device monitor -d D:\SmartKeychain
```

Simulasi: buka folder ini di VSCode → `F1` → **Wokwi: Start Simulator**
(`wokwi.toml` sudah menunjuk ke `.pio/build/esp32-c3-devkitm-1/firmware.bin`,
jadi build dulu).

## Ubah nama / tanggal / durasi

Semua di `src/config.h`:

```cpp
#define DURASI_NYALA        10000UL   // lama animasi per sentuhan
#define SIMULASI_HARI_ULTAH false     // true = paksa mode ultah untuk testing
#define TARGET_TANGGAL 14
#define TARGET_BULAN   11
```

Nama "ERIKA!" ada di `animations_bday.h` (2 tempat) dan `animations_daily.h`
(teks "HALO ERIKA!").

## Cara kerja hemat daya

- `RTC_DATA_ATTR` menyimpan status NTP + indeks animasi terakhir di RTC slow
  memory, jadi tetap ada walau deep sleep.
- WiFi untuk NTP dicoba sekali saat pertama kali menyala, dengan waktu tunggu
  koneksi hingga 8 detik. Status sinkron dan waktu disimpan di memori RTC;
  pada simulasi Wokwi, nilainya dipulihkan setelah `ESP.restart()` agar tidak
  mengulang sinkronisasi setiap kali bangun.
- Sebelum tidur: backlight dimatikan, layar dibersihkan, pin sentuh diberi
  pull-down, wakeup GPIO level HIGH.

## Catatan hardware asli

1. **TTP223** untuk sensor sentuh (ESP32-C3 tidak punya touch kapasitif native).
   Output modul ini HIGH saat disentuh — cocok dengan logika kode.
2. **GPIO2 = strapping pin.** Kalau board kadang gagal boot, pindahkan `TFT_DC`
   ke GPIO10 atau pastikan level DC saat reset tidak mengganggu boot.
3. **Backlight jangan langsung dari GPIO** di PCB final. Pakai transistor
   (2N2222 / AO3400) supaya arus LED tidak lewat pin MCU.
4. **Daya:** Li-Po 3.7V 250mAh → TP4056 (charge, Type-C) → LDO 3.3V
   (ME6211 / HT7333). Hindari boost converter untuk kasus tidur lama.
5. `setCpuFrequencyMhz(80)` dipakai; SPI tetap 40 MHz.

## Urutan animasi (anti-kembar)

Harian: `0` mata robot, `1` melambai, `2` mata love, `3` ngantuk,
`4` mata bintang, `5` tersipu.
Ulang tahun: `0` emoji pesta, `1` kue tart.

`pilihAcak()` memakai `esp_random()` dan menyimpan indeks terakhir di RTC,
jadi animasi yang sama tidak muncul dua kali berturut-turut.
