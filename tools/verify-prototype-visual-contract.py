#!/usr/bin/env python3
"""Regression guard for the user-approved MPC Master Mixer prototype appearance.

This test deliberately checks the existing visual contract; it does not render,
rewrite, format, or otherwise modify prototype/index.html.
"""
from pathlib import Path
import re
import sys

HTML = Path("prototype/index.html").read_text(encoding="utf-8")
STYLE = re.search(r"<style>(.*?)</style>", HTML, re.DOTALL)
if not STYLE:
    raise SystemExit("FAIL: prototype stylesheet is missing")
CSS = STYLE.group(1)

checks = {
    "dark charcoal base palette": r"--bg:#101319;--panel:#1a2029;--panel-hi:#222a35;--well:#111720",
    "amber, green and blue accents": r"--amber:#e9ad45;--green:#55c68a;--red:#e36e70;--blue:#70a9dc",
    "classic narrow channel-strip width": r"\.channel\{width:180px;min-width:180px",
    "channel strip dark gradient and border": r"background:linear-gradient\(180deg,#202731,#191f28\);border:1px solid var\(--line\)",
    "six-band EQ grid": r"\.eq\{display:grid;grid-template-columns:repeat\(6,minmax\(0,1fr\)\)",
    "four independent Aux controls": r"\.aux\{display:grid;grid-template-columns:repeat\(4,1fr\)",
    "green vertical volume fader": r"\.fader\{appearance:none;-webkit-appearance:none;flex:none;height:100%",
    "fixed Master dock at right": r"\.master-dock\{width:194px;min-width:194px",
    "Master stays outside scrolling channel viewport": r'<div class="scroll-area"[^>]*>.*?</div>\\s*</div>\\s*<aside class="master-dock"',
    "Peak/RMS/Clip meter presentation": r'data-meter="peak".*?data-meter="rms".*?data-clip',
    "approved mixer view tabs": r'>MIXER</button>.*?>EQ</button>.*?>AUX</button>.*?>VU MÈTRE</button>',
}
failures = []
for label, pattern in checks.items():
    haystack = HTML if label in {
        "fixed Master dock at right",
        "Master stays outside scrolling channel viewport",
        "Peak/RMS/Clip meter presentation",
        "approved mixer view tabs",
    } else CSS
    if not re.search(pattern, haystack, re.DOTALL):
        failures.append(label)

labels = re.search(r"const labels=\[(.*?)\];", HTML, re.DOTALL)
if not labels:
    failures.append("representative track list")
else:
    count = len(re.findall(r"'[^']*'", labels.group(1)))
    if count != 16:
        failures.append(f"16 representative tracks (found {count})")

if failures:
    print("Prototype visual contract: FAIL")
    for item in failures:
        print(f" - {item}")
    sys.exit(1)

print(f"Prototype visual contract: PASS ({len(checks)} appearance/layout checks; 16 tracks)")
