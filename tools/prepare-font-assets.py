#!/usr/bin/env python3
"""Expose the system Silica font stack to the embedded asset font manager."""
import json
from pathlib import Path
import sys

assets = Path(sys.argv[1])
manifest = assets / 'FontManifest.json'
fonts = json.loads(manifest.read_text())
# Material's themed text uses Roboto. Explicit AppBar title styles have no
# family; the engine uses its Linux/default generic fallback for those.
# Cover both platform implementations and the generic paragraph family.
families = ('Roboto', 'Ubuntu', 'Arial', 'sans-serif',
            'Sail Sans Pro Light', 'SailfishCJK', 'SailfishArabic')
fonts = [entry for entry in fonts if entry['family'] not in families]
for family in families:
    asset = ('SailfishCJK.ttc' if family == 'SailfishCJK' else
             'SailfishArabic.ttf' if family == 'SailfishArabic' else
             'SailSansPro-Light.ttf')
    fonts.append({'family': family, 'fonts': [{'asset': 'fonts/' + asset}]})
manifest.write_text(json.dumps(fonts, ensure_ascii=False))
for name, target in {
    'SailfishCJK.ttc': '/usr/share/fonts/wqy-zenhei/wqy-zenhei.ttc',
    'SailSansPro-Light.ttf': '/usr/share/fonts/sail-sans-pro/SailSansPro-Light.ttf',
    'SailfishArabic.ttf': '/usr/share/fonts/amiri/amiri-regular.ttf',
}.items():
    link = assets / 'fonts' / name
    link.parent.mkdir(exist_ok=True)
    if link.is_symlink():
        link.unlink()
    link.symlink_to(target)
