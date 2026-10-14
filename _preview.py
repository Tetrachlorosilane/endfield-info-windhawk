"""Renders a preview of the Endfield Info Windhawk panel.

Geometry follows the mod's defaults, so the image and the C++ layout agree:

    PanelWidth 420, RowHeight 30, CornerRadius 22, FontSize 15
    RingSize 26, RingThickness 4, RingRightOffset 12, BorderThickness 1
    Background #E6312F30 (alpha E6 = 90%), Border #FFC6CA4C, Text #FFF2F2F2
    row text inset 12px; an [[icon:...]] element is its own box in the flow
    (diameter = font size * 1.55, then an 8px content gap)

The backdrop is a checkerboard so the panel's alpha is visible.

**Rows 1-3 are the shipped default rows** (Row1/Row2/Row3 of the settings block),
with their tokens expanded for one sample (see SAMPLE). Row 4 is an *extra* syntax
sample, outside the defaults: it shows a `[[icon:file=...]]` image icon (drawn as
the fallback glyph, because the preview loads no files) and a right-parked gauge.
`_preview.md` says which is which, and so does the README's caption.

Rows carry real markup -- `[[icon:...]]` tags, the same syntax the mod reads -- and
`parse_row()` walks it the way `CompileRowText` + `BuildRowPlan` do: every element
occupies a box, and the ones after it start where it ended. What the preview does
*not* model is written down in _preview.md.
"""

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

# What the default rows' {tokens} are expanded to in this picture. One sample is
# unavoidable -- the preview draws pixels, not templates -- so it is written down
# here instead of being invented silently.
SAMPLE = "cpu 12%, mem 73%, gpu 9%, gpu.temp 42C, net.down 1.23 MB/s, net.up 128 KB/s"

# (text with its [[icon:...]] tags and its tokens expanded, [(fraction, colour), ...], inline?)
# inline=True means the gauge was written after some text, so it flows with the line;
# inline=False parks the gauges together at the right edge.
#
# Rows 1-3 mirror the settings block's Row1 / Row2 / Row3 defaults one for one:
#   [[icon:CPU]]{cpu|0|w3}%  CPU   {mem|0|w3}%  RAM   [[ring:{mem}]]
#   [[icon:Memory]]GPU {gpu|0|w3}%  {gpu.temp|0|w3}C   NET {net.down}
#   [[icon:Network]]NET  {net.down}   UP  {net.up}
# `|w3` right-aligns the number in three characters, which is why the expanded
# numbers below carry a leading space.
ROWS = [
    ("[[icon:CPU]] 12%  CPU    73%  RAM   ", [(0.73, (0xC6, 0xCA, 0x4C))], True),
    ("[[icon:Memory]]GPU   9%   42C   NET 1.23 MB/s", [], False),
    ("[[icon:Network]]NET  1.23 MB/s   UP  128 KB/s", [], False),
    # Extra syntax sample (NOT one of the shipped defaults): an image icon, whose file the
    # preview does not load, plus a right-parked gauge.
    ("[[icon:file=C:\\pics\\ssd.png]]DISK C:  68%   FAN",
     [(0.68, (0xC6, 0xCA, 0x4C)), (0.41, (0x6F, 0xB3, 0xE0))], False),
]
DEFAULT_ROWS = 3   # ROWS[:DEFAULT_ROWS] are the shipped defaults; the rest are samples

COL_BG = (0x31, 0x2F, 0x30, 0xE6)
COL_BORDER = (0xC6, 0xCA, 0x4C, 0xFF)
COL_TEXT = (0xF2, 0xF2, 0xF2, 0xFF)
COL_TRACK_ALPHA = 0x40

# Icon disc, from HudTheme.axaml (Hud.IconPlate / Hud.BoltDark).
COL_PLATE = (0xE9, 0xE7, 0xE4, 0xFF)
COL_GLYPH = (0x14, 0x13, 0x13, 0xFF)
# Mirrors kIconPlateRatio / kIconContentGap / kRowTextLeftInset.
ICON_RATIO = 1.55
ICON_GAP = 8
TEXT_LEFT = 12
# Mirrors kIconStyleNames: the names [[icon:NAME]] accepts, by glyph number.
GLYPHS = {"cpu": 1, "memory": 2, "ram": 2, "disk": 3, "network": 4, "net": 4, "gpu": 5}
# Mirrors kIconImageFallbackGlyph: an image icon whose file cannot be decoded still
# draws a glyph, so the preview shows Memory for one.
IMAGE_FALLBACK_GLYPH = 2

