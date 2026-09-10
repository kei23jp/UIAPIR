# UIAPIR ハードウェアテスト手順

`tests/run.sh` は、デスクトップ向けコンパイラ上でプロトコルデコーダとキャプチャ状態マシンをテストします。ただし、以下の3つの領域はホスト側テストでは検証できません。この文書では、それらを実機ボード上で検証する方法を説明します。

| 項目 | ホスト側テストでは検証できない理由 |
|---|---|
| `begin()` / `end()` の引数および所有権ルール | Arduino のピン番号を CH32 コアのピンマップ経由で解決し、TIM1/TIM2 レジスタへ書き込みます。どちらもターゲット実機以外には存在しません。 |
| 搬送波とエンベロープのタイミング | 搬送波は TIM1 ハードウェアから生成され、各エンベロープ時間は TIM2 を使ったビジーウェイトで生成されます。ホスト側テストで確認できるのは、コード内で「意図された」数値が各所で一致していることだけです。実際にピンから何が出ているかはプローブでしか確認できません。 |
| 実際の受信信号 | ホスト側テストは、ライブラリ自身が想定しているタイミングをキャプチャ状態マシンへ与えます。実際のリモコンや復調器が生成する信号はそれとは異なります。 |
| 送信と受信の一致 | ホスト側テストは送信側と受信側を別々に、どちらもライブラリ自身の想定タイミングで検証します。実際に送出した信号を実際に受信してデコードできるかは、光学経路を通してしか確認できません。 |

`extras/hardware-tests/` には6つのテストがあります。HT1〜HT3 は1台構成の自動テストで、シリアル経由で PASS/FAIL を報告します。HT4 と HT5 は測定結果を人が読み取るテストです。HT6 は2台構成の自動テストで、送信と受信を実際の光学経路を通して突き合わせます（Part D）。

---

## 1. 必要な機材

| 機材 | 使用するテスト | 備考 |
|---|---|---|
| UIAPduino Pro Micro CH32V003 V1.4 | すべて | HT6 では2台必要です |
| USB-UART アダプタ（ボードに合わせた 3V3 または 5V ロジック） | すべて | **必須です。** CH32V003 には USB ペリフェラルがなく、このコアには CDC もないため、`Serial` は PD5/PD6 上の USART1 です。結果を読み取る方法はこれ以外にありません。 |
| ジャンパ線 2本 | HT2 | ボード自身のヘッダ間を接続 |
| ジャンパ線 3本 + 1 kΩ 抵抗 2本 | HT6 | ボード間の START、STATUS、GND |
| PlatformIO Core | HT6 | HT6 のみ PlatformIO プロジェクトです。HT1〜HT5 は arduino-cli / IDE でビルドします |
| ロジックアナライザ（4 MHz 以上）またはオシロスコープ | HT4, HT5 | 1 MHz でもエンベロープは観測できますが、38 kHz の搬送波は十分に分解できません |
| 38 kHz IR 受信モジュール（VS1838B、PL-IRM など） | HT5 | |
| IR LED + NPN トランジスタまたは MOSFET + 直列抵抗 | HT4 では任意、HT5 ループバックでは必要 | IR LED を D6 から**直接駆動しないでください** |
| NEC リモコンと Sony リモコン | HT5 | 一般的な民生用リモコンで可。Sony のものは最も厳しいタイミング条件を確認できます |

---

## 2. 共通セットアップ

### シリアル

```text
USB-UART RX  ----  D15 (PD5)     board TX
USB-UART GND ----  GND
```

115200 baud、8N1。入力が必要でなければ、アダプタの TX は未接続のままにしてください。

ボード自身の USB-C から給電します。アダプタ側の電圧セレクタが一致している場合を除き、USB-UART アダプタから同時に VCC を供給しないでください。

### ビルドと書き込み

```bash
arduino-cli compile --fqbn "UIAP:ch32v:CH32V00x_EVT:pnum=CH32V003V1DOT4" \
  --library <path-to-UIAPIR> extras/hardware-tests/HT1_BeginContract
```

```bash
arduino-cli upload --fqbn "UIAP:ch32v:CH32V00x_EVT:pnum=CH32V003V1DOT4" \
  -p <port> extras/hardware-tests/HT1_BeginContract
```

IDE を使う場合は `.ino` を開き、**UIAPduino > Pro Micro CH32V003**、ボードリビジョン **V1.4** を選択してください。

