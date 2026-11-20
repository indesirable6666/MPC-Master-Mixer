#!/usr/bin/env python3
"""Offline structural and JavaScript syntax checks for the browser-only mixer prototype."""
from html.parser import HTMLParser
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

HTML = Path("prototype/index.html")
if not HTML.is_file():
    raise SystemExit("ERROR: prototype/index.html not found")
source = HTML.read_text(encoding="utf-8")

class PrototypeParser(HTMLParser):
    def __init__(self):
        super().__init__()
        self.ids = []
        self.scripts = []
        self.in_script = False
        self.script_parts = []
        self.labels = []
    def handle_starttag(self, tag, attrs):
        attrs = dict(attrs)
        if "id" in attrs:
            self.ids.append(attrs["id"])
        if tag == "script":
            self.in_script = True
            self.script_parts = []
        if tag == "meta" and attrs.get("name") == "viewport":
            if "width=device-width" not in attrs.get("content", ""):
                raise AssertionError("viewport must be sized for the device width")
    def handle_data(self, data):
        if self.in_script:
            self.script_parts.append(data)
    def handle_endtag(self, tag):
        if tag == "script" and self.in_script:
            self.scripts.append("".join(self.script_parts))
            self.in_script = False

parser = PrototypeParser()
parser.feed(source)
parser.close()

duplicates = sorted({item for item in parser.ids if parser.ids.count(item) > 1})
assert not duplicates, f"duplicate HTML ids: {duplicates}"
required_ids = {"strips", "master", "scrollArea", "prev", "next", "reset"}
missing = sorted(required_ids - set(parser.ids))
assert not missing, f"missing required interface elements: {missing}"
assert "MPC MASTER MIXER" in source
assert "MODE DÉMO" in source and "SIMULATION LOCALE" in source
assert "const labels=[" in source, "channel label data is missing"
assert "AUX 4" in source and "data-eq" in source and "data-volume" in source
assert "function makeStrip(index,isMaster=false)" in source
assert "for(let i=0;i<16;i++)" in source, "prototype must generate 16 channel strips"
assert len(parser.scripts) == 1, f"expected one inline script, found {len(parser.scripts)}"

node = shutil.which("node")
if not node:
    print("SKIP: Node.js not installed; HTML structure checks passed.")
else:
    with tempfile.NamedTemporaryFile("w", suffix=".js", encoding="utf-8", delete=False) as handle:
        handle.write(parser.scripts[0])
        js_path = handle.name
    result = subprocess.run([node, "--check", js_path], text=True, capture_output=True)
    Path(js_path).unlink(missing_ok=True)
    if result.returncode:
        print(result.stdout, end="")
        print(result.stderr, end="", file=sys.stderr)
        raise SystemExit(result.returncode)
    print("PASS: inline JavaScript syntax")

print("PASS: prototype structure, required controls, demo labeling and unique IDs")
print("NOTE: these checks do not validate live MPC audio, MIDI, graphics or touch integration.")
