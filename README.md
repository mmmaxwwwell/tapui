# TapUI

Tap a device's NFC tag or scan its QR code, and your phone opens a page
with that device's controls, connected to it over Bluetooth. The device
describes its own controls, so the page is the same for every device and a
new device or feature never needs a phone app update.

```
 NFC tag / QR code ──► https://mmmaxwwwell.github.io/tapui/?n=TapUI-1A2B
                                     │  (page cached on the phone after one visit)
                                     ▼
 phone page ◄──── Bluetooth LE ────► device: "here are my controls", values, commands
```

- `web/` is the page, a static site published to GitHub Pages. No build step.
- `fw/` is the firmware for a Seeed XIAO nRF52840: Zephyr, with a demo
  that exposes the board's RGB LED and chip temperature.
- `docs/protocol.md` is the wire format between them.

## Using it

1. Open the device's link: tap the phone on its NFC coil, or scan its QR code.
2. Tap **Connect**. The Bluetooth chooser lists just that device; pick it.
3. The device's controls appear. They show the device's real values and
   update live.

Android: Chrome. iPhone: Safari has no Web Bluetooth, so use the
[Bluefy](https://apps.apple.com/app/bluefy-web-ble-browser/id1492822055)
browser (scan the QR code from inside Bluefy).

### Off grid

After the phone has opened the page once with a connection, it keeps a copy
(a service worker). From then on a tap or scan opens the page with no
signal at all, for any device, because only `?n=` differs between devices.
The device link itself is Bluetooth and never needed internet.

Two limits:

- The **first** open on a phone needs internet. Nothing on the web side can
  avoid that: a browser has to fetch a page once, and it only allows
  Bluetooth on pages served over HTTPS.
- Chrome keeps the copy for an installed page indefinitely. For a page that
  was only visited, it may clear the copy under storage pressure after weeks
  unused. **Add to Home screen** (Chrome's menu) installs it and also gives
  it an icon.

## Firmware

Needs Nix. From the repo root:

```sh
nix develop .#fw
fw/scripts/west-init.sh     # once: fetches Zephyr into fw/.west-workspace (~1 GB)
fw/scripts/build.sh         # -> fw/build/xiao_ble/zephyr/zephyr.uf2
fw/scripts/test.sh          # unit tests on native_sim
```

Flash: double-tap the XIAO's reset button. It mounts as a USB drive. Copy
`zephyr.uf2` onto it.

At boot it prints its name and link on the USB serial console:

```
advertising as TapUI-1A2B
link: https://mmmaxwwwell.github.io/tapui/?n=TapUI-1A2B
```

### NFC

The nRF52840 can be an NFC tag itself. It needs a 13.56 MHz flexible coil
("NFC antenna for nRF52", 2–5 cm) soldered across the **NFC1/NFC2** pads on
the back of the XIAO (P0.09/P0.10). Polarity doesn't matter. The tag is
read-only and holds the device's link. It works whether or not anything is
connected over Bluetooth.

If boot logs `UICR NFCPINS has the NFC pads as GPIO`, an earlier firmware
(Arduino builds often do this) switched those pads to GPIO. That setting
lives in UICR, which a UF2 flash can't change; it takes an SWD probe and
`nrfjprog --eraseuicr` (or `pyocd erase --chip`) once.

### QR code

```sh
nix develop
python3 tools/qr.py TapUI-1A2B      # -> qr/TapUI-1A2B.svg, 37 mm square
```

## Making your own device

Replace `fw/src/main.c`; keep `fw/src/tapui/`. A device:

- writes its controls as JSON (see the `UI_JSON` in the demo, and the
  control types in `docs/protocol.md`),
- answers `on_hello` with that JSON and then each current value,
- handles `set`/`press` in `on_cmd`, echoing the new value with
  `tapui_send_*`,
- sends values whenever they change on their own.

The page ignores nothing silently: a control type it doesn't know shows as
"unsupported control".

## Developing the page

```sh
nix develop
npm ci
npm run serve        # http://localhost:8173/?n=TapUI-1A2B (desktop Chrome talks Bluetooth on localhost)
npm test             # protocol unit tests
npm run test:e2e     # the page against a simulated device in Chromium
```

Pushing to `main` runs both test suites and publishes `web/` to GitHub
Pages.
