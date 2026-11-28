#!/usr/bin/env bash
# SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
# SPDX-License-Identifier: MIT
#
# Make a long-lived self-signed code signing certificate for CI's macOS packages:
#   scripts/dev/make-signing-cert.sh [out-dir]
#
# macOS remembers Accessibility, Input Monitoring and Screen Recording per code signature.
# Ad-hoc signatures change with every build, so each update loses those permissions. Signed
# with one certificate, every build has the same designated requirement and keeps them.
#
# Add the two printed values as repository secrets (Settings > Secrets and variables > Actions):
#   MACOS_SIGNING_P12      contents of signing.p12.base64
#   MACOS_SIGNING_P12_PWD  contents of signing.p12.password
# Keep the files private: anyone with them can sign an app macOS treats as this Deskflow.
# Making a new certificate later means allowing the permissions once more.
set -euo pipefail

OUT=${1:-deskflow-signing}
NAME=${DESKFLOW_SIGNING_NAME:-Deskflow Self-Signed}
mkdir -p "$OUT"
umask 077

cat > "$OUT/cert.cnf" <<EOF
[req]
distinguished_name = dn
x509_extensions = ext
prompt = no
[dn]
CN = $NAME
[ext]
basicConstraints = critical, CA:false
keyUsage = critical, digitalSignature
extendedKeyUsage = critical, codeSigning
subjectKeyIdentifier = hash
EOF

openssl req -x509 -newkey rsa:2048 -sha256 -days 7300 -nodes \
  -keyout "$OUT/key.pem" -out "$OUT/cert.pem" -config "$OUT/cert.cnf" 2>/dev/null

PASSWORD=$(openssl rand -hex 16)
# macOS `security import` only reads the older PKCS#12 algorithms.
LEGACY=()
openssl pkcs12 -help 2>&1 | grep -q -- -legacy && LEGACY=(-legacy)
openssl pkcs12 -export "${LEGACY[@]}" -keypbe PBE-SHA1-3DES -certpbe PBE-SHA1-3DES -macalg sha1 \
  -inkey "$OUT/key.pem" -in "$OUT/cert.pem" -name "$NAME" -passout "pass:$PASSWORD" -out "$OUT/signing.p12"

base64 < "$OUT/signing.p12" | tr -d '\n' > "$OUT/signing.p12.base64"
printf '%s' "$PASSWORD" > "$OUT/signing.p12.password"
rm -f "$OUT/key.pem" "$OUT/cert.cnf"

echo "Certificate: $NAME, valid 20 years"
openssl x509 -in "$OUT/cert.pem" -noout -fingerprint -sha1
echo
echo "Add these repository secrets:"
echo "  MACOS_SIGNING_P12      = contents of $OUT/signing.p12.base64"
echo "  MACOS_SIGNING_P12_PWD  = contents of $OUT/signing.p12.password"
