/*
 * ============================================================
 *  Pompa air ESP32 + MQTT aman
 * ============================================================
 * ESP32 menyalakan/mematikan pompa lewat relay, dikendalikan dari dashboard
 * melalui broker Mosquitto milik sendiri.
 *
 * Keamanan & keandalan:
 *   - TLS     : koneksi terenkripsi, sertifikat broker diperiksa dengan CA (ca_cert.h)
 *   - Login   : username/password per perangkat (rahasia.h); hak topik diatur ACL broker
 *   - QoS 1   : perintah & status dikirim dengan konfirmasi (PUBACK)
 *   - Sesi persisten (clean session = 0): perintah yang dikirim saat ESP32
 *               offline disimpan broker dan diterima begitu ESP32 online lagi
 *   - LWT     : broker mengumumkan "offline" bila ESP32 putus tiba-tiba
 *   - Pengaman: pompa SELALU mati otomatis setelah batas waktu, walau koneksi putus
 *
 * Topik (BASE = aranus/<KODE_PERANGKAT>/pompa):
 *   BASE/status  -> "online" / "offline"                       (retained, QoS 1, LWT)
 *   BASE/state   -> {"pompa":1,"sisa_s":..,"batas_s":..,"catatan":"..",
 *                    "rssi":..,"up":..,"ts":..}                (retained, QoS 1)
 *   BASE/set     <- on | on:<menit> | off | ping                (QoS 1)
 *                   boleh diberi stempel waktu: on:10@<epoch>   (ditolak bila > 2 menit)
 *
 * Board  : "ESP32 Dev Module"
 * Library: "MQTT" oleh Joel Gaehwiler (Library Manager), BUKAN PubSubClient:
 *          PubSubClient hanya bisa mengirim (publish) dengan QoS 0.
 */
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <MQTT.h>
#include <time.h>

#include "config.h"
#include "rahasia.h"
#include "ca_cert.h"

static_assert(sizeof(WIFI_SSID) > 1, "Isi WIFI_SSID di rahasia.h");
static_assert(sizeof(MQTT_PASS) > 1, "Isi MQTT_PASS di rahasia.h");

WiFiClientSecure net;
MQTTClient mqtt(512);

char T_STATUS[64], T_STATE[64], T_SET[64], CLIENT_ID[48];

/* ---------------- Pompa ---------------- */
bool pompaNyala = false;
uint32_t pompaMulaiMs = 0, pompaBatasMs = 0;
const char *catatan = "baru menyala";   /* kejadian terakhir, ikut dikirim di state */

/* Perintah dari callback MQTT diantrekan dulu: library ini melarang
 * publish/subscribe dari dalam callback. */
const int ANTREAN_MAKS = 8;
String antrean[ANTREAN_MAKS];
int nAntrean = 0;

bool kirimState = false;
uint32_t tState = 0, tSambung = 0;
bool wifiTerakhir = false;

int levelRelay(bool nyala) { return (nyala != (RELAY_AKTIF_LOW != 0)) ? HIGH : LOW; }

void setPompa(bool nyala, uint32_t batasMs, const char *alasan)
{
  pompaNyala = nyala;
  pompaMulaiMs = millis();
  pompaBatasMs = nyala ? batasMs : 0;
  catatan = alasan;
  digitalWrite(PIN_RELAY, levelRelay(nyala));
  if (nyala) Serial.printf("[POMPA] NYALA selama %lu menit (%s)\n", (unsigned long)(batasMs / 60000UL), alasan);
  else Serial.printf("[POMPA] MATI (%s)\n", alasan);
  kirimState = true;
}

uint32_t sisaDetik()
{
  if (!pompaNyala) return 0;
  uint32_t jalan = millis() - pompaMulaiMs;
  return jalan >= pompaBatasMs ? 0 : (pompaBatasMs - jalan + 999) / 1000;
}

bool waktuValid() { return time(nullptr) > 1700000000; }

/* ---------------- Perintah ---------------- */
void onPesan(String &topic, String &payload)
{
  if (nAntrean < ANTREAN_MAKS) antrean[nAntrean++] = payload;
  else Serial.println("[MQTT] antrean perintah penuh, pesan dibuang");
}

void prosesPerintah(String p)
{
  p.trim();
  p.toLowerCase();
  Serial.printf("[MQTT] perintah: %s\n", p.c_str());

  /* Stempel waktu opsional "...@<epoch>": tolak perintah lama yang tertahan di
   * broker terlalu lama (mis. "on" yang dikirim berjam-jam lalu saat ESP32 mati). */
  int at = p.indexOf('@');
  if (at >= 0) {
    long t = p.substring(at + 1).toInt();
    p = p.substring(0, at);
    p.trim();
    if (t > 0 && waktuValid()) {
      long umur = (long)time(nullptr) - t;
      if (umur > PERINTAH_KEDALUWARSA_S || umur < -PERINTAH_KEDALUWARSA_S) {
        Serial.printf("[MQTT] perintah \"%s\" diabaikan: umurnya %ld detik\n", p.c_str(), umur);
        catatan = "perintah kedaluwarsa diabaikan";
        kirimState = true;
        return;
      }
    }
  }

  if (p == "off") {
    setPompa(false, 0, "dimatikan dari dashboard");
  } else if (p == "on" || p.startsWith("on:")) {
    long menit = p == "on" ? BATAS_DEFAULT_MENIT : p.substring(3).toInt();
    if (menit < 1 || menit > BATAS_MAKS_MENIT) {
      Serial.printf("[MQTT] durasi tidak valid: %ld menit (1-%d)\n", menit, BATAS_MAKS_MENIT);
      catatan = "durasi tidak valid";
      kirimState = true;
      return;
    }
    setPompa(true, (uint32_t)menit * 60000UL, "dinyalakan dari dashboard");
  } else if (p == "ping") {
    kirimState = true;
  } else {
    Serial.printf("[MQTT] perintah tidak dikenal: %s\n", p.c_str());
  }
}

