from pathlib import Path
import re
import shutil
import sys

upstream = Path(sys.argv[1]).resolve()
src = upstream / "bbmod" / "src"
dinput = src / "dinput8.cpp"

if not dinput.exists():
    raise SystemExit(f"Could not find {dinput}")

header_src = Path(__file__).with_name("shortcut_manager.hpp")
header_dst = src / "shortcut_manager.hpp"
shutil.copy2(header_src, header_dst)

text = dinput.read_text(encoding="utf-8", errors="ignore")

include_line = '#include "shortcut_manager.hpp"\n'
if include_line not in text:
    text = include_line + text

needle_re = re.compile(
    r'(dinput8AModule\s*=\s*\(IDirectInput8A\s*\*\)\s*new\s+ImguiDInput\s*\(\s*dinput8AModule\s*\)\s*;)'
)

if "SM_WrapDirectInput(dinput8AModule)" not in text:
    text, n = needle_re.subn(
        r'\1\n        dinput8AModule = SM_WrapDirectInput(dinput8AModule);',
        text,
        count=1,
    )

    if n != 1:
        raise SystemExit(
            "Patch failed: could not find the ImguiDInput wrapping line "
            "in bbmod/src/dinput8.cpp"
        )

dinput.write_text(text, encoding="utf-8")

print("Patched:", dinput)
print("Added:", header_dst)
