# DYA Input Module Studio

Web UI for the `dya__input_module` ZMK Studio custom subsystem.

The UI is intended for keyboards that keep several mutually exclusive input
module paths in one firmware image and choose the active profile for the next
boot.

## Runtime Behavior

- `target = 0` means the connected central/local half.
- `target > 0` means a split peripheral reported through the central half.
- Saving a profile updates settings for the next boot. It does not initialize a
  different module path at runtime.
- Peripheral saves are optimistic: the UI remains usable while waiting for the
  split state report, and the pending marker is cleared when the report arrives
  or after a short grace period.
- RPC calls use a bounded timeout so a missing or stalled subsystem does not
  leave the UI permanently busy.

## Usage

```sh
npm ci
npm run dev -- --host 127.0.0.1 --port 5173
```

The production build uses `/zmk-input-module/` as the default Vite base path,
matching `CONFIG_ZMK_INPUT_MODULE_STUDIO_RPC_UI_URL`.

```sh
npm run build
```

## Deployment

GitHub Pages deployment is handled by `.github/workflows/web-ui-pages.yml`.
The workflow builds `web/` and publishes `web/dist` to Pages on pushes that
touch `web/**`, `proto/**`, or the workflow itself.
