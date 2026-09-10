# Changelog

UIAPIR の注目すべき変更をこのファイルに記録します。

形式は [Keep a Changelog](https://keepachangelog.com/ja/1.1.0/) を参考にし、
バージョン番号は [Semantic Versioning](https://semver.org/lang/ja/) に従います。

## [Unreleased]

初回公開。

- Arduino Uno 系と Arduino-ESP32 向けのポータブルバックエンドを追加。CH32V003 では従来どおり
  TIM1 / TIM2 / EXTI を直接使い、それ以外では `tone()`、`micros()`、`attachInterrupt()` を使用。
  Uno、Pro Mini（ATmega328P、8 MHz / 16 MHz）、ESP32 Dev Module のコンパイルを CI に追加。初期化、各送信方式、学習、再生を手動確認する
  `PortableSmokeTest` を追加。ポータブルバックエンドの実機検証は未実施。
- 16-bit AVR で Sony SIRC のフレーム時間計算がオーバーフローし、送信間隔が長くなる問題を修正。

- NEC / 拡張 NEC（リピートフレームを含む）、AEHA/Kaseikyo（最大 20 バイト）、
  Sony SIRC（12 / 15 / 20 bit）、および RAW の送受信。
- RAW の学習と再生。プロトコルの自動判別。
- `UIAPIR_ENABLE_NEC` / `_AEHA` / `_SONY` によるプロトコル単位のビルド時除外。
- `UIAPIRLink.h`: UIAPduino 2 台間のデータ通信層（簡易モード / ACK 付き再送モード）。
- 実行時に容量を選択できる受信バッファ。
- デスクトップで動くプロトコル・キャプチャ・通信層のホストテストと、
  UIAPduino Pro Micro CH32V003 V1.4 向けの実機テストスケッチ（HT1〜HT6）。
- GitHub Actions によるホストテスト、Arduino Lint、UIAPduino 向けスケッチビルド。