**書き込み前に、RST ボタンを押しながら USB を接続してブートローダーモードにしてください。** UIAPduino は SWD プローブではなく、USB ブートローダー経由で minichlink を使って書き込みます（core の既定の Upload method が minichlink です）。シリアルポート経由の書き込みではないため、上の `-p <port>` は書き込み先の選択には使われません。新しいファームウェアが動き出すとブートローダーは終了するので、次の書き込みでは再度 RST を押しながら接続し直します。

PlatformIO / VS Code を使う場合は、リポジトリ直下の `UIAPIR.code-workspace` を開き、PlatformIO のステータスバーから環境を選びます。HT1〜HT5 は `sketches (PlatformIO)` プロジェクトの `HT1_BeginContract`〜`HT5_ReceiveDump` 環境に対応します。詳細は `extras/platformio/README.md` を参照してください。

ブートローダーは HID デバイス `1209:b803` として列挙されます。HID なので libusb ドライバのインストールは不要ですが、その代わり PlatformIO 同梱の minichlink（upstream 版）では書き込めません。upstream 版が対応しているのは `b003boot` までで、`1209:b003` を探して失敗します。UIAP の Arduino パッケージに入っている minichlink は `b803boot` を追加したフォークで、PlatformIO プロジェクトはそちらを呼ぶよう設定してあります。

書き込みに失敗したときは、まずブートローダーモードに入れているかを確認してください。

```powershell
Get-PnpDevice -PresentOnly | Where-Object { $_.InstanceId -match 'VID_1209&PID_B803' }
```

CH32V003 には USB ペリフェラルがないため、ブートローダーが動いていないボードはホストから見て「未接続」と区別がつきません。行が出なければ、RST を押しながら接続し直してください。

書き込みは成功したのにシリアルに何も出ない場合は、次の順で確認してください。

1. **そのファームウェアが出力しないもの**である。HT6 の送信側はシリアルポートを一切持ちません（GPIO で応答し、ログは受信側にあります）。`examples/Receive`、`SendProtocols`、`SendRaw`、`LearnAndReplay` も出力しない API サンプルです。出力するのは `examples/LearnRemote` と HT1〜HT5、そして HT6 受信側です。
2. **まだブートローダーにいる**。書き込み後に `-b`、リセット、または RST を押さずに挿し直すまで、新しいファームウェアは動き出しません。
3. **TX が RX につながっていない**。ボードの `15`（D15 / PD5）をアダプタの **RX** へ、GND を共通に。アダプタの TX は未接続で構いません。
4. **ボーレート違い**。すべて 115200 です。

5つのスケッチはいずれもビルドできることを確認済みです。HT2 が最も大きく、UIAPduino core 1.0.42ではフラッシュ使用量は約92%です。別バージョンのコアで容量を超えてしまう場合は、すでに検証済みのケースを繰り返している `"and back again"` ブロックを削除してください。

### このテストで使用するピン

| ピン | ポート | 用途 |
|---|---|---|
| D6 | PC4 | 搬送波出力。TIM1_CH4 を持つ唯一のパッドなので固定 |
| D15 | PD5 | アダプタへのシリアル TX |
| D3 | PC1 | テスト対象の受信入力 |
| D4 | PC2 | 2番目の受信入力（HT2） |
| D8, D9 | PC6, PC7 | 信号生成用（HT2）、ボード間ハンドシェイク START / STATUS（HT6） |

シリアル使用中は PD5/PD6 を他の用途に使用しないでください。

EXTI ラインはピン番号によって番号付けされ、各ラインは同時に1つのポートしか使用できません。そのため、受信ピンと同じピン番号を持つ別のピンに割り込みを設定しないでください。

---

## 3. Part A - ホスト側でテストできないロジック

### HT1 - `begin()` / `end()` の契約

**検証内容:**  
対象 MCU に存在しない RX ピンが拒否されること、異なるピン番号で指定しても実際には同一パッドになる RX/TX の組み合わせが拒否されること、D6 以外の TX が拒否されること、共有タイマを別インスタンスが取得できないこと。

**配線:** シリアルのみ。

**実行:** `HT1_BeginContract` を書き込み、シリアルモニタを開いてリセットを押します。

**期待される出力:**

```text
HT1 begin()/end() contract
PASS  begin(D3, D6) accepted
PASS  begin(UNUSED, D6) accepted (transmit only)
PASS  begin(100, D6) rejected: no such pin
PASS  begin(200, D6) rejected: no such pin
PASS  begin(D17, D6) accepted: D17 exists
PASS  begin(D6, D6) rejected: same pad
PASS  begin(A2, D6) rejected: A2 is the same PC4 as D6
PASS  begin(D6, A2) rejected: same pad, named the other way round
PASS  begin(D3, A2) accepted: A2 is the same PC4 as D6
PASS  begin(UNUSED, A2) accepted (transmit only)
PASS  sendNEC() works with TX given as A2
PASS  begin(D3, D5) rejected: TIM1_CH4 is only on PC4
PASS  begin(D3, 100) rejected: no such pin
PASS  first instance begins
PASS  second instance refused while the first owns the timers
PASS  first instance still transmits after the second called end()
PASS  second instance begins after the first released

17 passed, 0 failed
HT1 PASSED
```

