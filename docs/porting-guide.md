# Porting zmk-input-module to Another Keyboard

This guide describes how to use `zmk-input-module` from a keyboard config repository.

The model is intentionally user-selected, not auto-detected. The firmware stores the selected profile in Zephyr settings and assumes that profile on the next boot.

## When This Module Fits

Use this module when a keyboard has:

- interchangeable input modules
- shared pins between module paths
- no reliable hardware ID pin
- a static ZMK keymap/sensor/kscan graph that needs to point at a runtime-selected candidate

Typical examples:

- direct key module versus encoder module
- encoder module versus joystick module
- SPI trackball versus I2C touchpad
- multiple encoder definitions that must share one `sensor-bindings` slot

Do not use this module as a general hotplug framework. It does not rewrite Devicetree at runtime and it does not probe unknown hardware.

## Implementation Checklist

1. Add `zmk-input-module` to `config/west.yml`.
2. Define keyboard-specific profile IDs.
3. Define a `zmk,input-module-mux` node.
4. Define a `zmk,behavior-input-module-select` behavior node.
5. Bind the behavior in the keymap.
6. Move conflicting module device definitions into a unified overlay or snippet.
7. Mark mutually-exclusive candidate devices with `zephyr,deferred-init`.
8. Add selected candidate devices to each mux profile with `devices`.
9. Use sensor and kscan proxies where ZMK needs a stable static slot.
10. Build central, peripheral, and `settings_reset`.
11. Validate each profile on hardware.

## 1. Add the Module to west.yml

Add the module to the keyboard config manifest:

```yaml
- name: zmk-input-module
  remote: te9no
  revision: main
  path: modules/zmk-input-module
```

The keyboard repository should also pin a Zephyr revision that supports `zephyr,deferred-init` and `device_init()` if it needs deferred candidate devices.

## 2. Define Profile IDs

Keep profile IDs in the keyboard repository, not in `zmk-input-module`.

Example:

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

Recommended rules:

- reserve `0` for unspecified or disabled state
- use stable numeric values once released
- prefix constants with the keyboard name
- keep capability flags from `dt-bindings/zmk/input_module.h`

## 3. Define the Mux

Add a mux node in the keyboard base overlay or a shared `.dtsi`.

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

Use a keyboard-specific `settings-key`. This prevents settings collisions if multiple keyboards or modules reuse the same generic code.

## 4. Define the Selection Behavior

Add a keyboard-specific behavior node:

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

Then bind it in the keymap:

```dts
#include <dt-bindings/mykb/module_select.h>

&mykb_mod MYKB_MODULE_KEY
&mykb_mod MYKB_MODULE_ENC
&mykb_mod MYKB_MODULE_TB
```

The behavior stores the selected profile. The selected profile is intended to take effect on the next boot after settings are restored.

## 5. Add Candidate Devices

Put candidate module devices in a unified overlay or snippet. Mutually-exclusive candidates should normally be marked with `zephyr,deferred-init`.

Example direct key candidate:

```dts
/ {
    key_module_kscan: key_module_kscan {
        compatible = "zmk,kscan-gpio-direct";
        input-gpios = <&gpio0 1 GPIO_ACTIVE_LOW>;
        zephyr,deferred-init;
    };
};
```

Example SPI trackball candidate:

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

Then attach selected devices to mux profiles:

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

Order matters. If a child device depends on a bus, put the bus first.

## 6. Choose a Default Profile

If a freshly flashed board needs a working key path so the user can select another module, override the default profile in the unified snippet:

```dts
&mykb_module_mux {
    default-profile = <MYKB_MODULE_KEY>;
};
```

If no safe default exists, keep `MYKB_MODULE_UNSPECIFIED` and provide an operational path such as flashing with a known settings state.

## 7. Use the Sensor Proxy

Use `zmk,input-module-sensor-proxy` when ZMK needs one stable `sensor-bindings` slot, but the selected profile changes the underlying sensor device.

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

If the active profile has no sensor route, the proxy accepts ZMK's trigger setup without touching the raw deferred sensor.

## 8. Use the Kscan Proxy

Use `zmk,input-module-kscan-proxy` when ZMK has a static kscan graph but the raw key candidate must not be touched for non-key profiles.

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

Then wire the static ZMK path to the proxy instead of the raw candidate:

```dts
&kscan0 {
    direct {
        kscan = <&key_module_proxy>;
    };
};
```

## 9. Split Keyboard Notes

Settings are stored on each MCU. A central half and a peripheral half can therefore hold different selected profiles if only one side receives the selection behavior or only one side is reset.

Recommended operational rules:

- select the intended profile while both halves can receive the behavior
- reset settings on both halves when debugging profile mismatch
- validate central and peripheral separately
- use side-specific candidate overlays when the physical module path differs by side

## 10. Build Targets

For a unified firmware design, the normal build set should usually be:

- left or central unified firmware
- right or peripheral unified firmware
- `settings_reset`

Avoid keeping per-module firmware variants once the unified candidate graph is working. Otherwise, it becomes unclear whether a bug is in the old snippet path or the runtime-selected path.

## 11. Validation Checklist

Before treating a port as complete, verify:

- first boot fallback initializes the intended default profile
- selected profile is saved and restored after reboot
- `settings_reset` clears the saved profile as expected
- unselected candidates do not claim shared pins
- bus candidates initialize before dependent devices
- sensor proxy does not touch inactive encoder devices
- kscan proxy does not call raw kscan APIs for inactive profiles
- split profile state behaves as expected on both halves
- RAM and flash usage remain acceptable

## Common Pitfalls

- Using generic profile IDs in this module instead of keyboard-specific IDs.
- Reusing the same `settings-key` across unrelated keyboards.
- Forgetting `zephyr,deferred-init` on a pin-conflicting candidate.
- Wiring ZMK's static `sensor-bindings` or `kscan-composite` directly to a raw deferred candidate.
- Initializing a child candidate before its deferred bus.
- Assuming profile selection changes the active hardware path immediately without reboot.
- Testing only central and forgetting that peripheral settings are independent.
