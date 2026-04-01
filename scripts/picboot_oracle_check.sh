#!/usr/bin/env sh
set -eu

ROOT_DIR="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
BUILD_DIR="${ROOT_DIR}/build"
BIN="${BUILD_DIR}/picboot_oracle_check"

mkdir -p "${BUILD_DIR}"

: "${CC:=cc}"
: "${CFLAGS:=-std=c11 -Wall -Wextra -Werror -pedantic}"

"${CC}" ${CFLAGS} \
  -I"${ROOT_DIR}/bootloader/include" \
  "${ROOT_DIR}/bootloader/src/picboot.c" \
  "${ROOT_DIR}/tools/picboot_oracle_check.c" \
  -o "${BIN}"

exec "${BIN}" "$@"