**失敗時の読み方:**

- `begin(A2, D6) rejected` が失敗  
  → 解決後の物理ピンではなく、ピン番号そのものを比較しています。`A2` は 0xc2、`D6` は 6 ですが、どちらも PC4 です。そのため、受信入力とプッシュプルの搬送波出力が同一パッド上に配置されてしまいます。

- `begin(100, D6) rejected` が失敗  
  → 使用できない RX ピンが受理されています。コアの `attachInterrupt()` は未知のピンに対して何も通知せずに戻るため、受信割り込みが一度も発生しないままスケッチが動作してしまいます。

- `begin(D17, D6) accepted` が失敗  
  → 検証が厳しすぎて、実在するピンまで拒否しています。D17 は PD7 です。

### HT2 - 受信ピンの再バインド

**検証内容:**  
すでに開始済みのインスタンスに対して2回目の `begin()` を呼んだ際、古いピンの割り込みを残したままにせず、新しいピンへ割り込みが移動すること。

**配線:**

```text
D8 (PC6) ---- D3 (PC1)      generator A -> receiver candidate A
D9 (PC7) ---- D4 (PC2)      generator B -> receiver candidate B
USB-UART RX ---- D15 (PD5)
```

ボード自身のヘッダ間を、通常のジャンパ線2本で接続します。

IR 部品は不要です。スケッチが active-low の NEC フレームをビットバンギングで生成します。これは復調型 IR 受信モジュールの出力波形と同じ形式です。

**実行:** `HT2_PinRebinding` を書き込み、リセットを押します。

**期待される出力:**

```text
HT2 receiver rebinding
PASS  begin(D3, D6)
      received: protocol=1 address=18 command=52
PASS  A is received while bound to D3
PASS  B is ignored while bound to D3
PASS  begin(D4, D6) on the already started instance
PASS  A is ignored after rebinding to D4
      received: protocol=1 address=18 command=52
PASS  B is received after rebinding to D4
...
10 passed, 0 failed
HT2 PASSED
```

`protocol=1` は `UIAPIR_NEC`、18/52 は 0x12/0x34 です。そのため、フレームが正常にデコードされた場合は、ジェネレータからデコーダまでがエンドツーエンドで一致していることも確認できます。

`protocol=4`（`UIAPIR_RAW`）であっても、「受信できた」として扱います。このコアの `delayMicroseconds()` はハードウェア除算器なしで64ビット演算を行うため、ジェネレータのタイミングは正確ではありません。このテストの合否基準は、期待したピンにエッジが届いたかどうかだけです。

**失敗時の読み方:**

- `A is ignored after rebinding to D4` が失敗  
  → 古い EXTI がまだ有効です。ライブラリは、現在参照していないピンから受信し続けることになります。また `end()` は間違ったピンの割り込みを解除するため、古い割り込みが永久に有効なまま残ります。

- `B is received after rebinding to D4` が失敗  
  → 新しいピンの割り込みが設定されていません。2回目の `begin()` 以降、受信が何の通知もなく停止しています。

- 何も受信されない  
  → ジャンパ線を確認し、ジェネレータ用ピンが idle high になっていることを確認してください。

### HT3 - 共有タイマの所有権

**検証内容:**  
タイマを所有していないインスタンスで `end()` を呼んでも、TIM1 と TIM2 に影響を与えないこと。

このテストだけは、誤った値ではなく**ハング**として失敗が現れます。

TIM2 は、送信側のすべてのブロッキング待機処理が使用するタイムベースです。TIM2 が停止すると、`waitUs()` は値が進まないレジスタを待ち続けるため、永久ループになります。

**配線:** シリアルのみ。

**実行:** `HT3_TimerOwnership` を書き込み、リセットを押します。

**期待される出力:**

```text
HT3 shared timer ownership
PASS  owner.begin(D3, D6)
PASS  baseline transmit completes
      baseline elapsed = 232 ms (expect about 232)
PASS  baseline duration is plausible
PASS  intruder.begin() refused
      intruder.end() - must not touch TIM1/TIM2
      sending from the owning instance...
      returned, elapsed = 232 ms
PASS  transmit still returns after a foreign end()
PASS  transmit still takes the same time
PASS  transmit survives end() on a never-started instance
PASS  owner can begin() again after its own end()
PASS  and transmit again

9 passed, 0 failed
HT3 PASSED
```

