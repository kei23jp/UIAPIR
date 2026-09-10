# UIAPduino同士の通信

`#include <UIAPIRLink.h>` で形式と通信モードを選択できます。
データフレームには各形式の上限までデータを格納し、連番・長さ・CRC16・ACKは
独立した制御フレームで扱います。管理情報のためにアプリのデータ容量を減らしません。
従来の `UIAPIR.h` の送受信APIは変更していません。

## 容量とフォーマット

| `UIAPIRLinkFormat` | データの上限 | `maxPayload()` | 格納方法 |
|---|---:|---:|---|
| `NEC` | 16bit | 2 bytes | 標準アドレス8bit＋コマンド8bit |
| `NECExtended` | 24bit | 3 bytes | アドレス16bit＋コマンド8bit |
| `Sony12` | 12bit | 2 bytes | 最後のバイトは下位4bit |
| `Sony15` | 15bit | 2 bytes | 最後のバイトは下位7bit |
| `Sony20` | 20bit | 3 bytes | 最後のバイトは下位4bit |
| `AEHA` | 既定160bit | 20 bytes（既定） | `UIAPIR_MAX_AEHA_BYTES` バイトすべて |

最大容量はSimple/Reliableで同じです。AEHAの20バイトはライブラリの既定の上限であり、
AEHA規格全体の絶対的な上限ではありません。同定数を変更すれば通信層もその上限に追従します。
NECの反転チェック用ビットを自由なデータとは数えていません。Sonyは元の形式に従って
同じデータフレームを最低3回送ります。NEC/AEHAは38 kHz、Sonyは40 kHzです。

拡張NECで上位アドレスバイトが下位バイトの反転になる256通りは、標準NECとして送信し、
受信時に反転バイトを復元します。したがって **24bitの全値が往復可能** です。
AEHAも先頭のメーカー情報やパリティを上書きせず、`sendAEHA()` が受け付ける全バイトを
そのまま使用します。これは機器間のデータ転送用で、市販家電の有効なコマンドを生成するAPIではありません。
RAWそのものはアプリ用データ形式としては選択できません。

## 使用例

```cpp
#include <UIAPIRLink.h>

UIAPIR ir;
// 相手は endpoint=1。同じ format / mode / channel を設定する。
UIAPIRLink link(ir, UIAPIRLinkFormat::AEHA, UIAPIRLinkMode::Reliable, 0);
uint8_t capture[UIAPIR_RAW_BUFFER_SIZE];
IRCode scratch;
UIAPIRPacket received;

void setup() {
    UIAPIRConfig config(capture, sizeof(capture));
    if (!link.maxPayload() || sizeof(capture) < link.requiredCaptureSize() ||
        !ir.begin(3, 6, config)) while (true) {}
}

void loop() {
    if (link.poll(scratch, received)) {
        // received.bytes / received.length / received.bitLength を使用。
    }
    // アプリが送信すると決めた時（既定AEHAなら20バイトすべて送れる）:
    // uint8_t data[UIAPIR_MAX_AEHA_BYTES] = {0x01, 0x23, 0x45};
    // if (!link.busy()) link.send(data, sizeof(data));
}
```

Sony 20bitなら次の指定になります。

```cpp
UIAPIRLink link(ir, UIAPIRLinkFormat::Sony20, UIAPIRLinkMode::Reliable, 0);
uint8_t value[] = {0xff, 0xff, 0x0f}; // little-endian: 0xFFFFF
link.sendBits(value, 20);
// link.send(value, 3) でも同じ20bit。最後の上位4bitは0が必要。
```

配列の先頭が下位バイトです。NEC/Sonyで最大より短いデータを渡すと、物理フレームの残りは
ゼロ埋めされ、受信アプリには指定したビット長だけが返ります。`sendBits()` は1bitから
指定できますが、AEHAは8bit単位です。AEHAの1/2バイト送信は物理フレームを3バイトに
ゼロ埋めし、受信時に元の長さへ戻します。

上限を超える長さ、長さ0、NULL、最終バイトの未使用上位ビットが1の入力は拒否します。
例えばSony20で `{0xff, 0xff, 0xff}` を渡しても、黙って20bitへ切り捨てません。

## 2台用サンプル

`examples/TwoBoardLink/TwoBoardLink.ino` は、endpoint 0から選択形式の最大容量を送信し、
endpoint 1が受信します。両側にIR LEDの駆動回路と受信モジュールが必要です。

- `UIAPIR_LINK_ENDPOINT`: 0（送信側）または1（受信側）
- `UIAPIR_LINK_FORMAT`: 例 `UIAPIRLinkFormat::Sony15`
- `UIAPIR_LINK_RELIABLE`: 1（ACKあり）または0（送りっぱなし）

`UIAPIR_LINK_FORMAT` を指定しない場合は従来の `UIAPIR_LINK_PROTOCOL` も使えます。
`UIAPIR_NEC` は拡張NEC、`UIAPIR_SONY` はSony20、`UIAPIR_AEHA` はAEHAを選びます。
PlatformIOには `TwoBoardLinkSender` / `TwoBoardLinkReceiver` の環境があります。

## モードとAPI

- `Simple`: 制御フレームとデータを送って終了。ACK・自動再送はありません。
- `Reliable`: データ送信完了から800 ms以内にACKが来なければ、制御フレームとデータを再送。
  最初の送信に加えて最大3回再送します。受信側はCRC16を検査し、重複したパケットを
  再配送せずにACKを返します。ACKは相手の通信層での受理を示し、アプリの処理完了は示しません。

