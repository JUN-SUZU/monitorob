#!/bin/bash
# setup_tui.sh

if [ "$EUID" -ne 0 ]; then
  echo "Please run as root (sudo bash setup_tui.sh)"
  exit 1
fi

CURRENT_DIR=$(pwd)
TUI_BIN="$CURRENT_DIR/monitorob_tui"

if [ ! -f "$TUI_BIN" ]; then
  echo "Error: monitorob_tui not found in current directory."
  exit 1
fi

# /usr/local/bin にシンボリックリンクを作成
ln -sf "$TUI_BIN" /usr/local/bin/monitorob

echo "Done! You can now type 'monitorob' from anywhere to open the dashboard."