**失敗時の読み方:**  
出力が

```text
sending from the owning instance...
```

で止まり、その後まったく進まなくなった場合、その沈黙自体が失敗を意味します。ボード自体は動いていますが、`waitUs()` 内で停止しています。

ただし、シリアルアダプタの切断でも似た状態に見えるため、判断する前に一度リセットし、同じ現象が再現することを確認してください。

処理自体は戻ってくるものの、経過時間が 232 ms から大きく外れている場合は、TIM2 が停止しているのではなく、誤った速度で動いています。`SystemCoreClock` を確認してください。TIM2 のプリスケーラはこの値から算出されています。

ここで期待される 231.81 ms は、110 ms の完全な NEC 周期2回分に、末尾の 11.81 ms のリピートフレームを加えた時間です。

---

## 4. Part B - 送信タイミング

### HT4 - 搬送波とエンベロープ

**配線:**  
アナライザで D6（PC4）をプローブし、GND を共通にします。

シリアルは任意ですが、各ケースの開始時に通知が出るため、使用すると便利です。

光学側も観測したい場合のみ IR LED ドライバを追加してください。電気的な測定点は D6 です。

**実行:** `HT4_TransmitTiming` を書き込みます。

このスケッチは各ケース間に 500 ms の無信号時間を入れながら無限ループするため、1回の長いキャプチャを任意の位置で分割して解析できます。

**期待値:**  
以下は、ライブラリ内の定数と 48 MHz の `SystemCoreClock` から計算した値であり、実測値ではありません。そのため、実測結果と一致しない場合は実際に何らかの問題があることを意味します。

| ケース | 測定項目 | 期待値 |
|---|---|---|
| 1 | 搬送波周波数 | 38004.8 Hz（TIM1 ARR 1262） |
| 1 | デューティ比 | 33.33% |
| 2 | 搬送波周波数 | 40000.0 Hz（TIM1 ARR 1199） |
| 2 | デューティ比 | 33.33% |
| 3 | NEC リーダー | 9000 us mark、4500 us space |
| 3 | bit 0 / bit 1 | 560 us mark + 560 us / 560 us mark + 1690 us space |
| 3 | トレーラ | 560 us mark |
| 3 | フレーム長 | 67.98 ms |
| 3 | 最初のリピートまでのギャップ | 42.02 ms |
| 3 | フレーム開始からリピート開始まで | 110.00 ms |
| 3 | リピートフレーム | 11.81 ms、その後 98.19 ms のギャップ |
| 4 | extended NEC フレーム長 | 75.89 ms |
| 4 | 最初のリピートまでのギャップ | 34.11 ms |
| 4 | **周期は変わらず** | **110.00 ms** |
| 5 | AEHA リーダー | 3400 us mark、1700 us space |
| 5 | bit 0 / bit 1 | 425 us mark + 425 us / 425 us mark + 1275 us space |
| 5 | フレーム長 / ギャップ / 周期 | 53.12 / 76.88 / 130.00 ms |
| 6 | SIRC リーダー | 2400 us mark、600 us space |
| 6 | bit 0 / bit 1 | 600 us / 1200 us mark、各ビット間に 600 us space |
| 6 | フレーム長 / ギャップ / 周期 | 38.40 / **6.60** / 45.00 ms |
| 7 | フレーム長 / ギャップ / 周期 | 19.20 / 25.80 / 45.00 ms |

**特に重要な2点:**

- **ケース3とケース4では、フレーム長が 7.9 ms 異なるにもかかわらず、どちらも 110.00 ms の同じ周期になっていなければなりません。**  
  周期はフレーム開始から次のフレーム開始までで測定します。ギャップが固定長になっている実装では、ケース4のリピート開始が 7.9 ms 遅れます。

- **ケース6のギャップは 6.60 ms でなければなりません。**  
  これはライブラリが生成する最も短いギャップであり、受信側の 6000 us フレーム判定しきい値の上限を決定する条件です。実測ギャップがこれより短い場合、受信側が SIRC フレーム同士を結合してしまうため、`UIAPIR_FRAME_GAP_US` を再検討する必要があります。

ビジーウェイトによるタイミングは割り込み負荷によってずれます。SysTick は 1 ms ごとに発生し、そのハンドラの実行時間分だけ一部の期間が延びる可能性があります。

数 µs 程度の一貫した正方向のずれは想定内です。一方、時間の長さに比例してずれが大きくなる場合は、TIM2 のプリスケーラが間違っています。

