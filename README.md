# zmk-input-module

Runtime-selected input module profile support for ZMK keyboards.

This module provides the reusable core for keyboards that have several mutually-exclusive input module paths, but no reliable hardware ID pin for automatic module detection.

The user selects a module profile through a ZMK behavior. The selected profile is persisted in Zephyr settings, restored on the next boot, and applied before normal ZMK app initialization so the selected deferred input path can be initialized.

## Provided Features

- `zmk,input-module-mux`
- `zmk,behavior-input-module-select`
- `zmk,input-module-sensor-proxy`
- `zmk,input-module-kscan-proxy`
- public capability flags in `dt-bindings/zmk/input_module.h`
- public runtime API in `zmk/input_module.h`

## Keyboard Responsibilities

Keyboard config repositories should keep keyboard-specific state local:

- profile ID header
- default profile
- `settings-key`
- keymap bindings
- candidate device overlays
- split-role wiring
- module-specific validation notes

This module should stay generic. Do not put keyboard names, keyboard-specific profile constants, pin names, or module-specific device graphs in this repository.

## Porting Guide

See [docs/porting-guide.md](docs/porting-guide.md) for the implementation checklist and DTS examples.

## Requirements

The deferred candidate model requires Zephyr support for `zephyr,deferred-init` and `device_init()`.

If your keyboard does not need deferred device initialization, you can still use the profile selection/settings API, but pin-conflicting module paths should be deferred to avoid initializing unused buses or GPIO routes.
