# TapUI

Device-defined UI over Bluetooth LE: an NFC tag / QR code opens the page in
`web/` with `?n=<device name>`; the page connects over Web Bluetooth and
draws whatever controls the device describes. User docs: `README.md`. Wire
format: `docs/protocol.md` (keep it, `web/proto.js` and `fw/src/tapui/`
in step).

- **Page** (`web/`): plain ES modules, no build, no dependencies.
  `proto.js` is pure (node tests import it); `app.js` is DOM + Bluetooth;
  `sw.js` caches the page for offline use. **Bump `CACHE` in `sw.js` when
  you change any file in `web/`**, or phones keep serving the old copy
  first. Tests: `npm test`, `npm run test:e2e` (Playwright against
  `tests/e2e/fake-bluetooth.js`, a simulated device; keep it behaving like
  the firmware). Run them inside `nix develop`: Playwright's own Chromium
  doesn't run on NixOS.
- **Firmware** (`fw/`): upstream Zephyr v4.5.0-rc2 (no NCS), built with
  `nix develop .#fw`. `src/tapui/` is the reusable part; `src/main.c` is the
  demo device. `proto.c` and `ndef.c` are pure and unit-tested on native_sim
  (`fw/scripts/test.sh`); `tapui_ble.c` and `nfc_tag.c` are hardware.
  `TAPUI_WEST_WORKSPACE` can point the scripts at another v4.5 workspace
  (fieldpad's `fw/.west-workspace` works) instead of fetching one.
- **NFC tag**: upstream Zephyr has no NFC tag library, so `nfc_tag.c`
  drives nrfx NFCT directly as a Type 2 tag (READ and HLTA only; writes and
  NTAG commands get no answer). Not yet tried against a phone.
- Web Bluetooth can't connect to an address and needs a user tap for the
  chooser; that's why the link carries the advertised name and the page
  has a Connect button. Don't try to auto-connect.
