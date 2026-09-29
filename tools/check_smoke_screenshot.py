#!/usr/bin/env python3
"""Fail when the headless smoke test produced an empty framebuffer.

The Xvfb smoke test used to look at the exit status only. That status is 124
(the timeout fired because the game keeps running) even when the window never
receives a single frame, which is exactly how the "window has its decorations
but no content at all" regression reached users: the engine kept running, wrote
save data, and the CI stayed green.

This script inspects the screenshot taken while the game was running and exits
non-zero when almost nothing was drawn.
"""

import argparse
import sys

try:
    from PIL import Image
except ImportError:  # CI installs python3-pil; keep local runs working
    print("Pillow is not available; skipping the framebuffer check")
    raise SystemExit(0)

# A pixel counts as "drawn" when it is clearly brighter than black; the game
# opens on a dark title screen, so the threshold stays low on purpose.
PIXEL_THRESHOLD = 30


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("screenshot", help="PNG captured from the Xvfb display")
    parser.add_argument("--min-ratio", type=float, default=0.01,
                        help="minimum fraction of drawn pixels (default 0.01)")
    args = parser.parse_args()

    image = Image.open(args.screenshot).convert("RGB")
    width, height = image.size
    drawn = 0
    for pixel in image.getdata():
        if pixel[0] + pixel[1] + pixel[2] > PIXEL_THRESHOLD:
            drawn += 1
    ratio = drawn / float(width * height)
    print("framebuffer check: %dx%d, drawn-pixel ratio = %.4f (threshold %.4f)"
          % (width, height, ratio, args.min_ratio))
    if ratio < args.min_ratio:
        print("The game window looks empty: the engine is running but nothing "
              "was drawn to the screen.")
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