---

## 5. Part C - 実際の受信信号

### HT5 - 受信ダンプ

**配線:**

```text
IR receiver module OUT ---- D3 (PC1)
IR receiver module VCC ---- 5V or 3V3, matching the module
IR receiver module GND ---- GND
USB-UART RX            ---- D15 (PD5)
```

**実行:**  
`HT5_ReceiveDump` を書き込み、リモコンを受信モジュールへ向けます。

**確認項目:**

1. **デコード**

   NEC リモコンでは `NEC` と表示され、同じキーについて address と command が安定していなければなりません。

   Sony リモコンでは `SIRC` と表示され、`bits=12`、`15`、または `20` になります。

   エアコン用リモコンでは通常、バイト列付きの `AEHA`、またはペイロードが `UIAPIR_MAX_AEHA_BYTES` を超える場合は `RAW` と表示されます。

2. **1回のボタン押下あたりのフレーム数**

   ボタンを短く押してすぐ離します。

   - NEC: データフレーム1つ。場合によっては、その後にリピートコードが続く
   - Sony: **1回の押下につき3フレーム**

   Sony で3フレーム未満しか取得できない場合、キャプチャ側がフレーム同士を結合しています。これは、6000 us のしきい値が防ぐために存在する障害モードです。

   ビット数も記録してください。20-bit リモコンが最も厳しいケースです。

3. **キーを押し続ける**

   NEC は、最初に1つのフレームが出た後、`REPEAT` フレームが連続して出るはずです。

   Sony は、同一内容のフレームが一定間隔で連続するはずです。

4. **フレーム内の space**

   `RAW` として返されたフレームでは、スケッチが `max space` を表示し、それが 4500 us を超えると警告します。

   4500 us は NEC の leader space であり、サポートされている各フォーマットが1フレーム内に持つ space のうち最長です。

   すべての値は 6000 us のしきい値未満でなければなりません。ここで警告が出るリモコンでは、フレームが途中で分割され始める可能性があります。

5. **割り込みレイテンシ**

   `RAW` の NEC らしいフレームについて、最初の duration を 9000 us と比較します。

   すべての duration に一様なオフセットがある場合は、エッジ割り込み自体のレイテンシであり、±25% のマッチ許容範囲内であれば問題ありません。

   大きい、または不規則なオフセットがある場合は、別の割り込みがキャプチャ処理と競合している可能性があります。

6. **フラグ**

   `OVERFLOW` は、信号が `UIAPIR_RAW_BUFFER_SIZE` より長いことを意味します。

   `CLIPPED` は、いずれかの duration が 12.75 ms を超えたことを意味します。

   `send()` は、どちらかのフラグが付いた信号の再送を拒否します。

   長いエアコン用リモコンのフレームでは、どちらも発生する可能性があり、これは情報として通知されるものであって、不具合ではありません。

### HT5b - 学習・再送ループバック

受信モジュールをそのまま接続した状態で、送信用エミッタを追加します。

```text
D6 (PC4) ---- 1 kohm ---- transistor base
transistor collector ---- IR LED cathode side, LED anode to VCC through 100 ohm
transistor emitter ---- GND
```

`examples/LearnAndReplay` を書き込みます。

`examples/LearnRemote` でも同じ配線で確認できます。こちらは押しボタンで学習と再送を操作でき、学習したプロトコルとフィールドをシリアルへ出力するため、機器が反応しなかったときに「学習できていない」のか「再送が届いていない」のかを切り分けられます。配線はスケッチ冒頭のコメントにあります。

リモコンからキーを1つ学習させ、その後スケッチに元の機器へ向けて再送させ、機器が反応することを確認します。

これは送受信チェーン全体を確認する唯一のエンドツーエンドテストであり、最終的に「ライブラリが動作する」と言えるかどうかを確認するテストです。

再送した信号が新しい受信フレームとして拾われないよう、エミッタは受信モジュールとは別方向へ向けてください。

ライブラリは送信中に受信側の割り込みを解除するため、自分自身の送信信号を記録することはありません。ただし、送信後もしばらくは受信モジュールの AGC が回復途中の状態になります。

---

## 6. Part D - 2台構成の送受信テスト

### HT6 - 送信と受信を突き合わせる

HT1〜HT5 は、いずれも人がシリアルログか波形を読んで正否を判断するテストです。HT6 は判断まで自動化します。UIAPduino 2台を向かい合わせ、片方が送信し、もう片方が実際の光学経路を通して受信・デコードし、PASS/FAIL を出力します。

送受信を突き合わせて確認できるのはこのテストだけです。また、放置して連続実行できるのもこれだけです。

