# TapUI protocol

A device describes its own controls; the page at `web/` draws whatever it is
told. Nothing about a particular device lives in the page.

## Launch link

The device's QR code and NFC tag both carry:

```
https://mmmaxwwwell.github.io/tapui/?n=<advertised name>
```

`n` is the name the device advertises (`TapUI-1A2B`, the last two bytes of
its Bluetooth address). Web Bluetooth can't connect to an address, only
offer a chooser, so the page filters the chooser by this name and it lists
just that device. Without `n` it lists every device advertising the TapUI
service.

The page still needs one tap on **Connect**: browsers only open the
Bluetooth chooser from a user gesture.

## GATT

| | UUID | Properties |
|---|---|---|
| Service | `c7170001-8a2d-43c1-a783-922a8834a1fe` | |
| RX (page → device) | `c7170002-8a2d-43c1-a783-922a8834a1fe` | write, write without response |
| TX (device → page) | `c7170003-8a2d-43c1-a783-922a8834a1fe` | notify |

The advertising packet carries the 128-bit service UUID. The name is in
the scan response, since both don't fit in 31 bytes.

Both directions are a stream of UTF-8 lines ending in `\n`. A line may span
several writes or notifications; a receiver buffers until the newline. Lines
are at most 127 bytes page → device. Device → page lines have no limit.

## Page → device

Plain text, space separated:

```
hello                 send me the UI and every current value
set <id> <value>      a toggle (0/1), slider (number), select (option text)
press <id>            a button
```

The rest of the line after `set <id> ` is the value, spaces included.

## Device → page

One JSON object per line, `t` naming the kind:

```json
{"t":"ui","ui":{"title":"…","controls":[…]}}
{"t":"state","id":"bright","v":40}
{"t":"err","msg":"unknown control: foo"}
```

The device answers `hello` with `ui`, then a `state` per control that has a
value. It sends `state` whenever a value changes, including in answer to a
`set` (the page shows the device's value, not what it asked for).

## Controls

Every control has `type`, `id` (no spaces) and `label`.

| type | extra fields | value |
|---|---|---|
| `button` | | none, sends `press` |
| `toggle` | | `true` / `false` (sent as `1` / `0`) |
| `slider` | `min`, `max`, `step` (default 1), `unit` | number |
| `select` | `options`: array of strings | one of `options` |
| `readout` | `unit` | anything; display only |

A page meeting a type it doesn't know shows the label and "unsupported
control" instead of failing, so devices can add types ahead of the page.
