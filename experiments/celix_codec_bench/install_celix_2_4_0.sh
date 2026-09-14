#!/usr/bin/env bash
# Reproducibly fetch, verify (SHA-512), build, and install Apache Celix 2.4.0.
set -euo pipefail

VERSION="2.4.0"
ARCHIVE_URL="https://archive.apache.org/dist/celix/celix-${VERSION}/celix-${VERSION}.tar.gz"
SHA512_URL="${ARCHIVE_URL}.sha512"
EXPECTED_SHA512="76FB2BA448028894841E7315F62E864A0913144A528B666106B4946A0044B45AF13629A558E627DE4EA6331788BAE482B63601AEE669E44BBC32435CC0B72FF0"

PREFIX="${1:-/opt/celix-2.4.0}"
WORK="${CELIX_BUILD_ROOT:-${TMPDIR:-/tmp}/celix-pin-${VERSION}}"
SRC="${WORK}/celix-${VERSION}"
BUILD="${WORK}/build"
ARCHIVE="${WORK}/celix-${VERSION}.tar.gz"

mkdir -p "${WORK}"
echo "Downloading ${ARCHIVE_URL}"
curl -fsSL -o "${ARCHIVE}" "${ARCHIVE_URL}"
curl -fsSL -o "${ARCHIVE}.sha512" "${SHA512_URL}"

ACTUAL="$(sha512sum "${ARCHIVE}" | awk '{print toupper($1)}')"
ASF_HEX="$(tr -d ' \n\t\r' < "${ARCHIVE}.sha512" | sed -E 's/^[^:]*://; s/[^0-9A-Fa-f]//g' | tr 'a-f' 'A-F')"

if [[ "${ACTUAL}" != "${EXPECTED_SHA512}" ]]; then
  echo "SHA-512 mismatch against pinned constant" >&2
  echo "  expected ${EXPECTED_SHA512}" >&2
  echo "  actual   ${ACTUAL}" >&2
  exit 1
fi
if [[ "${ACTUAL}" != "${ASF_HEX}" ]]; then
  echo "SHA-512 mismatch against ASF .sha512 file" >&2
  echo "  asf      ${ASF_HEX}" >&2
  echo "  actual   ${ACTUAL}" >&2
  exit 1
fi
echo "SHA-512 OK (${ACTUAL:0:8}…${ACTUAL: -8})"

rm -rf "${SRC}" "${BUILD}"
tar -xzf "${ARCHIVE}" -C "${WORK}"
mkdir -p "${BUILD}" "${PREFIX}"

cmake -S "${SRC}" -B "${BUILD}" -G Ninja \
  -DCMAKE_INSTALL_PREFIX="${PREFIX}" \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_CXX_STANDARD=17 \
  -DCELIX_CXX17=ON \
  -DENABLE_TESTING=OFF \
  -DBUILD_PUBSUB=OFF \
  -DBUILD_REMOTE_SERVICE_ADMIN=OFF \
  -DBUILD_CXX_REMOTE_SERVICE_ADMIN=OFF \
  -DBUILD_DEPLOYMENT_ADMIN=OFF \
  -DBUILD_HTTP_ADMIN=OFF \
  -DBUILD_EXAMPLES=OFF \
  -DBUILD_SHELL_WUI=OFF \
  -DBUILD_REMOTE_SHELL=OFF \
  -DBUILD_CELIX_ETCDLIB=OFF \
  -DBUILD_PROMISES=OFF \
  -DBUILD_PUSHSTREAMS=OFF

cmake --build "${BUILD}" -j"$(nproc)"
cmake --install "${BUILD}"

echo "Installed Apache Celix ${VERSION} -> ${PREFIX}"
echo "export CMAKE_PREFIX_PATH=\"${PREFIX}\${CMAKE_PREFIX_PATH:+:\$CMAKE_PREFIX_PATH}\""
