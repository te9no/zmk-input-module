# zmk-input-module

ZMK キーボード向けの、実行時選択式 input module profile サポートです。

この module は、複数の排他的な入力モジュール経路を持ち、信頼できるハードウェア ID ピンを持たないキーボード向けの汎用コアです。

ユーザーは ZMK behavior から module profile を選択します。選択値は Zephyr settings に保存され、次回起動時に復元されます。その後、通常の ZMK app 初期化より前に選択済み profile が適用され、選択された deferred input path だけを初期化できます。

## 提供機能

- `zmk,input-module-mux`
- `zmk,behavior-input-module-select`
- `zmk,input-module-sensor-proxy`
- `zmk,input-module-kscan-proxy`
- `dt-bindings/zmk/input_module.h` の capability flag
- `zmk/input_module.h` の runtime API

## キーボード側の責務

キーボード config repository 側には、キーボード固有の状態を残してください。

- profile ID header
- default profile
- `settings-key`
- keymap binding
- candidate device overlay
- split-role wiring
- left/right で別 module を使う場合の profile 運用
- module-specific validation notes

この repository は汎用 module として保ちます。キーボード名、キーボード固有の profile 定数、pin 名、特定モジュール専用の device graph はここに入れないでください。

## 移植ガイド

他キーボードへの実装手順と DTS 例は [docs/porting-guide.md](docs/porting-guide.md) を参照してください。

Zephyr fork 側の deferred init 変更点は [docs/zephyr-deferred-init.md](docs/zephyr-deferred-init.md) にまとめています。

## 要件

pin 競合する candidate device を扱う場合、`zephyr,deferred-init` と `device_init()` に対応した Zephyr が必要です。

`zephyr,deferred-init` は、`status = "okay"` の device を build 対象に残したまま、通常 boot 時の自動初期化だけを遅らせるための指定です。この module は settings 復元後に選択済み profile を見て、必要な candidate device だけを `device_init()` で初期化します。

遅延初期化が不要なキーボードでも、profile 選択と settings 保存 API は利用できます。共有ピンを持つ module path では、未選択の bus や GPIO route を初期化しないために deferred candidate 化することを推奨します。
