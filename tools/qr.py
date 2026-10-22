"""QR code for one device's link, as an SVG to print and stick on it.

    python3 tools/qr.py TapUI-1A2B            -> qr/TapUI-1A2B.svg
    python3 tools/qr.py TapUI-1A2B --base URL

The name is what the device advertises; its USB console prints it at boot
("advertising as TapUI-1A2B"), and so does the Bluetooth list on a phone.
"""
import argparse
import pathlib
import urllib.parse

import qrcode
import qrcode.image.svg

BASE = "https://mmmaxwwwell.github.io/tapui/"

ap = argparse.ArgumentParser()
ap.add_argument("name")
ap.add_argument("--base", default=BASE)
ap.add_argument("--out", default="qr")
args = ap.parse_args()

url = f"{args.base}?n={urllib.parse.quote(args.name)}"
img = qrcode.make(url, image_factory=qrcode.image.svg.SvgPathImage, border=2)
out = pathlib.Path(args.out)
out.mkdir(exist_ok=True)
path = out / f"{args.name}.svg"
img.save(path)
print(f"{path}: {url}")
