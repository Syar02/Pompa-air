#!/usr/bin/env bash
# Ubah sertifikat CA broker (PEM) menjadi firmware/pompa_esp32/ca_cert.h.
# ESP32 memakai sertifikat ini untuk memastikan ia terhubung ke broker yang benar.
#
#   ./tools/buat_ca_cert.sh                     # dari /etc/mosquitto/certs/ca.crt
#   ./tools/buat_ca_cert.sh /path/ke/ca.crt     # dari file lain
#
# Sertifikat CA bersifat publik (bukan rahasia). JANGAN memakai ca.key di sini.
set -euo pipefail
cd "$(dirname "$0")/.."

CA=${1:-/etc/mosquitto/certs/ca.crt}
OUT=firmware/pompa_esp32/ca_cert.h

grep -q "BEGIN CERTIFICATE" "$CA" || { echo "Bukan sertifikat PEM: $CA"; exit 1; }
if grep -q "PRIVATE KEY" "$CA"; then echo "File berisi kunci privat, dibatalkan: $CA"; exit 1; fi

{
  echo "/* Sertifikat CA broker MQTT (publik, bukan rahasia)."
  echo " * Dibuat otomatis oleh tools/buat_ca_cert.sh dari $CA."
  echo " * Jangan diedit manual: jalankan ulang skrip itu bila CA broker diganti. */"
  echo "#ifndef CA_CERT_H"
  echo "#define CA_CERT_H"
  echo
  echo "static const char CA_CERT[] = R\"PEM("
  sed 's/\r$//' "$CA"
  echo ")PEM\";"
  echo
  echo "#endif"
} > "$OUT"
echo "Ditulis: $OUT"
