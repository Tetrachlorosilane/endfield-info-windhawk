"""Renders a preview of the Endfield Info Windhawk panel.

Geometry follows the mod's defaults, so the image and the C++ layout agree:

    PanelWidth 420, RowHeight 30, CornerRadius 22, FontSize 15
    RingSize 26, RingThickness 4, RingRightOffset 12, BorderThickness 1
    Background #E6312F30 (alpha E6 = 90%), Border #FFC6CA4C, Text #FFF2F2F2
    text inset 12px; rings are packed from the right edge inward

The backdrop is a checkerboard so the panel's alpha is visible.
"""

import math
import os

from PIL import Image, ImageDraw, ImageFont

SCALE = 2

PANEL_W = 420
ROW_H = 30
RADIUS = 22
FONT_SIZE = 15
BORDER = 1
RING_SIZE = 26
RING_THICK = 4
RING_RIGHT_OFFSET = 12

# (expanded text with ring tokens removed, [(fraction, colour), ...], inline?)
# inline=True means the gauge was written after some text, so it flows with the
# line; inline=False parks the gauges together at the right edge.
ROWS = [
    ("CPU  12%   MEM  73%  ", [], False),
    ("GPU  9%  ", [(0.09, (0xC6, 0xCA, 0x4C))], True),
    ("DISK C:  68%   FAN", [(0.68, (0xC6, 0xCA, 0x4C)), (0.41, (0x6F, 0xB3, 0xE0))], False),
]

COL_BG = (0x31, 0x2F, 0x30, 0xE6)
COL_BORDER = (0xC6, 0xCA, 0x4C, 0xFF)
COL_TEXT = (0xF2, 0xF2, 0xF2, 0xFF)
COL_TRACK_ALPHA = 0x40

# Icon plate, from HudTheme.axaml (Hud.IconPlate / Hud.BoltDark).
COL_PLATE = (0xE9, 0xE7, 0xE4, 0xFF)
COL_GLYPH = (0x14, 0x13, 0x13, 0xFF)
ICON_RATIO = 1.55           # plate diameter / font size
ICON_LEFT = 10
ICON_GAP = 8
# Per-row glyph, mirroring the Icon1..Icon4 settings: 0 none, 1 cpu, 2 memory,
# 3 disk, 4 network, 5 gpu. A row left as 0 inherits the first configured glyph.
ICON_STYLES = [2, 1, 5, 0]

FONTS = r"C:\Windows\Fonts"
font = ImageFont.truetype(FONTS + r"\segoeui.ttf", FONT_SIZE * SCALE)


def resolved_style(row):
    """Mirrors ResolveIconStyle: a row with no glyph of its own inherits the first."""
    if 0 <= row < len(ICON_STYLES) and ICON_STYLES[row]:
        return ICON_STYLES[row]
    for style in ICON_STYLES:
        if style:
            return style
    return 0


panel_h = len(ROWS) * ROW_H
W, H = PANEL_W * SCALE, panel_h * SCALE
ICON_D = ICON_RATIO * FONT_SIZE if any(ICON_STYLES) else 0.0
TEXT_LEFT = ICON_LEFT + ICON_D + ICON_GAP if ICON_D else 12


