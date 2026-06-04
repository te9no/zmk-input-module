# zmk-input-module

Runtime-selected input module profile support for ZMK keyboards.

This module provides runtime module profile selection, settings persistence, and proxy devices for static ZMK input graphs.

Provided devicetree features:

- zmk,input-module-mux
- zmk,behavior-input-module-select
- zmk,input-module-sensor-proxy
- zmk,input-module-kscan-proxy

Keyboard repositories should keep their own profile IDs, settings key, keymap bindings, and candidate device overlays.
