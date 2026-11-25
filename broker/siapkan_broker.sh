#!/usr/bin/env bash
# Menyiapkan Mosquitto di laptop untuk sistem pompa ESP32.
# Melanjutkan konfigurasi Tahap 2 (TLS 8883, password, ACL) yang sudah ada:
#   1. akun broker "pompa-esp32" (perangkat) dan "dashboard" (bila belum ada)
#   2. ACL topik aranus/pompa01/pompa/...   (ditambahkan, ACL lama tidak diubah)
#   3. listener WebSocket 127.0.0.1:8083 untuk dashboard (bila belum ada)
# Aman dijalankan berulang: bagian yang sudah ada dilewati.
#
#   sudo ./broker/siapkan_broker.sh
set -euo pipefail

KODE=pompa01
BASE="aranus/$KODE/pompa"
PW=/etc/mosquitto/passwd
ACL=/etc/mosquitto/acl
CONF=/etc/mosquitto/conf.d/hidro.conf

[ "$(id -u)" = 0 ] || { echo "Jalankan dengan sudo: sudo $0"; exit 1; }
for f in "$PW" "$ACL" "$CONF"; do
  [ -f "$f" ] || { echo "Tidak ditemukan: $f (selesaikan Tahap 2 dulu)"; exit 1; }
done

echo "== 1. Akun broker =="
for u in pompa-esp32 dashboard; do
  if grep -q "^$u:" "$PW"; then
    echo "   $u sudah ada (lewati). Ganti password: sudo mosquitto_passwd $PW $u"
  else
    echo "   Buat password untuk akun $u (saran: openssl rand -base64 18):"
    mosquitto_passwd "$PW" "$u"
  fi
done

echo "== 2. ACL $BASE =="
if grep -q "$BASE" "$ACL"; then
  echo "   sudah ada (lewati)"
else
  cat >> "$ACL" << EOF

# --- Sistem pompa ESP32 ($KODE) ---
# Perangkat: hanya kirim status & state, hanya baca perintah (tanpa wildcard di write)
user pompa-esp32
topic write $BASE/status
topic write $BASE/state
topic read  $BASE/set

# Dashboard: baca semua topik pompa, hanya kirim perintah
user dashboard
topic read  $BASE/#
topic write $BASE/set
EOF
  echo "   ditambahkan"
fi

echo "== 3. Listener WebSocket untuk dashboard =="
if grep -q "protocol websockets" "$CONF"; then
  echo "   sudah ada (lewati)"
else
  cat >> "$CONF" << 'EOF'

# WebSocket untuk dashboard (hanya bisa diakses dari laptop ini)
listener 8083 127.0.0.1
protocol websockets
EOF
  echo "   ditambahkan (127.0.0.1:8083)"
fi

chown mosquitto:mosquitto "$PW" "$ACL"
chmod 600 "$PW" "$ACL"

echo "== 4. Restart broker =="
service mosquitto restart
sleep 1
if ! service mosquitto status > /dev/null 2>&1; then
  echo "   GAGAL start. Lihat: tail -20 /var/log/mosquitto/mosquitto.log"; exit 1
fi
echo "   broker aktif, port terbuka:"
ss -ltn | grep -E ':(8883|8083)\b' | sed 's/^/   /'
echo
echo "Selesai. Pastikan tunnel jalan:  bore local 8883 --to bore.pub --port 38883"