**1台でループバックできない理由**は、ライブラリが送信中に受信割り込みを意図的に解除しているためです。自分の発光を記録しないための仕様なので、1台では自分の信号を聞けません。2台であれば、NEC のキー長押しや SIRC が必須とする3フレームといったバーストも、そのまま丸ごと受信できます。受信側の実装で最も間違えやすい部分がここです。

搬送波の周波数とデューティ比は測定しません。そこは引き続き HT4 と測定器が必要です。

### 追加で必要な機材

| 機材 | 備考 |
|---|---|
| UIAPduino Pro Micro CH32V003 V1.4 | 2台目 |
| ジャンパ線 3本 | ボード間の START、STATUS、GND |
| 1 kΩ 抵抗 2本 | START と STATUS に直列 |
| PlatformIO Core | `pio` コマンド |

USB-UART は1台のままで足ります。ログは受信側に置きます。デコード結果があるのは受信側であり、送信側は API の戻り値を GPIO 線で答えるためです。

### 配線

```text
        transmitter                                  receiver
   +-------------------+                        +-------------------+
   |                   |     [1k] NPN           |                   |
   |  A2 (D6, PC4) o---|-----------|<  IR LED   |                   |
   |                   |                 |      |    IR module      |
   |                   |                ))) ((( |  OUT o            |
   |                   |                        |      |            |
   |                   |                        |  3 (D3, PC1) o    |
   |                   |                        |                   |
   |   8 (D8, PC6) o<--|------[1k]--------------|--o 8 (D8, PC6)    |  START
   |   9 (D9, PC7) o---|------[1k]------------->|--o 9 (D9, PC7)    |  STATUS
   |        GND    o---|------------------------|--o GND            |
   |                   |                        |                   |
   |                   |                        | 15 (D15, PD5) o---|--> USB-UART RX
   +-------------------+                        +-------------------+
```

両方のボードで同じピン番号を使い、START と STATUS だけが入出力を入れ替えて交差します。注意点は以下です。

- **GND は必ず共通にしてください。** IR 経路は光学的なので基準電位は不要ですが、START と STATUS は通常のロジック信号なので共通 GND が必要です。
- **電源は各ボードの USB-C から個別に供給してください。** 5V や 3V3 を橋渡ししないでください。
- **START と STATUS には直列抵抗を入れてください。** 逆向きに駆動するファームウェアを誤って書き込んだ場合（環境を間違えて書き込むのは容易に起こります）、抵抗が入っていれば故障を防げます。
- **LED はモジュールへ向け、数 cm 離してください。** 遠いのは問題ありませんが、近すぎて復調器が飽和するとマークが伸び、ライブラリ側の不具合に見えるタイミングが報告されます。
- 基板シルクの `A2` は Arduino の D6、`3` は D3 です。両ファームウェアは起動時に、コアが D3/D8/D9 を実際に PC1/PC6/PC7 へ割り当てているかを確認し、違っていれば動作を拒否します。

### 書き込みと実行

書き込みは1台ずつ行ってください。2台同時に接続すると、書き込みツールから見て同一のターゲットが2つ存在することになります。

```bash
cd extras/hardware-tests/HT6_TwoBoard && pio run -e transmitter -t upload
```

```bash
cd extras/hardware-tests/HT6_TwoBoard && pio run -e receiver -t upload
```

```bash
cd extras/hardware-tests/HT6_TwoBoard && pio device monitor -e receiver
```

初回の `pio run` は CH32V プラットフォームとツールチェーンを取得するため数分かかります。書き込まずにビルドだけ確認する場合は `pio run` のみを実行してください。

起動順序は問いません。一方を実行中にもう一方をリセットしても構いません。受信側が各ケース番号を START 線上のパルス数で通知するため、リセットの影響は1ケースの失敗にとどまり、以降のケースが番号ずれで誤判定されることはありません。

### 期待される出力

