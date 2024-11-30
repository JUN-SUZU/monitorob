# monitorob - Server monitoring application

Run your server by this application and monitor it from your graphical terminal.

## Installation

```bash
git clone https://github.com/JUN-SUZU/monitorob.git
g++ -o monitorob monitorob.cpp
```

## How to use

Write your server's information in `servers.conf` file.

```conf
; servers.conf
; 1行に1つのサーバーの設定
; 要素はスペース区切り 波かっこ2つで囲む例: {{}} {{}}
; 空行を間に挟むとエラーになる
; 1列目: サーバーの名前
; 2列目: サーバーのルートディレクトリの絶対パス
; 3列目: サーバーの起動コマンド(コマンドは/bin不要, ファイル名はフルパス)
; 4列目: 設定したいサーバーの状態(0: offline, 1: online)(0でオンラインになっていたら停止する, 1でオフラインになっていたら起動する)
; 例
; {{server1}} {{/var/www/html}} {{node /var/www/html/index.js}} {{0}}
; {{server2}} {{/home/user/projects/app}} {{node /home/jun/projects/app/index.js}} {{1}}
```

## Contributing

Pull requests are welcome. For major changes, please open an issue first to discuss what you would like to change.

## License

[MIT](https://choosealicense.com/licenses/mit/)
