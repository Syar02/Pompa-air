# Pompa Air ESP32 + MQTT Aman

Sistem sederhana untuk menyalakan dan mematikan pompa air dari dashboard web, lewat broker Mosquitto milik sendiri. Sistem ini terpisah dari proyek `hidroponik-stm32` dan tidak mengubahnya.

```
ESP32 + relay + pompa ──WiFi──► bore.pub:38883 ══tunnel══► laptop:8883  (Mosquitto: TLS + login + ACL)
Dashboard (browser di laptop) ──ws://localhost:8083──────────────────►  Mosquitto
```

| Fitur | Penerapan |
|---|---|
| Enkripsi | TLS; ESP32 memeriksa sertifikat broker dengan CA di `ca_cert.h` |
| Login | Akun `pompa-esp32` (perangkat) dan `dashboard`; tanpa login ditolak |
| Hak akses | ACL: hanya `dashboard` yang boleh mengirim perintah ke `set` |
| QoS 1 | Perintah, status, dan state dikirim dengan konfirmasi (PUBACK) |
| Sesi persisten | Perintah yang dikirim saat ESP32 offline diterima begitu ESP32 online lagi |
| Perintah basi ditolak | Dashboard memberi stempel waktu; ESP32 menolak perintah yang lebih tua dari 2 menit |
| Pengaman pompa | Pompa selalu mati otomatis setelah batas waktu (bawaan 10 menit, maks. 60), walau koneksi putus |
| LWT | Broker mengumumkan `offline` bila ESP32 putus tiba-tiba |

## Isi folder

| Path | Isi |
|---|---|
| `firmware/pompa_esp32/pompa_esp32.ino` | Firmware ESP32 (Arduino IDE) |
| `firmware/pompa_esp32/config.h` | Pin relay, broker, batas waktu |
| `firmware/pompa_esp32/rahasia.h` | WiFi & password broker. **Rahasia**, ada di `.gitignore` |
| `firmware/pompa_esp32/ca_cert.h` | Sertifikat CA broker (publik), dibuat `tools/buat_ca_cert.sh` |
| `dashboard/index.html` | Dashboard web (login, tombol pompa, log MQTT) |
| `broker/siapkan_broker.sh` | Menambah akun, ACL, dan listener WebSocket ke Mosquitto |
| `tools/buat_ca_cert.sh` | Membuat ulang `ca_cert.h` bila CA broker diganti |

## Langkah

### 1. Siapkan broker (sekali)

Butuh konfigurasi Tahap 2 yang sudah ada (`/etc/mosquitto/conf.d/hidro.conf`, `passwd`, `acl`).

```bash
cd /home/syar/data/pompa-esp32-mqtt
sudo ./broker/siapkan_broker.sh
```

Skrip meminta password baru untuk akun `pompa-esp32` (buat dengan `openssl rand -base64 18`). ACL hidroponik yang lama tidak diubah.

### 2. Jalankan tunnel (setiap kali dipakai)

```bash
bore local 8883 --to bore.pub --port 38883
```

Bila port 38883 sedang dipakai orang lain, pilih port lain, lalu ubah `MQTT_PORT` di `config.h`.

### 3. Wiring

| ESP32 | Modul relay | Keterangan |
|---|---|---|
| GPIO26 | IN | Ubah `PIN_RELAY` di `config.h` bila memakai pin lain |
| VIN (5 V) | VCC | Modul relay 5 V |
| GND | GND | |

Pompa disambung lewat kontak relay **COM** dan **NO** (normally open), dengan catu daya pompa sendiri. Dengan begitu pompa mati saat relay tidak aktif. Untuk pompa 220 V, pastikan semua sambungan terisolasi dan kerjakan dalam keadaan listrik mati.

Modul relay umumnya aktif LOW (`RELAY_AKTIF_LOW 1`). Bila relay justru menyala saat pompa seharusnya mati, ubah menjadi `0`.

### 4. Firmware (Arduino IDE)

