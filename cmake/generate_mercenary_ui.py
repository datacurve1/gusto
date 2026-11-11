#!/usr/bin/env python3

from pathlib import Path
import re
import sys

if len(sys.argv) != 3:
    raise SystemExit(
        "Usage: generate_mercenary_ui.py SOURCE_XML OUTPUT_XML"
    )

source = Path(sys.argv[1])
output = Path(sys.argv[2])

content = source.read_text(encoding="utf-8")

pattern = (
    r'(<Label item="MMGW_TotalSlotsText">)'
    r'(.*?)'
    r'(</Label>)'
)

matches = list(re.finditer(pattern, content, re.DOTALL))

if len(matches) != 1:
    raise SystemExit("Expected exactly one MMGW_TotalSlotsText label")

match = matches[0]
section = match.group(2)

old = "<RightAnchorOffset>135</RightAnchorOffset>"
new = "<RightAnchorOffset>240</RightAnchorOffset>"

if section.count(old) == 1:
    section = section.replace(old, new, 1)
elif section.count(new) == 1:
    print("Mercenary counter already expanded to 160 pixels")
else:
    raise SystemExit("Unexpected mercenary counter layout")

content = (
    content[:match.start(2)]
    + section
    + content[match.end(2):]
)

output.parent.mkdir(parents=True, exist_ok=True)
output.write_text(content, encoding="utf-8")

print(f"Generated: {output}")
