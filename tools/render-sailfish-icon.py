#!/usr/bin/env python3
"""Export the vector launcher icon; requires CairoSVG (build-time only)."""
from pathlib import Path
import cairosvg

root = Path(__file__).resolve().parents[1] / "assets/icon/sailfish"
for size in (86, 108, 128, 172):
    target = root / str(size) / "harbour-mconnect.png"
    target.parent.mkdir(exist_ok=True)
    cairosvg.svg2png(url=str(root / "harbour-mconnect.svg"),
                    write_to=str(target), output_width=size, output_height=size)