FONTS = r"C:\Windows\Fonts"
font = ImageFont.truetype(FONTS + r"\segoeui.ttf", FONT_SIZE * SCALE)

panel_h = len(ROWS) * ROW_H
W, H = PANEL_W * SCALE, panel_h * SCALE


def parse_icon(body):
    """Mirrors ParseIconTag: (glyph, path, ripple) for a valid tag, None for a bad one.

    A bad tag is ordinary text in the mod, so it has to be ordinary text here too."""
    ripple = None
    for suffix, value in ((":ripple", True), (":noripple", False)):
        if body.lower().endswith(suffix):
            body = body[: -len(suffix)]
            ripple = value
            break
    if body.lower().startswith("file="):
        # CopyUserPath: quotes and surrounding whitespace are the user's, not the loader's.
        path = body[5:].strip()
        if path[:1] in ('"', "'"):
            path = path[1:].split(path[0], 1)[0]
        path = path.strip()
        return (IMAGE_FALLBACK_GLYPH, path, ripple) if path else None
    glyph = GLYPHS.get(body.lower())
    return (glyph, "", ripple) if glyph else None


def parse_row(text):
    """Mirrors the element walk: [("text", s) | ("icon", (glyph, path, ripple)), ...]."""
    elements = []
    rest = text
    while rest:
        at = rest.find("[[icon:")
        if at < 0:
            elements.append(("text", rest))
            break
        close = rest.find("]]", at)
        if close < 0:
            elements.append(("text", rest))
            break
        icon = parse_icon(rest[at + 7:close]) if close > at + 7 else None
        if icon is None:
            # Not markup: one character of ordinary text, so a tag written inside it can
            # still be recognised -- the same rule the C++ scan follows.
            elements.append(("text", rest[: at + 1]))
            rest = rest[at + 1:]
            continue
        if at > 0:
            elements.append(("text", rest[:at]))
        elements.append(("icon", icon))
        rest = rest[close + 2:]
    return elements


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


def draw_icon_element(draw, left, centre_y, diameter, glyph):
    """One [[icon:...]] element: its own box, disc centred in the row.

    Mirrors PaintIconElement + IconElementCenter. `left` is the plan's x."""
    radius = diameter / 2 * SCALE
    cx = left + radius
    draw.ellipse([cx - radius, centre_y - radius, cx + radius, centre_y + radius], fill=COL_PLATE)
    if glyph:
        draw_icon(draw, glyph, cx, centre_y, diameter)


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

    # The row is a flow: text and icon elements are drawn in the order they were written,
    # and each one moves the cursor on by its own width.
    cursor = TEXT_LEFT * SCALE
    for kind, value in parse_row(text):
        if kind == "icon":
            glyph, _path, _ripple = value
            diameter = FONT_SIZE * ICON_RATIO
            draw_icon_element(draw, cursor, centre_y, diameter, glyph)
            cursor += (diameter + ICON_GAP) * SCALE
        else:
            draw.text((cursor, centre_y), value, font=font, fill=COL_TEXT, anchor="lm")
            cursor += draw.textlength(value, font=font)

    if inline:
        # The gauge is written after the text, so it starts where the flow stopped (the mod
        # measures the text for real; this is the same relation with PIL's own measurement).
        centre_x = cursor + (RING_SIZE / 2) * SCALE
        for fraction, colour in rings:
            outer = RING_SIZE * SCALE
            thickness = RING_THICK * SCALE
            draw_ring(draw, centre_x, centre_y, (outer - thickness) / 2, thickness, fraction, colour)
            centre_x += ((RING_SIZE / 2) + 4) * SCALE
        continue

    # Right-parked layout, for the rows whose gauges are written first.
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
      f"{sum(len(r[1]) for r in ROWS)} rings, "
      f"{sum(1 for r in ROWS for k, _ in parse_row(r[0]) if k == 'icon')} icons)")
print(f"  rows 1-{DEFAULT_ROWS} = the shipped default rows (tokens expanded: {SAMPLE})")
print(f"  rows {DEFAULT_ROWS + 1}-{len(ROWS)} = extra syntax sample(s), not in the defaults")
