"""Check packaged assets without a browser or dependencies."""
from pathlib import Path
import re
import xml.etree.ElementTree as ET

root = Path(__file__).resolve().parents[1] / "payload/www"
svg = ET.parse(root / "icons.svg").getroot()
symbols = {node.attrib["id"] for node in svg if node.tag.endswith("symbol")}
assert len(symbols) >= 25
html = (root / "index.html").read_text()
app = (root / "app.js").read_text()
references = set(re.findall(r'icons.svg#(icon-[\w]+)', html))
references.update("icon-" + name for name in re.findall(r"icon\('([\w]+)'\)", app))
controls = app[app.index("const controls ="):app.index("function activeDevice")]
references.update("icon-" + name for name in re.findall(r"\['[^']+','[^']+','([^']+)',\[", controls))
assert references <= symbols, references - symbols
assert '.shell:has(#navigation[hidden]){grid-template-columns:minmax(0,1fr)' in (root / "app.css").read_text()
print(f"Valid SVG XML; {len(references)} referenced icons present; unpaired layout has its own full-width column")
