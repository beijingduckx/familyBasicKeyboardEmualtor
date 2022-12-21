# Family Basic Keyboard Emulator

Amazonなどで売られているEZ-USB FX2LP Development board と Windows PCを使って、実機のFamily Basic Keyboardをエミュレートします。  
実験的なプロジェクトです。

# 特徴
- 主に必要なものは、EZ-USB Development boardとファミリーコンピュータ拡張コネクタ用延長ケーブル  (その分、電気的には色々怪しい..)
- テープロード、セーブサポート

# 材料
1. Windows PC (64bit)
1. EZ-USB FX2LP Development board  ([例](https://www.amazon.co.jp/ez-usb/s?k=ez-usb))
1. ファミリーコンピュータ拡張コネクタ延長ケーブル ([例](https://www.amazon.co.jp/gp/product/B07KWXPNFR/ref=ppx_yo_dt_b_asin_title_o03_s00?ie=UTF8&psc=1))
1. 2と3とを接続する手段 (D-SUB15ピンコネクタ + 配線など)
1. EZ-USB Development board と PCとを接続する USBケーブル

# 起動
1. EZ-USBが、WinUSBドライバで認識されるようにWindowsを設定します
1. 回路図にしたがって、拡張コネクタケーブルとEZ-USBを接続します
2. ファミリーコンピュータに FAMILY BASICカセットを装着し、拡張コネクタケーブルを接続します
3. ファミリーコンピュータの電源を入れます (この時は、FAMILY BASICが起動しません)
4. EZ-USBのUSB端子とPCを接続します (念のため、ファミリーコンピュータの電源を先に入れるようにしてください)
5. `fbKeyboard.exe` を実行します (この時にFAMILY BASICがスタートします)

# 使い方
- fbKeyboard.exe の画面が`Keyboard mode`となっている時は、PCのキーボード入力がFAMILY BASICへのキーボード入力になります
- FAMILY BASICでセーブ(BGセーブ含む)するときは、`File -> Save` を選び、PCに保存するファイルを選択します
   その後、FAMILY BASIC でセーブを開始し、`Start Save`ボタンを押します
- FAMILY BASICでロードするときは、FAMILY BASICのロードを開始し、`File -> Load`を選び、ロードするファイルを
  選択します

# Tips
- 拡張コネクタ延長ケーブルは、ファミリーコンピュータに差す側でないほうの端を数ミリ短くすると、D-SUB 15pinコネクタがしっかり刺さり、信号が取り出せるようになります

# 非保証
本リポジトリ内のプログラム、回路図は、正常に動作することを期待して作成していますが、正常な動作を保証しません。  
本リポジトリ内のプログラム・回路図をいかなる形式で利用したことによる損害(ファミリーコンピュータが破損する、EZ-USBが破損する、PCが破損するなど)に対して、制作者は一切補償しません。

# 技術的なこと 
## テープイメージについて
### フォーマット
実験的なプログラムのため、特に凝ったことはしていません。  
サンプリング周波数32kHzで信号を1bitサンプルした値を上位から順に並べたものです。  
ちょうど、X1 Emulatorなどに使われる.TAP形式と同じです(ヘッダ部をのぞく)。  
実際、FAMILY BASICでセーブしたテープの音を 32kHzの .wav形式で保存し、それをX1 Emulator用の.TAP形式生成ツールで変換すると、このソフトで読み込むことができます。

### 制限
早送りや巻き戻し、一時停止、再生の機能はつけていません。1つのSAVEに対して、1ファイルです。    
LOADは常にファイルの先頭から行われます。

## 回路について
EZ-USBは、5V入力トレラントです。  
が、出力端子に5Vのプルアップが接続された場合の動作はデータシートに記載がありません(と思っています)。つまり、この条件での動作保証がありません。  
本回路は、このたぶん保証されていない条件で動作していますので、その点ご了承ください。

## プログラムについて
- `firmware`フォルダのプログラムは、SDCCをインストールしPATHが通ったPowerSehll環境で、`./compile.ps1`とすればコンパイルできます
- EZ-USBのGPIFは、立ち上がりエッジを検出して割り込みを発生させるために使っています
- Windows側のプログラムのコンパイルは、VisualStudio 2022, SDL2開発ライブラリ、libusb、ImGui (https://github.com/ocornut/imgui) が必要です  
 (SDL2ライブラリは、プロジェクトディレクトリ内にincludeディレクトリ、libディレクトリを作成してその中に入れます)

# 動作確認済みソフト
- FAMILY BASIC v1.0  (手持ちは、これしかないのです... V2.0, V2.1, V3は未確認)
- ロードランナー (HFC-LR) エディット画面 (キーボードを認識しないときもありますが、数回試すと認識するはずです)
