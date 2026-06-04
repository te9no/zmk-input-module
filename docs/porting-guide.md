# zmk-input-module を他キーボードへ移植する

この文書は、キーボード config repository から `zmk-input-module` を使うための実装手順です。

この方式は、意図的に「ユーザー選択式」です。自動判別ではありません。ファームウェアはユーザーが選んだ profile を Zephyr settings に保存し、次回起動時にその profile が装着されている前提で初期化します。

## この module が向いているケース

以下の条件を持つキーボードに向いています。

- 交換式の入力モジュールがある
- module path 間で pin を共有している
- 信頼できる hardware ID pin がない
- ZMK の静的な keymap / sensor / kscan graph から、実行時に選ばれた candidate へ接続したい

典型例:

- direct key module と encoder module を切り替える
- encoder module と joystick module を切り替える
- SPI trackball と I2C touchpad を切り替える
- 複数の encoder 定義を 1 つの `sensor-bindings` slot に集約する

この module は汎用 hotplug framework ではありません。runtime に Devicetree を書き換えたり、不明な hardware を probe したりはしません。

## 実装チェックリスト

1. `config/west.yml` に `zmk-input-module` を追加する。
2. キーボード固有の profile ID を定義する。
3. `zmk,input-module-mux` node を定義する。
4. `zmk,behavior-input-module-select` behavior node を定義する。
5. keymap に profile 選択 behavior を配置する。
6. 競合する module device 定義を unified overlay または snippet に移す。
7. 排他的な candidate device に `zephyr,deferred-init` を付ける。
8. 各 mux profile の `devices` に、選択時に初期化する candidate device を追加する。
9. ZMK 側に固定 slot が必要な場合は sensor proxy / kscan proxy を使う。
10. central / peripheral / `settings_reset` を build する。
11. 各 profile を実機で検証する。

## 1. west.yml に module を追加する

キーボード config manifest に module を追加します。

```yaml
- name: zmk-input-module
  remote: te9no
  revision: main
  path: modules/zmk-input-module
```

candidate device を deferred init する場合、そのキーボード repository は `zephyr,deferred-init` と `device_init()` に対応した Zephyr revision も固定する必要があります。

## 2. Profile ID を定義する

profile ID は `zmk-input-module` ではなく、キーボード repository 側に置きます。

例:

```c
#pragma once

#include <dt-bindings/zmk/input_module.h>

#define MYKB_MODULE_UNSPECIFIED 0
#define MYKB_MODULE_KEY 1
#define MYKB_MODULE_ENC 2
#define MYKB_MODULE_JOY 3
#define MYKB_MODULE_TB 4
#define MYKB_MODULE_TPD 5
```

推奨ルール:

- `0` は unspecified / disabled state 用に予約する
- 一度公開した numeric value は安定させる
- 定数にはキーボード名の prefix を付ける
- capability flag は `dt-bindings/zmk/input_module.h` から使う

## 3. Mux を定義する

キーボードの base overlay または共通 `.dtsi` に mux node を追加します。

```dts
#include <dt-bindings/mykb/module_select.h>
#include <dt-bindings/zmk/input_module.h>

/ {
    mykb_module_mux: module_mux {
        compatible = "zmk,input-module-mux";
        settings-key = "mykb/module";
        default-profile = <MYKB_MODULE_UNSPECIFIED>;

        profile_key {
            profile-id = <MYKB_MODULE_KEY>;
            display-name = "KEY";
            capabilities = <ZMK_INPUT_MODULE_CAP_KSCAN>;
        };

        profile_encoder {
            profile-id = <MYKB_MODULE_ENC>;
            display-name = "ENC";
            capabilities = <ZMK_INPUT_MODULE_CAP_ENCODER>;
        };

        profile_trackball {
            profile-id = <MYKB_MODULE_TB>;
            display-name = "TB";
            capabilities = <ZMK_INPUT_MODULE_CAP_SPI>;
        };
    };
};
```

`settings-key` はキーボード固有にしてください。複数のキーボードや module が同じ汎用実装を使う場合でも、settings の衝突を避けられます。

## 4. Selection Behavior を定義する

キーボード固有の behavior node を追加します。

```dts
/ {
    behaviors {
        mykb_mod: mykb_mod {
            compatible = "zmk,behavior-input-module-select";
            #binding-cells = <1>;
            label = "MYKB_MODULE_SELECT";
        };
    };
};
```

keymap から使います。

```dts
#include <dt-bindings/mykb/module_select.h>

&mykb_mod MYKB_MODULE_KEY
&mykb_mod MYKB_MODULE_ENC
&mykb_mod MYKB_MODULE_TB
```

この behavior は選択 profile を保存します。保存した profile は、settings 復元後の次回起動で有効になる想定です。

## 5. Candidate Device を追加する

candidate module device は unified overlay または snippet に置きます。排他的な candidate は基本的に `zephyr,deferred-init` を付けます。

Direct key candidate の例:

```dts
/ {
    key_module_kscan: key_module_kscan {
        compatible = "zmk,kscan-gpio-direct";
        input-gpios = <&gpio0 1 GPIO_ACTIVE_LOW>;
        zephyr,deferred-init;
    };
};
```

SPI trackball candidate の例:

