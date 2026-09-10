# UIAPIR API リファレンス

追加の2台間通信API（`#include <UIAPIRLink.h>`）は [LINK.md](LINK.md) を参照してください。

`#include <UIAPIR.h>` だけで、以下すべてが使えます。`UIAPIRTypes.h`、`UIAPIRProtocolDefs.h`、`UIAPIRCapture.h` は `UIAPIR.h` から取り込まれるため、個別にインクルードする必要はありません。`uiapir_protocol` 名前空間のヘルパーを使う場合のみ `#include <UIAPIRProtocol.h>` を追加します。

使い方の流れは [README](../README.md) に、内部の設計は [ARCHITECTURE.md](ARCHITECTURE.md) にあります。この文書は「何が公開されていて、どの引数で false が返るか」を網羅することが目的です。

- [`UIAPIR` クラス](#uiapir-クラス)
- [`UIAPIRConfig`](#uiapirconfig)
- [`IRCode` とペイロード](#ircode-とペイロード)
- [`uiapir_protocol` ヘルパー](#uiapir_protocol-ヘルパー)
- [ビルド時マクロ](#ビルド時マクロ)
- [プロトコル定数](#プロトコル定数)

---

## `UIAPIR` クラス

1 つの物理インスタンスが、キャリア出力・タイムベース・受信割り込みを占有します。**コピーも代入もできません**（`= delete`）。複数のインスタンスを同時に `begin()` することはできません。CH32V003 では TIM1 / TIM2 / EXTI を直接使い、他の Arduino 対応ボードでは `tone()` / `micros()` / `attachInterrupt()` を使います。

### ライフサイクル

```cpp
bool begin(uint8_t rxPin, uint8_t txPin = UIAPIR_DEFAULT_TX_PIN);
bool begin(uint8_t rxPin, uint8_t txPin, const UIAPIRConfig &config);
bool begin(uint8_t rxPin, const UIAPIRConfig &config);
void end();
```

ピンを確保し、タイムベースと受信割り込みを開始します。`rxPin` に `UIAPIR_UNUSED_PIN` を渡すと送信専用インスタンスになり、受信バッファも確保されません。`txPin` に `UIAPIR_UNUSED_PIN` を渡すと受信専用になります。

CH32V003 の `txPin` は PC4 でなければなりません。キャリアが TIM1_CH4 から出るためです。**`6` と `A2` はどちらも PC4 を指すため、両方受け付けます**（基板シルクは `A2`）。他の Arduino 対応ボードでは `tone()` を出力できる GPIO を指定してください。`rxPin` には `attachInterrupt()` を使える GPIO が必要です。

すでに `begin()` 済みのインスタンスに再度 `begin()` を呼ぶと、古いピンの割り込みを解除してから新しいピンで張り直します。

`false` になる条件は以下です。

| | |
|---|---|
| 別の `UIAPIR` インスタンスが既に `begin()` 済み | タイマーと受信割り込みは意図的に単一インスタンス専有です |
| CH32V003 で `txPin` が PC4 でも `UIAPIR_UNUSED_PIN` でもない | TIM1_CH4 は PC4 にしか出ません |
| `rxPin` がこのボードで割り込みを使えない | 通してしまうと「一度も発火しない受信機」になります |
| `rxPin` と `txPin` が同じ物理パッド | 1 つのパッドに復調器出力とプッシュプルのキャリア出力は同居できません |
| `config.captureBufferSize` が範囲外 | `UIAPIR_MIN_FRAME_DURATIONS` 以上 `UIAPIR_RAW_BUFFER_SIZE` 以下である必要があります |
| バッファの確保に失敗 | `config.captureBuffer` が `nullptr` のときのみ（`malloc`） |
| CH32V003 で TIM2 のプリスケーラを作れない | `SystemCoreClock` が 1 MHz を作れない値の場合。UIAPduino では起こりません |

`end()` はキャリア出力と受信割り込みを止め、ライブラリが確保したバッファを解放します。**`begin()` に拒否されたインスタンスで `end()` を呼んでも、他のインスタンスのハードウェアには触れません。** 所有権を持つ別のインスタンスが送信中でも、その送信を止めてしまうことはありません。

### 受信

```cpp
bool available();
bool receive(IRCode &code);
bool learn(IRCode &code);   // receive() の別名
void resume();
```

`available()` は 1 フレーム分の捕捉が完了していれば `true` を返します。フレームの終わりは **`UIAPIR_FRAME_GAP_US`（既定 6000 us）より長いスペース**で判定します。`begin()` していない場合は常に `false` です。

`receive()` は `available()` が `false` なら何もせず `false` を返します。`true` を返したときは `code` を必ず書き換えます。まず `code.clear()` 相当で全体を消してから、捕捉した duration を `UIAPIR_RAW` として格納し、続いて NEC → AEHA → SIRC の順にデコードを試みます。**どれも一致しなかった場合も戻り値は `true` で、`code.protocol` が `UIAPIR_RAW` のままになります。**「受信できたか」と「デコードできたか」は別なので、`code.protocol` で区別してください。

```cpp
if (ir.receive(code)) {
  if (code.protocol == UIAPIR_RAW) {
    // 未知のフォーマット。再送はできるが address / command は読めない
  }
}
```

呼ぶたびに新しい `IRCode` を作らないでください。`IRCode` は既定で 346 バイトあり、CH32V003 のリンカが予約するスタックは 448 バイトしかありません。ファイルスコープに 1 つ置いて使い回します。

`learn()` は `receive()` の別名です。学習リモコンのコードで意図が読みやすくなるためだけに存在し、動作は同一です。

`resume()` は捕捉状態を捨てて次のフレームを待ち直します。戻り値はありません。送信の直後に呼ぶと、自分の発光の反射がバッファに残っていた場合にそれを捨てられます。

### 送信

すべての送信 API は完了までブロックします。送信中は受信割り込みが外れるため、自分の発光を記録することはありません。

```cpp
bool send(const IRCode &code, uint8_t repeats = 0);
```

`code.protocol` で分岐します。プロトコルが判別できているものは **address と command からフレームを再生成**するため、キャリア周波数もリピート構造も正しくなります。`UIAPIR_RAW` の場合のみ、捕捉した duration をそのまま出します。

`repeats` は「最初の 1 回に加えて送る回数」です。SIRC では合計が `UIAPIR_SONY_MIN_FRAMES`（3）まで引き上げられます。

`send()` は、次の場合に `false` を返します。

- `begin()` が成功していない、または `txPin` に `UIAPIR_UNUSED_PIN` を指定した受信専用インスタンス
- `code.protocol` が `UIAPIR_UNKNOWN`、またはこのビルドに含まれていない（[`UIAPIR_ENABLE_*`](#ビルド時マクロ) 参照）
- NEC / AEHA / SIRC の各ペイロードが、後述する対応送信 API の引数条件を満たさない
- RAW の `count` が 0、`carrierKHz` が 20〜60 の範囲外、または **`UIAPIR_FLAG_RAW_OVERFLOW` / `UIAPIR_FLAG_TIMING_CLIPPED` が立っている**

最後の欠損フラグが立った捕捉は、タイミング情報が失われています。再生すると学習したものとは別の信号になるため、成功を報告せずに拒否します。

```cpp
bool sendNEC(uint16_t address, uint8_t command,
             bool extended = false, uint8_t repeats = 0);
bool sendNECRepeat();
```

`extended` が `false` のとき `address` は 0..255 です。`true` のときは 16 ビットですが、**上位バイトが下位バイトの反転になっている 256 通りは拒否されます**。それらは標準 NEC 用に予約されており、送っても 8 ビットアドレスとしてデコードされてしまうためです。

リピートフレームは前のフレーム開始から 110 ms 後に始まります。`sendNECRepeat()` はリピートフレームだけを 1 つ送ります。

```cpp
bool sendAEHA(const uint8_t *data, uint8_t length, uint8_t frames = 1);
```

`length` は `UIAPIR_AEHA_MIN_BYTES`（3）以上 `UIAPIR_MAX_AEHA_BYTES`（既定 20）以下です。2 フレーム目以降は前のフレーム開始から 130 ms 後に始まります。`data` が `nullptr`、`length` が範囲外、`frames` が 0 のときに `false` を返します。

**パリティは検査も生成もしません。** `data` はそのまま送られます。顧客コードから正しいパリティを作るには [`aehaParity()`](#uiapir_protocol-ヘルパー) を使ってください。

```cpp
bool sendSony(uint16_t address, uint8_t command,
              uint8_t bits = 12, uint8_t frames = UIAPIR_SONY_MIN_FRAMES);
```

`bits` は 12、15、20 のいずれかで、それぞれアドレス幅が 5、8、13 ビットになります。`command` は 7 ビットです。`frames` は 3 以上でなければなりません（SIRC は 1 回の送信を 3 回以上繰り返すことを要求します）。範囲外の `bits`、8 ビット目が立った `command`、アドレス幅に収まらない `address`、3 未満の `frames` はすべて `false` です。

```cpp
bool sendRaw(const uint16_t *timingsUs, uint16_t count, uint8_t carrierKHz = 38);
bool sendRawTicks(const uint8_t *ticks, uint16_t count, uint8_t carrierKHz = 38);
```

先頭要素を mark として、mark と space を交互に出します。`sendRaw()` はマイクロ秒、`sendRawTicks()` は `UIAPIR_RAW_TICK_US`（既定 50 us）単位の tick を取ります。後者は `IRCode` に格納されている形式そのままなので、学習データの再生にはこちらを使います。

`carrierKHz` は 20〜60 の範囲です。ポインタが `nullptr`、`count` が 0、範囲外の `carrierKHz` で `false` になります。

---

## `UIAPIRConfig`

受信バッファの確保方法を `begin()` に伝えるだけの構造体です。

```cpp
struct UIAPIRConfig {
    uint8_t *captureBuffer;
    uint16_t captureBufferSize;

    explicit UIAPIRConfig(uint16_t size = UIAPIR_RAW_BUFFER_SIZE);
    UIAPIRConfig(uint8_t *buffer, uint16_t size);
};
```

サイズだけを渡すとライブラリが `malloc()` し、`end()` で解放します。バッファを渡すとヒープを使わず、そのストレージは `end()` まで有効である必要があります。

```cpp
UIAPIRConfig small(67);                        // NEC フレーム 1 つ分
uint8_t storage[99];
UIAPIRConfig supplied(storage, sizeof(storage)); // 6 バイト AEHA フレーム分
```

**注意:** これは実行時の設定なので、縮められるのは受信バッファだけです。`IRCode` に埋め込まれている RAW 領域は `UIAPIR_RAW_BUFFER_SIZE` で固定されており、C++ のオブジェクトレイアウトは実行時には変えられません。両方を縮めたい場合はビルド時マクロを使ってください。

---

## `IRCode` とペイロード

```cpp
struct IRCode {
    IRProtocol protocol;
    uint8_t    flags;        // IRCodeFlags のビット論理和
    uint8_t    carrierKHz;
    IRPayload  data;

    void clear();
    uint16_t rawMicros(uint16_t index) const;
};
```

`clear()` は構造体全体を 0 で埋め、`protocol` を `UIAPIR_UNKNOWN` にします。`receive()` が内部で呼ぶため、通常は明示的に呼ぶ必要はありません。

`rawMicros(index)` は RAW の tick をマイクロ秒に直します。`protocol` が `UIAPIR_RAW` でない場合、または `index` が `data.raw.count` 以上の場合は **0 を返します**。

`carrierKHz` は `receive()` が判別結果に応じて設定します（NEC / AEHA は 38、SIRC は 40、RAW は 38）。復調型の受信モジュールはキャリア成分を落としてしまうため、**未知の RAW フレームの本来のキャリア周波数は知りようがありません。** 38 kHz は推定値です。

### `IRProtocol`

| 値 | 意味 |
|---|---|
| `UIAPIR_UNKNOWN` | 未設定。`clear()` 直後の状態 |
| `UIAPIR_NEC` | NEC / 拡張 NEC |
| `UIAPIR_AEHA` | AEHA（家製協）/ Kaseikyo |
| `UIAPIR_SONY` | Sony SIRC |
| `UIAPIR_RAW` | 未判別。duration の列としてのみ保持 |

### `IRCodeFlags`

`code.flags` にビットで立ちます。

| 値 | 意味 |
|---|---|
| `UIAPIR_FLAG_NONE` | 0 |
| `UIAPIR_FLAG_REPEAT` | NEC のリピートフレーム。address も command も持ちません |
| `UIAPIR_FLAG_RAW_OVERFLOW` | 受信バッファに収まりきらず、末尾が欠けています |
| `UIAPIR_FLAG_TIMING_CLIPPED` | 1 tick に収まらない長さの duration があり、値が丸められています |

後ろの 2 つが立った `IRCode` は `send()` に拒否されます。学習用途では、この 2 つを検査して次の押下を待つのが正しい扱いです。

```cpp
const uint8_t lossy = UIAPIR_FLAG_RAW_OVERFLOW | UIAPIR_FLAG_TIMING_CLIPPED;
if ((code.flags & lossy) == 0) {
  // 再生できる
}
```

### ペイロード

`IRPayload` は共用体です。`protocol` に対応するメンバーだけが有効です。

```cpp
struct IRNECData {
    uint16_t address;
    uint8_t  command;
    uint8_t  addressBits;   // 8 = 標準 NEC, 16 = 拡張 NEC
};

struct IRAEHAData {
    uint8_t length;                          // 3 .. UIAPIR_MAX_AEHA_BYTES
    uint8_t bytes[UIAPIR_MAX_AEHA_BYTES];
};

struct IRSonyData {
    uint16_t address;  // 12 bit: 5 bit / 15 bit: 8 bit
                       // 20 bit: 下位 5 bit がデバイス、bit 5..12 が拡張フィールド
    uint8_t  command;  // 7 bit
    uint8_t  bits;     // 12, 15, 20
};

struct IRRawData {
    uint16_t count;
    uint8_t  ticks[UIAPIR_RAW_BUFFER_SIZE];  // UIAPIR_RAW_TICK_US 単位
};
```

RAW を tick で持つのは SRAM を節約するためです。マイクロ秒を `uint16_t` で持つと 2 倍かかります。

---

## `uiapir_protocol` ヘルパー

`#include <UIAPIRProtocol.h>` が必要です。デコーダとエンコーダが共有している判定を、そのまま呼べるように公開しています。**`UIAPIR_ENABLE_*` で無効にしたプロトコルのヘルパーは宣言ごと消えます。**

```cpp
namespace uiapir_protocol {

bool matchTicks(uint8_t actualTicks, uint16_t targetUs);

#if UIAPIR_ENABLE_NEC
bool decodeNEC(const uint8_t *ticks, uint16_t count, IRNECData &out, bool &repeat);
bool necAddressIsExtendable(uint16_t address);
#endif

#if UIAPIR_ENABLE_AEHA
bool decodeAEHA(const uint8_t *ticks, uint16_t count, IRAEHAData &out);
uint8_t aehaParity(const uint8_t *bytes);
bool aehaParityValid(const uint8_t *bytes, uint8_t length);
#endif

#if UIAPIR_ENABLE_SONY
bool decodeSony(const uint8_t *ticks, uint16_t count, IRSonyData &out);
uint8_t sonyAddressBits(uint8_t bits);
#endif

uint8_t framesFromRepeats(uint8_t repeats, uint8_t minimumFrames);
bool rawIsReplayable(uint8_t flags);

}
```

| 関数 | 用途 |
|---|---|
| `matchTicks` | 捕捉した tick が目標マイクロ秒の許容帯（±`UIAPIR_TOLERANCE_PERCENT` + 1 tick）に入るか。**受信データを自前で検証するときは、独自の閾値ではなくこれを使ってください。** ライブラリ自身が使っている基準と一致します |
| `necAddressIsExtendable` | 16 ビット値が拡張 NEC アドレスとして送れるか（上位バイトが下位バイトの反転でないか） |
| `aehaParity` | 顧客コード 2 バイトから期待されるパリティ（`bytes[2]` の下位 4 ビット）を計算 |
| `aehaParityValid` | `bytes[2]` の下位 4 ビットが `aehaParity()` と一致するか。`bytes` が `nullptr` か `length` が 3 未満なら `false` |
| `sonyAddressBits` | 12 / 15 / 20 に対して 5 / 8 / 13 を返す。それ以外は 0 |
| `framesFromRepeats` | `repeats + 1` を `minimumFrames` まで引き上げ、255 で飽和させる |
| `rawIsReplayable` | `flags` に欠損ビットが立っていないか。`send()` が RAW の再生前に呼ぶ判定と同じもの |

`decodeNEC` / `decodeAEHA` / `decodeSony` は `receive()` が内部で呼ぶものです。捕捉済みの tick 列を自前で持っている場合にのみ直接使ってください。

---

## ビルド時マクロ

スケッチ内の `#define` は**ライブラリには伝わりません**。Arduino はライブラリの `.cpp` を別の翻訳単位としてコンパイルするためです。スケッチ側とライブラリ側で `IRCode` のサイズがずれたまま、警告なしにリンクが通ってしまいます。必ずビルド全体に適用されるコンパイラ定義として渡してください。

```bash
arduino-cli compile --build-property "compiler.cpp.extra_flags=-DUIAPIR_RAW_BUFFER_SIZE=200 -DUIAPIR_MAX_AEHA_BYTES=12" ...
```

PlatformIO では `platformio.ini` の `build_flags` に書きます。

| マクロ | 既定 | 説明 |
|---|---|---|
| `UIAPIR_RAW_BUFFER_SIZE` | 340 | RAW の最大 duration 数。`IRCode` のサイズを決めます |
| `UIAPIR_RAW_TICK_US` | 50 | 1 tick のマイクロ秒。**変更可能な範囲は 36〜50 と狭い**（下限は 9 ms の NEC リーダーが `uint8_t` に収まる限界、上限は SIRC の 600 us と 1200 us の許容帯が重なる限界） |
| `UIAPIR_MAX_AEHA_BYTES` | 20 | AEHA ペイロードの最大バイト数 |
| `UIAPIR_FRAME_GAP_US` | 6000 | フレームの終わりと判定するスペースの長さ |
| `UIAPIR_TOLERANCE_PERCENT` | 25 | デコーダの許容誤差 |
| `UIAPIR_DEFAULT_TX_PIN` | 6 | `begin()` の `txPin` 既定値 |
| `UIAPIR_ENABLE_NEC` | 1 | 0 で NEC のデコーダと `sendNEC()` / `sendNECRepeat()` を除外 |
| `UIAPIR_ENABLE_AEHA` | 1 | 0 で AEHA のデコーダと `sendAEHA()` を除外 |
| `UIAPIR_ENABLE_SONY` | 1 | 0 で SIRC のデコーダと `sendSony()` を除外 |

無効にしたプロトコルの API は**宣言ごと消えます**。呼び出すと実行時に `false` が返るのではなく、コンパイルエラーになります。RAW は外せません。

矛盾する値を指定した場合は、実行時に誤動作するのではなく `#error` でビルドが止まります。たとえば `UIAPIR_RAW_BUFFER_SIZE` を NEC フレーム（67 durations）より小さくすると、`"UIAPIR_RAW_BUFFER_SIZE cannot hold a NEC frame"` で失敗します。

削減効果の実測値は [README](../README.md#使わないプロトコルを外す) にあります。

## プロトコル定数

`UIAPIRProtocolDefs.h` が単一の出典です。送信側、デコーダ、ホストテストがすべてここから導出されるため、送受で値がずれることがありません。出典は [ChaN](https://elm-chan.org/docs/ir_format.html) と [SB-Projects](https://www.sbprojects.net/knowledge/ir/nec.php) で、節ごとに `[ChaN]` `[SBP]` として引用されています。

自前でフレームを組む場合に使うものを挙げます。

| マクロ | 値 | |
|---|---|---|
| `UIAPIR_NEC_CARRIER_KHZ` | 38 | |
| `UIAPIR_NEC_FRAME_PERIOD_US` | 110000 | リピート間隔 |
| `UIAPIR_NEC_FRAME_DURATIONS` | 67 | 1 フレームの duration 数 |
| `UIAPIR_NEC_REPEAT_DURATIONS` | 3 | リピートフレームの duration 数 |
| `UIAPIR_NEC_MAX_STANDARD_ADDRESS` | 0xff | |
| `UIAPIR_AEHA_CARRIER_KHZ` | 38 | |
| `UIAPIR_AEHA_FRAME_PERIOD_US` | 130000 | |
| `UIAPIR_AEHA_MIN_BYTES` | 3 | 顧客コード 2 + パリティ 4 bit + データ 4 bit |
| `UIAPIR_AEHA_DURATIONS(bytes)` | `3 + 16 * bytes` | 必要なバッファ量の計算に |
| `UIAPIR_SONY_CARRIER_KHZ` | 40 | |
| `UIAPIR_SONY_FRAME_PERIOD_US` | 45000 | |
| `UIAPIR_SONY_MIN_FRAMES` | 3 | SIRC が要求する最小送信回数 |
| `UIAPIR_SONY_MAX_BITS` | 20 | |
| `UIAPIR_SONY_DURATIONS(bits)` | `2 + 2 * bits - 1` | |
| `UIAPIR_MIN_FRAME_DURATIONS` | 3 | 捕捉が受け付ける最短のフレーム |
| `UIAPIR_UNUSED_PIN` | 0xff | `begin()` に渡すと、その方向を使いません |

各フォーマットの T 値やリーダー長といった個別のタイミングも同じヘッダにあります。名前は `UIAPIR_<プロトコル>_<部位>_US` の規則です。
