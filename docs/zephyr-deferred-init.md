# Zephyr deferred init 変更点

`zmk-input-module` の単一ファームウェア化では、通常の Zephyr にはない deferred device initialization 機能を使います。

この文書は、`te9no/zephyr` fork に入っている変更点をまとめたものです。

## 固定 revision

SparAkashaAnanta では以下の Zephyr revision を参照しています。

```yaml
- name: zephyr
  remote: te9no
  revision: af6fff80212a92f56c6ca9a3a339ab4957a85334
```

この revision には、deferred init に必要な 3 つの commit が含まれています。

| Commit | 内容 |
| ------ | ---- |
| `659c18c8b81` | `device_init()` を追加し、deferred device を明示初期化できるようにする |
| `11369223522` | `zephyr,deferred-init` Devicetree property を base binding に追加する |
| `af6fff80212` | `zephyr,deferred-init` を全 binding で使える global property として扱う |

## なぜ Zephyr 側の変更が必要か

ZMK / Zephyr の通常の device model では、Devicetree で `status = "okay"` の device は boot 時に自動初期化されます。

単一ファームウェアで複数 module 候補を同時に compile する場合、この動作は問題になります。

例:

- `KEY` profile は GPIO direct kscan を使う
- `ENC` profile は encoder A/B を使う
- `JOY` profile は ADC と encoder A/B を使う
- `TB` profile は SPI bus と PMW3610 を使う
- `TPD` profile は I2C bus と touchpad を使う

これらが同じ pin を共有している場合、未選択 profile の device まで boot 時に初期化されると、共有 pin や bus を誤って claim する可能性があります。

そのため、候補 device は firmware に含めたまま、通常 boot では初期化せず、settings 復元後に選択 profile の device だけを初期化する必要があります。

## 変更 1: `device_init()` の追加

Commit: `659c18c8b81 Add deferred device initialization support`

変更ファイル:

- `include/zephyr/device.h`
- `include/zephyr/init.h`
- `kernel/init.c`

追加された API:

```c
int device_init(const struct device *dev);
```

役割:

- boot 時に初期化されなかった device を、後から明示的に初期化する
- 対象 device の元の init entry を探して実行する
- 初期化済み device に対しては、保存済み init result を返す
- init 成功時に device runtime PM の auto enable 処理も行う

戻り値の意味:

- `0`: 初期化成功、またはすでに成功済み
- `-EINVAL`: `dev == NULL`
- `-ENOENT`: 対応する init entry が見つからない
- その他の負値: driver init が返した error

## 変更 2: init entry に deferred flag を追加

Commit: `659c18c8b81 Add deferred device initialization support`

`struct init_entry` に `bool deferred` が追加されています。

```c
struct init_entry {
    union init_function init_fn;
    const struct device *dev;
    bool deferred;
};
```

device init entry を作るとき、Devicetree node に `zephyr,deferred-init` があるかを見て、この flag に入れます。

```c
.deferred = DT_NODE_HAS_PROP(node_id, zephyr_deferred_init),
```

通常 boot の init loop では、device entry が deferred の場合、その device の init を呼びません。

## 変更 3: `zephyr,deferred-init` binding の追加

Commit: `11369223522 Add deferred-init devicetree binding`

変更ファイル:

- `dts/bindings/base/base.yaml`

追加 property:

```yaml
zephyr,deferred-init:
  type: boolean
  description: |
    Skip this device during the standard boot initialization sequence.
    The device must be initialized explicitly with device_init().
```

これにより、Devicetree で以下のように書けるようになります。

```dts
trackball: trackball@0 {
    compatible = "pixart,pmw3610";
    reg = <0>;
    zephyr,deferred-init;
};
```

## 変更 4: global property として扱う

Commit: `af6fff80212 Allow deferred-init as a global devicetree property`

変更ファイル:

- `scripts/dts/python-devicetree/src/devicetree/edtlib.py`

Zephyr の Devicetree validation は、binding に宣言されていない property が node にあると error にします。

`zephyr,deferred-init` は特定 driver だけでなく、任意の device node に付けたい property です。そのため `edtlib.py` 側で、`compatible` や `status` などと同じように、binding 個別宣言がなくても許可する property として扱っています。

これにより、外部 module の独自 binding や ZMK 側の binding でも、個別に `zephyr,deferred-init` を宣言せずに利用できます。

## zmk-input-module での使い方

`zmk-input-module` は settings 復元後に profile を決定し、mux profile の `devices` に並んだ device を順番に `device_init()` します。

```dts
&mykb_module_mux {
    profile_trackball {
        devices = <&spi2 &trackball>;
    };
};
```

この例では、`spi2` を先に初期化し、その後 `trackball` を初期化します。bus に依存する child device がある場合は、この順序が重要です。

## 注意点

- deferred init は node を `disabled` にする機能ではありません。
- `status = "okay"` の device は compile されます。
- 通常 boot の init sequence からだけ除外されます。
- `device_init()` しない限り、その device は ready になりません。
- 未初期化 device を ZMK の静的 graph が直接触ると問題になります。
- 必要に応じて `zmk,input-module-sensor-proxy` や `zmk,input-module-kscan-proxy` を使います。
- 共有 pin への副作用が完全に消えるかは driver / pinctrl 実装にも依存します。最終判断は実機確認が必要です。

## upstream について

現時点では、この変更は `te9no/zephyr` fork で維持します。upstream 提案は行わない方針です。
