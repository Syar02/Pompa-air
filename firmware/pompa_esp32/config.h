/*
 * ============================================================
 *  KONFIGURASI  —  Pompa air ESP32 + MQTT (TLS, login, QoS 1)
 * ============================================================
 * Pengaturan yang tidak rahasia. WiFi & password broker ada di rahasia.h.
 */
#ifndef CONFIG_H
#define CONFIG_H

/* Kode perangkat -> topik BASE = aranus/<KODE_PERANGKAT>/pompa
 * Harus sama dengan ACL di broker dan kolom "Kode perangkat" di dashboard. */
#define KODE_PERANGKAT      "pompa01"

/* Broker: Mosquitto di laptop, diakses lewat tunnel
 *   bore local 8883 --to bore.pub --port 38883                              */
#define MQTT_HOST           "bore.pub"
#define MQTT_PORT           38883

/* Relay pompa */
#define PIN_RELAY           26      /* GPIO ke pin IN modul relay              */
#define RELAY_AKTIF_LOW     1       /* 1 = relay menyala saat pin LOW (umum)   */

/* LED status (LED bawaan board ESP32 Dev Module): nyala = tersambung MQTT */
#define PIN_LED             2

/* Pengaman pompa */
#define BATAS_DEFAULT_MENIT 10      /* "on" tanpa durasi -> mati otomatis setelah ini */
#define BATAS_MAKS_MENIT    60      /* durasi terpanjang yang diterima ("on:60")      */
#define PERINTAH_KEDALUWARSA_S 120  /* perintah berstempel waktu lebih tua dari ini diabaikan */

/* Interval kirim status (ms) */
#define INTERVAL_STATE_NYALA_MS  5000UL    /* saat pompa menyala (hitung mundur) */
#define INTERVAL_STATE_MATI_MS   60000UL   /* saat pompa mati                    */

/* Zona waktu (WIB = UTC+7), dipakai untuk jam NTP */
#define TZ_OFFSET_S         (7 * 3600)

#endif
