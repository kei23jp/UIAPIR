# UIAPIR

UIAPIR は、[UIAPduino Pro Micro CH32V003 V1.4](https://www.uiap.jp/en/uiapduino/pro-micro/ch32v003/v1dot4) を主対象にした、軽量な赤外線リモコンライブラリです。NEC、AEHA、Sony SIRC、RAW 信号の送信・受信・学習・再生を、16 KiB フラッシュ / 2 KiB SRAM の CH32V003 に収まる規模で行います。UIAPduino では TIM1 / TIM2 / EXTI を直接使います。

追加ライブラリには依存しないため、容量を意識する Arduino Uno 系や Arduino-ESP32 でも使えます。これらのボードでは、標準の `tone()`、`micros()`、`attachInterrupt()` を使うポータブルバックエンドを提供します。

- **NEC** — 8 bit アドレス、拡張 16 bit アドレス、リピートフレーム
- **AEHA / Kaseikyo** — 3〜20 バイトの可変長ペイロード
- **Sony SIRC** — 12 / 15 / 20 bit フレーム
- **RAW** — 未知のフォーマットをそのまま学習して再生
- 受信したフレームのプロトコルを自動判別
- 使わないプロトコルはビルド時に丸ごと除外できる
- UIAPduino 2 台間のデータ通信層（任意）

## 目次

- [ステータス](#ステータス)
- [インストール](#インストール)
- [配線と固定リソース](#配線と固定リソース)
- [使ってみる](#使ってみる) — [受信して再生する](#受信して再生する) / [直接送信する](#直接送信する)
- [学習データの扱い](#学習データの扱い) — [受信バッファ](#受信バッファ)
- [ビルド時オプション](#ビルド時オプション) — [使わないプロトコルを外す](#使わないプロトコルを外す)
- [2 台間のデータ通信](#2-台間のデータ通信)
- [制約](#制約) — [ハードウェア](#ハードウェア上の制約) / [プロトコル](#プロトコル上の制限)
- [テスト](#テスト) — [ホストテスト](#ホストテスト) / [実機テスト](#実機テスト)
- [開発](#開発) — [リポジトリ構成](#リポジトリ構成) / [リリース](#リリース)
- [参考資料](#参考資料) / [ライセンス](#ライセンス) / [謝辞](#謝辞)

## ドキュメント

| 文書 | 内容 |
|---|---|
| [extras/API.md](extras/API.md) | API リファレンス。公開されている全メソッドの引数・戻り値と、どの引数で `false` が返るか |
| [extras/ARCHITECTURE.md](extras/ARCHITECTURE.md) | 内部設計 |
| [extras/LINK.md](extras/LINK.md) | 2 台間データ通信層 `UIAPIRLink.h` の使い方と制約 |
| [extras/HARDWARE_TESTS.md](extras/HARDWARE_TESTS.md) | 実機テストの手順書 |

利用者向けの文書（README、CHANGELOG、CONTRIBUTING、API リファレンス）は日本語で書いています。ソースコードのコメントと、内部設計・PlatformIO・HT6 の資料は、開発時の共有を優先して英語で書いています。

## ステータス

バージョン 0.1.0 は初回公開に向けた実装です。UIAPduino 向けのプロトコル単位のビルド時除外と2台間通信層を含み、プロトコルデコーダとキャプチャ状態機械にはホスト側のユニットテストがあります。

実機では、UIAPduino 2 台を向かい合わせた HT6（実際の光学経路を通す 17 ケースの自動テスト）が 17/17 で通っています。NEC / 拡張 NEC / NEC リピート / AEHA / SIRC 12・15・20 bit / RAW の送受信一致、バーストの取りこぼしがないこと、引数を拒否したときに赤外線を一切出さないことが、これで確認できています。`examples/LearnRemote` も実機で学習・再送が動作しています。

Arduino Uno、Arduino Pro Mini（ATmega328P、8 MHz / 16 MHz）、ESP32 Dev Module は、軽量なポータブル利用先として `examples/Receive` と `extras/hardware-tests/PortableSmokeTest` のコンパイルを CI で確認します。後者は初期化、終了・再初期化、NEC / AEHA / Sony / RAW 送信、リモコン信号の学習・再生を手動で確認する共通ファームウェアです。ポータブルバックエンドの実機での送受信タイミングは、まだ検証していません。

**未検証:** キャリア周波数とデューティ比の実測（HT4、ロジックアナライザまたはオシロスコープが必要）、受信ピンの再バインド（HT2、ジャンパ 2 本）、市販リモコンの受信ダンプ（HT5）。手順は `extras/HARDWARE_TESTS.md` にあります。

## インストール

Arduino IDE で **Sketch > Include Library > Add .ZIP Library...** を選び、リリースページで配布される `UIAPIR-0.1.0.zip` を指定します。リポジトリを直接使う場合は、Arduino の `libraries/` フォルダへ `UIAPIR` という名前で置いても構いません。

主対象の UIAPduino では **UIAPduino > Pro Micro CH32V003** を選択してください。Arduino Uno 系や ESP32 で使う場合は、それぞれのボードを選択し、割り込み可能な RX ピンと `tone()` 対応の TX ピンを指定します。

## 配線と固定リソース

ハードウェアバックエンドでは、以下を使用します。

| 対象 | 位置付け | IR LED キャリア | 受信 |
|---|---|---|---|
| UIAPduino / CH32V003 | 主対象。実機の HT1〜HT6 あり | PC4 / TIM1_CH4（`D6` または `A2`） | ユーザーが選択した GPIO + EXTI |
| Arduino Uno / Pro Mini / Arduino-ESP32 | ポータブルサポート。CI でコンパイル確認 | `tone()` を出力できる GPIO | `attachInterrupt()` を使える GPIO。ATmega328P Pro Mini は D2 / D3（A3 は不可） |

IR LED はトランジスタまたは MOSFET を介して駆動してください。大電流の IR LED を D6 から直接駆動しないでください。

一般的な復調型受信モジュール（38 kHz タイプ）のデジタル出力を、選択した RX ピンに接続します。入力にはプルアップが設定されます。

CH32V003 では TIM1 と TIM2 は予約されるため、UIAPIR が動作中は Servo、tone、および無関係な PWM 機能を使用してはいけません。他のボードでは、送信中に `tone()` が使うタイマーまたは PWM チャネルを他用途と共有しないでください。

## 使ってみる

### 受信して再生する

`begin()` の引数は順に RX ピン、TX ピンです。受信したフレームは `receive()` が `IRCode` へ書き込み、`send()` がそれをそのまま送り返します。プロトコルの指定は要りません。

```cpp
#include <UIAPIR.h>

UIAPIR ir;
IRCode code;

void setup() {
  if (!ir.begin(3, 6)) {
    // UIAPduino の TX は PC4（D6 または A2）に固定です。
    // Uno 系 / ESP32 では RX に割り込み可能なピン、TX に tone() 対応のピンを指定します。
    while (true) {}
  }
}

void loop() {
  if (ir.receive(code)) {
    delay(100);
    ir.send(code);
  }
}
```

### 直接送信する

```cpp
ir.sendNEC(0x12, 0x34);                 // 8 ビットアドレス
ir.sendNEC(0x12, 0x34, false, 2);       // 110 ms 間隔でリピートコードを 2 回追加
ir.sendNEC(0x1234, 0x56, true);         // 拡張 16 ビットアドレス
ir.sendNECRepeat();

uint8_t aeha[] = {0x02, 0x20, 0x80, 0x00, 0x12, 0x34};
ir.sendAEHA(aeha, sizeof(aeha));        // 1 フレーム
ir.sendAEHA(aeha, sizeof(aeha), 2);     // 130 ms 間隔で 2 フレーム

ir.sendSony(1, 19, 12);                 // 45 ms 間隔で 3 フレーム

// send() の `repeats` は、すべてのプロトコルで同じ意味です。
// 追加送信の回数を表すため、総送信回数は repeats + 1 になります。
// ただし SIRC は常に最低 3 フレーム送信します。
ir.send(code, 3);                       // `code` の種類にかかわらず合計 4 回送信

// 学習済み RAW フレームに UIAPIR_FLAG_RAW_OVERFLOW または
// UIAPIR_FLAG_TIMING_CLIPPED が設定されている場合、タイミング情報が欠落しています。
// send() は異なる信号を誤って送信するのではなく、そのデータの送信を拒否します。

uint16_t raw[] = {9000, 4500, 560, 560, 560, 1690, 560};
ir.sendRaw(raw, sizeof(raw) / sizeof(raw[0]), 38);
```

## 学習データの扱い

認識済みのフレームは、コンパクトな構造化メンバーを使用します。

```cpp
code.data.nec.address
code.data.nec.command

code.data.aeha.length
code.data.aeha.bytes[i]

code.data.sony.address
code.data.sony.command
code.data.sony.bits
```

未知のフレームは `code.data.raw` を使用します。RAW 値は SRAM を節約するため、マイクロ秒ではなく 50 us 単位の tick 値として保存されるので、マイクロ秒で読むには `rawMicros()` を使います。

```cpp
uint16_t durationUs = code.rawMicros(i);
```

なお、復調型 IR 受信モジュールはキャリア成分を除去してしまうため、未知の RAW フレームから元のキャリア周波数を知ることはできません。UIAPIR は未知の RAW データに 38 kHz を割り当てます（NEC と AEHA も 38 kHz、デコード済みの Sony は 40 kHz）。

### 受信バッファ

デフォルトのバッファは 340 durations（340 バイト）で、20 バイトの AEHA フレームに必要な容量です。

この受信バッファは、すべての `UIAPIR` オブジェクトに固定メンバーとして確保されるのではなく、`begin()` によって割り当てられます。送信専用インスタンスでは受信バッファは割り当てられません。`end()` は、ライブラリが割り当てたバッファを解放します。

受け取りたいフレームに必要な容量の目安です。

| 収めたいもの | 必要な durations |
|---|---|
| NEC フレーム全体 | 67 |
| Sony SIRC 20 bit | 41 |
| AEHA | `UIAPIR_AEHA_DURATIONS(byteCount)`（6 バイトなら 99、20 バイトなら 323） |
| NEC リピートフレームのみ | 3（受け付ける絶対最小値） |

初期化時に `UIAPIRConfig` を渡すことで、より小さい受信バッファを選択できます。

```cpp
UIAPIR ir;
UIAPIRConfig config(67);  // NEC フレーム全体を格納するのに十分

if (!ir.begin(3, 6, config)) {
  // サイズが不正、またはメモリ確保に失敗。
}
```

ヒープを完全に使用しないようにするには、`end()` が呼ばれるまで有効なストレージを用意してください。

```cpp
uint8_t captureStorage[67];
UIAPIRConfig config(captureStorage, sizeof(captureStorage));
ir.begin(3, 6, config);
```

バッファに収まらない長さの信号は `UIAPIR_FLAG_RAW_OVERFLOW` が付いた状態で返り、デコードも再生もできません。

上限は `UIAPIR_RAW_BUFFER_SIZE` です。受信した RAW データは `IRCode` へコピーされるため、実行時にこれを超える容量は指定できません。`IRCode` 側の RAW 領域は固定サイズなので、そちらも縮めたい場合は次のビルド時オプションを使ってください。実行時の設定では C++ オブジェクトのレイアウトは変えられません。

## ビルド時オプション

`UIAPIR_RAW_BUFFER_SIZE`、`UIAPIR_RAW_TICK_US`、`UIAPIR_MAX_AEHA_BYTES`、`UIAPIR_FRAME_GAP_US`、および後述の `UIAPIR_ENABLE_*` は変更できますが、ビルド全体に適用されるコンパイラ定義として指定する必要があります。

スケッチ内の `#define` では効きません。Arduino はライブラリの `.cpp` を別々の翻訳単位としてコンパイルするため、定義がライブラリ側に伝わらないからです。そうなるとスケッチ側はあるサイズで `IRCode` を作り、ライブラリ側は別のサイズを想定してアクセスするのに、リンクは何の警告もなく成功してしまいます。代わりに `arduino-cli` へ指定してください。

```bash
arduino-cli compile --build-property compiler.cpp.extra_flags=-DUIAPIR_RAW_BUFFER_SIZE=200 ...
```

いずれかのプロトコル制限を破る値を設定した場合は、実行時に誤動作するのではなく、プロトコル定数に基づく `#error` ガードによってビルドが失敗します。

特に `UIAPIR_RAW_TICK_US` で指定できる範囲は **36〜50 us** と非常に狭く、両端がそれぞれ別の理由で塞がっています。

- **36 未満**: 9 ms の NEC リーダーが `uint8_t` の tick 値に収まらなくなります。
- **50 超**: 許容範囲が広がって Sony SIRC の 600 us と 1200 us の mark が重なり、すべての SIRC の `1` が `0` としてデコードされます。SIRC のシンボル比は 2:1 なので、NEC の 3:1 より先にこの制約に達します。
- **257 超**: 255 tick をマイクロ秒へ変換した値が `uint16_t` をオーバーフローします。

既定の 50 us はこの範囲の上限で、判別余裕は 1 tick しか残っていません。プロトコルを削る場合を除き、固定値として扱うことを推奨します。

### 使わないプロトコルを外す

`UIAPIR_ENABLE_NEC`、`UIAPIR_ENABLE_AEHA`、`UIAPIR_ENABLE_SONY` に 0 を指定すると、そのプロトコルのデコーダと送信 API がビルドから外れます。既定はすべて 1 です。

```bash
arduino-cli compile --build-property "compiler.cpp.extra_flags=-DUIAPIR_ENABLE_AEHA=0 -DUIAPIR_ENABLE_SONY=0" ...
```

宣言ごと消えるため、無効にしたプロトコルの `sendAEHA()` などを呼ぶとコンパイルエラーになります。実行時に `false` を返して黙るより、ビルドが「そのコードは積んでいない」と言う方が正しいためです。

RAW は外せません。すべての捕捉は RAW から始まり、デコードできなかった信号を再送するのも `sendRawTicks()` だからです。無効にしたプロトコルの信号は `UIAPIR_RAW` として届くので、**再送そのものは問題なくできます**。address や command として読めなくなるだけです。

タイミング定数は、そのプロトコルを外しても定義され続けます。フレーム区切りの閾値 `UIAPIR_FRAME_GAP_US` は 20 ビット SIRC のギャップを上限としていますが、SIRC を外してもこの制約は残ります。プロトコルを 1 つ外したことで、受信側が全信号に適用する閾値が静かに動くことがないようにするためです。

AEHA を外すと `UIAPIR_RAW_BUFFER_SIZE` の下限が外れます。20 バイト AEHA は 323 durations を要求し、これが既定 340 を決めている当のものだからです。NEC だけなら 67 まで縮められます。

実測値です（`examples/LearnRemote`、arduino-cli、UIAPduino V1.4）。

| 構成 | flash | 静的 RAM | ヒープ | RAM 合計 |
|---|---|---|---|---|
| 既定 | 15056 | 976 | 340 | 1316 |
| NEC のみ | 13708 (−1348) | 976 | 340 | 1316 |
| NEC のみ + `UIAPIR_RAW_BUFFER_SIZE=128` | 13708 | 764 | 128 | 892 (−424) |
| NEC のみ + `UIAPIR_RAW_BUFFER_SIZE=67` | 13708 | 704 | 67 | 771 (−545) |

8 通りの組み合わせすべてを `-Werror` でコンパイルできることは、ホストテストが確認します。

## 2 台間のデータ通信

UIAPduino 同士でデータをやり取りする場合は、任意追加のヘッダ `UIAPIRLink.h` を使えます。リモコン用の 3 プロトコルを、そのままデータ搬送に使うための層です。

- 搬送プロトコルは NEC / AEHA / Sony から選択
- 送りっぱなしの**簡易モード**と、ACK と最大 3 回の再送を行う**信頼性モード**
- 1 回で送れるデータ量は、標準 NEC 16 bit、拡張 NEC 24 bit、Sony 12 / 15 / 20 bit、AEHA は既定で 20 バイト
- 連番・長さ・CRC16 は独立した RAW 制御フレームで送るため、データ領域の容量を削りません
- ヒープを使わず、送信待ちは 1 件、重複除去つき

使い方と制約は [extras/LINK.md](extras/LINK.md)、動く例は `examples/TwoBoardLink` にあります。通信層の実機検証はまだ行っていません。

## 制約

以下は主対象の CH32V003 と、赤外線リモコンという枠組みから来る制約です。UIAPIR は動作不能な設定を黙って握り潰さず、`begin()` と `send*()` が `false` を返して拒否します。Uno 系 / ESP32 では、各ボードの `tone()` と割り込みに使えるピンの制約に従ってください。

### ハードウェア上の制約

このボードの CH32V003 には 18 本のデジタルピンがあります。PA1、PA2、PC0～PC7、PD0～PD7 です。

`begin()` は、受信できない状態のまま開始するのではなく、動作不能な設定を拒否します。

- チップ上に存在しない RX ピン  
  core の `attachInterrupt()` は不明なピンを指定しても何も通知せずに戻るため、そのままだと一見正常に開始できたように見えても、受信割り込みが一度も発生しない状態になります。

- キャリア出力と同じパッドを使用する RX ピン  
  この環境では Arduino のピン番号と物理パッドは一対一ではありません。たとえば `A2` と `D6` は番号としては異なりますが、どちらも同じ PC4 です。そのため比較は Arduino ピン番号ではなく、解決後の物理ピン同士で行われます。

- PC4 以外の TX ピン  
  TIM1_CH4 が出力されるパッドは PC4 だけです。ただし判定は RX 側と同じく物理ピンで行うため、`D6` と `A2` はどちらも受け付けます。基板シルクに `A2` と書いてあるパッドを、番号が違うという理由だけで拒否しないためです。

開始済みのインスタンスに対して再び `begin()` を呼び出した場合、古いピンの割り込みを有効なまま残すのではなく、新しいピンで受信機を再設定します。

TIM1 と TIM2 は共有リソースなので、それらを所有できる `UIAPIR` インスタンスは 1 つだけです。2 つ目のインスタンスによる `begin()` は拒否されます。また、そのインスタンスで `end()` を呼び出してもタイマーには触れないため、所有中のインスタンスが送信途中である場合に、その送信を停止してしまうことはありません。

RX ピンを選ぶ際には、ほかにも注意点があります。EXTI ラインはピン番号ごとに割り当てられ、各ラインは一度に 1 つのポートしか扱えません。そのため、D2（PC0）の RX ピンと D10（PD0）の別の割り込みを同時に使用することはできません。

PD5/PD6 はデフォルトの Serial ピン、PC1/PC2 は I2C ピン、PC4～PC7 は SPI ピンです。

### プロトコル上の制限

以下のタイミング値や制限値はすべて、参照元とともに `src/UIAPIRProtocolDefs.h` の 1 か所にまとめられています。

送信処理、デコーダ、ホスト側テストはすべて同じマクロを参照するため、それぞれの値が食い違うことはありません。また、維持する必要のある値同士の関係は、単なるコメントではなく `#error` ガードとして実装されています。

| | NEC | AEHA | Sony SIRC |
|---|---|---|---|
| 単位 T | 560 us | 425 us | 600 us |
| キャリア | 38 kHz | 38 kHz | 40 kHz |
| ペイロード | 固定 32 bit | 3～20 bytes | 12 / 15 / 20 bit |
| アドレス | 8 bit、または拡張時 16 bit | ペイロード内 | 5 / 8 / 13 bit |
| コマンド | 8 bit | ペイロード内 | 7 bit |
| フレーム周期 | 110 ms | 130 ms | 45 ms |
| 1 回の押下あたりのフレーム数 | 1 + リピート | デフォルト 1 | 最低 3 |

`send*()` は、以下のような値を切り捨てるのではなく拒否します。

- 255 を超える標準 NEC アドレス  
  16 ビットアドレスを使う場合は `extended = true` を指定してください。

- 上位バイトが下位バイトのビット反転になっている拡張 NEC アドレス  
  この 256 通りの値は標準 NEC 用に予約されており、送信後に受信すると 8 ビットアドレスとして解釈されるためです。

- 3 バイト未満の AEHA ペイロード  
  この形式で必要となる 16 ビットのカスタマーコード、4 ビットのパリティ、4 ビットの data0 を格納できないためです。  
  また、`UIAPIR_MAX_AEHA_BYTES` を超えるペイロードも拒否されます。

- 12、15、20 ビット以外の SIRC フレーム長、各フレームの 5、8、13 ビットのアドレスフィールドに収まらないアドレス、0x7f を超えるコマンド、または 3 フレーム未満の指定

フレーム周期は、あるフレームの開始時点から次のフレームの開始時点までとして測定されます。そのため、ギャップ時間は直前に送信したフレーム長に応じて調整されます。

NEC フレームの長さは、ペイロードによって 59 ms～76 ms の範囲で変化します。固定ギャップを使うと、その差だけリピートコードの位置がずれてしまいます。

20 バイト AEHA ペイロードのように、フレーム自体が規定のフレーム周期より長くなった場合、送信側は受信機が識別可能な最短のギャップ時間を使用します。

`uiapir_protocol::aehaParity()` は AEHA のパリティニブル（カスタマーコードを 4 ビット単位で XOR した値）を計算します。

これは検査用として提供されており、強制はされません。Kaseikyo 系の派生形式すべてが、このルールに従っているわけではないためです。

## テスト

### ホストテスト

プロトコルデコーダとエッジキャプチャのステートマシンは、意図的に Arduino や CH32 への依存をなくしてあります。どちらもデスクトップ上の C++14 コンパイラで実行できます。

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .	ests
un.ps1
```

```sh
sh tests/run.sh
```

コンパイラは環境変数 `CXX` で指定できます。

`tests/test_protocols.cpp` はデコード処理とプロトコルごとの制限を、`tests/test_capture.cpp` は合成したエッジストリームをキャプチャステートマシンへ流し込んで、次のケースを検証します。

- リピートフレームの連続受信
- ハードウェアカウンタで表現できる時間を超えるアイドル期間
- 前のフレームがまだ読み出されていない間に次のフレームが到着するケース
- 極端に短いバースト
- バッファオーバーフロー
- duration のクリッピング

タイミング値や制限値はヘッダから取得しているため、プロトコル定数とそれを使うコードが食い違えばテストが失敗します。

### 実機テスト

ホストテストではハードウェアに触れる部分を検証できません。`begin()` のピン検証は Arduino のピン番号を CH32 core のピンマップで物理ピンへ解決し、`end()` の所有権管理はタイマーレジスタへ書き込み、キャリア信号とエンベロープの duration は TIM1 と TIM2 が生成します。そして何より、送信した信号を実際に受信してデコードできるかは光学経路を通してしか分かりません。ホストテストは送信側と受信側を別々に、どちらもライブラリ自身の想定タイミングで検証しているだけだからです。

そのため `extras/hardware-tests/` に 6 つの実機テストがあります。手順は `extras/HARDWARE_TESTS.md` にあります。

| | 内容 |
|---|---|
| HT1〜HT3 | 1 台構成の自動テスト。Serial 経由で PASS/FAIL を報告 |
| HT4 / HT5 | ロジックアナライザや実際のリモコンを使う測定用 |
| HT6 | UIAPduino 2 台を向かい合わせ、実際の光学経路を通して送受信を突き合わせる自動テスト。17 ケースを繰り返し PASS/FAIL を出力。これのみ PlatformIO プロジェクトで `extras/hardware-tests/HT6_TwoBoard/` にあります |
| PortableSmokeTest | Arduino Uno 系 / ESP32 共通。初期化、各プロトコルと RAW の送信、学習、再生をシリアル表示付きで手動確認 |

### PlatformIO / VS Code

リポジトリ直下の `UIAPIR.code-workspace` を VS Code で開くと、ライブラリ本体と 2 つの PlatformIO プロジェクトが 1 つのウィンドウにまとまります。ビルドする環境は PlatformIO のステータスバーから選びます。

| プロジェクト | 内容 |
|---|---|
| `sketches (PlatformIO)` | `examples/` 5 種と HT1〜HT5。スケッチ 1 つにつき 1 環境 |
| `HT6 two-board (PlatformIO)` | HT6 の送信側・受信側ファームウェア |

コマンドラインからも同じものをビルドできます。

```bash
cd extras/platformio && pio run
```

書き込みは SWD プローブではなく USB ブートローダー経由（minichlink）です。**RST ボタンを押しながら USB を接続**してから書き込んでください。

スケッチ自体は変更していません。Arduino IDE と arduino-cli はこれまでどおりビルドできます。仕組みと制約は `extras/platformio/README.md` に書いてあります。

### 全スケッチのテストビルド

UIAPduino core 1.0.42とArduino CLIをインストールした環境では、ホストテストに加えて
`examples/`と`extras/hardware-tests/`の全スケッチを一括コンパイルできます。

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\check.ps1
```

```sh
sh scripts/check.sh
```

使用するFQBNのデフォルト値は
`UIAP:ch32v:CH32V00x_EVT:pnum=CH32V003V1DOT4`です。
別の構成を検証する場合は、PowerShellでは`-Fqbn`、Unix系環境では
`UIAPIR_FQBN`環境変数で変更できます。

GitHub Actionsは、ホストテストとArduino Lintを`.github/workflows/check.yml`で実行し、
UIAPduino 向けの全スケッチに加えて Arduino Uno と ESP32 Dev Module の `Receive` サンプルと `PortableSmokeTest` を
`.github/workflows/compile-examples.yml` でコンパイルします。UIAPduino core 1.0.42 は Board Manager
からインストールするため、ローカル環境に依存しません。

## 開発

### リポジトリ構成

```text
src/                       ライブラリ本体
examples/                  Arduino IDEに表示する使用例
tests/                     デスクトップ上で動作する自動テスト
extras/API.md              APIリファレンス
extras/ARCHITECTURE.md     内部設計
extras/HARDWARE_TESTS.md   実機テストの手順書
extras/hardware-tests/     配線や測定器を使用する実機テスト
extras/platformio/         PlatformIOから全スケッチをビルドするプロジェクト
scripts/                   ローカルで全チェックを行うスクリプト
.github/workflows/         GitHub Actionsの設定
```

`.test-build/`と`build/`はローカル生成物、`dist/`は必要に応じて作成する
リリース成果物の置き場所で、いずれもGit管理の対象外です。

### リリース

1. `library.properties`の`version`を更新します。
2. `CHANGELOG.md`の`Unreleased`項目を同じバージョンへ移します。
3. `scripts/check.ps1`または`scripts/check.sh`を実行します。
4. ハードウェアに影響する変更では`extras/HARDWARE_TESTS.md`の実機確認を行います。
5. CIの成功後、同じバージョンのGitタグ（例: `v0.1.0`）とGitHub Releaseを作成します。

Arduino Library Managerへ登録する場合は、リポジトリを公開し、`library.properties`の
`version`と一致するタグを打ってから、[Arduino Library Manager registry](https://github.com/arduino/library-registry)
へ登録申請します。申請前に、登録側のルールも含めた検査を一度通しておくと確実です。

```sh
arduino-lint --project-type library --compliance specification --library-manager submit .
```

## ライセンス

MIT License です。全文は [LICENSE](LICENSE) にあります。

プロトコル名は互換性を示す目的でのみ使用しています。本プロジェクトは NEC、Sony、AEHA、UIAP、WCH、Arduino-IRremote、ChaN のいずれとも提携・関連していません。

このリポジトリへコントリビュートされた内容も、同じ MIT License のもとで提供されるものとみなします。詳細は [CONTRIBUTING.md](CONTRIBUTING.md) にあります。

## 参考資料

### 開発環境

- [UIAPduino Pro Micro CH32V003 V1.4](https://www.uiap.jp/en/uiapduino/pro-micro/ch32v003/v1dot4)
- [WCH Arduino core for CH32](https://github.com/openwch/arduino_core_ch32)
- [Community-PIO-CH32V: platform-ch32v](https://github.com/Community-PIO-CH32V/platform-ch32v)

### プロトコル仕様の出典

`src/UIAPIRProtocolDefs.h` のタイミング定数はすべてこの 2 つが出典で、ソース中では `[ChaN]` `[SBP]` として節ごとに引用しています。

- [ChaN: 赤外線リモコンの通信フォーマット](https://elm-chan.org/docs/ir_format.html)  
  NEC / AEHA / SIRC の T 値、キャリア周波数とデューティ比（1/3）、フレーム周期。AEHA のパリティ（顧客コードを 4 bit 単位で XOR）もここに拠ります。
- [SB-Projects: NEC Infrared Transmission Protocol](https://www.sbprojects.net/knowledge/ir/nec.php)  
  NEC の 9 ms / 4.5 ms リーダー、560 us のビットマーク、1.69 ms の "1" スペース、110 ms のリピート周期。拡張 NEC アドレスの制約（上位バイトが下位バイトの反転になる 256 通りは標準 NEC 用に予約）もここに拠ります。

NEC については 2 つの出典で T 値が異なります（[ChaN] は 562 us、[SBP] は 560 us）。UIAPIR は市販リモコンと主要なデコーダが実際に出している [SBP] の値を採用しています。詳細は `src/UIAPIRProtocolDefs.h` のコメントにあります。

## 謝辞

UIAPIR は [ChaN](https://elm-chan.org/) 氏が長年公開されている
- [赤外線リモコンの通信フォーマット](https://elm-chan.org/docs/ir_format.html)  
- [ChaN: 赤外線リモコン制御モジュール (IR-CTRL)](https://elm-chan.org/fsw/irctrl/00index.html) 

を参考にさせていただいております。日本語でわかりやすく、出典として引用できる形でまとめられて公開されていること深く感謝いたします。
