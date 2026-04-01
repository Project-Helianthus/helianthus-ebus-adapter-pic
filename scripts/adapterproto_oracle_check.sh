#!/bin/sh
set -eu

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname "$0")" && pwd)
PROJECT_DIR=$(CDPATH= cd -- "$SCRIPT_DIR/.." && pwd)
WORKSPACE_DIR=$(CDPATH= cd -- "$PROJECT_DIR/../.." && pwd)
BUILD_DIR=${BUILD_DIR:-"$PROJECT_DIR/build"}
PICFW_ORACLE_BIN="$BUILD_DIR/picfw_oracle_check"
C_JSON="$BUILD_DIR/adapterproto_oracle_c.json"
GO_JSON="$BUILD_DIR/adapterproto_oracle_go.json"

mkdir -p "$BUILD_DIR"

"$PICFW_ORACLE_BIN" --json > "$C_JSON"
(cd "$WORKSPACE_DIR/helianthus-tinyebus" && GOWORK=off go run ./cmd/adapterproto-oracle) > "$GO_JSON"

if ! cmp -s "$C_JSON" "$GO_JSON"; then
  diff -u "$C_JSON" "$GO_JSON"
  exit 1
fi

echo "adapterproto oracle parity check passed"