def checker(size, cell=10):
    img = Image.new("RGB", size, (0x24, 0x24, 0x26))
    draw = ImageDraw.Draw(img)
    for y in range(0, size[1], cell):
        for x in range(0, size[0], cell):
            if (x // cell + y // cell) % 2 == 0:
                draw.rectangle([x, y, x + cell - 1, y + cell - 1], fill=(0x2E, 0x2E, 0x31))
    return img


def draw_icon(draw, style, cx, cy, size):
    """Mirrors PaintIconGlyph for the built-in styles, so the preview shows the
    same glyphs the mod will draw. Each glyph is centred on (cx, cy)."""
    s = size * SCALE
    h = s / 2
    if style == 1:            # cpu: chip, die window, pins on all four sides
        body, die, pin = s * 0.58, s * 0.24, s * 0.09
        draw.rectangle([cx - body / 2, cy - body / 2, cx + body / 2, cy + body / 2],
                       fill=COL_GLYPH)
        draw.rectangle([cx - die / 2, cy - die / 2, cx + die / 2, cy + die / 2],
                       fill=COL_PLATE)
        for i in (-1, 0, 1):
            along = i * s * 0.20
            draw.rectangle([cx + along - pin / 2, cy - h, cx + along + pin / 2, cy - body / 2],
                           fill=COL_GLYPH)
            draw.rectangle([cx + along - pin / 2, cy + body / 2, cx + along + pin / 2, cy + h],
                           fill=COL_GLYPH)
            draw.rectangle([cx - h, cy + along - pin / 2, cx - body / 2, cy + along + pin / 2],
                           fill=COL_GLYPH)
            draw.rectangle([cx + body / 2, cy + along - pin / 2, cx + h, cy + along + pin / 2],
                           fill=COL_GLYPH)
    elif style == 2:          # memory: a filled module with one notch
        body, thickness = s * 0.74, s * 0.40
        draw.rectangle([cx - body / 2, cy - thickness / 2, cx + body / 2, cy + thickness / 2],
                       fill=COL_GLYPH)
        notch = s * 0.10
        draw.rectangle([cx - notch / 2, cy - thickness / 2, cx + notch / 2, cy + thickness / 2],
                       fill=COL_PLATE)
    elif style == 3:          # disk: platter with a spindle
        radius = s * 0.44
        draw.ellipse([cx - radius, cy - radius, cx + radius, cy + radius], fill=COL_GLYPH)
        hub = s * 0.16
        draw.rectangle([cx - hub / 2, cy - hub / 2, cx + hub / 2, cy + hub / 2], fill=COL_PLATE)
    elif style == 4:          # network: three ascending bars
        bar = s * 0.16
        draw.rectangle([cx - s * 0.30, cy, cx - s * 0.30 + bar, cy + s * 0.30], fill=COL_GLYPH)
        draw.rectangle([cx - s * 0.07, cy - s * 0.16, cx - s * 0.07 + bar, cy + s * 0.30],
                       fill=COL_GLYPH)
        draw.rectangle([cx + s * 0.16, cy - s * 0.32, cx + s * 0.16 + bar, cy + s * 0.30],
                       fill=COL_GLYPH)
    elif style == 5:          # gpu: shroud with a fan
        body = s * 0.64
        draw.rectangle([cx - body / 2, cy - body / 2, cx + body / 2, cy + body / 2],
                       fill=COL_GLYPH)
        radius = s * 0.19
        draw.ellipse([cx - radius, cy - radius, cx + radius, cy + radius], fill=COL_PLATE)


def draw_ring(draw, cx, cy, radius, thickness, fraction, colour):
    """Mirrors DrawRing: full-circle track, then a clockwise arc from 12 o'clock."""
    track = (colour[0], colour[1], colour[2], COL_TRACK_ALPHA)
    box = [cx - radius, cy - radius, cx + radius, cy + radius]
    draw.ellipse(box, outline=track, width=int(thickness))
    if fraction <= 0:
        return
    # PIL angles run clockwise from 3 o'clock; the mod starts at 12 o'clock.
    draw.arc(box, start=-90, end=-90 + 360 * fraction, fill=colour, width=int(thickness))


canvas = checker((W, H)).convert("RGBA")
layer = Image.new("RGBA", (W, H), (0, 0, 0, 0))
draw = ImageDraw.Draw(layer)

draw.rounded_rectangle(
    [0, 0, W - 1, H - 1],
    radius=RADIUS * SCALE,
    fill=COL_BG,
    outline=COL_BORDER,
    width=BORDER * SCALE,
)

for index, (text, rings, inline) in enumerate(ROWS):
    top = index * ROW_H
    centre_y = (top + ROW_H / 2) * SCALE

    # The icon plate is drawn once per row, as the mod does, so it repeats down
    # the left edge like the original's per-row indicator. Each row uses its own
    # glyph, resolving an unset row to the first configured one.
    style = resolved_style(index)
    if style:
        icon_cy = (top + ROW_H / 2) * SCALE
        plate_r = ICON_D / 2 * SCALE
        draw.ellipse([ICON_LEFT * SCALE, icon_cy - plate_r,
                      (ICON_LEFT + ICON_D) * SCALE, icon_cy + plate_r], fill=COL_PLATE)
        draw_icon(draw, style, (ICON_LEFT + ICON_D / 2) * SCALE, icon_cy, ICON_D)

    if inline:
        # Text first, gauge immediately after it (the mod measures the text to
        # find the exact x, so the estimate here is close enough for a preview).
        draw.text((TEXT_LEFT * SCALE, centre_y), text, font=font, fill=COL_TEXT, anchor="lm")
        text_width = draw.textlength(text, font=font)
        centre_x = TEXT_LEFT * SCALE + text_width + (RING_SIZE / 2) * SCALE
        for fraction, colour in rings:
            outer = RING_SIZE * SCALE
            thickness = RING_THICK * SCALE
            draw_ring(draw, centre_x, centre_y, (outer - thickness) / 2, thickness, fraction, colour)
            centre_x += ((RING_SIZE / 2) + 4) * SCALE
        continue

    # Right-parked layout. The text is drawn whether or not this row has rings.
    draw.text((TEXT_LEFT * SCALE, centre_y), text, font=font, fill=COL_TEXT, anchor="lm")

    centre_x = (PANEL_W - RING_RIGHT_OFFSET) * SCALE
    for fraction, colour in reversed(rings):
        centre_x -= (RING_SIZE / 2) * SCALE
        outer = RING_SIZE * SCALE
        thickness = RING_THICK * SCALE
        draw_ring(draw, centre_x, centre_y, (outer - thickness) / 2, thickness, fraction, colour)
        centre_x -= ((RING_SIZE / 2) + 4) * SCALE

canvas.alpha_composite(layer)
out_path = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                        "assets", "panel-preview.png")
canvas.convert("RGB").save(out_path)
print(f"wrote {out_path}  {W}x{H}  ({len(ROWS)} rows, "
      f"{sum(len(r[1]) for r in ROWS)} rings)")