1. **Board:** pasang paket *esp32 by Espressif Systems* (Boards Manager), pilih **ESP32 Dev Module**.
2. **Library:** di Library Manager cari `MQTT`, pasang **MQTT by Joel Gaehwiler** (versi 2.6.0). Bukan *PubSubClient*: PubSubClient hanya bisa mengirim dengan QoS 0.
3. **Salin folder** `firmware/pompa_esp32` ke Windows, misalnya `Documents\Arduino\pompa_esp32`. Arduino IDE kadang tidak bisa membuka folder di `\\wsl$`.
4. **Isi `rahasia.h`:** `WIFI_SSID`, `WIFI_PASS` (WiFi 2,4 GHz), dan `MQTT_PASS` (password akun `pompa-esp32` dari langkah 1). Compile berhenti dengan pesan error bila masih kosong.
5. **Upload**, lalu buka Serial Monitor (115200). Hasil yang benar:
   ```
   === Pompa air ESP32 + MQTT (TLS, login, QoS 1) ===
   [WIFI] menghubungkan ke "NamaWiFi"...
   [WIFI] tersambung, IP 192.168.1.23, sinyal -55 dBm
   [MQTT] menghubungkan ke bore.pub:38883 (TLS)... OK
   ```
   LED biru di board menyala = tersambung ke broker.

### 5. Dashboard

Buka `dashboard/index.html` di browser laptop, misalnya `\\wsl$\Ubuntu\home\syar\data\pompa-esp32-mqtt\dashboard\index.html` (nama distro WSL bisa berbeda). Masuk dengan:

- Alamat broker: `ws://localhost:8083`
- Kode perangkat: `pompa01`
- Username/password: akun `dashboard`

## Uji coba

| Uji | Cara | Hasil yang benar |
|---|---|---|
| Nyala/mati | Pilih durasi, klik **Nyalakan**, lalu **Matikan** | Relay berbunyi; log menampilkan `✓ PUBACK` lalu `← state` |
| Batas waktu | Nyalakan 1 menit, tunggu | Pompa mati sendiri, kejadian terakhir: `batas waktu habis` |
| Perintah saat offline | Cabut daya ESP32, tunggu status `Offline`, klik **Nyalakan** (1 menit), pasang lagi daya dalam 2 menit | Pompa menyala begitu ESP32 online (QoS 1 + sesi persisten) |
| Perintah basi | Seperti di atas, tetapi pasang daya setelah lebih dari 2 menit | Pompa tetap mati, kejadian terakhir: `perintah kedaluwarsa diabaikan` |
| Keamanan | `mosquitto_pub -h localhost -p 8883 --cafile /etc/mosquitto/certs/ca.crt -t aranus/pompa01/pompa/set -m on` | Ditolak: `not authorised` |

## Topik

`BASE = aranus/pompa01/pompa`

| Topik | Arah | Isi | QoS | Retained |
|---|---|---|---|---|
| `BASE/status` | ESP32 → dashboard | `online` / `offline` (LWT) | 1 | Ya |
| `BASE/state` | ESP32 → dashboard | `{"pompa":1,"sisa_s":540,"batas_s":600,"catatan":"…","rssi":-58,"up":120,"ts":…}`, tiap 5 dtk saat menyala, tiap 60 dtk saat mati, dan setiap ada perubahan | 1 | Ya |
| `BASE/set` | dashboard → ESP32 | `on` (10 menit), `on:<menit>` (1–60), `off`, `ping`; boleh diakhiri stempel waktu `@<epoch>` | 1 | Tidak |

## Bila bermasalah

| Gejala di Serial Monitor | Penyebab |
|---|---|
| `[WIFI] terputus, mencoba lagi...` terus | SSID/password salah, atau WiFi 5 GHz |
| `[MQTT] ... gagal (error .., kode 5)` | Login ditolak: `MQTT_PASS` salah atau akun belum dibuat (langkah 1) |
| `[MQTT] ... gagal (error -..., kode 0)` | Broker tidak terjangkau atau TLS gagal: cek tunnel bore, port, dan `ca_cert.h` |
| `subscribe perintah gagal (cek ACL)` | ACL belum memuat topik pompa: jalankan ulang langkah 1 |

Log broker di laptop: `sudo tail -f /var/log/mosquitto/mosquitto.log`.

Dashboard tidak bisa terhubung ke `ws://localhost:8083` dari browser Windows: pastikan `ss -ltn | grep 8083` menampilkan listener di WSL. Bila sudah ada tetapi tetap gagal, ubah baris `listener 8083 127.0.0.1` menjadi `listener 8083` di `hidro.conf`, restart broker, lalu pakai alamat `ws://<IP WSL>:8083` (lihat `hostname -I`).
# Pompa-air
