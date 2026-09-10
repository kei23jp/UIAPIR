# Contributing to UIAPIR

変更は、対象を小さく保ち、既存のAPIとの互換性とCH32V003のメモリ制約を考慮してください。

## 変更前の確認

Windowsでは次を実行します。

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\check.ps1
```

Unix系環境では次を実行します。

```sh
sh scripts/check.sh
```

このチェックはホストテストに加えて、`examples/`と`extras/hardware-tests/`の全スケッチを
`UIAP:ch32v:CH32V00x_EVT:pnum=CH32V003V1DOT4`向けにコンパイルします。

プロトコル処理だけを変更した場合は、短いホストテストだけを実行できます。

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\tests\run.ps1
```

```sh
sh tests/run.sh
```

タイマー、GPIO、割り込みまたは送受信タイミングに影響する変更では、
`extras/HARDWARE_TESTS.md`に従って実機テストも行ってください。

## リリース

リリース時には`library.properties`と`CHANGELOG.md`のバージョンを一致させ、
すべてのGitHub Actionsと必要な実機テストが成功してから同じバージョンのタグを作成します。

配布するのがライブラリのソースだけであれば、UIAPIR の MIT License 以外に添付するものは
ありません。Arduino core や CH32 の SDK は、利用者が各自で導入する依存環境だからです。

ただし UIAPIR を組み込んだ完成ファームウェア（`.bin` / `.hex` / `.elf`）を配布する場合は、
バイナリに UIAPIR 以外のコードがリンクされている点に注意してください。UIAPduino が使う
WCH の Arduino core には、少なくとも次の 2 系統が含まれます。

- **BSD-3-Clause** — CH32V003 の variant や WCH の周辺ライブラリ（Nanjing Qinheng
  Microelectronics 著作権表示）。再頒布時に著作権表示とライセンス条文の添付を求めます。
- **LGPL-2.1-or-later** — `Print.cpp` や `WString.cpp` など Arduino 由来のコア部分。
  静的リンクしたバイナリを配布する場合、著作権表示だけでは足りず、利用者が
  ライブラリ部分を差し替えて再リンクできる状態（オブジェクトファイルの提供など）が
  求められます。

UIAPIR のソースそのものは MIT のままで、これらの条件が及ぶのは配布するバイナリだけです。
配布する前に、実際にリンクされたファイルのライセンス表記を確認し、
`THIRD_PARTY_NOTICES` としてまとめて同梱してください。

## ライセンスへの同意

このリポジトリへコントリビュートした時点で、その内容が本プロジェクトの MIT License の
もとで提供されることに同意したものとみなします。

By submitting a contribution to this repository, you agree that your contribution is
licensed under the MIT License used by this project.