```dts
&spi2 {
    status = "okay";
    zephyr,deferred-init;

    trackball: trackball@0 {
        compatible = "pixart,pmw3610";
        reg = <0>;
        spi-max-frequency = <2000000>;
        zephyr,deferred-init;
    };
};
```

選択時に初期化する device を mux profile へ接続します。

```dts
&mykb_module_mux {
    profile_key {
        devices = <&key_module_kscan>;
    };

    profile_trackball {
        devices = <&spi2 &trackball>;
    };
};
```

順序は重要です。child device が bus に依存する場合は、bus を先に並べます。

## 6. Default Profile を決める

fresh flash 後に key path が必要で、ユーザーがそこから別 module を選択する設計なら、unified snippet 側で default profile を上書きします。

```dts
&mykb_module_mux {
    default-profile = <MYKB_MODULE_KEY>;
};
```

安全な default がない場合は `MYKB_MODULE_UNSPECIFIED` のままにし、既知の settings state で flash するなど、別の運用経路を用意してください。

## 7. Sensor Proxy を使う

ZMK 側には 1 つの安定した `sensor-bindings` slot が必要だが、profile によって背後の sensor device が変わる場合は `zmk,input-module-sensor-proxy` を使います。

```dts
/ {
    encoder_enc: encoder_enc {
        compatible = "alps,ec11";
        a-gpios = <&gpio0 2 GPIO_ACTIVE_HIGH>;
        b-gpios = <&gpio0 3 GPIO_ACTIVE_HIGH>;
        steps = <24>;
        zephyr,deferred-init;
    };

    encoder_joy: encoder_joy {
        compatible = "alps,ec11";
        a-gpios = <&gpio0 2 GPIO_ACTIVE_HIGH>;
        b-gpios = <&gpio0 3 GPIO_ACTIVE_HIGH>;
        steps = <3>;
        zephyr,deferred-init;
    };

    encoder_proxy: encoder_proxy {
        compatible = "zmk,input-module-sensor-proxy";

        profile_encoder {
            profile-id = <MYKB_MODULE_ENC>;
            sensor = <&encoder_enc>;
            triggers-per-rotation = <10>;
        };

        profile_joystick {
            profile-id = <MYKB_MODULE_JOY>;
            sensor = <&encoder_joy>;
            triggers-per-rotation = <1>;
        };
    };
};

&sensors {
    sensors = <&encoder_proxy>;
};
```

active profile に sensor route がない場合、proxy は ZMK の trigger setup を受け付けますが、raw deferred sensor には触りません。

## 8. Kscan Proxy を使う

ZMK 側に静的な kscan graph が必要だが、non-key profile では raw key candidate を触りたくない場合は `zmk,input-module-kscan-proxy` を使います。

```dts
/ {
    key_module_kscan: key_module_kscan {
        compatible = "zmk,kscan-gpio-direct";
        input-gpios = <&gpio0 1 GPIO_ACTIVE_LOW>;
        zephyr,deferred-init;
    };

    key_module_proxy: key_module_proxy {
        compatible = "zmk,input-module-kscan-proxy";

        profile_key {
            profile-id = <MYKB_MODULE_KEY>;
            kscan = <&key_module_kscan>;
        };
    };
};
```

ZMK の静的経路は raw candidate ではなく proxy に接続します。

```dts
&kscan0 {
    direct {
        kscan = <&key_module_proxy>;
    };
};
```

## 9. Split Keyboard の注意点

settings は各 MCU に保存されます。そのため central half と peripheral half は、片側だけが selection behavior を受け取ったり、片側だけ settings reset された場合、異なる selected profile を保持できます。

推奨運用:

- 両 half が behavior を受け取れる状態で profile を選択する
- profile mismatch を疑う場合は両 half の settings を reset する
- central / peripheral を別々に検証する
- 物理的な module path が左右で異なる場合は side-specific candidate overlay を使う

## 10. Build Target 方針

unified firmware design では、通常の build set は以下に寄せます。

- left / central unified firmware
- right / peripheral unified firmware
- `settings_reset`

unified candidate graph が動き始めたら、per-module firmware variant は残さない方がよいです。古い snippet path と runtime-selected path のどちらに問題があるのか分かりにくくなるためです。

## 11. 検証チェックリスト

移植完了と判断する前に、以下を確認します。

- 初回起動 fallback が意図した default profile を初期化する
- 選択 profile が保存され、reboot 後に復元される
- `settings_reset` で保存 profile が期待通り消える
- 未選択 candidate が共有 pin を claim しない
- bus candidate が dependent device より先に初期化される
- sensor proxy が inactive encoder device に触らない
- kscan proxy が inactive profile で raw kscan API を呼ばない
- split profile state が両 half で期待通り動く
- RAM / flash 使用量が許容範囲に収まる

## よくある落とし穴

- generic module 側に keyboard-specific profile ID を置いてしまう。
- unrelated keyboard 間で同じ `settings-key` を使い回す。
- pin 競合する candidate に `zephyr,deferred-init` を付け忘れる。
- ZMK の静的 `sensor-bindings` や `kscan-composite` を raw deferred candidate に直接つなぐ。
- deferred bus より先に child candidate を初期化しようとする。
- profile 選択が reboot なしで即座に active hardware path を切り替えると誤解する。
- central だけ検証して peripheral の独立 settings を見落とす。