| API | 意味 |
|---|---|
| `UIAPIRLink(ir, format, mode, endpoint, channel=0)` | endpointは0/1、channelは0〜3。起動時に固定する設定。 |
| `maxPayloadBits()` | 形式の最大ビット数。無効設定・無効化した形式・RAW容量不足なら0。 |
| `maxPayload()` | 最大ビット数の格納に必要なバイト数（端数切り上げ）。Sonyでは最後のバイトが部分的。 |
| `requiredCaptureSize()` | 選択形式と制御フレームを受信するためのバッファ容量。 |
| `send(bytes, length)` | バイト配列を内部へコピーし送信。Sonyの最大長のみ端数ビットを扱う。 |
| `sendBits(bytes, bits)` | ビット長を明示して送信。AEHAは8bit単位。 |
| `poll(scratch, packet)` | 制御受信、データ検査、ACK、自動再送を進める。新規データを受け取った時だけtrue。 |
| `busy()` | ACK待ち、ACK送信予定、またはフレーム間待機中ならtrue。新規送信は受け付けない。 |
| `status()` | `Idle` / `Pending` / `Sent` / `Acknowledged` / `Failed`。最後に受理した送信の状態。 |

`packet.length` はデータ格納バイト数、`packet.bitLength` は有効ビット数です。
`poll()` がfalseの時のpacket内容は未規定です。`send()` / `sendBits()` の引数拒否では
直前の状態を保持し、実際の送信APIが失敗した場合は `Failed` にします。
`Failed` でもデータが相手に届き、ACKだけが失われた可能性があります。

両側ともこまめに `poll()` を呼んでください。送信APIと `poll()` 内のACK送信・再送は
既存のブロッキング処理を使用します。ACK待ち自体は非ブロッキングです。
SonyのACKは最終データ受信から100 ms待って送り、後続のSIRCフレームとの衝突を避けます。
ACKは単発の制御フレームなので、ACK受信後の送信間隔は8 msです。
同じIRインスタンスを `receive()` と `poll()` で同時に読み出さないでください。

## 制御フレームと互換性

送信順は「独自RAW制御フレーム → 8 ms以上の空白 → 選択形式のデータ」です。
制御フレームは8000 us mark / 1000 us spaceのリーダー、560 us markと
560/1690 us spaceの32bit LSB-first、最後の560 us markからなります（67 durations）。
データ形式と同じキャリア周波数を使います。既存のNEC/AEHA/Sonyデコーダは
このリーダーをデータとして認識せず、通信層がRAW結果から読み取ります。

32bitの内容はヘッダ8bit、長さ8bit、CRC16です。ヘッダは上位からchannel 2bit、
送信元1bit、連番3bit、Reliable 1bit、ACK 1bit。長さはAEHAではバイト数、それ以外ではビット数です。
CRC16/CCITT-FALSEは `{wire version=2, format enum, data header, length}` と
アプリのデータ全バイトを対象とします。ACKは向きを反転してACKビットを立て、長さとCRC16を返します。
制御だけ・データだけの受信ではアプリに配送しません。

制御フレームが増える分、通信時間は増えます。前版の容量制限付き通信層とは
ワイヤー互換性がありません。2台とも更新してください。
従来のリモコン送受信API・データ形式には影響しません。

## メモリとビルド

再送用データは最大容量を確保してコピーします。データ容量を縮めるための内部制限は設けません。
`IRCode` と受信バッファは呼び出し側で確保し、通信層自体はヒープを使いません。

NEC専用で受信領域を縮める例:

```text
-DUIAPIR_ENABLE_AEHA=0 -DUIAPIR_ENABLE_SONY=0 -DUIAPIR_RAW_BUFFER_SIZE=67
```

Sony専用でも **制御フレームのためRAW容量は67以上** が必要です（旧版の41では不足）。
AEHAを最大Nバイトに設定する場合は `UIAPIR_MAX_AEHA_BYTES=N`、
RAW容量は `max(67, 3+16*N)` 以上にしてください。Nは既存APIの範囲で3以上です。
受信バッファも `requiredCaptureSize()` 以上にします。
これらの定義はスケッチ内だけでなく、ライブラリを含むビルド全体に適用してください。

UIAP core 1.0.42 / Arduino CLIで、最大長を送るサンプルのビルドを確認しました。
全形式有効の既定構成はFlash 13,440 byte、Arduino表示のRAM 1,404 byte、
拡張NEC専用・RAW容量67の構成はFlash 12,204 byte、RAM 796 byteです。
通信オブジェクト自体はmap上で既定76 byte、AEHA無効時56 byteです。
これらはビルド上の値で、実機のスタック最大使用量の測定ではありません。

## 検証と制約

ホストテストは実際の形式のビット列を生成し、既存デコーダを通して、各形式の上限での
往復、Sonyの端数ビット、拡張NECの予約アドレス256通り、AEHAの全長、CRC、制御・ACK欠落、
再送・重複、連番とmillisの周回を確認します。制御が既存形式へ誤判別されないことも確認します。
この通信層の実機光学テストは未実施です。既存HT6の実機成功記録とは区別してください。
標準/拡張NEC、Sony 12/15/20、AEHAそれぞれのArduinoビルドと、PlatformIOの両endpointも成功しています。

これは2台が交互に使うデータグラム通信です。TCPの接続確立、分割・再構成、衝突回避はありません。
両方が同時送信すると失敗することがあるため、アプリで送信順を決めてください。
連番は8パケットで再利用します。極端に古いACK、片側だけの再起動、CRCの衝突まで含む
厳密な一度限りの配送は保証しません。CRCは誤り検出であり、認証・暗号化ではありません。
