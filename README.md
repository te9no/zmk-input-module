# zmk-input-module

ZMK キーボード向けの、実行時選択式 input module profile サポートです。

この module は、複数の排他的な入力モジュール経路を持ち、信頼できるハードウェア ID ピンを持たないキーボード向けの汎用コアです。

ユーザーは ZMK behavior から module profile を選択します。選択値は Zephyr settings に保存され、次回起動時に復元されます。その後、通常の ZMK app 初期化より前に選択済み profile が適用され、選択された deferred input path だけを初期化できます。

split keyboard では、profile は左右それぞれの MCU に保存されます。profile 選択 behavior は event source、つまりそのキー入力が発生した物理 half で実行されます。左右で別 module を接続する場合は、left / right それぞれの half で profile を選択してください。

## 提供機能

- `zmk,input-module-mux`
- `zmk,behavior-input-module-select`
- `zmk,input-module-sensor-proxy`
- `zmk,input-module-kscan-proxy`
- `dt-bindings/zmk/input_module.h` の capability flag
- `zmk/input_module.h` の runtime API
- DYA Studio custom subsystem: `dya__input_module`

## DYA Studio SubSystem

`CONFIG_ZMK_INPUT_MODULE_STUDIO_RPC=y` を有効にすると、DYA Studio から module profile を確認・選択できます。

提供する subsystem ID は `dya__input_module` です。

- `GetState`: central/local 側の selected/applied profile と候補 profile 一覧を返します。
- `GetAllStates`: central の状態を notification し、split peripheral にも state report を要求します。
- `SetSelected`: `target = 0` なら central/local、`target > 0` なら split peripheral に profile 選択要求を relay します。

`SetSelected` は即時に別 module device を初期化しません。保存されるのは「次回起動で有効にする selected profile」です。pin 競合を避けるため、profile 変更後は対象 half を reboot / power-cycle してください。

split peripheral には ZMK Studio RPC 本体を載せません。`ZMK_INPUT_MODULE` が split build で `ZMK_SPLIT_RELAY_EVENT` を選択し、central の DYA Studio subsystem から peripheral へ request/report を relay します。

## WebUI

Studio custom UI は `web/` に置いています。

```sh
cd web
npm install
npm run dev
```

production build は次で確認できます。

```sh
npm run build
```

Vite の default base path は `/zmk-input-module/` です。firmware 側の `CONFIG_ZMK_INPUT_MODULE_STUDIO_RPC_UI_URL` は `https://te9no.github.io/zmk-input-module/` を advertise します。

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
