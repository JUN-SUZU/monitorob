#!/bin/bash
# install_service.sh
# monitorobサーバーをsystemdに登録するスクリプト

if [ "$EUID" -ne 0 ]; then
  echo "Please run as root (sudo bash install_service.sh)"
  exit 1
fi

# 現在のディレクトリと実行ファイルのパスを取得
CURRENT_DIR=$(pwd)
SERVER_BIN="$CURRENT_DIR/monitorob_server"

if [ ! -f "$SERVER_BIN" ]; then
  echo "Error: monitorob_server not found in current directory."
  exit 1
fi

SERVICE_FILE="/etc/systemd/system/monitorob.service"
USER_NAME=$SUDO_USER

echo "Installing monitorob.service..."

cat <<EOF > $SERVICE_FILE
[Unit]
Description=monitorob Process Supervisor Daemon
After=network.target

[Service]
Type=simple
User=$USER_NAME
WorkingDirectory=$CURRENT_DIR
ExecStart=$SERVER_BIN
Restart=always
RestartSec=5
# ログはjournalctlで管理されるため標準出力のままでOK

[Install]
WantedBy=multi-user.target
EOF

systemctl daemon-reload
systemctl enable monitorob.service
systemctl start monitorob.service

echo "Done! monitorob server is now running as a daemon."
echo "Check status with: systemctl status monitorob.service"
echo "Check logs with: journalctl -u monitorob.service -f"