/* ---------------- MQTT ---------------- */
void publishState()
{
  char buf[220];
  snprintf(buf, sizeof(buf),
           "{\"pompa\":%d,\"sisa_s\":%lu,\"batas_s\":%lu,\"catatan\":\"%s\",\"rssi\":%d,\"up\":%lu,\"ts\":%ld}",
           pompaNyala ? 1 : 0, (unsigned long)sisaDetik(), (unsigned long)(pompaBatasMs / 1000UL), catatan,
           (int)WiFi.RSSI(), (unsigned long)(millis() / 1000UL), waktuValid() ? (long)time(nullptr) : 0L);
  if (!mqtt.publish(T_STATE, buf, true, 1)) Serial.println("[MQTT] kirim state gagal");
}

bool sambungMqtt()
{
  Serial.printf("[MQTT] menghubungkan ke %s:%d (TLS)... ", MQTT_HOST, MQTT_PORT);
  /* Sisa antrean lokal dibuang SEBELUM tersambung. Perintah tertunda dari broker
   * datang setelah CONNACK, jadi tidak ikut terbuang. */
  nAntrean = 0;
  if (!mqtt.connect(CLIENT_ID, MQTT_USER, MQTT_PASS)) {
    /* returnCode 5 = login ditolak; lastError negatif = gagal jaringan/TLS */
    Serial.printf("gagal (error %d, kode %d)\n", (int)mqtt.lastError(), (int)mqtt.returnCode());
    return false;
  }
  Serial.println("OK");
  mqtt.publish(T_STATUS, "online", true, 1);
  if (!mqtt.subscribe(T_SET, 1)) {
    Serial.println("[MQTT] subscribe perintah gagal (cek ACL)");
    mqtt.disconnect();
    return false;
  }
  kirimState = true;
  return true;
}

/* ---------------- Setup & loop ---------------- */
void setup()
{
  /* Relay dimatikan dulu sebelum pin dijadikan output: pompa tidak sempat menyala saat boot */
  digitalWrite(PIN_RELAY, levelRelay(false));
  pinMode(PIN_RELAY, OUTPUT);
  pinMode(PIN_LED, OUTPUT);
  digitalWrite(PIN_LED, LOW);

  Serial.begin(115200);
  delay(200);
  Serial.println("\n=== Pompa air ESP32 + MQTT (TLS, login, QoS 1) ===");

  snprintf(T_STATUS, sizeof(T_STATUS), "aranus/%s/pompa/status", KODE_PERANGKAT);
  snprintf(T_STATE, sizeof(T_STATE), "aranus/%s/pompa/state", KODE_PERANGKAT);
  snprintf(T_SET, sizeof(T_SET), "aranus/%s/pompa/set", KODE_PERANGKAT);
  /* Client ID harus TETAP agar broker mengenali sesi persisten yang sama */
  snprintf(CLIENT_ID, sizeof(CLIENT_ID), "pompa-esp32-%s", KODE_PERANGKAT);

  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  Serial.printf("[WIFI] menghubungkan ke \"%s\"...\n", WIFI_SSID);

  configTime(TZ_OFFSET_S, 0, "pool.ntp.org", "time.google.com");

  net.setCACert(CA_CERT);
  mqtt.begin(MQTT_HOST, MQTT_PORT, net);
  mqtt.setKeepAlive(60);
  mqtt.setCleanSession(false);   /* sesi persisten */
  mqtt.setTimeout(10000);        /* TLS lewat tunnel bisa lambat */
  mqtt.setWill(T_STATUS, "offline", true, 1);
  mqtt.onMessage(onPesan);
}

void loop()
{
  uint32_t now = millis();

  /* Pengaman: dicek di setiap putaran, apa pun kondisi jaringan */
  if (pompaNyala && now - pompaMulaiMs >= pompaBatasMs) setPompa(false, 0, "batas waktu habis");

  bool wifiOk = WiFi.status() == WL_CONNECTED;
  if (wifiOk != wifiTerakhir) {
    wifiTerakhir = wifiOk;
    if (wifiOk) Serial.printf("[WIFI] tersambung, IP %s, sinyal %d dBm\n", WiFi.localIP().toString().c_str(), (int)WiFi.RSSI());
    else Serial.println("[WIFI] terputus, mencoba lagi...");
  }
  if (!wifiOk) { digitalWrite(PIN_LED, LOW); delay(10); return; }

  mqtt.loop();
  if (!mqtt.connected()) {
    digitalWrite(PIN_LED, LOW);
    /* Tunggu jam NTP (maks. 20 dtk sejak menyala): sertifikat diperiksa masa berlakunya */
    if (!waktuValid() && now < 20000UL) { delay(10); return; }
    if (tSambung == 0 || now - tSambung >= 5000UL) {
      tSambung = now;
      sambungMqtt();
    }
    delay(10);
    return;
  }
  digitalWrite(PIN_LED, HIGH);

  while (nAntrean > 0) {
    String p = antrean[0];
    for (int i = 1; i < nAntrean; i++) antrean[i - 1] = antrean[i];
    nAntrean--;
    prosesPerintah(p);
  }

  uint32_t interval = pompaNyala ? INTERVAL_STATE_NYALA_MS : INTERVAL_STATE_MATI_MS;
  if (kirimState || now - tState >= interval) {
    kirimState = false;
    tState = now;
    publishState();
  }
  delay(10);
}