```text
UIAPIR HT6 - two-board send and receive test
cases 17, capture 112 durations, frame gap 6000 us, raw tick 50 us
waiting for the transmitter

run 1
[00] NEC standard           frames 1/1  api true    98 ms  PASS
[01] NEC extended           frames 1/1  api true   106 ms  PASS
[02] NEC + 2 repeats        frames 3/3  api true   262 ms  PASS
[03] AEHA 6 byte x2         frames 2/2  api true   226 ms  PASS
[04] SIRC 12 bit x3         frames 3/3  api true   139 ms  PASS
[05] SIRC 15 bit x3         frames 3/3  api true   143 ms  PASS
[06] SIRC 20 bit x3         frames 3/3  api true   158 ms  PASS
[07] RAW ticks              frames 1/1  api true    39 ms  PASS
[08] RAW via send()         frames 1/1  api true    39 ms  PASS
[09] reject NEC addr > 255  frames 0/0  api false   30 ms  PASS
[10] reject NEC complement  frames 0/0  api false   30 ms  PASS
[11] reject AEHA too short  frames 0/0  api false   30 ms  PASS
[12] reject AEHA too long   frames 0/0  api false   30 ms  PASS
[13] reject SIRC 13 bit     frames 0/0  api false   30 ms  PASS
[14] reject SIRC cmd 0x80   frames 0/0  api false   30 ms  PASS
[15] reject SIRC 2 frames   frames 0/0  api false   30 ms  PASS
[16] reject RAW clipped     frames 0/0  api false   30 ms  PASS
run 1: 17/17 PASS
```

2回目以降は失敗したケースだけを表示するため、連続運転時は数秒ごとに1行の要約が出るだけになります。1回の実行は、実行間の 2 秒の待ちを含めて10秒前後です。

実機で 17/17 の通過を確認済みで、`elapsed` は下表と 1 ms 以内で一致しました。下表の値はプロトコル定数から算出したものであり実測値ではないため、これより大きくずれるケースがあれば調査対象です。

| ケース | フレーム数 | 送出時間 | elapsed |
|---|---|---|---|
| NEC 0x12 / 0x34 | 1 | 67.98 ms | 98 ms |
| 拡張 NEC 0xfffe / 0xff | 1 | 75.89 ms | 106 ms |
| NEC + リピート2回 | 3 | 110 + 110 + 11.81 ms | 262 ms |
| AEHA 6 byte × 2 | 2 | 130 + 65.88 ms | 226 ms |
| SIRC 12 bit × 3 | 3 | 45 + 45 + 19.20 ms | 139 ms |
| SIRC 15 bit × 3 | 3 | 45 + 45 + 23.40 ms | 143 ms |
| SIRC 20 bit × 3 | 3 | 45 + 45 + 38.40 ms | 158 ms |
| RAW、1 ms × 9 | 1 | 9.00 ms | 39 ms |
| 拒否ケース | 0 | 送出なし | 30 ms |

全ケース共通で乗っている 30 ms はハンドシェイクです。START パルス列の終了判定 12 ms と、搬送波開始前の待ち 20 ms の合計です。

リピートおよび複数フレームのケースは開始間隔で測っているため、ケース 02 は「フレーム長を3回足した値」ではなく 262 ms になります。周期が「フレーム長＋固定ギャップ」に退行した場合に動くのがこの値です。

**特に確認すべきはケース 06 です。** 20 bit の全ビット 1 は 38.40 ms で、SIRC の 45 ms 周期に残るギャップは 6.60 ms しかありません。受信側のフレーム区切り閾値 6000 µs が使える上限を決めているのがこの値であり、ここでフレームが 3 つ揃うことが、閾値が現実の信号で成立している証拠になります。

### 失敗の読み方

失敗したケースは理由を出力します。STATUS 線があることで区別できるのは以下です。

| 出力 | 意味 |
|---|---|
| `transmitter never went idle` | STATUS が一度も HIGH になりません。そのボードに別のファームウェアが入っている、ボード間の GND が繋がっていない、STATUS 線の断線、または送信側が `begin()` に失敗するかピンマップ検査に失敗して意図的に LOW を保持しています。 |
| `transmitter did not answer START` | パルス列の後も STATUS が LOW になりません。START 線の断線、または誤ったピンへの配線です。 |
| `transmitter did not go idle again` | STATUS が LOW のまま戻りません。送信側が `send()` から抜けていません。TIM2 が停止した場合の症状であり、HT3 が切り分ける不具合と同じものです。 |
| `send() returned false, expected true` | 呼び出しが拒否されました。光学経路とは無関係です。ケースの引数とライブラリの検証条件を比較してください。 |
| `send() returned true, expected false` | より重大です。拒否すべき引数が受理されています。そのケースが `UIAPIRProtocolDefs.h` の制限と一致しているか確認してください。 |
| `N frames decoded, expected M` | 少ない場合はフレームの取りこぼしか未送出、多い場合は1フレームが分割された、または前のケースが漏れ込んでいます。 |
| `payload does not match what was sent` | デコードは成功しましたが内容が異なります。RAW ケースでは、実際に受信した duration が次の行に µs 単位で表示されます。 |
| `decoded as the wrong protocol` | 多くはプロトコルを期待した箇所で RAW になった場合です。フレームは届いているが、いずれかの duration が許容帯を外れています。 |
| `wrong number of durations` | RAW フレームのエッジ数が送出時と違います。実際に受信した duration が次の行に表示されます。 |
| `repeat flag on the wrong frame` | 先頭フレームがリピートコードだった、または後続フレームが通常フレームでした。 |
| `capture overflowed or clipped a duration` | duration が 12.75 ms を超えたか、フレームがバッファを超えました。このテスト計画では、テスト対象以外が発光していることを意味します（日光、蛍光灯、プラズマディスプレイ、他人のリモコンなど）。 |

