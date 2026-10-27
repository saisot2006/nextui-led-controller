#!/bin/sh
cd "$(dirname "$0")"
BIN=./ledbp
[ -f "$BIN" ] || BIN=$(ls -d ./*ledbp 2>/dev/null | head -1)
if [ -z "$BIN" ] || [ ! -f "$BIN" ]; then
  { echo "khong thay file ledbp trong:"; ls -l; } > ledbp.log 2>&1
  exit 1
fi
chmod +x "$BIN" 2>/dev/null
"$BIN" > ledbp.log 2>&1
echo "exit=$?" >> ledbp.log