似ているが原因が異なる2つの失敗があります。

- **`frames 0/1` かつ `api true`** — 送信側は送出したと認識し、受信側は何も聞いていません。光学経路の問題です。LED の極性、トランジスタ、モジュールの電源電圧、見通しを確認してください。
- **`frames 0/1` かつ `api false`** — 呼び出しが拒否され、実際に何も送出されていません。光学経路は関係ありません。

全ケースが同時に失敗する場合は、ライブラリではなく配線か組み合わせを疑ってください。特定のプロトコルだけ失敗する場合は、そのプロトコルのタイミング定数を確認してください。

### テスト計画の変更

`include/HT6TestPlan.h` がテスト計画そのものです。ピン、ハンドシェイクのタイミング、ケース表がすべてここにあり、ケースに関する情報は他のどこにもありません。ケースの追加は、`HT6_CASES` に1行、`HT6_CASE_NAMES` に1つ、必要なら送信側の `runCase()` に1分岐です。行と名前の数が食い違うと `static_assert` でビルドが失敗するため、覚えておく必要があるのは `runCase()` の分岐だけです（分岐のない kind は `send() returned false` として現れます）。

ケース番号は START 線上のパルス数で伝わるため、**2台には同一リビジョンのヘッダをビルドして書き込んでください。** リビジョンが異なると、同じ名前で別のテストを実行することになります。表の範囲外の番号は、不一致そのものとしてではなく `send() returned false` として現れます。

受信側はフレームを保存せず、届いた時点でカウンタへ畳み込みます。`IRCode` 1つで 2048 byte 中 346 byte を占めるためです。ケース実行中は何も出力しません。連続する 20 bit SIRC フレームの間隔は 6.6 ms しかなく、キャプチャは完成フレームを1つしか保持しないため、このループ内で `Serial.print()` を呼ぶとフレームを落とし、それが受信側の不具合として報告されてしまいます。

英語版のプロジェクト固有の情報（ファイル構成、ビルド設定、フラッシュ使用量）は `extras/hardware-tests/HT6_TwoBoard/README.md` にあります。

---

## 7. 記録シート

| テスト | 結果 | 備考 |
|---|---|---|
| HT1 `begin()` / `end()` contract | PASS / FAIL | |
| HT2 receiver rebinding | PASS / FAIL | |
| HT3 shared timer ownership | PASS / FAIL | |
| HT4-1 carrier 38 kHz | ____ Hz, ____ % | 期待値 38004.8 Hz, 33.33% |
| HT4-2 carrier 40 kHz | ____ Hz, ____ % | 期待値 40000.0 Hz, 33.33% |
| HT4-3 NEC period | ____ ms | 期待値 110.00 |
| HT4-4 extended NEC period | ____ ms | 期待値 110.00、上と同じ |
| HT4-5 AEHA period | ____ ms | 期待値 130.00 |
| HT4-6 SIRC 20 bit gap | ____ ms | 期待値 6.60、6.00 を超える必要あり |
| HT4-7 SIRC 12 bit period | ____ ms | 期待値 45.00 |
| HT5 NEC remote decodes | yes / no | model: |
| HT5 Sony remote decodes | yes / no | model: , bits: |
| HT5 Sony frames per press | ____ | 期待値 3 |
| HT5 max intra-frame space | ____ us | 6000 未満であること |
| HT5 leader mark as measured | ____ us | 公称値 9000、差分 = latency |
| HT5b learn and replay | works / no | device: |
| HT6 run 1 | ____ / 17 | 全ケース PASS であること |
| HT6 case 06 SIRC 20 bit | ____ / 3 frames | 6.60 ms ギャップでフレームが分離できているか |
| HT6 case 02 elapsed | ____ ms | 期待値 262、開始間隔で測っている証拠 |
| HT6 rejections 09-16 | ____ / 8 | すべて api false かつ 0 frames |
| HT6 soak | ____ runs, ____ failures | 連続運転した場合 |

ボードリビジョン、コアバージョン、`SystemCoreClock` も一緒に記録しておくことを推奨します。

上記の期待値はすべて 48 MHz を前提としています。
