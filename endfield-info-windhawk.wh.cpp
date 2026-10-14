// ==WindhawkMod==
// @id              endfield-info-windhawk
// @name            Endfield Info Windhawk
// @description     A configurable always-on-top status capsule: user-defined rows with {variable} tokens, on-demand collectors (CPU / memory / network / GPU), ring gauges, per-row icons, an optional notification island fed by other programs over a named pipe, and proximity-or-always display with click-to-toggle.
// @version         0.3.0
// @author          Silicon Dioxide
// @github          https://github.com/Tetrachlorosilane/endfield-info-windhawk
// @include         windhawk.exe
// @compilerOptions -lgdi32 -lpdh -liphlpapi -lpsapi -ladvapi32
// ==/WindhawkMod==

// ==WindhawkModReadme==
/*
# Endfield Info Windhawk

A lightweight always-on-top status capsule, built as a Windhawk **tool mod**: it
runs in its own dedicated `windhawk.exe` process and never injects into, or hooks
anything in, another process. Nothing is collected until a row asks for it.

## Rows and variables

Each row is free text with `{variable}` tokens. Write whatever you want; the
collectors for the groups you reference are the only ones that start.

| Token | Meaning |
| :-- | :-- |
| `{cpu}` | CPU total usage % |
| `{cpu.user}` `{cpu.kernel}` | CPU split |
| `{cpu.ghz}` `{cpu.ghz.max}` | Effective / nominal clock (GHz) |
| `{cpu.name}` `{cpu.cores}` `{cpu.threads}` | CPU identity |
| `{mem}` | Memory usage % |
| `{mem.used}` `{mem.total}` `{mem.free}` | Memory in GB |
| `{mem.commit}` | Commit usage % |
| `{net.down}` `{net.up}` | Current rates, auto-scaled |
| `{net.down.bps}` `{net.up.bps}` | Raw bytes/s |
| `{net.total}` | Total transferred, auto-scaled |
| `{gpu}` `{gpu.mem}` | GPU core / memory utilisation % |
| `{gpu.temp}` | GPU temperature (C) |
| `{gpu.vram.used}` `{gpu.vram.total}` | VRAM in GB |
| `{gpu.name}` `{gpu.fan}` `{gpu.power}` | GPU identity / fan % / watts |
| `{time}` `{date}` `{uptime}` | Clock and uptime |
| `{mod.ws}` `{mod.cpu}` | This mod's own footprint |
| `{ext.provider.key}` | A value pushed by another program (see below) |

A `{variable|spec}` suffix formats the value: `0` `1` `2` decimals, `gb`, `mb`,
`pct`, `int`, `time`. `[[ring:VALUE:MAX:THICK:COLOR]]` draws a gauge; one placed
after text sits inline on that row. A variable that is known but unreadable on
this machine (a GPU with no temperature sensor, say) renders as `--` rather than
as a plausible-looking zero.

## Fonts

`Font family` sets the main face. `Fallback fonts` is a semicolon-separated list
of candidates for the characters that face cannot draw, so a Chinese song title
or an emoji next to a Segoe UI panel no longer turns into boxes. Leaving it empty
is free: the panel then never asks the system which glyphs a font has.

A single run can also be given its own family with `[[font:Name]]...[[/font]]`
(names may be a list: `[[font:Consolas;Arial]]`), and a row may switch as often as
you like. A run is drawn in one family, so a run that mixes scripts uses the first
candidate that covers all of it. A tag that is mis-spelt, names a family that is
not installed, or is one of more than six distinct chains is drawn as ordinary
text rather than silently changing the typeface. The same applies to a
`[[ring:...]]` that cannot be built: you see what you wrote.

## Row sizes

Rows do not have to match. `Row N font size` and `Row N height` give one row its
own metrics (`0` inherits `Font size` / `Row height`), and a tag at the *start* of
the row's text overrides the setting: `[[size:18]]` and `[[h:44]]`, in any order
and repeatable (the last one wins). The panel is as tall as its rows add up to,
and a row is never shorter than its own font needs - growing a font grows the row
rather than letting the glyphs spill into the next one.

`[[size:NN]]` also decorates a *run*, the same way a font tag does: written
anywhere but the very start it applies to the text it wraps, closed by
`[[/size]]` (or running to the end of the line). So
`CPU [[size:20]]{cpu.ghz}GHz[[/size]]` draws only those characters larger. Runs do
not nest: `[[/size]]` returns to the row's own size. The row's height floor follows
the largest text in it, and the icon plate follows the global `Font size` only, so
a bigger row size changes the text and nothing else. A tag written anywhere but the
start is ordinary text and is drawn as such.

## Rings, icons and themes

* Ring gauges are drawn where you write them: right-docked by default, or inline
  after text.
* Every row can carry a disc icon: a built-in glyph (`CPU`, `Memory`, `Disk`,
  `Network`, `GPU`), your own image file, or nothing. One image can be set for
  all rows at once.
* Three colour presets plus a `Custom` theme that unlocks the colour fields.
* An intro animation (slide / fade / none) with a ripple, and a speed you set per
  the machine.

## Display

* Anchored to the **top-centre of the topmost monitor** (the one highest in the
  virtual desktop, not necessarily the primary one).
* Always topmost, and it never takes focus.
* **Mouse proximity** - hidden until the pointer reaches the top edge, then it
  slides in and hides again shortly after the pointer leaves.
* **Always shown** - stays up; clicking the capsule toggles it, and the collapsed
  state is visible at a glance.
* The panel is never hidden completely: collapsed it is an 8 px tab that can
  always be clicked back.

## The island (optional notifications)

Set `Island` to on and a card can appear under the capsule for an event pushed by
another program: a title, a body line, a severity (`info` / `warn` / `critical`,
which picks the colour) and a lifetime. It fades out on its own, and the card's
close button dismisses it early. Nothing shows a card unless a program pushes one.

## External data (`{ext.provider.key}`)

Turn on `External data` and the mod serves a **named pipe**; another program can
`HELLO`, then `SET provider key ttl text`, and rows that reference
`{ext.provider.key}` show that text (values expire on their own, and the row falls
back to `--`). The optional `External publisher command` setting lets the mod
start your publisher for you *while a row actually references `{ext.*}`*, and it
is stopped again when nothing consumes it; the mod never starts or restarts your
other software by itself.

**This is a trust boundary, not a security channel.** The pipe is local-only and
its DACL admits the user running the mod (plus SYSTEM and Administrators), so any
program running as you can push text. The mod prints that text and nothing else:
it executes nothing and accepts no image bytes - icons are built-in glyphs or
files you chose yourself. Treat the values as untrusted input if you display them
next to anything you trust.

## Collectors start on demand

Nothing is collected until a row actually references it. If no row mentions
`{gpu...}`, neither NVML nor the PDH GPU counters are ever loaded - which matters,
because those two are by far the most expensive things this mod can do:

| Stage | Working set |
| :-- | :-- |
| Baseline, CPU + memory + network only | **~7 MB** |
| `+ nvml.dll` for `{gpu}` / `{gpu.temp}` | ~28 MB |
| `+` PDH GPU counters (non-NVIDIA fallback) | ~35 MB |

Measured on the development machine. `PdhCloseQuery` does not give the memory
back, so a collector that has started stays started until the mod is reloaded.
Reading a real GPU sensor is not free (~1.9 ms per sample with NVML); that is a
deliberate trade for accuracy, and it only happens when you asked for it.
*/
// ==/WindhawkModReadme==

// ==WindhawkModSettings==
/*
- Row1: "{cpu|0|w3}%  CPU   {mem|0|w3}%  RAM   [[ring:{mem}]]"
  $name: 'Row 1'
  $description: 'Text with {variable} tokens. Format specs chain with a bar: {cpu|2} decimals, |w4 pad to 4, |pad0 pad with zeros. [[ring:VALUE:MAX:THICK:COLOR]] draws a gauge, and one placed after text sits inline.'
- Row2: "GPU {gpu|0|w3}%  {gpu.temp|0|w3}C   NET {net.down}"
  $name: 'Row 2'
  $description: 'Same syntax as row 1. Speed and byte totals are pre-formatted, so only the digit spec applies to them.'
- Row3: ""
  $name: 'Row 3'
  $description: 'Same syntax as row 1.'
- Row4: ""
  $name: 'Row 4'
  $description: 'Same syntax as row 1.'
- Row1FontSize: 0
  $name: 'Row 1 font size (px)'
  $description: '0 inherits Font size. A row can also set its own at the start of its text, with [[size:18]], which wins over this setting.'
- Row1Height: 0
  $name: 'Row 1 height (px)'
  $description: '0 inherits Row height. A row is never shorter than its own font needs, and a row-prefix [[h:44]] wins over this setting.'
- Row2FontSize: 0
  $name: 'Row 2 font size (px)'
  $description: '0 inherits Font size. [[size:18]] at the start of row 2 wins over this setting.'
- Row2Height: 0
  $name: 'Row 2 height (px)'
  $description: '0 inherits Row height. [[h:44]] at the start of row 2 wins over this setting.'
- Row3FontSize: 0
  $name: 'Row 3 font size (px)'
  $description: '0 inherits Font size. [[size:18]] at the start of row 3 wins over this setting.'
- Row3Height: 0
  $name: 'Row 3 height (px)'
  $description: '0 inherits Row height. [[h:44]] at the start of row 3 wins over this setting.'
- Row4FontSize: 0
  $name: 'Row 4 font size (px)'
  $description: '0 inherits Font size. [[size:18]] at the start of row 4 wins over this setting.'
- Row4Height: 0
  $name: 'Row 4 height (px)'
  $description: '0 inherits Row height. [[h:44]] at the start of row 4 wins over this setting.'
- Theme: Endfield
  $name: 'Theme'
  $description: 'A colour preset. A named theme is used as-is. Pick Custom to make the colour fields below take effect.'
  $options:
    - Endfield: Endfield yellow-green on warm near-black
    - Midnight: Cool blue on blue-black
    - Mono: Neutral grey on black
    - Custom: Use the colour fields below
- ClickAction: Proximity
  $name: 'What a click does'
  $description: 'Proximity switches between mouse-activated and always shown. Toggle shows and hides the panel.'
  $options:
    - Proximity: Switch between mouse-activated and always shown
    - Toggle: Show or hide the panel
- Monitor: Topmost
  $name: 'Target monitor'
  $description: 'Which monitor to anchor the panel to.'
  $options:
    - Topmost: The monitor highest on the virtual desktop
    - Primary: The Windows primary monitor
- PanelWidth: 420
  $name: 'Panel width (px)'
  $description: '200-1600.'
- RowHeight: 30
  $name: 'Row height (px)'
  $description: '18-80.'
- CornerRadius: 22
  $name: 'Corner radius (px)'
  $description: '0-60. Set to half the panel height for a full capsule.'
- BorderThickness: 1
  $name: 'Border thickness (px)'
  $description: '0-8. Zero removes the border.'
- FontSize: 15
  $name: 'Font size (px)'
  $description: '9-48.'
- FontFamily: Segoe UI
  $name: 'Font family'
  $description: 'Any installed family name, for example Segoe UI, Consolas, Microsoft YaHei UI or JetBrains Mono. A name that is not installed falls back to Segoe UI and is reported in the log.'
- FallbackFonts: ""
  $name: 'Fallback fonts'
  $description: 'Semicolon-separated candidates for characters the main family cannot draw, for example Microsoft YaHei UI;Segoe UI Emoji. Empty means no fallback, which is also the cheapest: the panel then never asks which glyphs a font has. A single run is drawn in one family, so a run that mixes scripts uses the first candidate that covers all of it.'
- TextColor: ""
  $name: 'Text colour (Custom theme only)'
  $description: 'AARRGGBB. Used only when Theme is Custom; otherwise the theme decides. Leave empty for the Endfield default.'
- AccentColor: ""
  $name: 'Accent colour (Custom theme only)'
  $description: 'AARRGGBB. Default ring and border colour. Used only when Theme is Custom.'
- BackgroundColor: ""
  $name: 'Background (Custom theme only)'
  $description: 'AARRGGBB, alpha first. Used only when Theme is Custom.'
- BorderColor: ""
  $name: 'Border colour (Custom theme only)'
  $description: 'AARRGGBB. Shown while the panel is expanded. Used only when Theme is Custom.'
- BorderCollapsedColor: ""
  $name: 'Collapsed border (Custom theme only)'
  $description: 'AARRGGBB. Shown while collapsed. Used only when Theme is Custom.'
- RingSize: 26
  $name: 'Ring size (px)'
  $description: '12-64. Outer diameter of a default [[ring:...]] gauge.'
- RingThickness: 4
  $name: 'Ring thickness (px)'
  $description: '1-16. Stroke width of a default [[ring:...]] gauge.'
- RingRightOffset: 12
  $name: 'Ring right offset (px)'
  $description: '0-200. Distance from the right edge to the last ring.'
- RingColor: ""
  $name: 'Ring colour (Custom theme only)'
  $description: 'AARRGGBB. Colour of a ring gauge that has no :COLOR field of its own, for example [[ring:{cpu}]]. Leave empty to use the border colour. Used only when Theme is Custom.'
- Icon1: Memory
  $name: 'Row 1 icon'
  $description: 'The plate drawn at the left of row 1. Rows with no icon of their own inherit the first configured one, so setting only this gives every row the same glyph. None leaves the space empty.'
  $options:
    - None: No icon
    - CPU: Processor
    - Memory: Memory module
    - Disk: Storage drive
    - Network: Network link
    - GPU: Graphics adapter
- Icon2: ""
  $name: 'Row 2 icon'
  $description: 'Icon for row 2. Empty inherits the first configured icon.'
  $options:
    - "": Inherit
    - None: No icon
    - CPU: Processor
    - Memory: Memory module
    - Disk: Storage drive
    - Network: Network link
    - GPU: Graphics adapter
- Icon3: ""
  $name: 'Row 3 icon'
  $description: 'Icon for row 3. Empty inherits the first configured icon.'
  $options:
    - "": Inherit
    - None: No icon
    - CPU: Processor
    - Memory: Memory module
    - Disk: Storage drive
    - Network: Network link
    - GPU: Graphics adapter
- Icon4: ""
  $name: 'Row 4 icon'
  $description: 'Icon for row 4. Empty inherits the first configured icon.'
  $options:
    - "": Inherit
    - None: No icon
    - CPU: Processor
    - Memory: Memory module
    - Disk: Storage drive
    - Network: Network link
    - GPU: Graphics adapter
- Icon1Path: ""
  $name: 'Row 1 icon file'
  $description: 'Full path to a PNG, JPEG, BMP, GIF or TIFF for row 1, drawn centred on the plate. Overrides the built-in glyph. SVG is not supported because GDI+ has no SVG decoder.'
- Icon2Path: ""
  $name: 'Row 2 icon file'
  $description: 'Image file for row 2. Empty inherits nothing; the built-in glyph is used.'
- Icon3Path: ""
  $name: 'Row 3 icon file'
  $description: 'Image file for row 3. Empty inherits nothing; the built-in glyph is used.'
- Icon4Path: ""
  $name: 'Row 4 icon file'
  $description: 'Image file for row 4. Empty inherits nothing; the built-in glyph is used.'
- IntroStyle: Slide
  $name: 'Intro animation'
  $description: 'How the panel arrives. Slide uses the original overshoot-and-settle curve; Fade is a plain fade; None appears instantly.'
  $options:
    - Slide: Overshoot and settle, as in the original
    - Fade: Plain fade in
    - None: Appear instantly
- IntroMs: 1200
  $name: 'Intro duration (ms)'
  $description: '200-4000. Duration of the arrival; collapsing takes half of it, since there is nothing to read on the way out.'
- RippleCount: 2
  $name: 'Intro ripples'
  $description: '0-3. Concentric rings expanding from the icon while the panel arrives, as in the original.'
- IconPath: ""
  $name: 'Custom icon file'
  $description: 'Full path to a PNG, JPEG, BMP, GIF or TIFF. Used on every row that has no icon file of its own, and it takes precedence over the built-in glyphs. A row set to None still draws no icon at all. SVG is not supported because GDI+ has no SVG decoder.'
- UpdateMs: 1000
  $name: 'Update interval (ms)'
  $description: '200-10000. Clocks refresh every second regardless.'
- HotZoneDepth: 8
  $name: 'Mouse hot-zone depth (px)'
  $description: '2-64. How far down from the top edge counts as near the mouse.'
- HideDelayMs: 700
  $name: 'Hide delay (ms)'
  $description: '0-10000. Grace period after the pointer leaves before hiding.'
- ExternalData: Off
  $name: 'External data'
  $description: 'Allows other programs to publish text into the panel through the named pipe endfield-info-windhawk. Off by default; nothing is created unless a row contains an {ext.provider.key} token.'
  $options:
    - Off: No external data
    - On: Accept external providers
- ExtProviders: ""
  $name: 'Allowed external providers'
  $description: 'Semicolon separated provider names, for example music;discord. Empty allows any name, which is still logged and rate limited. Matched case-insensitively.'
- ExternalPublisherWatchProcess: ""
  $name: 'External publisher watch process'
  $description: 'Optional. A process name such as cloudmusic. When set, the publisher command above is started only while that process is running, and stopped when it exits -- a watchdog. The mod only observes the process; it never starts or restarts it, and never touches any other application. Leave empty to start the command whenever {ext.*} values are in use.'
- ExternalPublisherCommand: ""
  $name: 'External publisher command'
  $description: 'Optional. A command line the mod starts for you while {ext.*} values are in use, e.g. pwsh -NoProfile -File C:\path\_netease-bridge.ps1. The process is stopped when the mod unloads, and restarted at most 5 times if it dies. The mod never starts or restarts your other applications.'
- ExtValueTtlMs: 30000
  $name: 'External value lifetime (ms)'
  $description: '100-600000. How long a value stays valid when the provider sends - as its ttl. An expired value renders as two dashes.'
- ExtMaxValue: 128
  $name: 'External value length (chars)'
  $description: '16-512. Longer values are truncated with a trailing ellipsis. 128 keeps four values plus template text inside one row buffer.'
- ExtMaxProviders: 8
  $name: 'External provider limit'
  $description: '1-8. Providers beyond this are rejected with an error; existing ones keep their slots.'
- ExtMaxUpdates: 100
  $name: 'External updates per second'
  $description: '1-1000, per connection. Updates beyond this are dropped and counted; the connection is not closed.'
- ExtEventMs: 6000
  $name: 'External event card duration (ms)'
  $description: '1000-60000. How long a card stays on screen. Used as-is when the provider sends - as its ttl; a provider that sends longer is capped here (a value ttl of ten minutes is not a display duration), and a shorter one is honoured.'
- ExtEventQueue: 4
  $name: 'External event queue'
  $description: '1-8. Events waiting behind the one on screen. When full, the lowest priority and oldest event is dropped.'
- IslandMinWidth: 260
  $name: 'Island minimum width'
  $description: '120-640. Narrowest width for an event card. The card grows with its text but never below this, and never past 640.'
- IslandEnabled: Off
  $name: 'Notification island'
  $description: 'Turns an external event into a temporary card: title, optional body, a severity colour and a repeat badge. The card covers the rows while it is up and gives them back when it goes.'
  $options:
    - Off: Values only
    - On: Show event cards
*/
// ==/WindhawkModSettings==
// 开发期诊断开关：默认**静默**。这些日志曾经是排查布局/图标/性能的唯一手段，所以保留语句，
// 需要时把 kDbgTrace 改成 true 即可打开。**不删除**它们是有理由的：删掉会让它们专用的计数器
// 变成 unused（有一次误删还伤到了活代码），而这里的参数在 `if (kDbgTrace)` 内仍然被求值，
// 因此既静默、又不会引入 unused 级联。
static const bool kDbgTrace = false;
#define DBG(...) do { if (kDbgTrace) Wh_Log(__VA_ARGS__); } while (0)
#include <winsock2.h>
#include <netioapi.h>
#include <iphlpapi.h>
#include <windows.h>
#include <pdh.h>
#include <psapi.h>
// For ConvertStringSecurityDescriptorToSecurityDescriptorW: the pipe's DACL is
// built from SDDL so the intent stays readable.
#include <sddl.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>

// ---------------------------------------------------------------------------
// Constants
// ---------------------------------------------------------------------------
static const UINT_PTR kTimerTick = 1;      // cursor polling (HUD thread)
static const UINT_PTR kTimerPerf = 2;      // self-measurement
static const UINT kMsgRatesReady = WM_APP + 1;
static const UINT kMsgApplySettings = WM_APP + 2;
static const UINT kMsgQuit = WM_APP + 3;
// Posted to the HUD *thread* (never to the window handle) when an event is queued:
// the handle can be gone while a pipe thread still holds it (design 5.6.3 R5).
static const UINT kMsgIslandEvent = WM_APP + 4;

static const int kMaxRows = 4;
static const int kMaxRingsPerRow = 4;
static const int kCursorPollMs = 40;       // proximity responsiveness vs. cost
static const int kMaxMonitors = 8;

// External data providers (named pipe). The caps live here because the settings
// reader and the slot table both need them before the external-data section.
static const int kMaxExtProviders = 8;
static const int kMaxExtKeys = 32;
static const int kExtMaxValueChars = 512;   // absolute ceiling; the setting clamps lower
static const int kExtMaxLineBytes = 4096;   // one pipe message
static const int kExtProviderListChars = 256;
static const int kExtProviderChars = 16;
static const int kExtKeyChars = 32;
static const int kExtInstances = 4;         // concurrent provider connections

// PixelFormat32bppPARGB
static const INT kPixelFormat32bppPARGB = 0x000E200B;

// PDH status values used below. Windhawk's MinGW pdh.h declares the functions
// and structs but none of the PDH_* status constants, so they are repeated here
// with their documented values.
#ifndef PDH_MORE_DATA
#define PDH_MORE_DATA ((PDH_STATUS)0x800007D2L)
#endif
#ifndef PDH_CSTATUS_VALID_DATA
#define PDH_CSTATUS_VALID_DATA ((PDH_STATUS)0x00000000L)
#endif
#ifndef PDH_CSTATUS_NEW_DATA
#define PDH_CSTATUS_NEW_DATA ((PDH_STATUS)0x00000001L)
#endif

// A formatted PDH counter value is usable when its status says either "valid data" or
// "new data". The CPU clock path accepted both; the GPU array readers accepted only
// ERROR_SUCCESS (the value of VALID_DATA) and therefore threw away counters that had just
// been created -- the first sample after PdhAddCounter reports NEW_DATA. One rule, asked
// by all of them.
static bool PdhValueUsable(PDH_STATUS status) {
    return status == PDH_CSTATUS_VALID_DATA || status == PDH_CSTATUS_NEW_DATA;
}

// ---------------------------------------------------------------------------
// Small helpers. min/max macros are an MSVC convenience that this toolchain does
// not provide, so they are spelled out here.
// ---------------------------------------------------------------------------
static int ClampInt(int value, int low, int high) {
    if (value < low) return low;
    if (value > high) return high;
    return value;
}
static double MinDouble(double a, double b) { return a < b ? a : b; }
static int MinInt(int a, int b) { return a < b ? a : b; }
static int MaxInt(int a, int b) { return a > b ? a : b; }

// ---------------------------------------------------------------------------
// Ring gauges
//
// A row may carry inline gauges:  [[ring:VALUE]]  [[ring:VALUE:MAX:THICK:COLOUR]]
//
// VALUE and MAX take either a literal number or a {variable} token, so
// [[ring:{cpu}]] tracks CPU usage live. Anything omitted falls back to the
// RingSize / RingThickness settings and the border colour.
//
// GDI+ has no arc primitive, so each ring is stroked as a polyline around the
// circle. At these diameters 48 segments are indistinguishable from a true arc.
// ---------------------------------------------------------------------------
static const int kRingSegments = 48;

struct RingSpec {
    double value = 0;
    double max = 100;
    int thickness = 4;
    DWORD color = 0;
    bool hasColor = false;
};

// Defined with the other ring helpers; forward-declared because the row expander
// needs it to turn a ring token into a gauge. Values and ModSettings are likewise
// defined later, so they are named here rather than included.
struct Values;
struct ModSettings;
static bool ParseRing(const WCHAR *body, int bodyLen, const Values &v,
                      const ModSettings &settings, RingSpec &out);
// Defined with the external-data section, but RequiredGroups needs it first.
static bool ExtIsExtToken(const WCHAR *name, int len);
static void ExtStart(const ModSettings &snap);
static void ExtStop();
static void ExtPublisherTick();
// The sampler reaps the publisher when nothing consumes {ext.*} any more, so it needs
// these two before their definitions.
static bool ExtPublisherAlive();
static void ExtPublisherStop();

// ---------------------------------------------------------------------------
// Minimal GDI+ surface, resolved at runtime.
//
// gdiplus.h is deliberately not included: its API is all static inline
// Gdiplus::* wrappers (not link-visible), and its RectF would collide with the
// one below.
// ---------------------------------------------------------------------------
extern "C" {
typedef struct GpGraphics GpGraphics;
typedef struct GpBrush GpBrush;
typedef struct GpSolidFill GpSolidFill;
typedef struct GpPath GpPath;
typedef struct GpPen GpPen;
typedef struct GpFont GpFont;
typedef struct GpFontFamily GpFontFamily;
typedef struct GpStringFormat GpStringFormat;
typedef struct GpImage GpImage;
typedef struct GpBitmap GpBitmap;
}

typedef struct GdiplusStartupInput {
    UINT32 GdiplusVersion;
    void *DebugEventCallback;
    BOOL SuppressBackgroundThread;
    BOOL SuppressExternalCodecs;
    GdiplusStartupInput() {
        GdiplusVersion = 1;
        DebugEventCallback = nullptr;
        SuppressBackgroundThread = FALSE;
        SuppressExternalCodecs = FALSE;
    }
} GdiplusStartupInput;
typedef struct GdiplusStartupOutput { void *NotificationHook; void *NotificationUnhook; } GdiplusStartupOutput;

struct RectF { float X, Y, Width, Height; };

// GDI+ affine matrix, same layout as its GpMatrix.
struct GpMatrix { float m11, m12, m21, m22, dx, dy; };

static const int kSmoothingAntiAlias = 4;
static const int kTextHintAntiAliasGridFit = 4;
static const int kAlignNear = 0;
static const int kAlignCenter = 1;
// StringFormatFlagsNoWrap. Without it GDI+ wraps a run that is wider than the
// layout box, and because the line alignment is centred (kAlignCenter) a wrapped
// run pushes its FIRST line upward -- so one row of segments, each drawn in its
// own box, ends up with the segments on visibly different baselines. That is the
// "the first row's text is not on one straight line" symptom. A status row has no
// business wrapping: it is one line of readings, and a second line is not where the
// extra text is supposed to go.
static const int kStringFormatNoWrap = 0x00001000;
// Never hand DrawString a degenerate layout box: a zero-width one makes GDI+ clip the
// run away entirely, which is what made every row after the first disappear. Matches
// the floor FitTextToWidth is given, so the box and the fit agree on the budget.
static const float kMinLayoutWidth = 1.0f;
// InterpolationModeHighQualityBicubic. Used for the one-off downscale at load time;
// the per-frame path never resamples at all.
static const int kInterpolationHighQualityBicubic = 7;

struct GdiPlusApi {
    HMODULE module = nullptr;
    int(WINAPI *Startup)(ULONG_PTR *, const GdiplusStartupInput *, GdiplusStartupOutput *) = nullptr;
    void(WINAPI *Shutdown)(ULONG_PTR) = nullptr;
    int(WINAPI *CreateBitmapFromScan0)(INT, INT, INT, INT, BYTE *, GpBitmap **) = nullptr;
    int(WINAPI *LoadImageFromFile)(const WCHAR *, GpImage **) = nullptr;
    int(WINAPI *GetImageWidth)(GpImage *, UINT *) = nullptr;
    int(WINAPI *GetImageHeight)(GpImage *, UINT *) = nullptr;
    int(WINAPI *DrawImageRect)(GpGraphics *, GpImage *, float, float, float, float) = nullptr;
    int(WINAPI *SetInterpolationMode)(GpGraphics *, int) = nullptr;
    int(WINAPI *BitmapGetPixel)(GpBitmap *, INT, INT, DWORD *) = nullptr;
    int(WINAPI *GetImageGraphicsContext)(GpImage *, GpGraphics **) = nullptr;
    int(WINAPI *DisposeImage)(GpImage *) = nullptr;
    int(WINAPI *SetSmoothingMode)(GpGraphics *, int) = nullptr;
    int(WINAPI *SetTextRenderingHint)(GpGraphics *, int) = nullptr;
    int(WINAPI *CreateSolidFill)(DWORD, GpSolidFill **) = nullptr;
    int(WINAPI *SetSolidFillColor)(GpSolidFill *, DWORD) = nullptr;
    int(WINAPI *DeleteBrush)(GpBrush *) = nullptr;
    int(WINAPI *FillPath)(GpGraphics *, GpBrush *, GpPath *) = nullptr;
    int(WINAPI *DrawPath)(GpGraphics *, GpPen *, GpPath *) = nullptr;
    int(WINAPI *DrawEllipse)(GpGraphics *, GpPen *, float, float, float, float) = nullptr;
    int(WINAPI *FillRectangle)(GpGraphics *, GpBrush *, float, float, float, float) = nullptr;
    int(WINAPI *MultiplyWorldTransform)(GpGraphics *, const GpMatrix *, int) = nullptr;
    // One call per arc instead of per segment. The exported signature takes a
    // count and a PointF array.
    int(WINAPI *DrawLines)(GpGraphics *, GpPen *, const POINTF *, int) = nullptr;
    int(WINAPI *CreatePen1)(DWORD, float, int, GpPen **) = nullptr;
    int(WINAPI *DeletePen)(GpPen *) = nullptr;
    int(WINAPI *CreatePath)(int, GpPath **) = nullptr;
    int(WINAPI *DeletePath)(GpPath *) = nullptr;
    int(WINAPI *AddPathArc)(GpPath *, float, float, float, float, float, float) = nullptr;
    int(WINAPI *ClosePathFigure)(GpPath *) = nullptr;
    int(WINAPI *CreateFontFamilyFromName)(const WCHAR *, void *, GpFontFamily **) = nullptr;
    int(WINAPI *DeleteFontFamily)(GpFontFamily *) = nullptr;
    int(WINAPI *CreateFont)(GpFontFamily *, float, int, int, GpFont **) = nullptr;
    int(WINAPI *DeleteFont)(GpFont *) = nullptr;
    int(WINAPI *CreateStringFormat)(int, WORD, GpStringFormat **) = nullptr;
    int(WINAPI *DeleteStringFormat)(GpStringFormat *) = nullptr;
    int(WINAPI *SetStringFormatAlign)(GpStringFormat *, int) = nullptr;
    int(WINAPI *SetStringFormatLineAlign)(GpStringFormat *, int) = nullptr;
    int(WINAPI *DrawString)(GpGraphics *, const WCHAR *, int, GpFont *, const RectF *, GpStringFormat *, GpBrush *) = nullptr;
    int(WINAPI *MeasureString)(GpGraphics *, const WCHAR *, int, GpFont *, const RectF *, GpStringFormat *, RectF *, INT *, INT *) = nullptr;
    int(WINAPI *DeleteGraphics)(GpGraphics *) = nullptr;

    bool Load() {
        module = ((HMODULE(WINAPI *)(LPCWSTR))LoadLibraryW)(L"gdiplus.dll");
        if (!module) return false;
#define RESOLVE(field, name)                                   \
        field = (decltype(field))GetProcAddress(module, name); \
        if (!field) { Wh_Log(L"gdiplus export missing"); return false; }
        RESOLVE(Startup, "GdiplusStartup")
        RESOLVE(Shutdown, "GdiplusShutdown")
        RESOLVE(CreateBitmapFromScan0, "GdipCreateBitmapFromScan0")
        RESOLVE(LoadImageFromFile, "GdipLoadImageFromFile")
        RESOLVE(GetImageWidth, "GdipGetImageWidth")
        RESOLVE(GetImageHeight, "GdipGetImageHeight")
        RESOLVE(DrawImageRect, "GdipDrawImageRect")
        RESOLVE(SetInterpolationMode, "GdipSetInterpolationMode")
        RESOLVE(BitmapGetPixel, "GdipBitmapGetPixel")
        RESOLVE(GetImageGraphicsContext, "GdipGetImageGraphicsContext")
        RESOLVE(DisposeImage, "GdipDisposeImage")
        RESOLVE(SetSmoothingMode, "GdipSetSmoothingMode")
        RESOLVE(SetTextRenderingHint, "GdipSetTextRenderingHint")
        RESOLVE(CreateSolidFill, "GdipCreateSolidFill")
        RESOLVE(SetSolidFillColor, "GdipSetSolidFillColor")
        RESOLVE(DeleteBrush, "GdipDeleteBrush")
        RESOLVE(FillPath, "GdipFillPath")
        RESOLVE(DrawPath, "GdipDrawPath")
        RESOLVE(DrawEllipse, "GdipDrawEllipse")
        RESOLVE(FillRectangle, "GdipFillRectangle")
        RESOLVE(MultiplyWorldTransform, "GdipMultiplyWorldTransform")
        RESOLVE(DrawLines, "GdipDrawLines")
        RESOLVE(CreatePen1, "GdipCreatePen1")
        RESOLVE(DeletePen, "GdipDeletePen")
        RESOLVE(CreatePath, "GdipCreatePath")
        RESOLVE(DeletePath, "GdipDeletePath")
        RESOLVE(AddPathArc, "GdipAddPathArc")
        RESOLVE(ClosePathFigure, "GdipClosePathFigure")
        RESOLVE(CreateFontFamilyFromName, "GdipCreateFontFamilyFromName")
        RESOLVE(DeleteFontFamily, "GdipDeleteFontFamily")
        RESOLVE(CreateFont, "GdipCreateFont")
        RESOLVE(DeleteFont, "GdipDeleteFont")
        RESOLVE(CreateStringFormat, "GdipCreateStringFormat")
        RESOLVE(DeleteStringFormat, "GdipDeleteStringFormat")
        RESOLVE(SetStringFormatAlign, "GdipSetStringFormatAlign")
        RESOLVE(SetStringFormatLineAlign, "GdipSetStringFormatLineAlign")
        RESOLVE(DrawString, "GdipDrawString")
        RESOLVE(MeasureString, "GdipMeasureString")
        RESOLVE(DeleteGraphics, "GdipDeleteGraphics")
#undef RESOLVE
        return true;
    }
};

// The single resolved GDI+ entry-point table. Declared here so every drawing
// helper below can reach it.
static GdiPlusApi g_gdiplus;

// ---------------------------------------------------------------------------
// Installed fonts
//
// GDI+ accepts a font family name that is not in \Windows\Fonts as long as the
// font is installed for the session, so this enumerates the two registry keys
// that record installed families and turns them into a settings dropdown.
// ---------------------------------------------------------------------------
static WCHAR g_fontList[4096];

// True when the list already holds this family. The list is ';'-separated.
static bool FontListContains(const WCHAR *name, int length) {
    if (!name || length <= 0) return false;
    const WCHAR *p = g_fontList;
    while (*p) {
        const WCHAR *end = wcschr(p, L';');
        int entryLength = end ? (int)(end - p) : (int)wcslen(p);
        if (entryLength == length && _wcsnicmp(p, name, length) == 0) return true;
        if (!end) break;
        p = end + 1;
    }
    return false;
}

static void AppendFontName(const WCHAR *name, int length) {
    if (!name || length <= 0 || length > 120) return;
    // Skip the hidden families Windows installs but does not expose.
    if (name[0] == L'@') return;
    // The registry lists one value per style, so the same family repeats.
    if (FontListContains(name, length)) return;

    size_t used = wcslen(g_fontList);
    if (used + (size_t)length + 2 >= 4096) return;
    if (used > 0) g_fontList[used++] = L';';
    wcsncpy(g_fontList + used, name, length);
    g_fontList[used + length] = L'\0';
}

static void EnumerateFonts() {
    if (g_fontList[0]) return;

    HKEY key = nullptr;
    static const WCHAR *keys[] = {
        L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Fonts",
        L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Fonts",
    };

    for (const WCHAR *subKey : keys) {
        for (int hive = 0; hive < 2; hive++) {
            HKEY root = hive == 0 ? HKEY_LOCAL_MACHINE : HKEY_CURRENT_USER;
            if (RegOpenKeyExW(root, subKey, 0, KEY_READ, &key) != ERROR_SUCCESS) continue;

            DWORD index = 0;
            WCHAR valueName[128];
            DWORD nameLength = 128;
            while (RegEnumValueW(key, index, valueName, &nameLength, nullptr, nullptr,
                                 nullptr, nullptr) == ERROR_SUCCESS) {
                // A value name is either "Arial (TrueType)" or "Arial Bold".
                WCHAR *paren = wcsstr(valueName, L" (");
                if (paren) *paren = L'\0';
                // Trim a trailing style word so only the family remains.
                static const WCHAR *styles[] = {
                    L" Bold Italic", L" Bold Oblique", L" Bold", L" Italic", L" Oblique",
                    L" Light Italic", L" Light", L" Regular", L" Black", L" Medium",
                };
                for (const WCHAR *style : styles) {
                    size_t nameLen = wcslen(valueName);
                    size_t styleLen = wcslen(style);
                    if (nameLen > styleLen &&
                        _wcsicmp(valueName + nameLen - styleLen, style) == 0) {
                        valueName[nameLen - styleLen] = L'\0';
                        break;
                    }
                }
                AppendFontName(valueName, (int)wcslen(valueName));
                index++;
                nameLength = 128;
            }
            RegCloseKey(key);
        }
    }
}

// True when the family appears in the enumerated installed-font list.
static bool FontIsInstalled(const WCHAR *family) {
    if (!family || !*family) return false;
    size_t length = wcslen(family);
    if (length > 120) return false;
    return FontListContains(family, (int)length);
}

// ---------------------------------------------------------------------------
// Settings
// ---------------------------------------------------------------------------
// ---------------------------------------------------------------------------
// Design tokens, taken from the ECP Avalonia theme so the mod can look like the
// project it came from.
//
// The original expresses depth as a ladder of near-black surfaces with a single
// yellow-green accent. Reproducing the exact steps matters more than the hues:
// adjacent surfaces are only a few levels apart, which is what reads as
// "layered" rather than "flat". Surfaces the mod does not draw are recorded here
// rather than declared as unused constants:
//
//   surface.raised   #242426   settings card
//   surface.sunken   #1E1E20   option group nested inside a card / tray menu body
//   surface.pill     #312F30   the HUD capsule itself   -> kThemes[Endfield]
//   surface.badge    #262425   the progress ring plate  -> WithOpacity(track)
//   plate.light      #E9E7E4   icon plate under the glyph
//   glyph.dark       #141313   glyph drawn on that plate
//
//   text.primary     #FFFFFF   full opacity               -> theme text
//   text.secondary   #FFFFFF   at 0.55                    -> kTextSecondaryOpacity
//   text.tertiary    #FFFFFF   at 0.40                    -> kTextTertiaryOpacity
//   text.label       #85858B   muted grey field labels
//   accent           #C6CA4C   yellow-green                -> kAccent / theme
//   accent.onDark    #171719   text on an accent-filled button
//
// The accent-filled button is the one high-contrast element in the whole UI, and
// the mod's border plays that role here.
// ---------------------------------------------------------------------------
static const DWORD kAccent = 0xFFC6CA4C;

// Opacity tiers from HudTheme.axaml (Hud.TextSecondaryOpacity / TertiaryOpacity).
// Using them instead of hard-coded alphas keeps the ring track, the collapsed
// grip and any secondary text visibly related to the same scale.
static const double kTextSecondaryOpacity = 0.55;
static const double kTextTertiaryOpacity = 0.40;

// Applies an opacity tier to a colour's alpha channel.
static DWORD WithOpacity(DWORD color, double opacity) {
    if (opacity < 0.0) opacity = 0.0;
    if (opacity > 1.0) opacity = 1.0;
    DWORD baseAlpha = (color >> 24) & 0xFF;
    DWORD alpha = (DWORD)(baseAlpha * opacity + 0.5);
    return (color & 0x00FFFFFF) | (alpha << 24);
}

// ---------------------------------------------------------------------------
// Intro animation, translated from HudAnimations.cs.
//
// The original is a 6-second timeline of 17 parallel animations. A 260 ms panel
// cannot host all of it, so the parts that read at this size are kept and the
// rest is dropped:
//
//   kept     panel scale with KS_BackOut overshoot, panel height 60 -> 90 -> 60,
//            icon pop 0.4 -> 1.12 -> 1.0, content fade, optional ripples
//   dropped  the copyright/title crossfade, the number-slide, the square/circle
//            form morph, and the retract half of the timeline
//
// Easing. Avalonia takes KeySpline control points directly, so the same two
// control points reproduce a curve exactly as a cubic bezier evaluated on y for a
// given x:
//
//   KS_In      (0.42, 0,     1,    1)
//   KS_Out     (0,    0,     0.58, 1)
//   KS_InOut   (0.42, 0,     0.58, 1)
//   KS_Smooth  (0.65, 0,     0.35, 1)
//   KS_BackOut (0.175, 0.885, 0.32, 1 + BounceStrength)
//
// KS_BackOut is the overshoot that gives the HUD its character.
//
// Cues are fractions of the intro. The intro is stretched so the shape-settle
// moment always lands at kIntroEndCue of the configured duration, which is what
// keeps the pacing right whether the user picks 3 s or 10 s.
// ---------------------------------------------------------------------------
// Reference values from the original, kept for the record. The implementation
// derives its own equivalents, so these are documentation rather than inputs.
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-const-variable"
static const double kIntroEndCue = 0.42;              // TNumReady, the settle point
static const double kBounceStrengthDefault = 0.275;   // original user-facing default
#pragma clang diagnostic pop
static const double kCueStart = 0.04;      // TStart
static const double kCueAppear = 0.07;     // TAppear
static const double kCueIconPop = 0.10;    // TBoltPop
// Growth gets a larger share of the timeline than the original's TExpand = 0.12.
// The original spends 6 s end to end, so a 0.12 cue is a leisurely 720 ms there;
// on a 1.2 s panel the same cue would be 144 ms, which reads as a snap rather than
// a grow. Every later cue is correspondingly later so the ordering is preserved.
static const double kCueGrow = 0.16;       // TExpand
static const double kCueSettle = 0.30;     // THoldB
// TContract is kept for the record: the port does not implement the original's
// retract-then-reopen step, because that step exists to hand over to the
// copyright/title crossfade, which this port dropped. See EvaluateIntro.
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-const-variable"
static const double kCueContract = 0.36;   // TContract
#pragma clang diagnostic pop
static const double kCueContentIn = 0.38;  // TNumIn
static const double kCueContentReady = 0.66;  // text fully surfaced

// Opacity stages between kCueContentIn and kCueContentReady, as fractions of the
// whole intro. With kCueContentReady this gives the text fade five steps rising
// 0 -> 15 -> 40 -> 65 -> 85 -> 100 %, spread over ~0.28 of the intro (about 340 ms
// on the 1200 ms default) instead of one 48 ms move.
static const double kCueTextFade1 = 0.44;
static const double kCueTextFade2 = 0.51;
static const double kCueTextFade3 = 0.57;
static const double kCueTextFade4 = 0.62;

// Collapsing runs at half the arriving duration. The two directions are not
// symmetric in the first place (arriving needs a hold at full height for the
// content to surface; leaving does not), and matching durations made the collapse
// feel sluggish: there is nothing to read on the way out, so it only needs to be
// long enough to look deliberate.
static const double kCollapseDurationRatio = 0.5;

// Icon geometry, matching the bolt's animated footprint. The original icon is
// scaled by GlobalScale exactly like the text, so it is sized from FontSize.
static const double kIconScaleStart = 0.4;    // BoltIcon start scale
static const double kIconScalePeak = 1.12;    // BoltIcon peak scale
static const double kIconPlateRatio = 1.55;   // plate diameter / glyph size
static const double kIconLeftMargin = 10.0;
static const double kIconContentGap = 8.0;

// Ripples: three concentric rings expanding from the icon. Radii, peak opacity
// and the host's downward offset are the original's numbers.
static const double kRippleScaleEnd[3] = {1.5, 2.0, 2.5};
static const double kRipplePeakOpacity[3] = {0.50, 0.50, 0.60};
// (The original's RippleRise offset lived here. It is not applied -- see PaintRipples --
// so the constant and the frame field it fed are gone rather than kept unused.)

// The icon plate: a light disc with a dark glyph, from HudTheme.axaml
// (Hud.IconPlate #E9E7E4 / Hud.BoltDark #141313).
static const DWORD kPlateLight = 0xFFE9E7E4;
static const DWORD kGlyphDark = 0xFF141313;

// ---------------------------------------------------------------------------
// Icons are per-row: every row carries its own plate, its own built-in glyph
// choice and its own optional image file, and each plate emits its own ripples.
//
// The layout is row-aligned on purpose. Rows already form a fixed list (Row1..Row4)
// with per-row text, so a matching Icon1..Icon4 list is the least surprising way to
// configure them: the Nth icon belongs to the Nth row, and no separate capacity
// setting is needed because kMaxRows already bounds it.
// ---------------------------------------------------------------------------
struct IconSpec {
    int style = 0;                  // 0 none, 1 cpu, 2 memory, 3 disk, 4 network, 5 gpu
    // True when the settings key held a value. Without it an explicit `None` cannot be
    // told apart from "this row was never configured", so it fell through to the
    // inherited glyph and README's "set it to None to leave a row without an icon" was
    // simply untrue (measured: Icon1=Memory + Icon3=None gave row 3 the Memory glyph).
    bool styleSet = false;
    WCHAR path[MAX_PATH] = L"";     // empty = use the built-in glyph
};

// Copies a user-supplied path, stripping the quoting and padding people naturally
// add. "C:\x\y.png", 'C:\x\y.png' and a trailing space all mean the same file, and
// passing the quotes through made GdipLoadImageFromFile fail on a path that looks
// correct in the settings box.
static void CopyUserPath(WCHAR *dest, size_t destCount, const WCHAR *source) {
    dest[0] = L'\0';
    if (!source) return;
    while (*source == L' ' || *source == L'\t') source++;

    WCHAR quote = 0;
    if (*source == L'"') { quote = L'"'; source++; }
    else if (*source == L'\'') { quote = L'\''; source++; }

    size_t written = 0;
    while (*source && written + 1 < destCount) {
        // A matching closing quote ends the path; anything after it is ignored.
        if (quote && *source == quote) break;
        dest[written++] = *source++;
    }
    // Trim trailing whitespace, which a careless copy/paste leaves behind.
    while (written > 0 && (dest[written - 1] == L' ' || dest[written - 1] == L'\t')) written--;
    dest[written] = L'\0';
}

// Names accepted by the IconN settings, indexed by style.
static const WCHAR *const kIconStyleNames[] = {
    L"None", L"CPU", L"Memory", L"Disk", L"Network", L"GPU",
};
static const int kIconStyleCount = (int)(sizeof(kIconStyleNames) / sizeof(kIconStyleNames[0]));

// Position of an icon by name. Returns 0 ("None") for anything unrecognised, which
// is also what an empty setting resolves to.
static int IconStyleFromName(const WCHAR *name) {
    if (!name || !name[0]) return 0;
    for (int i = 0; i < kIconStyleCount; i++) {
        if (_wcsicmp(name, kIconStyleNames[i]) == 0) return i;
    }
    return 0;
}

// Glyphs with no image file of their own fall back to the first configured glyph,
// so setting only Icon1 still produces a plate on every row.
static int ResolveIconStyle(const ModSettings &settings, int index);
static const WCHAR *ResolveIconPath(const ModSettings &settings, int index);
static int ResolveIconSlot(const ModSettings &settings, int index);


// Colour presets. The alpha on the background matches the mod's 90% default.
struct ThemePreset {
    const WCHAR *name;
    DWORD background;
    DWORD border;
    DWORD borderCollapsed;
    DWORD text;
    DWORD accent;
};

static const ThemePreset kThemes[] = {
    // Faithful to Styles/HudTheme.axaml: warm near-black pill, yellow-green accent.
    {L"Endfield", 0xE6312F30, 0xFFC6CA4C, 0xFF6A6C3A, 0xFFFFFFFF, 0xFFC6CA4C},
    // Same structure, cool cast.
    {L"Midnight", 0xE61A1D24, 0xFF5AA9E6, 0xFF2F5878, 0xFFEDF3FA, 0xFF5AA9E6},
    // No hue at all, for people who want the numbers only.
    {L"Mono", 0xE61C1C1E, 0xFFB8B8BC, 0xFF5A5A5E, 0xFFF2F2F2, 0xFFB8B8BC},
};
static const int kThemeCount = (int)(sizeof(kThemes) / sizeof(kThemes[0]));

// ---- Intro animation state ---------------------------------------------------
struct IntroState {
    bool active = false;
    DWORD startedAt = 0;
    double durationMs = 0;
    double fromHeight = 0;
    double toHeight = 0;
    int style = 0;        // 0 slide, 1 fade, 2 none
    int rippleCount = 0;
};

static IntroState g_intro;

// Amplitude of the panel's BackOut overshoot. Much smaller than the original's
// 0.275, for two reasons: the original animates a fixed 560x60 capsule, and more
// importantly 0.275 is a ~6% overshoot, which on a 250 px tall panel means it
// springs 14 px past the target and visibly settles back. At 0.04 the overshoot is
// about 3 px, which reads as a soft landing instead of a retract.
static const double kPanelBounceStrength = 0.04;

static double Lerp(double a, double b, double t) { return a + (b - a) * t; }

// One edge in a keyframe chain: reach `value` at `cue`, easing into it.
struct IntroKey { double cue; double value; double (*ease)(double); };

static double EvaluateKeys(double cue, const IntroKey *keys, int count) {
    if (count <= 0) return 0.0;
    if (cue <= keys[0].cue) return keys[0].value;
    for (int i = 1; i < count; i++) {
        if (cue <= keys[i].cue) {
            double t = (cue - keys[i - 1].cue) / (keys[i].cue - keys[i - 1].cue);
            if (t < 0.0) t = 0.0;
            if (t > 1.0) t = 1.0;
            double eased = keys[i].ease ? keys[i].ease(t) : t;
            return Lerp(keys[i - 1].value, keys[i].value, eased);
        }
    }
    return keys[count - 1].value;
}

// The KeySplines this panel actually uses. The original also had KS_Smooth, which only
// drove RippleRise -- the rise this port deliberately does not apply -- so it is gone
// with it rather than kept as an unused helper.
static double Clamp01(double x) { return x < 0.0 ? 0.0 : (x > 1.0 ? 1.0 : x); }

static double CubicBezierY(double x, double x1, double y1, double x2, double y2) {
    x = Clamp01(x);
    if (x <= 0.0) return 0.0;
    if (x >= 1.0) return 1.0;
    auto sample = [](double t, double c1, double c2) {
        double u = 1.0 - t;
        return 3.0 * u * u * t * c1 + 3.0 * u * t * t * c2 + t * t * t;
    };
    double lo = 0.0, hi = 1.0, t = x;
    for (int i = 0; i < 20; i++) {
        t = (lo + hi) / 2.0;
        if (sample(t, x1, x2) < x) lo = t;
        else hi = t;
    }
    return sample(t, y1, y2);
}

static double EaseIn(double t) { return CubicBezierY(t, 0.42, 0.0, 1.0, 1.0); }
static double EaseOut(double t) { return CubicBezierY(t, 0.0, 0.0, 0.58, 1.0); }
static double EaseInOut(double t) { return CubicBezierY(t, 0.42, 0.0, 0.58, 1.0); }
static double EaseBackOut(double t) {
    return CubicBezierY(t, 0.175, 0.885, 0.32, 1.0 + kPanelBounceStrength);
}

// Progress through the intro, 0..1. Returns 1 when there is no intro running.
//
// Callers must not re-derive progress with a second call: GetTickCount advances
// between them, so two calls can disagree about whether the intro has finished and
// the value would jump backwards. g_intro.active is the single source of truth
// once EvaluateIntro has run for this frame.
static double IntroCue() {
    if (!g_intro.active) return 1.0;
    DWORD now = GetTickCount();
    double elapsed = (double)(now - g_intro.startedAt);
    if (g_intro.durationMs <= 0.0 || elapsed >= g_intro.durationMs) return 1.0;
    return elapsed / g_intro.durationMs;
}

// Derived animation values for the current frame.
struct IntroFrame {
    double cue;
    double scale;         // panel scale about its centre
    double height;        // panel height, meaningful only while cue < 1
    double contentAlpha;  // text and rings
    double iconScale;
    double iconAlpha;
    bool expanding;       // true = arriving, false = collapsing
    double rippleProgress;  // 0 until the ripples start, then 0..1
};

// Note: this deliberately does not consult g_displayHeight. That variable holds
// the resting window geometry and lives further down the file; the caller owns the
// resting height and substitutes it when no intro is running.
// Eases the intro out of its finishing frame. The cue inside f is the only timing
// sample taken for this frame; re-deriving it here could disagree about whether
// the intro has ended, which made the painted height jump backwards on the last
// frame.
static IntroFrame EvaluateIntro() {
    IntroFrame f = {};
    f.cue = IntroCue();
    f.height = 0.0;   // only meaningful while an intro is running; see note above
    f.scale = 1.0;
    f.contentAlpha = 1.0;
    f.iconScale = 1.0;
    f.iconAlpha = 1.0;
    f.expanding = true;

    if (!g_intro.active) return f;
    if (f.cue >= 1.0) {
        g_intro.active = false;
        f.height = g_intro.toHeight;
        f.contentAlpha = 1.0;
        f.iconScale = 1.0;
        f.iconAlpha = 1.0;
        return f;
    }

    const double cue = f.cue;
    const bool expanding = g_intro.toHeight >= g_intro.fromHeight;
    f.expanding = expanding;
    // The move is always from where the panel is to where it is going. Expanding
    // means toHeight is the larger value, so this single pair covers both
    // directions; deriving them per-direction is what previously made the collapse
    // animate *upwards* from the tab to the expanded height.
    const double start = g_intro.fromHeight;
    const double end = g_intro.toHeight;

    if (g_intro.style == 2) {   // None: appear instantly
        f.height = end;
        f.contentAlpha = 1.0;
        return f;
    }
    if (g_intro.style == 1) {   // Fade: opacity only, height already at rest
        f.height = end;
        f.contentAlpha = expanding ? EaseOut(Clamp01(cue / kCueContentReady))
                                   : 1.0 - EaseIn(Clamp01(cue));
        f.iconAlpha = f.contentAlpha;
        return f;
    }

    // Panel height.
    //
    // Expanding and collapsing are deliberately NOT symmetric. Arriving holds at
    // full height while the content surfaces, so the growth is a short move and
    // the rest of the intro is the fade. Leaving has no such hold: the shrink has
    // to run across the whole duration, because compressing it into the grow cue
    // left the panel sitting at tab height for most of the animation with only a
    // fade left to watch.
    if (expanding) {
        const IntroKey growKeys[] = {
            {0.0, start, nullptr},
            {kCueGrow, end, EaseBackOut},
        };
        f.height = EvaluateKeys(cue, growKeys, 2);
    } else {
        // The shrink runs the whole collapse, ending exactly on the end of the
        // (already halved) timeline. It used to stop at kCollapseDurationRatio -- the
        // duration ratio used as a *cue* -- which is cue 0.5 of a timeline that had
        // already been halved: the panel sat at tab height for the second half while
        // the frame clock kept repainting it at 16 ms, and the visible collapse was
        // IntroMs/4 rather than the half the README promises. The content fade and the
        // scale shrink below are still keyed to kCueSettle; nothing draws content while
        // collapsed (PaintPanel takes the tab branch), so they only have to be over by
        // the time the tab is all that is left.
        const IntroKey shrinkKeys[] = {
            {0.0, start, nullptr},
            {1.0, end, EaseInOut},
        };
        f.height = EvaluateKeys(cue, shrinkKeys, 2);
    }
    // Panel scale carries the overshoot as it opens, and a matching shrink as it
    // closes. The shrink is essential: on the way out the panel fills the resting
    // rectangle, so the scale is what visually reduces it to the tab.
    if (expanding) {
        const IntroKey scaleKeys[] = {
            {0.0, 0.94, nullptr},
            {kCueAppear, 1.0, EaseBackOut},
        };
        f.scale = EvaluateKeys(cue, scaleKeys, 2);
    } else {
        const IntroKey shrinkScale[] = {
            {0.0, 1.0, nullptr},
            {kCueSettle, 0.88, EaseInOut},
        };
        f.scale = EvaluateKeys(cue, shrinkScale, 2);
    }

    // Icon: opacity 0 -> 1 with a 0.4 -> 1.12 -> 1.0 pop.
    const IntroKey iconAlphaKeys[] = {
        {kCueStart, 0.0, nullptr},
        {kCueIconPop, 1.0, EaseIn},
    };
    f.iconAlpha = expanding ? EvaluateKeys(cue, iconAlphaKeys, 2)
                            : 1.0 - EaseIn(Clamp01(cue));
    if (expanding) {
        const IntroKey iconScaleKeys[] = {
            {0.0, kIconScaleStart, nullptr},
            {kCueStart, kIconScaleStart, EaseIn},
            {kCueIconPop, kIconScalePeak, EaseBackOut},
            {kCueGrow, 1.0, EaseOut},
        };
        f.iconScale = EvaluateKeys(cue, iconScaleKeys, 4);
    } else {
        f.iconScale = Lerp(1.0, kIconScaleStart, EaseIn(Clamp01(cue)));
    }

    // Content fades in after the shape has settled, so the arrival reads as
    // "panel grows, then the numbers surface".
    //
    // The fade is deliberately long and stepped rather than one move: at the
    // original cue spacing it was 0.38 -> 0.42, which on a 1200 ms intro is 48 ms
    // and reads as the text simply appearing. Four stages at 0 / 25 / 60 / 100 %
    // over kCueContentReady - kCueContentIn (= 0.28, so ~340 ms) give the numbers
    // a visible rise instead of a switch.
    //
    // Every stage eases with KS_InOut, which is slow at both ends, so no stage
    // snaps: they chain into a continuous curve.
    if (expanding) {
        const IntroKey contentKeys[] = {
            {kCueContentIn, 0.0, nullptr},
            {kCueTextFade1, 0.15, EaseInOut},
            {kCueTextFade2, 0.40, EaseInOut},
            {kCueTextFade3, 0.65, EaseInOut},
            {kCueTextFade4, 0.85, EaseInOut},
            // nullptr easing on the last stage: it lands exactly on full opacity
            // instead of approaching it asymptotically.
            {kCueContentReady, 1.0, nullptr},
        };
        f.contentAlpha = EvaluateKeys(cue, contentKeys, 6);
    } else {
        f.contentAlpha = 1.0 - EaseIn(Clamp01(cue / kCueSettle));
    }

    // Ripples run from the grow cue to the settle cue. The original's RippleRise
    // (lifting the host by 16 px while they expand) is deliberately not applied -- see
    // PaintRipples -- so there is no rise term to compute here.
    if (expanding && g_intro.rippleCount > 0 && cue > kCueGrow) {
        f.rippleProgress = EaseOut(Clamp01((cue - kCueGrow) / (kCueSettle - kCueGrow)));
    }
    return f;
}

// ---------------------------------------------------------------------------
// Font chains: the global fallback list and the per-run [[font:...]] override
//
// A chain is an ordered list of candidate family names. Two kinds exist:
//
//   * the global list from "Fallback fonts", which every row may consult, and
//   * one per [[font:...]] tag written in a row, which overrides the main family for
//     the run the tag wraps.
//
// The load-bearing half of this design is what happens when *neither* is configured:
// no coverage query, no second font object, no extra segments -- the painter builds
// one font and draws everything with it, exactly as it did before the feature
// existed. That gate is RowNeedsFontPath(), decided from the settings, never per
// frame (docs/font-and-row-metrics-design.md 1).
// ---------------------------------------------------------------------------
static const int kMaxFontFamilies = 4;      // candidates per chain
static const int kMaxFontFamilyChars = 48;
static const int kMaxFontChains = 6;        // [[font:...]] chains; the global one is separate
static const int kMaxFontKeyChars = kMaxFontFamilies * kMaxFontFamilyChars;

struct FontChain {
    WCHAR names[kMaxFontFamilies][kMaxFontFamilyChars] = {};
    int count = 0;
};

// The chain used for a text run that has no tag of its own and no global list to consult.
static const FontChain kNoFontChain;

// One registered [[font:...]] tag: the name list as it was written (canonicalised into
// `key`) plus the chain that will actually be used for it. The key is what a row's own tag
// is matched against when it is expanded -- so the tag text is parsed once here and never
// re-validated on the sampler thread.
struct FontTag {
    WCHAR key[kMaxFontKeyChars] = {};
    FontChain chain;
};

// Copies [begin,end) with leading and trailing spaces and tabs removed. Returns false when
// nothing is left or when the name does not fit: a family name that is too long is dropped,
// never truncated, because half a name is a different font and GDI+ would substitute one.
static bool CopyTrimmedName(const WCHAR *begin, const WCHAR *end, WCHAR *out, int outCount) {
    while (begin < end && (*begin == L' ' || *begin == L'\t')) begin++;
    while (end > begin && (end[-1] == L' ' || end[-1] == L'\t')) end--;
    const int length = (int)(end - begin);
    if (length <= 0 || length >= outCount) return false;
    wcsncpy(out, begin, length);
    out[length] = L'\0';
    return true;
}

// Splits a ';'-separated candidate list into a chain, dropping empty entries and
// case-insensitive duplicates. *rejected counts the entries that had content but were not
// stored (too long, or past kMaxFontFamilies), so the settings reader can report them once
// rather than dropping them in silence.
static int ParseFontChainList(const WCHAR *begin, const WCHAR *end, FontChain &out,
                              int *rejected) {
    out.count = 0;
    if (rejected) *rejected = 0;
    if (!begin || !end || end <= begin) return 0;

    const WCHAR *p = begin;
    while (p < end) {
        const WCHAR *sep = p;
        while (sep < end && *sep != L';') sep++;

        bool hadContent = false;
        for (const WCHAR *c = p; c < sep; c++) {
            if (*c != L' ' && *c != L'\t') hadContent = true;
        }

        WCHAR name[kMaxFontFamilyChars];
        bool stored = false;
        if (CopyTrimmedName(p, sep, name, kMaxFontFamilyChars)) {
            bool duplicate = false;
            for (int i = 0; i < out.count; i++) {
                if (_wcsicmp(out.names[i], name) == 0) {
                    duplicate = true;
                    break;
                }
            }
            if (duplicate) {
                stored = true;   // already in the list: nothing was lost
            } else if (out.count < kMaxFontFamilies) {
                wcscpy(out.names[out.count], name);
                out.count++;
                stored = true;
            }
        }
        if (hadContent && !stored && rejected) (*rejected)++;

        if (sep >= end) break;
        p = sep + 1;
    }
    return out.count;
}

// Canonical text of a parsed chain: its names joined with ';'. Two tags that differ only in
// spacing -- or that name a family which was dropped as unusable -- share a key, which is
// what lets a tag written inside a row find the chain ReadSettings registered for it while
// the tag writers stay free to format it their own way.
static void FontChainKey(const FontChain &chain, WCHAR *out, int outCount) {
    if (!out || outCount <= 0) return;
    out[0] = L'\0';
    size_t used = 0;
    for (int i = 0; i < chain.count; i++) {
        if (i > 0) {
            if (used + 1 >= (size_t)outCount) break;
            out[used++] = L';';
        }
        const WCHAR *name = chain.names[i];
        while (*name && used + 1 < (size_t)outCount) out[used++] = *name++;
    }
    out[used] = L'\0';
}

// Drops candidates that are not installed, using the same registry-backed list the main
// family is validated against. Without this a typo in a fallback name would be handed to
// GDI+, which substitutes *some* font for an unknown family -- and that substitute would
// then answer the coverage questions, so the panel could quietly draw a font nobody asked
// for while claiming the fallback worked.
static void FilterChainToInstalled(FontChain &chain, int *dropped) {
    if (dropped) *dropped = 0;
    int kept = 0;
    for (int i = 0; i < chain.count; i++) {
        if (FontIsInstalled(chain.names[i])) {
            if (kept != i) wcscpy(chain.names[kept], chain.names[i]);
            kept++;
        } else if (dropped) {
            (*dropped)++;
        }
    }
    chain.count = kept;
}

// The chain registered for a tag's canonical text, or -1 when there is none.
static int FontTagIndexOf(const FontTag *tags, int tagCount, const WCHAR *key) {
    for (int i = 0; i < tagCount; i++) {
        if (_wcsicmp(tags[i].key, key) == 0) return i;
    }
    return -1;
}

// The cost gate, pure so it can be asserted: a row needs the multi-font path only when it
// carries a font tag of its own or a global fallback list exists. With neither, the painter
// never builds a second font and never asks GDI which glyphs exist.
static bool RowNeedsFontPath(bool rowHasTag, bool hasGlobalChain) {
    return rowHasTag || hasGlobalChain;
}

// Registers every usable [[font:...]] tag in one raw row and returns how many of them will
// be honoured -- which is the row's half of the cost gate. A tag whose name is empty, too
// long, past the chain limit or not installed is deliberately *not* registered: it then
// draws as ordinary text, so a typo shows up on screen instead of silently changing the
// typeface. Pairs with ExpandTokensAndRings, which matches tags by this same canonical key.
static int CollectRowFontTags(const WCHAR *row, FontTag *tags, int *tagCount, int *rejected) {
    int honoured = 0;
    if (!row) return 0;

    const WCHAR *p = row;
    while (*p) {
        if (p[0] != L'[' || p[1] != L'[') {
            p++;
            continue;
        }
        const WCHAR *close = wcsstr(p + 2, L"]]");
        if (!close) break;
        const WCHAR *body = p + 2;
        const int bodyLen = (int)(close - body);

        // "[[/font]]" ends a run and needs no registration; anything that is not "font:"
        // is left for ExpandTokensAndRings to treat as ordinary text.
        if (bodyLen > 5 && _wcsnicmp(body, L"font:", 5) == 0) {
            FontChain candidate;
            int over = 0;
            ParseFontChainList(body + 5, close, candidate, &over);

            WCHAR key[kMaxFontKeyChars];
            FontChainKey(candidate, key, kMaxFontKeyChars);

            int index = -1;
            if (candidate.count > 0) {
                index = FontTagIndexOf(tags, *tagCount, key);
                if (index < 0 && *tagCount < kMaxFontChains) {
                    FontChain usable = candidate;
                    int gone = 0;
                    FilterChainToInstalled(usable, &gone);
                    if (usable.count > 0) {
                        wcsncpy(tags[*tagCount].key, key, kMaxFontKeyChars - 1);
                        tags[*tagCount].key[kMaxFontKeyChars - 1] = L'\0';
                        tags[*tagCount].chain = usable;
                        index = *tagCount;
                        (*tagCount)++;
                    }
                }
            }
            if (index >= 0) honoured++;
            else if (rejected) (*rejected)++;
        }
        p = close + 2;
    }
    return honoured;
}

// ---------------------------------------------------------------------------
// Per-row metrics: one row's font size and box height
//
// Three sources, in this order of precedence: a row-prefix tag ([[size:NN]] / [[h:NN]]), the
// Row N setting, then the global Font size / Row height. A zero at either of the first two
// means "not set", which is the whole of the inheritance rule.
//
// The tags are read from the *start* of the row only. A row's height is a property of the
// row, so a tag in the middle of the text would have to mean "this row changes height
// halfway through", which has no sensible meaning; it stays ordinary text instead.
// ---------------------------------------------------------------------------
static const int kMinRowHeight = 18;        // the Row height setting's own clamp
static const int kMaxRowHeight = 80;
static const int kMinRowFontSize = 9;       // the Font size setting's own clamp
static const int kMaxRowFontSize = 48;
// The vertical slack the shipped defaults have (row height 30 for a 15 px font). A row needs
// about that much room for a descender plus a ring, so it is also the floor for a row whose
// font has grown: without it a 30 px font in a 30 px row overlaps its neighbours, because
// GDI+ does not clip a glyph to the layout box.
static const int kRowHeightFontSlack = 15;

// One metric tag, e.g. [[h:44]], at p. Only digits are accepted, so [[h:4x]] stays text.
static bool ParseMetricTag(const WCHAR *p, const WCHAR *name, int *out) {
    const size_t nameLen = wcslen(name);
    if (p[0] != L'[' || p[1] != L'[') return false;
    if (_wcsnicmp(p + 2, name, nameLen) != 0) return false;
    if (p[2 + nameLen] != L':') return false;

    const WCHAR *digits = p + 3 + nameLen;
    const WCHAR *close = wcsstr(digits, L"]]");
    if (!close || close == digits) return false;

    int value = 0;
    for (const WCHAR *c = digits; c < close; c++) {
        if (*c < L'0' || *c > L'9') return false;
        value = value * 10 + (int)(*c - L'0');
        if (value > 999) return false;      // not a size anyone meant; treat it as text
    }
    *out = value;
    return true;
}

// Reads the row-prefix tags and returns the first character that is not part of one, so the
// caller can store the row's text without them. A repeated tag overwrites the earlier one.
static const WCHAR *ParseRowMetricTags(const WCHAR *row, int *fontSize, int *height) {
    *fontSize = 0;
    *height = 0;
    if (!row) return L"";

    const WCHAR *p = row;
    for (;;) {
        int value = 0;
        // Height is asked first because it is the row's own property, while a size tag only
        // says how big the content is. The two write separate fields, so the order cannot
        // make one depend on the other -- reading the height first just keeps that visible.
        if (ParseMetricTag(p, L"h", &value)) {
            *height = value;
        } else if (ParseMetricTag(p, L"size", &value)) {
            *fontSize = value;
        } else {
            break;
        }
        p = wcsstr(p + 2, L"]]") + 2;
    }
    return p;
}

// Parses the digits of a [[size:NN]] tag body, [begin,end). Digits only, and the value is
// clamped to what the painter can draw -- the same rule the row-prefix form uses. Returns
// false when there is nothing numeric to read, so the caller leaves the tag as text.
static bool ParseInlineSize(const WCHAR *begin, const WCHAR *end, int *out) {
    if (!begin || !end || end <= begin) return false;
    int value = 0;
    int digits = 0;
    for (const WCHAR *p = begin; p < end; p++) {
        if (*p < L'0' || *p > L'9') return false;
        value = value * 10 + (int)(*p - L'0');
        if (++digits > 3) return false;
    }
    *out = ClampInt(value, kMinRowFontSize, kMaxRowFontSize);
    return true;
}

// The largest [[size:NN]] run in a row's text, or 0 when it has none. A row's height has to
// fit the biggest text in it, so this is what keeps a large run from spilling into the row
// below: without it the floor would be computed from the row's own size only.
static int MaxInlineRunSize(const WCHAR *row) {
    int largest = 0;
    if (!row) return 0;
    const WCHAR *p = row;
    while (*p) {
        if (p[0] != L'[' || p[1] != L'[') {
            p++;
            continue;
        }
        const WCHAR *close = wcsstr(p + 2, L"]]");
        if (!close) break;
        // "[[/size]]" has a five-character body and is not a size tag.
        if ((int)(close - (p + 2)) > 5 && _wcsnicmp(p + 2, L"size:", 5) == 0) {
            int size = 0;
            if (ParseInlineSize(p + 7, close, &size) && size > largest) largest = size;
        }
        p = close + 2;
    }
    return largest;
}

struct ModSettings {
    WCHAR rows[kMaxRows][256] = {};
    int rowCount = 0;
    // ClickAction == Toggle: a click shows or hides the panel (default).
    // ClickAction == Proximity: a click switches between mouse-activated and
    // always shown, which is a display *mode*, not a visibility setting.
    bool clickToggle = true;
    int monitorMode = 0;           // 0 topmost, 1 primary
    int panelWidth = 420;
    int rowHeight = 30;
    int cornerRadius = 22;
    int borderThickness = 1;
    int fontSize = 15;
    WCHAR fontFamily[64] = L"Segoe UI";
    // Per-row overrides, paired with the *published* row order: empty rows are skipped, so the
    // Nth row on screen is not necessarily the Nth Row setting, and every per-row setting is
    // read against the settings number but stored at the row that ends up being drawn. 0 in
    // either means "inherit the global setting".
    int rowFontSize[kMaxRows] = {};
    int rowHeightPx[kMaxRows] = {};
    // The largest text that appears in each row: its own size, or the largest [[size:NN]] run
    // inside it. The row's height floor comes from this, not from the row's own size, or a
    // big run would be allowed to overflow into the row below.
    int rowMaxFontSize[kMaxRows] = {};
    // Global candidate list for characters the main family cannot draw ("Fallback fonts"),
    // plus the chains registered from [[font:...]] tags found in the rows. A count of zero
    // means "not configured", which is what keeps the whole multi-font path switched off.
    FontChain fallbackFonts;
    FontTag tagFonts[kMaxFontChains];
    int tagFontCount = 0;
    bool rowFontTag[kMaxRows] = {};
    DWORD background = 0xE6312F30;
    DWORD border = 0xFFC6CA4C;
    DWORD borderCollapsed = 0xFF6A6C3A;
    DWORD text = 0xFFF2F2F2;
    int ringSize = 26;
    int ringThickness = 4;
    int ringRightOffset = 12;
    int updateMs = 1000;
    int hotZoneDepth = 8;
    int hideDelayMs = 700;

    // External data providers. Off by default, and even when on nothing is
    // created unless a row actually references {ext.*} (see RequiredGroups).
    bool externalData = false;
    bool islandEnabled = false;      // cost of the event channel is zero until an EVENT arrives
    WCHAR extProviders[kExtProviderListChars] = {};
    int extValueTtlMs = 30000;
    int extMaxValue = 128;
    int extMaxProviders = kMaxExtProviders;
    int extMaxUpdates = 100;
    int extEventMs = 6000;
    int extEventQueue = 4;
    int islandMinWidth = 260;        // floor for the card's width; text widens it

    // Left icon plates and intro animation. One icon per row, row-aligned with the
    // RowN settings.
    IconSpec icons[kMaxRows] = {};
    int iconCount = 0;             // slots that will draw a plate
    // Drawn row -> the settings row whose icon slot belongs to it, stored as (settings row + 1),
    // 0 meaning "no mapping: this row is its own slot". The two orders differ as soon as a row
    // is left empty, and they must not be conflated: the *settings* are keyed by row number
    // (`Icon3` belongs to `Row 3`, as the README says), while the painter only knows the drawn
    // order. A slot that belongs to a row with no text is still a slot -- it is what every
    // other row inherits from, so dropping it made `Icon1 = Memory` on its own leave the whole
    // panel without icons.
    int iconSlotOfRow[kMaxRows] = {};
    // A custom icon file that applies to every row ("Custom icon file"). A row's own
    // IconN path wins over it; a row that says None gets no icon at all.
    WCHAR iconPath[MAX_PATH] = L"";
    // The glyph an image falls back to when it cannot be decoded: the first glyph the
    // user chose, or Memory when the icons are images only, or 0 when there are none.
    int iconFallbackStyle = 0;
    int introStyle = 0;            // 0 slide, 1 fade, 2 none
    int introMs = 1200;
    int rippleCount = 2;

    // Appearance preset and per-colour overrides.
    int themeIndex = 0;            // index into kThemes, -1 for "Custom"
    DWORD accent = kAccent;
    DWORD ringColor = 0;           // valid only when ringColorUserSet
    bool ringColorUserSet = false;
    bool userSetBackground = false;
    bool userSetBorder = false;
    bool userSetBorderCollapsed = false;
    bool userSetText = false;
    bool userSetAccent = false;
};

// The font size one row is drawn at, and the height its box gets. The height never falls
// below what that row's font needs (MinimumRowHeightForFont), so growing a row's font grows
// its row rather than letting the glyphs spill into the next one.
static int MinimumRowHeightForFont(int fontSize) {
    return fontSize + kRowHeightFontSlack;
}

static int RowFontSizePx(const ModSettings &settings, int row) {
    const int wanted = (row >= 0 && row < kMaxRows && settings.rowFontSize[row] > 0)
                           ? settings.rowFontSize[row]
                           : settings.fontSize;
    return ClampInt(wanted, kMinRowFontSize, kMaxRowFontSize);
}

static int RowHeightPx(const ModSettings &settings, int row) {
    const int wanted = (row >= 0 && row < kMaxRows && settings.rowHeightPx[row] > 0)
                           ? settings.rowHeightPx[row]
                           : settings.rowHeight;
    // The floor comes from the largest text in the row, so a [[size:24]] run inside an
    // otherwise small row still gets a box that can hold it.
    const int largest = (row >= 0 && row < kMaxRows && settings.rowMaxFontSize[row] > 0)
                            ? settings.rowMaxFontSize[row]
                            : RowFontSizePx(settings, row);
    return MaxInt(wanted, MinimumRowHeightForFont(largest));
}

// Top edge of one row, and the height of `count` rows together. These two are the single
// source the painter, the icon geometry and the intro's target height all read, so they
// cannot disagree about where a row starts or how tall the panel is.
static int RowTopPx(const ModSettings &settings, int row) {
    int top = 0;
    for (int i = 0; i < row && i < kMaxRows; i++) top += RowHeightPx(settings, i);
    return top;
}

static int PanelRowsHeight(const ModSettings &settings, int count) {
    int height = 0;
    for (int i = 0; i < count && i < kMaxRows; i++) height += RowHeightPx(settings, i);
    return height;
}

static void ReadSettings(ModSettings &s) {
    // Settings row number (1-based, the `RowN` key) -> the index the row is published at, or
    // -1 for a row that was left empty. The two differ as soon as a row is skipped, and every
    // per-row setting is *written* against the settings number while everything the painter
    // touches is indexed by the published one, so the mapping has to be explicit.
    int rowSlotOf[kMaxRows];
    for (int i = 0; i < kMaxRows; i++) rowSlotOf[i] = -1;

    s.rowCount = 0;
    for (int i = 0; i < kMaxRows; i++) {
        WCHAR key[16];
        swprintf(key, 16, L"Row%d", i + 1);
        PCWSTR value = Wh_GetStringSetting(key);
        if (value && value[0]) {
            // Keep only non-empty rows, preserving their relative order.
            const int slot = s.rowCount;
            rowSlotOf[i] = slot;

            // The row's own metrics can be written at the start of its text. Parse them out
            // and store what is left, so neither the painter nor the token expander has to
            // know these tags exist.
            int tagFontSize = 0, tagHeight = 0;
            const WCHAR *text = ParseRowMetricTags(value, &tagFontSize, &tagHeight);
            wcsncpy(s.rows[slot], text, 255);
            s.rows[slot][255] = L'\0';

            WCHAR sizeKey[24];
            WCHAR heightKey[24];
            swprintf(sizeKey, 24, L"Row%dFontSize", i + 1);
            swprintf(heightKey, 24, L"Row%dHeight", i + 1);
            const int settingSize = ClampInt((int)Wh_GetIntSetting(sizeKey), 0, kMaxRowFontSize);
            const int settingHeight = ClampInt((int)Wh_GetIntSetting(heightKey), 0, kMaxRowHeight);

            // A tag wins over the setting, and both are clamped to what the painter can draw.
            s.rowFontSize[slot] = tagFontSize > 0
                                      ? ClampInt(tagFontSize, kMinRowFontSize, kMaxRowFontSize)
                                      : settingSize;
            s.rowHeightPx[slot] = tagHeight > 0
                                      ? ClampInt(tagHeight, kMinRowHeight, kMaxRowHeight)
                                      : settingHeight;
            // The prefix tags are gone from the stored text, so anything left in it that looks
            // like [[size:NN]] is an inline run -- and that run may be bigger than the row.
            s.rowMaxFontSize[slot] = MaxInt(s.rowFontSize[slot], MaxInlineRunSize(s.rows[slot]));
            // Which settings row this drawn row came from, so the icon slots (which are keyed
            // by settings row number) can be looked up from the drawn order.
            s.iconSlotOfRow[slot] = i + 1;
            s.rowCount++;
        }
        Wh_FreeStringSetting(value);
    }
    for (int i = s.rowCount; i < kMaxRows; i++) {
        s.rows[i][0] = L'\0';
        s.rowFontSize[i] = 0;
        s.rowHeightPx[i] = 0;
        s.rowMaxFontSize[i] = 0;
    }

    PCWSTR clickAction = Wh_GetStringSetting(L"ClickAction");
    s.clickToggle = !clickAction || wcscmp(clickAction, L"Proximity") != 0;
    Wh_FreeStringSetting(clickAction);

    PCWSTR monitor = Wh_GetStringSetting(L"Monitor");
    s.monitorMode = 0;
    if (monitor && wcscmp(monitor, L"Primary") == 0) s.monitorMode = 1;
    Wh_FreeStringSetting(monitor);

    PCWSTR font = Wh_GetStringSetting(L"FontFamily");
    if (font && font[0]) {
        wcsncpy(s.fontFamily, font, 63);
        s.fontFamily[63] = L'\0';
        // A family that is not installed falls back to Segoe UI rather than
        // silently rendering in whatever GDI+ picks.
        if (!FontIsInstalled(s.fontFamily)) {
            Wh_Log(L"Font '%s' is not installed; using Segoe UI", s.fontFamily);
            wcsncpy(s.fontFamily, L"Segoe UI", 63);
            s.fontFamily[63] = L'\0';
        }
    }
    Wh_FreeStringSetting(font);

    // "Fallback fonts": a ';'-separated candidate list, consulted (after the main family)
    // for the characters the main family cannot draw. An empty list is the default and the
    // cheapest configuration there is: it is half of the cost gate for the whole feature.
    PCWSTR fallback = Wh_GetStringSetting(L"FallbackFonts");
    int fallbackRejected = 0;
    if (fallback) {
        ParseFontChainList(fallback, fallback + wcslen(fallback), s.fallbackFonts,
                           &fallbackRejected);
    } else {
        s.fallbackFonts.count = 0;
    }
    int fallbackMissing = 0;
    FilterChainToInstalled(s.fallbackFonts, &fallbackMissing);
    Wh_FreeStringSetting(fallback);
    if (fallbackRejected || fallbackMissing) {
        // Bounded by construction: this runs once per settings change, not per frame.
        Wh_Log(L"Fallback fonts: kept %d, dropped %d past the %d-name limit, %d not installed",
               s.fallbackFonts.count, fallbackRejected, kMaxFontFamilies, fallbackMissing);
    }

    // [[font:...]] tags are collected from the rows themselves, so that expanding a row can
    // encode its tag as an index instead of a name. Each chain is registered once and shared
    // by every row that writes it the same way.
    s.tagFontCount = 0;
    for (int i = 0; i < kMaxRows; i++) s.rowFontTag[i] = false;
    int tagRejected = 0;
    for (int i = 0; i < s.rowCount; i++) {
        if (CollectRowFontTags(s.rows[i], s.tagFonts, &s.tagFontCount, &tagRejected) > 0) {
            s.rowFontTag[i] = true;
        }
    }
    if (tagRejected) {
        Wh_Log(L"%d [[font:...]] tag(s) ignored: unusable or uninstalled name, or more than "
               L"%d distinct chains; they will draw as ordinary text",
               tagRejected, kMaxFontChains);
    }

    s.panelWidth = ClampInt((int)Wh_GetIntSetting(L"PanelWidth"), 200, 1600);
    s.rowHeight = ClampInt((int)Wh_GetIntSetting(L"RowHeight"), 18, 80);
    s.cornerRadius = ClampInt((int)Wh_GetIntSetting(L"CornerRadius"), 0, 60);
    s.borderThickness = ClampInt((int)Wh_GetIntSetting(L"BorderThickness"), 0, 8);
    s.fontSize = ClampInt((int)Wh_GetIntSetting(L"FontSize"), 9, 48);
    s.ringSize = ClampInt((int)Wh_GetIntSetting(L"RingSize"), 12, 64);
    s.ringThickness = ClampInt((int)Wh_GetIntSetting(L"RingThickness"), 1, 16);
    s.ringRightOffset = ClampInt((int)Wh_GetIntSetting(L"RingRightOffset"), 0, 200);
    s.updateMs = ClampInt((int)Wh_GetIntSetting(L"UpdateMs"), 200, 10000);
    s.hotZoneDepth = ClampInt((int)Wh_GetIntSetting(L"HotZoneDepth"), 2, 64);
    s.hideDelayMs = ClampInt((int)Wh_GetIntSetting(L"HideDelayMs"), 0, 10000);
    s.introMs = ClampInt((int)Wh_GetIntSetting(L"IntroMs"), 200, 4000);
    s.rippleCount = ClampInt((int)Wh_GetIntSetting(L"RippleCount"), 0, 3);

    PCWSTR introStyle = Wh_GetStringSetting(L"IntroStyle");
    s.introStyle = 0;
    if (introStyle) {
        if (wcscmp(introStyle, L"Fade") == 0) s.introStyle = 1;
        else if (wcscmp(introStyle, L"None") == 0) s.introStyle = 2;
    }
    Wh_FreeStringSetting(introStyle);

    // Icons are read as a row-aligned list: Icon1/Icon1Path .. Icon{kMaxRows}, plus
    // one optional file that applies to every row (IconPath). An entry counts as
    // configured when it names a glyph (including None, which is a choice) or
    // supplies an image path.
    //
    // Every slot is read, including the ones whose row is empty: `Icon3` belongs to `Row 3`
    // whether or not that row has text, and an empty row's slot is still what the other rows
    // inherit from (see IconSlotForRow). Which drawn row each slot belongs to is recorded in
    // the row loop above.
    s.iconCount = 0;
    for (int i = 0; i < kMaxRows; i++) {
        IconSpec &slot = s.icons[i];
        slot.style = 0;
        slot.styleSet = false;
        slot.path[0] = L'\0';

        WCHAR key[32];
        swprintf(key, 32, L"Icon%d", i + 1);
        PCWSTR styleName = Wh_GetStringSetting(key);
        if (styleName && styleName[0]) {
            slot.styleSet = true;
            slot.style = IconStyleFromName(styleName);
        }
        Wh_FreeStringSetting(styleName);

        swprintf(key, 32, L"Icon%dPath", i + 1);
        PCWSTR path = Wh_GetStringSetting(key);
        if (path && path[0]) {
            CopyUserPath(slot.path, MAX_PATH, path);
            if (slot.path[0] == L'\0') {
                // The field had content but nothing usable survived trimming.
                Wh_Log(L"Icon%d: the icon path is empty after trimming "
                       L"quotes and spaces; ignoring it", i + 1);
            }
        }
        Wh_FreeStringSetting(path);

        if (slot.style > 0 || slot.path[0]) s.iconCount++;
    }

    // The global icon file, if any. Read after the slots so the fallback below can tell
    // whether there is a glyph anywhere to fall back on.
    {
        PCWSTR global = Wh_GetStringSetting(L"IconPath");
        s.iconPath[0] = L'\0';
        if (global && global[0]) {
            CopyUserPath(s.iconPath, MAX_PATH, global);
            if (s.iconPath[0] == L'\0') {
                Wh_Log(L"Icons: the global icon path is empty after trimming quotes and "
                       L"spaces; ignoring it");
            }
        }
        Wh_FreeStringSetting(global);
    }

    // The glyph an image falls back on when it cannot be decoded, otherwise the row shows
    // an empty plate -- which reads as "the icon turned white" and sends the reader off in
    // the wrong direction. Resolved here, once, rather than by writing a style into the
    // image-only slots: the global image does not belong to any slot, so the rule has to
    // live where both cases can see it.
    s.iconFallbackStyle = 0;
    for (int i = 0; i < kMaxRows; i++) {
        if (s.icons[i].style > 0) {
            s.iconFallbackStyle = s.icons[i].style;
            break;
        }
    }
    if (!s.iconFallbackStyle && (s.iconCount > 0 || s.iconPath[0])) {
        s.iconFallbackStyle = 2;   // Memory
        Wh_Log(L"Icons: no built-in glyph was chosen, so an image that cannot be loaded "
               L"falls back to the Memory glyph");
    }

    // Palette resolution. The two sources are mutually exclusive on purpose:
    //
    //   Theme = Endfield / Midnight / Mono  ->  that preset is used, and the
    //                                           colour fields below are ignored
    //   Theme = Custom                      ->  the colour fields are used
    //
    // Letting the fields win over a named theme would mean the preset could never
    // be seen on a config that had ever stored a colour, which is exactly the
    // complaint that produced this arrangement.
    PCWSTR theme = Wh_GetStringSetting(L"Theme");
    s.themeIndex = 0;
    if (theme && wcscmp(theme, L"Custom") == 0) {
        s.themeIndex = -1;
    } else if (theme) {
        for (int i = 0; i < kThemeCount; i++) {
            if (wcscmp(theme, kThemes[i].name) == 0) {
                s.themeIndex = i;
                break;
            }
        }
    }
    Wh_FreeStringSetting(theme);

    // Read the custom fields first, so "was it set?" is known before choosing.
    struct ColorKey { PCWSTR key; DWORD *target; bool *userSet; };
    ColorKey colorKeys[] = {
        {L"BackgroundColor", &s.background, &s.userSetBackground},
        {L"BorderColor", &s.border, &s.userSetBorder},
        {L"BorderCollapsedColor", &s.borderCollapsed, &s.userSetBorderCollapsed},
        {L"TextColor", &s.text, &s.userSetText},
        {L"AccentColor", &s.accent, &s.userSetAccent},
        {L"RingColor", &s.ringColor, &s.ringColorUserSet},
    };
    for (const ColorKey &ck : colorKeys) {
        *ck.userSet = false;
        PCWSTR raw = Wh_GetStringSetting(ck.key);
        if (raw && raw[0]) {
            const WCHAR *p = raw;
            while (*p == L' ' || *p == L'\t') p++;
            if (*p == L'#') p++;
            WCHAR *end = nullptr;
            unsigned long long v = wcstoull(p, &end, 16);
            if (end && end != p && v <= 0xFFFFFFFFull) {
                *ck.target = (DWORD)v;
                *ck.userSet = true;
            }
        }
        Wh_FreeStringSetting(raw);
    }

    if (s.themeIndex >= 0 && s.themeIndex < kThemeCount) {
        // A named theme wins outright: the colour fields are not consulted. The
        // ring-colour flag is cleared here rather than merely being unused, so
        // there is no path by which a stale field can leak into a preset palette.
        const ThemePreset &preset = kThemes[s.themeIndex];
        s.background = preset.background;
        s.border = preset.border;
        s.borderCollapsed = preset.borderCollapsed;
        s.text = preset.text;
        s.accent = preset.accent;
        s.ringColor = 0;
        s.ringColorUserSet = false;
    } else {
        // Custom: a field left empty falls back to the Endfield value, so an
        // incomplete custom palette still produces something coherent.
        const ThemePreset &fallback = kThemes[0];
        if (!s.userSetBackground) s.background = fallback.background;
        if (!s.userSetBorder) s.border = fallback.border;
        if (!s.userSetBorderCollapsed) s.borderCollapsed = fallback.borderCollapsed;
        if (!s.userSetText) s.text = fallback.text;
        if (!s.userSetAccent) s.accent = fallback.accent;
    }

    // External data providers. A string enum rather than 0/1, matching every other
    // non-numeric choice in this settings block (Theme, ClickAction, IntroStyle).
    PCWSTR externalData = Wh_GetStringSetting(L"ExternalData");
    s.externalData = externalData && wcscmp(externalData, L"On") == 0;
    Wh_FreeStringSetting(externalData);

    PCWSTR island = Wh_GetStringSetting(L"IslandEnabled");
    s.islandEnabled = island && wcscmp(island, L"On") == 0;
    Wh_FreeStringSetting(island);

    PCWSTR providers = Wh_GetStringSetting(L"ExtProviders");
    if (providers && providers[0]) {
        wcsncpy(s.extProviders, providers, kExtProviderListChars - 1);
        s.extProviders[kExtProviderListChars - 1] = L'\0';
    } else {
        s.extProviders[0] = L'\0';
    }
    Wh_FreeStringSetting(providers);

    s.extValueTtlMs = ClampInt((int)Wh_GetIntSetting(L"ExtValueTtlMs"), 100, 600000);
    s.extMaxValue = ClampInt((int)Wh_GetIntSetting(L"ExtMaxValue"), 16, kExtMaxValueChars);
    s.extMaxProviders = ClampInt((int)Wh_GetIntSetting(L"ExtMaxProviders"), 1, kMaxExtProviders);
    s.extMaxUpdates = ClampInt((int)Wh_GetIntSetting(L"ExtMaxUpdates"), 1, 1000);
    s.extEventMs = ClampInt((int)Wh_GetIntSetting(L"ExtEventMs"), 1000, 60000);
    s.extEventQueue = ClampInt((int)Wh_GetIntSetting(L"ExtEventQueue"), 1, 8);
    s.islandMinWidth = ClampInt((int)Wh_GetIntSetting(L"IslandMinWidth"), 120, 640);
}

// Per-row icon resolution.
//
// A row with no icon of its own inherits the first configured one, which makes "set
// Icon1 only" mean "this glyph on every row", while Icon2..Icon4 override individual
// rows. On top of that there is one optional global image (`IconPath`) that applies to
// every row. The rules for the two live in the three functions below, in one place,
// because they interlock (see ResolveIconPath).
//
// "Configured" is deliberately not one predicate: a row can choose a glyph (including
// `None`, which is a choice -- "this row has no icon" -- and not an absence) and/or name
// a file, and the two are consulted separately. `IconSpec::styleSet` is what separates an
// explicit `None` from "never set".

// The settings slot whose icon belongs to a drawn row, or -1 when there is none.
//
// `ReadSettings` records the mapping; a `ModSettings` built by hand (the regression host does
// that) has none, and then a drawn row is its own slot -- which is also the right answer for
// every configuration that has no empty row at all.
static int IconSlotForRow(const ModSettings &settings, int row) {
    if (row < 0 || row >= kMaxRows) return -1;
    const int mapped = settings.iconSlotOfRow[row];
    return mapped > 0 ? mapped - 1 : row;
}

// The glyph a row falls back on when it has no (working) image. Highest first:
//
//   1. the glyph the row chose itself (`None` means none, and must not inherit)
//   2. the shared fallback for an image-only row: the first glyph anywhere, else Memory
//   3. otherwise the first glyph anywhere, for rows that said nothing at all
//
// Resolved rather than written into the slots, so the global image -- which belongs to
// no slot -- gets the same fallback as a row's own file.
static int ResolveIconStyle(const ModSettings &settings, int index) {
    const int slot = IconSlotForRow(settings, index);
    if (slot >= 0) {
        const IconSpec &rowIcon = settings.icons[slot];
        if (rowIcon.style > 0 || rowIcon.styleSet) return rowIcon.style;
        if (rowIcon.path[0]) return settings.iconFallbackStyle;
    }
    for (int i = 0; i < kMaxRows; i++) {
        if (settings.icons[i].style > 0) return settings.icons[i].style;
    }
    return settings.iconFallbackStyle;
}

// The image file a row shows, or "" when it shows a glyph. Highest first:
//
//   1. the row's own `Row N icon file`
//   2. nothing, when the row says `None`: no icon at all, not even the global one
//   3. the global `IconPath`, which is the panel-wide default for every row
//   4. a file another row set -- the documented "set Icon1 only and every row uses it"
//      inheritance, which only comes into play when there is no global image (with one,
//      the per-row file is the exception, so it must not leak to the other rows)
static const WCHAR *ResolveIconPath(const ModSettings &settings, int index) {
    const int slot = IconSlotForRow(settings, index);
    if (slot >= 0) {
        const IconSpec &rowIcon = settings.icons[slot];
        if (rowIcon.path[0]) return rowIcon.path;
        if (rowIcon.styleSet && rowIcon.style <= 0) return L"";
    }
    if (settings.iconPath[0]) return settings.iconPath;
    for (int i = 0; i < kMaxRows; i++) {
        if (settings.icons[i].path[0]) return settings.icons[i].path;
    }
    return L"";
}

// Which cache slot holds the image for a row. Rows that resolve to the same file share
// one slot -- the lowest row that wants it -- so a file is decoded and resampled once
// however many rows show it, which is the normal case for the global icon. The sharing
// is done at resolution time instead of by adding a slot or by reference counting, so
// every file still has exactly one owner and the per-slot dispose path is unchanged.
// Returns -1 when the row shows no image at all.
static int ResolveIconSlot(const ModSettings &settings, int index) {
    const WCHAR *path = ResolveIconPath(settings, index);
    if (!path || !path[0]) return -1;
    for (int i = 0; i < index && i < kMaxRows; i++) {
        const WCHAR *earlier = ResolveIconPath(settings, i);
        if (earlier && earlier[0] && _wcsicmp(earlier, path) == 0) return i;
    }
    return index;
}

// ---------------------------------------------------------------------------
// Variable registry
// ---------------------------------------------------------------------------
enum Group : unsigned {
    G_NONE = 0,
    G_CPU = 1 << 0,
    G_MEM = 1 << 1,
    G_NET = 1 << 2,
    G_GPU = 1 << 3,
    G_SYS = 1 << 4,   // clock / date / uptime, always collected (free)
    G_EXT = 1 << 5,   // external providers; these are pushed, not collected
};

struct VarDef {
    const WCHAR *name;
    unsigned group;
};

// Registry order also documents the token list shown in the readme.
static const VarDef kVariables[] = {
    {L"cpu", G_CPU}, {L"cpu.user", G_CPU}, {L"cpu.kernel", G_CPU}, {L"cpu.ghz", G_CPU},
    {L"cpu.ghz.max", G_CPU}, {L"cpu.name", G_CPU}, {L"cpu.cores", G_CPU}, {L"cpu.threads", G_CPU},
    {L"mem", G_MEM}, {L"mem.used", G_MEM}, {L"mem.total", G_MEM}, {L"mem.free", G_MEM},
    {L"mem.commit", G_MEM},
    {L"net.down", G_NET}, {L"net.up", G_NET}, {L"net.down.bps", G_NET}, {L"net.up.bps", G_NET},
    {L"net.total", G_NET}, {L"net.nics", G_NET},
    {L"gpu", G_GPU}, {L"gpu.mem", G_GPU}, {L"gpu.temp", G_GPU}, {L"gpu.name", G_GPU},
    {L"gpu.vram.used", G_GPU}, {L"gpu.vram.total", G_GPU}, {L"gpu.fan", G_GPU}, {L"gpu.power", G_GPU},
    {L"time", G_SYS}, {L"date", G_SYS}, {L"uptime", G_SYS},
    {L"mod.ws", G_SYS}, {L"mod.cpu", G_SYS},
};
static const int kVariableCount = (int)(sizeof(kVariables) / sizeof(kVariables[0]));

static unsigned GroupForName(const WCHAR *name, int nameLen) {
    for (int i = 0; i < kVariableCount; i++) {
        if ((int)wcslen(kVariables[i].name) == nameLen &&
            _wcsnicmp(kVariables[i].name, name, nameLen) == 0) {
            return kVariables[i].group;
        }
    }
    return G_NONE;
}

// Which groups does the configured text actually reference?
static unsigned RequiredGroups(const ModSettings &s) {
    unsigned groups = 0;
    for (int r = 0; r < s.rowCount; r++) {
        const WCHAR *p = s.rows[r];
        while (*p) {
            if (*p == L'{') {
                const WCHAR *end = wcschr(p + 1, L'}');
                if (!end) break;
                const WCHAR *pipe = p + 1;
                while (pipe < end && *pipe != L'|') pipe++;
                int nameLen = (int)(pipe - (p + 1));
                // {ext.*} names only exist at run time, so the static registry
                // cannot answer them; the prefix is what selects the group.
                groups |= ExtIsExtToken(p + 1, nameLen) ? G_EXT
                                                        : GroupForName(p + 1, nameLen);
                p = end + 1;
            } else {
                p++;
            }
        }
    }
    return groups;
}

// ---------------------------------------------------------------------------
// Collected values
// ---------------------------------------------------------------------------
struct Values {
    double cpu = 0, cpuUser = 0, cpuKernel = 0;
    double cpuGhz = 0, cpuGhzMax = 0;
    WCHAR cpuName[64] = L"CPU";
    int cores = 0, threads = 0;

    double memPercent = 0, memUsedGb = 0, memTotalGb = 0, memFreeGb = 0, memCommit = 0;

    double netDownBps = 0, netUpBps = 0, netTotalBytes = 0;
    int netNics = 0;

    bool gpuValid = false;
    double gpu = 0, gpuMem = 0, gpuTemp = 0;
    // Temperature is a separate capability: NVML reads a real sensor, the PDH
    // counter path has no equivalent. Without this flag a PDH-only machine
    // reported "0C", which looks like a real (and alarming) reading.
    bool gpuTempValid = false;
    // Fan, power and the VRAM counters are read one by one, and the PDH fallback has no
    // equivalent for the first two or for the total. They carried no flag, so "this
    // machine cannot read it" was rendered as a very real-looking 0 -- the same failure
    // gpuTempValid exists to prevent, one field over.
    bool gpuFanValid = false, gpuPowerValid = false;
    bool vramUsedValid = false, vramTotalValid = false;
    double vramUsedGb = 0, vramTotalGb = 0, gpuFan = 0, gpuPower = 0;
    WCHAR gpuName[64] = L"GPU";

    WCHAR timeText[16] = L"";
    WCHAR dateText[16] = L"";
    WCHAR uptimeText[24] = L"";

    double modWsMb = 0, modCpuMs = 0;
};

static CRITICAL_SECTION g_valuesLock;       // guards g_rowState and g_publishedRows
// Cleared when the sampler could not be joined at unload, in which case the lock is left
// to the process exit instead of being deleted under a thread that may still hold it.
static bool g_valuesLockUsable = true;
static Values g_values;                     // published snapshot
static unsigned g_activeGroups = 0;

// ---------------------------------------------------------------------------
// External data: named-pipe providers
//
// Any process can publish text into {ext.provider.key} tokens. The values live in
// a fixed slot table: nothing is allocated, nothing runs unless a row references
// {ext.*} (see RequiredGroups), and the pipe is not created until then either.
//
// Concurrency (see docs/external-data-island-design.md 5.6):
//   * the pipe thread writes the table, and it takes g_extLock itself;
//   * the sampler thread takes g_extLock around expanding the rows, so a lookup
//     never sees a half-written entry;
//   * the helpers below take NO lock. That is what lets the regression host call
//     them directly -- it never runs WhTool_ModInit, so no critical section is
//     initialised in that process.
//   * the HUD thread never touches g_extLock. It only reads the two atomic
//     LONGs below, which is what keeps the lock graph acyclic.
// ---------------------------------------------------------------------------
struct ExtEntry {
    WCHAR  key[kExtKeyChars + 1];
    WCHAR  text[kExtMaxValueChars + 1];   // raw text as sent, sanitised
    DWORD  updatedAt;                     // GetTickCount()
    DWORD  ttlMs;                         // 0 = invalidated
};
struct ExtProvider {
    WCHAR    name[kExtProviderChars + 1];
    ExtEntry entries[kMaxExtKeys];
    int      count;
    unsigned drops;                       // bounded counter for the log
};

static ExtProvider      g_ext[kMaxExtProviders];
static CRITICAL_SECTION g_extLock;
static bool             g_extLockReady = false;
static volatile LONG    g_extDirty = 0;        // pipe -> sampler: re-sample now
static volatile LONG    g_extStop = 0;
static volatile LONG    g_extDeadlineMs = 0;   // sampler -> HUD: when to nudge
static HANDLE           g_extThreads[kExtInstances] = {};
static int              g_extThreadCount = 0;
static HANDLE           g_extStopEvent = nullptr;
static bool             g_extStartAttempted = false;
static int              g_extBusyLogs = 0;     // bounded: "all instances busy" messages
static int              g_extStallLogs = 0;    // bounded: "the read never completed" messages
static int              g_extConnLogs = 0;     // bounded: per-connection "what is this pipe" lines
static int              g_extConnectLogs = 0;  // bounded: which branch of the accept path ran
static int              g_extReadErrLogs = 0;  // bounded: why a read gave up

// Does this name select the external namespace at all? Full validation happens in
// ExtLookup, so a malformed {ext...} still reaches the "unknown" path and keeps
// its literal text on screen.
static bool ExtIsExtToken(const WCHAR *name, int len) {
    return name && len > 4 && _wcsnicmp(name, L"ext.", 4) == 0;
}

static bool ExtNameEq(const WCHAR *stored, const WCHAR *name, int len) {
    return (int)wcslen(stored) == len && _wcsnicmp(stored, name, len) == 0;
}

// provider: [A-Za-z0-9_-]{1,16}   key: [A-Za-z0-9_.-]{1,32}
static bool ExtNameValid(const WCHAR *s, int len, bool provider) {
    if (len <= 0 || len > (provider ? kExtProviderChars : kExtKeyChars)) return false;
    for (int i = 0; i < len; i++) {
        WCHAR c = s[i];
        bool ok = (c >= L'a' && c <= L'z') || (c >= L'A' && c <= L'Z') ||
                  (c >= L'0' && c <= L'9') || c == L'_' || c == L'-' ||
                  (!provider && c == L'.');
        if (!ok) return false;
    }
    return true;
}

// One line of text: control characters dropped, tab becomes a space, ends
// trimmed, and a value that does not fit is cut and marked with an ellipsis.
//
// It deliberately does NOT expand {tokens}. That is not an extra guard: the
// expansion pass walks the *template* and copies resolved values into its output
// without rescanning them, so injected text can never become a variable. The
// assertion in the regression test exists so that property cannot be lost
// silently by someone "helpfully" making expansion recursive.
static void ExtSanitize(const WCHAR *src, WCHAR *dst, int cap) {
    if (cap <= 0) return;
    if (cap < 3) { dst[0] = L'\0'; return; }
    int n = 0;
    bool more = false;
    for (const WCHAR *p = src; p && *p; p++) {
        WCHAR c = *p;
        if (c == L'\r' || c == L'\n') continue;
        if (c == L'\t') c = L' ';
        else if (c < 0x20 || c == 0x7F || (c >= 0x80 && c <= 0x9F)) continue;
        if (n >= cap - 2) { more = true; break; }
        dst[n++] = c;
    }
    while (n > 0 && dst[n - 1] == L' ') n--;
    int start = 0;
    while (start < n && dst[start] == L' ') start++;
    if (start > 0) {
        for (int i = 0; i < n - start; i++) dst[i] = dst[start + i];
        n -= start;
    }
    if (more) dst[n++] = 0x2026;   // HORIZONTAL ELLIPSIS
    dst[n] = L'\0';
}

// Strict: [+-]?digits[.digits]? and nothing else. "73%", " 42", "4 2", ".5", "1."
// and "1e3" are all rejected, so a value that only looks numeric never drives a
// ring gauge or a |0 spec.
static bool ExtParseNumber(const WCHAR *text, double &out) {
    if (!text || !*text) return false;
    const WCHAR *p = text;
    if (*p == L'+' || *p == L'-') p++;
    if (!(*p >= L'0' && *p <= L'9')) return false;
    while (*p >= L'0' && *p <= L'9') p++;
    if (*p == L'.') {
        p++;
        if (!(*p >= L'0' && *p <= L'9')) return false;
        while (*p >= L'0' && *p <= L'9') p++;
    }
    if (*p) return false;
    out = _wtof(text);
    return true;
}

// Result of resolving {ext.provider.key}. ER_NONE means the name is not a
// well-formed external token at all; the caller keeps the literal text then.
enum ExtResult { ER_NONE, ER_VALUE, ER_EXPIRED };

// No numeric flag is stored: whether a value can drive a gauge or a |0 spec is
// decided by parsing its text where it is needed (ExtParseNumber for specs,
// wcstod like every other text variable for [[ring:...]]). A stored flag would
// be a second source of truth that can disagree with the text on screen.
static ExtResult ExtLookup(const WCHAR *name, int len, const WCHAR **text) {
    if (!ExtIsExtToken(name, len)) return ER_NONE;
    const WCHAR *body = name + 4;
    int bodyLen = len - 4;
    const WCHAR *dot = nullptr;
    for (int i = 0; i < bodyLen; i++) {
        if (body[i] == L'.') { dot = body + i; break; }
    }
    if (!dot) return ER_NONE;
    int providerLen = (int)(dot - body);
    int keyLen = bodyLen - providerLen - 1;
    if (!ExtNameValid(body, providerLen, true)) return ER_NONE;
    if (!ExtNameValid(dot + 1, keyLen, false)) return ER_NONE;

    for (int i = 0; i < kMaxExtProviders; i++) {
        const ExtProvider &p = g_ext[i];
        if (!p.name[0] || !ExtNameEq(p.name, body, providerLen)) continue;
        for (int k = 0; k < p.count; k++) {
            const ExtEntry &e = p.entries[k];
            if (!ExtNameEq(e.key, dot + 1, keyLen)) continue;
            // A known provider with a missing key is the same on screen as an
            // expired value: "--". Only a malformed name is "unknown".
            if (!e.ttlMs) return ER_EXPIRED;
            if ((DWORD)(GetTickCount() - e.updatedAt) >= e.ttlMs) return ER_EXPIRED;
            if (text) *text = e.text;
            return ER_VALUE;
        }
        return ER_EXPIRED;
    }
    return ER_EXPIRED;
}

// Earliest moment at which some live value stops being live, or 0 when nothing is
// live. The sampler publishes this (it holds g_extLock while expanding); the HUD
// tick reads the atomic and asks for another sample once it has passed.
static DWORD ExtNextDeadline(const ExtProvider *table, int count) {
    DWORD best = 0;
    for (int i = 0; i < count; i++) {
        for (int k = 0; k < table[i].count; k++) {
            const ExtEntry &e = table[i].entries[k];
            if (!e.ttlMs) continue;
            if ((DWORD)(GetTickCount() - e.updatedAt) >= e.ttlMs) continue;
            DWORD at = e.updatedAt + e.ttlMs;
            if (!best || (LONG)(at - best) < 0) best = at;
        }
    }
    return best;
}

// Eviction pick: the entry that expires first, and among equals the one updated
// longest ago. Pure, so the policy is assertable rather than implied by a loop.
static int ExtEvictIndex(const ExtProvider &p) {
    int best = 0;
    for (int i = 1; i < p.count; i++) {
        DWORD a = p.entries[i].updatedAt + p.entries[i].ttlMs;
        DWORD b = p.entries[best].updatedAt + p.entries[best].ttlMs;
        if ((LONG)(a - b) < 0) { best = i; continue; }
        if (a == b && (LONG)(p.entries[i].updatedAt - p.entries[best].updatedAt) < 0) best = i;
    }
    return best;
}

static int ExtFindProvider(const WCHAR *name, int len, bool create, int maxProviders) {
    int limit = maxProviders < kMaxExtProviders ? maxProviders : kMaxExtProviders;
    for (int i = 0; i < limit; i++) {
        if (g_ext[i].name[0] && ExtNameEq(g_ext[i].name, name, len)) return i;
    }
    if (!create) return -1;
    for (int i = 0; i < limit; i++) {
        if (!g_ext[i].name[0]) {
            // Copy by LENGTH: "name" is a slice of the frame line, so wcsncpy from
            // the pointer swallows whatever follows it -- in practice the newline
            // that framed the line. The stored name would then be one character
            // longer than the provider a row asks for, ExtNameEq would never match,
            // and the row would render "--" forever. It also made every reconnect
            // allocate a fresh slot instead of reusing the provider's own.
            int copy = len < kExtProviderChars ? len : kExtProviderChars;
            wcsncpy(g_ext[i].name, name, copy);
            g_ext[i].name[copy] = L'\0';
            g_ext[i].count = 0;
            g_ext[i].drops = 0;
                    return i;
        }
    }
    return -1;
}

// Allowlist: empty means "any provider name", otherwise a semicolon separated
// list. A name list, not a secret -- see the design notes on why there is no
// token.
static bool ExtProviderAllowed(const ModSettings &s, const WCHAR *name, int len) {
    if (!s.extProviders[0]) return true;
    const WCHAR *p = s.extProviders;
    while (*p) {
        const WCHAR *end = p;
        while (*end && *end != L';') end++;
        const WCHAR *start = p;
        while (start < end && (*start == L' ' || *start == L'\t')) start++;
        const WCHAR *stop = end;
        while (stop > start && (stop[-1] == L' ' || stop[-1] == L'\t')) stop--;
        if (stop > start && (int)(stop - start) == len && _wcsnicmp(start, name, len) == 0) return true;
        p = *end ? end + 1 : end;
    }
    return false;
}

// What do the rows actually ask for? "The panel shows --" has three very
// different causes -- the provider never sent that key, the name is spelled
// differently, or no row references {ext.*} at all -- and they look identical on
// screen. One bounded line per settings change (and once at startup) separates
// them. It prints the names the rows reference, which is what the user wrote, not
// any value a provider sent.
static void ExtLogRowReferences(const ModSettings &s) {
    int logged = 0;
    for (int r = 0; r < s.rowCount && logged < 8; r++) {
        const WCHAR *p = s.rows[r];
        while (*p && logged < 8) {
            if (*p != L'{') { p++; continue; }
            const WCHAR *end = wcschr(p + 1, L'}');
            if (!end) break;
            const WCHAR *bar = p + 1;
            while (bar < end && *bar != L'|') bar++;
            int len = (int)(bar - (p + 1));
            if (ExtIsExtToken(p + 1, len)) {
                WCHAR name[64];
                int copy = len < 63 ? len : 63;
                wcsncpy(name, p + 1, copy);
                name[copy] = L'\0';
                Wh_Log(L"EXT row%d wants %s", r + 1, name);
                logged++;
            }
            p = end + 1;
        }
    }
    if (!logged) Wh_Log(L"EXT no row references {ext.*}; the pipe will not be created");
}

// Diagnostic only: the last lookup status seen for a few external names, so a row
// that renders "--" can be attributed in the log. "expired" means the store had the
// name and its value ran out of time; "value" means the text is being rendered (so a
// blank row would be downstream, in layout or paint); "unknown" means the name is
// malformed or the provider never registered. It logs on *change* only, which keeps
// it bounded no matter how often the panel repaints, and the value text is never
// logged.
static const int kExtStatusSlots = 8;
struct ExtStatusSeen {
    WCHAR name[64];
    int   len;
    int   status;         // 1 = value, 0 = expired, -1 = malformed or unregistered
};
static ExtStatusSeen g_extStatusSeen[kExtStatusSlots];

static const WCHAR *ExtStatusName(int status) {
    return status > 0 ? L"value" : (status == 0 ? L"expired" : L"unknown");
}

static void ExtLogStatus(const WCHAR *name, int nameLen, int status) {
    if (nameLen <= 0 || nameLen > 63) return;
    int free = -1;
    for (int i = 0; i < kExtStatusSlots; i++) {
        if (g_extStatusSeen[i].len == nameLen &&
            _wcsnicmp(g_extStatusSeen[i].name, name, nameLen) == 0) {
            if (g_extStatusSeen[i].status == status) return;
            g_extStatusSeen[i].status = status;
            Wh_Log(L"EXT %s -> %s", g_extStatusSeen[i].name, ExtStatusName(status));
            return;
        }
        if (free < 0 && g_extStatusSeen[i].len == 0) free = i;
    }
    if (free < 0) return;                  // table full: stay quiet rather than spam
    wcsncpy(g_extStatusSeen[free].name, name, 63);
    g_extStatusSeen[free].name[63] = L'\0';
    g_extStatusSeen[free].len = nameLen;
    g_extStatusSeen[free].status = status;
    Wh_Log(L"EXT %s -> %s", g_extStatusSeen[free].name, ExtStatusName(status));
}

// ---------------------------------------------------------------------------
// External data: the event queue behind the island (design 7.3)
//
// Written by the pipe threads, read by the HUD, so it has its own lock and never
// nests with g_extLock: the HUD never touches the value store, and a pipe thread
// that enqueues an event touches nothing else. Everything below that decides
// *order* is a pure function, so the rules -- replace your own pending event, merge
// an identical title, prefer the higher severity, drop the weakest when full -- are
// asserted without a window or a pipe.
// ---------------------------------------------------------------------------
static const int    kIslandQueueMax = 8;              // ExtEventQueue clamps to 1..8
static const int    kIslandTitleChars = 96;
static const int    kIslandBodyChars = 160;
static const DWORD  kIslandMergeWindowMs = 2000;
// Phase durations. They live here, not in the drawing code, because the state
// machine is what decides when a phase is over; the visual layer reads the same
// numbers so the two cannot disagree about what "settled" means.
static const DWORD  kIslandPopMs = 220;
static const DWORD  kIslandCollapseMs = 140;
// A card's time on screen, and how long an event may wait for its turn.
//
// The protocol's ttl on an EVENT is a *display* duration (ExtEventMs documents itself
// as the default for `-`), but a publisher's value ttl is often minutes and the mod
// used to take it verbatim: with the bridge sending its 600000 ms value ttl, one card
// held the island for ten minutes, an 8-slot queue held hours, and the card on screen
// was minutes behind the music -- which reads as "the island is broken". Publishers
// that deliberately ask for a short flash keep it; only the runaway upper end is
// capped. Staleness then bounds the *lag*: a dense stream is dropped, not shown late.
static const DWORD  kIslandMaxCardMs = 60000;         // matches ExtEventMs's own range
static const DWORD  kIslandStaleMs = 10000;

static DWORD IslandCardDuration(DWORD eventTtlMs, DWORD defaultMs) {
    DWORD d = eventTtlMs ? eventTtlMs : defaultMs;
    if (d > kIslandMaxCardMs) d = kIslandMaxCardMs;
    return d;
}

enum ExtSeverity { ES_INFO = 0, ES_WARN = 1, ES_CRITICAL = 2 };

struct IslandEvent {
    WCHAR       provider[kExtProviderChars + 1];
    WCHAR       title[kIslandTitleChars];
    WCHAR       body[kIslandBodyChars];
    ExtSeverity severity;
    DWORD       ttlMs;
    unsigned    repeat;
    DWORD       queuedAt;
};

enum IslandState { IS_IDLE = 0, IS_POPPING, IS_CARD, IS_COLLAPSING };

struct IslandView {
    IslandState state;
    DWORD       phaseStarted;
    DWORD       cardDeadline;
    ExtSeverity showing;          // severity of the card on screen
};

static IslandEvent      g_islandQueue[kIslandQueueMax];
static int              g_islandCount = 0;
static CRITICAL_SECTION g_islandLock;
static bool             g_islandLockReady = false;
static unsigned         g_islandDrops = 0;
static unsigned         g_islandAccepted = 0;
static unsigned         g_islandWakes = 0;            // HUD-side: wakes actually seen

// Removes events that have waited too long to still be worth showing, and returns the
// new count. The queue was already bounded by depth; this bounds the *lag*, which is
// what a fast publisher actually breaks: a lyric line per second arrives far faster
// than one card per few seconds, so without this the island would be showing lines
// from a minute ago. Dropped ones are counted, so the log can say how many.
//
// A critical event gets a longer window than an info line: a stale lyric is worthless,
// an alarm is not, and dropping it silently would be the worst of both.
static DWORD IslandStaleBoundMs(ExtSeverity sev) {
    return sev == ES_CRITICAL ? kIslandStaleMs * 4 : kIslandStaleMs;
}

static int IslandDropStale(IslandEvent *queue, int count, DWORD now, unsigned *drops) {
    int kept = 0;
    for (int i = 0; i < count; i++) {
        if ((DWORD)(now - queue[i].queuedAt) > IslandStaleBoundMs(queue[i].severity)) {
            if (drops) (*drops)++;
            continue;
        }
        queue[kept++] = queue[i];
    }
    return kept;
}
// HUD thread only: the state machine's view, the card currently on screen, and a
// bounded log counter. The queue itself stays behind its lock.
static IslandView      g_islandView;
static IslandEvent     g_islandCard;
static bool            g_islandCardValid = false;
static int             g_islandLogs = 0;

static bool ExtParseSeverity(const WCHAR *s, int len, ExtSeverity *out) {
    if (len <= 0 || !s) return false;
    if (len == 4 && _wcsnicmp(s, L"info", 4) == 0) { *out = ES_INFO; return true; }
    if (len == 4 && _wcsnicmp(s, L"warn", 4) == 0) { *out = ES_WARN; return true; }
    if (len == 8 && _wcsnicmp(s, L"critical", 8) == 0) { *out = ES_CRITICAL; return true; }
    return false;
}

// Which queued event to show next: highest severity first, FIFO inside a level.
static int IslandPick(const IslandEvent *queue, int count) {
    int best = -1;
    for (int i = 0; i < count; i++) {
        // Strictly greater, so the earliest of the highest level wins.
        if (best < 0 || queue[i].severity > queue[best].severity) best = i;
    }
    return best;
}

// Adds an event; returns the new count. `drops` counts what was lost, so "the queue
// was full" is visible in the log instead of being silent.
static int IslandEnqueue(IslandEvent *queue, int count, int cap, const IslandEvent &ev,
                         unsigned *drops) {
    if (cap > kIslandQueueMax) cap = kIslandQueueMax;
    if (cap < 1) cap = 1;

    for (int i = 0; i < count; i++) {
        if (_wcsicmp(queue[i].provider, ev.provider) != 0) continue;
        if (_wcsicmp(queue[i].title, ev.title) == 0 &&
            (DWORD)(ev.queuedAt - queue[i].queuedAt) <= kIslandMergeWindowMs) {
            // The same thing again: bump the badge instead of queueing a twin.
            //
            // Severity is the one field that must not be inherited from the first
            // sighting. "The same title, but now critical" is exactly the escalation this
            // window would otherwise swallow: the entry kept its old level, so the state
            // machine never saw a critical waiting and the card stayed at warn. Take the
            // higher of the two -- a repeat can raise the level, never lower it.
            if (ev.severity > queue[i].severity) queue[i].severity = ev.severity;
            // The newest text describes the same thing most accurately.
            wcsncpy(queue[i].body, ev.body, kIslandBodyChars - 1);
            queue[i].body[kIslandBodyChars - 1] = L'\0';
            queue[i].repeat++;
            queue[i].queuedAt = ev.queuedAt;
            queue[i].ttlMs = ev.ttlMs;
            return count;
        }
        queue[i] = ev;            // a provider's own pending event is replaced
        return count;
    }

    if (count < cap) {
        queue[count] = ev;
        return count + 1;
    }

    // Full: the weakest, and among equals the earliest, makes room. A critical must
    // never be the one lost to an info.
    int victim = 0;
    for (int i = 1; i < count; i++) {
        if (queue[i].severity < queue[victim].severity ||
            (queue[i].severity == queue[victim].severity &&
             queue[i].queuedAt < queue[victim].queuedAt)) {
            victim = i;
        }
    }
    if (ev.severity >= queue[victim].severity) queue[victim] = ev;
    if (drops) (*drops)++;
    return count;
}

// The island's state machine (design 7.2). Pure on purpose: the caller supplies the
// clock and whether the current animation phase finished, so every transition is
// assertable. *popOut receives the queue index to present when a card starts.
static IslandState StepIsland(IslandView *v, const IslandEvent *queue, int count,
                              DWORD now, bool phaseDone, DWORD cardMs, int *popOut) {
    if (popOut) *popOut = -1;
    const int pick = IslandPick(queue, count);
    const bool criticalWaiting = pick >= 0 && queue[pick].severity == ES_CRITICAL;

    switch (v->state) {
    case IS_IDLE:
        if (pick >= 0) {
            v->state = IS_POPPING;
            v->phaseStarted = now;
            v->showing = queue[pick].severity;
            v->cardDeadline = now + IslandCardDuration(queue[pick].ttlMs, cardMs);
            if (popOut) *popOut = pick;
        }
        break;
    case IS_POPPING:
        if (criticalWaiting && v->showing < ES_CRITICAL) {
            v->state = IS_COLLAPSING;     // a critical outranks a card still growing
            v->phaseStarted = now;
        } else if (phaseDone) {
            v->state = IS_CARD;
            v->phaseStarted = now;
        }
        break;
    case IS_CARD:
        if (criticalWaiting && v->showing < ES_CRITICAL) {
            v->state = IS_COLLAPSING;
            v->phaseStarted = now;
        } else if ((LONG)(now - v->cardDeadline) >= 0) {
            v->state = IS_COLLAPSING;     // even a critical goes away: no notification centre
            v->phaseStarted = now;
        }
        break;
    case IS_COLLAPSING:
        if (phaseDone) {
            if (pick >= 0) {
                // Straight into the next card: a queue that is not empty must not wait
                // an extra idle tick (design 7.2).
                v->state = IS_POPPING;
                v->phaseStarted = now;
                v->showing = queue[pick].severity;
                v->cardDeadline = now + IslandCardDuration(queue[pick].ttlMs, cardMs);
                if (popOut) *popOut = pick;
            } else {
                v->state = IS_IDLE;
                v->phaseStarted = now;
            }
        }
        break;
    }
    return v->state;
}

static const WCHAR *IslandStateName(IslandState s) {
    switch (s) {
    case IS_POPPING: return L"Popping";
    case IS_CARD: return L"Card";
    case IS_COLLAPSING: return L"Collapsing";
    default: return L"Idle";
    }
}

// Removes the entry that matches `want` (provider + title + timestamp, so a slot that
// was replaced in the meantime simply does not match). Returns false when it is gone,
// which is not an error: the next tick pops whatever replaced it.
static bool IslandTakeMatching(IslandEvent *queue, int *count, const IslandEvent &want) {
    for (int i = 0; i < *count; i++) {
        if (_wcsicmp(queue[i].provider, want.provider) != 0) continue;
        if (_wcsicmp(queue[i].title, want.title) != 0) continue;
        if (queue[i].queuedAt != want.queuedAt) continue;
        for (int j = i; j + 1 < *count; j++) queue[j] = queue[j + 1];
        (*count)--;
        return true;
    }
    return false;
}

// What the card shows, decided without touching GDI+ so it can be asserted.
struct IslandCardLayout {
    int   lines;          // 1 without a body, 2 with one
    bool  hasBadge;       // repeat > 1
    WCHAR badge[8];
};

static IslandCardLayout PlanIslandCard(const IslandEvent &ev) {
    IslandCardLayout plan = {};
    plan.lines = ev.body[0] ? 2 : 1;
    plan.hasBadge = ev.repeat > 1;
    if (plan.hasBadge) {
        unsigned shown = ev.repeat > 99 ? 99 : ev.repeat;
        swprintf(plan.badge, 8, L"\x00D7" L"%u", shown);
    }
    return plan;
}

// Severity to colour. A provider cannot choose colours (design 14 Q4): info borrows
// the theme's own text colour so it never looks foreign in any theme, while warn and
// critical are fixed and therefore mean the same thing everywhere.
static DWORD ResolveSeverityColor(ExtSeverity sev, DWORD themeText) {
    switch (sev) {
    case ES_WARN: return 0xFFF2C14E;
    case ES_CRITICAL: return 0xFFE4573D;
    default: return themeText;
    }
}

struct IslandMetrics {
    int titleChars;
    int bodyChars;
};

// The card's target width. Pure so the two properties that matter can be asserted
// (design 12): it is never narrower than the user's configured minimum, and it never
// grows without bound. The estimate is coarse on purpose -- the painter measures the
// real text and ellipsises to fit -- because measuring needs GDI+ and this has to be
// decidable before anything is drawn.
static const int kIslandMaxWidth = 640;

static int ComputeIslandCardWidth(const IslandMetrics &m, const ModSettings &cfg,
                                  int monitorWidth) {
    int chars = m.titleChars > m.bodyChars ? m.titleChars : m.bodyChars;
    if (chars < 0) chars = 0;
    int width = 26 + chars * (cfg.rowHeight / 2);      // padding, accent bar, glyphs
    if (width < cfg.islandMinWidth) width = cfg.islandMinWidth;
    // Design 12: a long title must not grow the window without bound. Design 8 wants
    // that bound to be the screen rather than an arbitrary number, so the cap is
    // whichever is smaller -- a narrow monitor beats the 640 default.
    int cap = kIslandMaxWidth;
    if (monitorWidth > 0 && monitorWidth - 24 < cap) cap = monitorWidth - 24;
    if (cap < 120) cap = 120;                         // keep it inside the setting's range
    if (width > cap) width = cap;
    return width;
}

// The clickable rectangle while the island is up is the card's own rectangle, derived
// from the same resting width the window is placed at. The union-of-two-states rule from
// design 8 belonged to the width *animation*: with that animation reverted (it flickered
// -- see the collapse notes in PaintPanel) there are no two states to union, so the rule
// is written down here as a comment rather than carried as dead code.

// Drives the island from the HUD's frame clock. Defined with the other HUD updaters,
// because it reads g_settings and belongs to that thread; everything it decides with
// is the pure state machine above.

// Writes one value. Takes the lock itself, so the pipe thread cannot forget to.
static void ExtStoreValue(int slot, const WCHAR *key, const WCHAR *text, DWORD ttlMs, int maxChars) {
    if (slot < 0 || slot >= kMaxExtProviders) return;
    EnterCriticalSection(&g_extLock);
    ExtProvider &p = g_ext[slot];
    int index = -1;
    for (int i = 0; i < p.count; i++) {
        if (_wcsicmp(p.entries[i].key, key) == 0) { index = i; break; }
    }
    if (index < 0) {
        if (p.count >= kMaxExtKeys) {
            index = ExtEvictIndex(p);
            p.drops++;
        } else {
            index = p.count++;
        }
        wcsncpy(p.entries[index].key, key, kExtKeyChars);
        p.entries[index].key[kExtKeyChars] = L'\0';
    }
    ExtEntry &e = p.entries[index];
    int cap = maxChars + 1;
    if (cap > kExtMaxValueChars + 1) cap = kExtMaxValueChars + 1;
    if (cap < 3) cap = 3;
    ExtSanitize(text, e.text, cap);
    e.updatedAt = GetTickCount();
    e.ttlMs = ttlMs;
    LeaveCriticalSection(&g_extLock);
}

// Marks a key expired without freeing the slot, so a provider that comes back to
// the same key reuses it instead of consuming another one.
static void ExtInvalidate(int slot, const WCHAR *key) {
    if (slot < 0 || slot >= kMaxExtProviders) return;
    EnterCriticalSection(&g_extLock);
    ExtProvider &p = g_ext[slot];
    for (int i = 0; i < p.count; i++) {
        if (_wcsicmp(p.entries[i].key, key) == 0) { p.entries[i].ttlMs = 0; break; }
    }
    LeaveCriticalSection(&g_extLock);
}

// ---------------------------------------------------------------------------
// CPU
// ---------------------------------------------------------------------------
static bool g_cpuPrimed = false;
static ULONGLONG g_prevIdle = 0, g_prevKernel = 0, g_prevUser = 0;

// CPU identity is static for the process lifetime, so it lives in globals rather
// than in the published Values snapshot (writing into that snapshot from two
// threads would be a data race). ReadCpuStatic() runs on the sampler thread, and
// again on every settings-version change, so everything it writes must be safe to
// write a second time -- see g_cpuGhzFromApi.
static WCHAR g_cpuName[64] = L"CPU";
static double g_cpuGhzMax = 1.0;
// >0 once the power API has supplied the nominal. From that point the registry's
// "~MHz" must never win again: ReadCpuStatic() re-reads the registry whenever the
// settings change, which used to revert a 2.20 GHz nominal to the registry's 2.42
// for the rest of the session, because CpuNominalEnsure only tries the API once.
static double g_cpuGhzFromApi = 0.0;
static int g_cpuCores = 0, g_cpuThreads = 0;

static void ReadCpuStatic() {
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE,
                      L"HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0",
                      0, KEY_READ, &key) == ERROR_SUCCESS) {
        WCHAR name[64] = {};
        DWORD size = sizeof(name);
        DWORD type = 0;
        if (RegQueryValueExW(key, L"ProcessorNameString", nullptr, &type,
                             (LPBYTE)name, &size) == ERROR_SUCCESS && name[0]) {
            WCHAR *start = name;
            while (*start == L' ') start++;
            wcsncpy(g_cpuName, start, 63);
            g_cpuName[63] = L'\0';
            size_t len = wcslen(g_cpuName);
            while (len && g_cpuName[len - 1] == L' ') g_cpuName[--len] = L'\0';
        }
        DWORD mhz = 0;
        size = sizeof(mhz);
        if (RegQueryValueExW(key, L"~MHz", nullptr, &type, (LPBYTE)&mhz, &size) == ERROR_SUCCESS &&
            mhz > 0) {
            // Fallback only: the power API's value outranks the registry's, and this
            // function re-runs on every settings change, so writing unconditionally
            // here silently reverted the nominal once the API had already answered.
            if (g_cpuGhzFromApi <= 0.0) g_cpuGhzMax = mhz / 1000.0;
        }
        RegCloseKey(key);
    }

    SYSTEM_INFO si = {};
    GetNativeSystemInfo(&si);
    g_cpuThreads = (int)si.dwNumberOfProcessors;

    DWORD len = 0;
    GetLogicalProcessorInformationEx(RelationProcessorCore, nullptr, &len);
    if (len) {
        auto *buffer = (BYTE *)malloc(len);
        if (buffer &&
            GetLogicalProcessorInformationEx(RelationProcessorCore,
                                             (PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX)buffer,
                                             &len)) {
            int cores = 0;
            DWORD offset = 0;
            while (offset < len) {
                auto *info = (PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX)(buffer + offset);
                if (info->Relationship == RelationProcessorCore) cores++;
                offset += info->Size;
            }
            if (cores > 0) g_cpuCores = cores;
        }
        free(buffer);
    }
    if (g_cpuCores <= 0) g_cpuCores = g_cpuThreads > 1 ? g_cpuThreads / 2 : g_cpuThreads;
}

// ---------------------------------------------------------------------------
// Nominal CPU clock: two sources, and they disagree
// ---------------------------------------------------------------------------
// The registry's "~MHz" is what ACPI/BIOS reports and is not always the nominal clock
// (measured: 2.42 GHz on a CPU whose nominal is 2.20 GHz). The power manager knows the
// real one: PROCESSOR_POWER_INFORMATION.MaxMhz. So the API wins and the registry value
// stays as the fallback -- which is why the two are told apart by separate globals:
// ReadCpuStatic() re-reads the registry on every settings change, so without
// g_cpuGhzFromApi a later registry read would undo the API's answer for good (the API
// is only ever tried once). powrprof is loaded on demand through GetProcAddress, the
// same way iphlpapi and nvml are, so the import table does not change.
struct ProcPowerInfo {
    ULONG number;
    ULONG maxMhz;
    ULONG currentMhz;
    ULONG mhzLimit;
    ULONG maxIdleState;
    ULONG currentIdleState;
};

typedef LONG(WINAPI *CallNtPowerInformationFn)(int, PVOID, ULONG, PVOID, ULONG);

static bool g_cpuNominalSettled = false;    // one attempt; the registry value then stands

// Pure, so the choice between the two sources can be asserted: the *highest* per-processor
// maximum is the nominal -- a hybrid CPU reports a lower maximum for its efficiency cores,
// and a single "nominal" must not be the slow one.
static double CpuNominalFromInfo(const ProcPowerInfo *info, int count, double fallbackGhz) {
    ULONG best = 0;
    for (int i = 0; i < count; i++) {
        if (info[i].maxMhz > best) best = info[i].maxMhz;
    }
    if (best == 0) return fallbackGhz;
    return best / 1000.0;
}

static void CpuNominalEnsure() {
    if (g_cpuNominalSettled) return;
    g_cpuNominalSettled = true;

    const double fallback = g_cpuGhzMax;
    HMODULE lib = LoadLibraryW(L"powrprof.dll");
    if (!lib) {
        Wh_Log(L"CPU clock: powrprof.dll unavailable, keeping the registry's %.2f GHz",
               fallback);
        return;
    }
    auto call = (CallNtPowerInformationFn)GetProcAddress(lib, "CallNtPowerInformation");
    if (!call) {
        Wh_Log(L"CPU clock: CallNtPowerInformation missing, keeping %.2f GHz", fallback);
        return;
    }

    const int count = g_cpuThreads > 0 ? g_cpuThreads : 1;
    const ULONG bytes = (ULONG)(sizeof(ProcPowerInfo) * (size_t)count);
    auto *info = (ProcPowerInfo *)malloc(bytes);
    if (!info) return;
    memset(info, 0, bytes);

    const LONG status = call(11 /* ProcessorInformation */, nullptr, 0, info, bytes);
    const double nominal = CpuNominalFromInfo(info, count, fallback);
    free(info);

    if (status != 0) {
        Wh_Log(L"CPU clock: CallNtPowerInformation status=0x%08X, keeping %.2f GHz",
               (unsigned)status, fallback);
        return;
    }
    Wh_Log(L"CPU clock: nominal %.2f GHz from the power API (registry said %.2f)", nominal,
           fallback);
    g_cpuGhzMax = nominal;
    // Latch the source: from here on the registry value is only a fallback, even
    // though ReadCpuStatic() will read it again on the next settings change.
    g_cpuGhzFromApi = nominal;
}

// ---------------------------------------------------------------------------
// Effective CPU clock
// ---------------------------------------------------------------------------
// Windows does not hand out a frequency; what it exposes is the *ratio* of the clock
// actually in use to the nominal one -- PDH's "\Processor Information(_Total)\%
// Processor Performance" (100 = nominal, >100 = turbo). That is the counter Task
// Manager's "Speed" reads. The alternative, CallNtPowerInformation(ProcessorInformation),
// would report CurrentMhz/MaxMhz directly, but it would add powrprof to the import list
// and its value is refreshed far more coarsely on several systems; PDH is already
// linked, so this costs no new dependency.
//
// The query is created only when some row really references {cpu.ghz}, and -- because
// PdhCloseQuery does not give its memory back (AGENTS 8) -- it is never torn down once
// created, exactly like the GPU backend's.
//
// It cannot ride on the GPU backend's query: that one only exists while the GPU group is
// in use, so sharing it would make {cpu.ghz} depend on {gpu} being displayed.
struct CpuClockBackend {
    PDH_HQUERY query = nullptr;
    PDH_HCOUNTER perfCounter = nullptr;
    bool tried = false;             // one attempt only; a failure is not retried forever
    int logBudget = 4;              // bounded diagnostics, never per-sample
};

static CpuClockBackend g_cpuClock;

// Pure, so the arithmetic and the "unusable reading" decision can be asserted:
// ratio <= 0 means the counter gave us nothing (virtual machines and disabled
// performance counters do this), and returning 0 lets the caller fall back to the
// nominal clock instead of inventing a number.
static double ComputeEffectiveGhz(double baseGhz, double processorPerformance) {
    if (baseGhz <= 0.0) return 0.0;
    if (processorPerformance <= 0.0) return 0.0;
    double ghz = baseGhz * processorPerformance / 100.0;
    // A ratio far outside anything a CPU does is treated as unusable rather than shown.
    if (ghz > baseGhz * 4.0) return 0.0;
    return ghz;
}

static void CpuClockLog(PCWSTR format, ...) {
    if (g_cpuClock.logBudget <= 0) return;
    g_cpuClock.logBudget--;
    va_list args;
    va_start(args, format);
    WCHAR line[192];
    wcsncpy(line, L"CPU clock: ", 11);
    _vsnwprintf(line + 11, 180, format, args);
    va_end(args);
    line[191] = L'\0';
    Wh_Log(L"%s", line);
}

static void CpuClockEnsure() {
    if (g_cpuClock.query || g_cpuClock.tried) return;
    g_cpuClock.tried = true;

    PDH_HQUERY query = nullptr;
    PDH_STATUS status = PdhOpenQueryW(nullptr, 0, &query);
    if (status != ERROR_SUCCESS) {
        CpuClockLog(L"PdhOpenQuery failed status=0x%08X; reporting the nominal clock",
                    (unsigned)status);
        return;
    }
    PDH_HCOUNTER counter = nullptr;
    status = PdhAddEnglishCounterW(
        query, L"\\Processor Information(_Total)\\% Processor Performance", 0, &counter);
    if (status != ERROR_SUCCESS) {
        CpuClockLog(L"no %% Processor Performance status=0x%08X; reporting the nominal clock",
                    (unsigned)status);
        PdhCloseQuery(query);
        return;
    }
    PdhCollectQueryData(query);     // first collect only primes the counter
    g_cpuClock.query = query;
    g_cpuClock.perfCounter = counter;
    CpuClockLog(L"measuring the effective clock (percentage of the nominal one)");
}

// Returns false whenever the counter cannot be trusted; the caller then reports the
// nominal clock, which is what {cpu.ghz.max} shows.
static bool CpuClockRead(double baseGhz, double *outGhz) {
    if (!g_cpuClock.query || !outGhz) return false;
    PDH_STATUS status = PdhCollectQueryData(g_cpuClock.query);
    if (status != ERROR_SUCCESS) {
        CpuClockLog(L"PdhCollectQueryData failed status=0x%08X", (unsigned)status);
        return false;
    }
    PDH_FMT_COUNTERVALUE value = {};
    status = PdhGetFormattedCounterValue(g_cpuClock.perfCounter, PDH_FMT_DOUBLE, nullptr,
                                        &value);
    if (status != ERROR_SUCCESS) {
        CpuClockLog(L"PdhGetFormattedCounterValue failed status=0x%08X", (unsigned)status);
        return false;
    }
    if (!PdhValueUsable(value.CStatus)) {
        CpuClockLog(L"counter status=0x%08X is not valid data", (unsigned)value.CStatus);
        return false;
    }
    const double ghz = ComputeEffectiveGhz(baseGhz, value.doubleValue);
    if (ghz <= 0.0) {
        CpuClockLog(L"reading %.1f%% of nominal is not usable", value.doubleValue);
        return false;
    }
    *outGhz = ghz;
    return true;
}

// Costs a PDH query, so it is opened only for the token that needs it: {cpu} and
// {cpu.name} must not pay for it. Same rule RequiredGroups applies to whole groups,
// one level finer, because the group is coarser than the cost.
// Does this one row reference {cpu.ghz}? The scan is its own function so the token
// matching can be asserted without building a whole ModSettings.
static bool RowUsesEffectiveClock(const WCHAR *row) {
    const WCHAR *p = row;
    while (*p) {
        if (*p == L'{') {
            const WCHAR *end = wcschr(p + 1, L'}');
            if (!end) break;
            // The name ends at ':' (format), '|' (alternative) or the brace; a plain
            // prefix test would also match {cpu.ghz.max}, so the character after the
            // name has to be a delimiter.
            if (end - (p + 1) >= 7 && wcsncmp(p + 1, L"cpu.ghz", 7) == 0) {
                const WCHAR next = *(p + 1 + 7);
                if (next == L'}' || next == L':' || next == L'|') return true;
            }
            p = end + 1;
        } else {
            p++;
        }
    }
    return false;
}

static void ReadCpu(Values &v, bool wantEffectiveClock) {
    // The nominal clock has two sources; settle which applies before anything uses it.
    CpuNominalEnsure();
    FILETIME idleFt, kernelFt, userFt;
    if (GetSystemTimes(&idleFt, &kernelFt, &userFt)) {
        ULARGE_INTEGER i, k, u;
        i.LowPart = idleFt.dwLowDateTime; i.HighPart = idleFt.dwHighDateTime;
        k.LowPart = kernelFt.dwLowDateTime; k.HighPart = kernelFt.dwHighDateTime;
        u.LowPart = userFt.dwLowDateTime; u.HighPart = userFt.dwHighDateTime;

        if (g_cpuPrimed) {
            ULONGLONG idle = i.QuadPart - g_prevIdle;
            ULONGLONG kern = k.QuadPart - g_prevKernel;
            ULONGLONG user = u.QuadPart - g_prevUser;
            ULONGLONG total = kern + user;
            if (total > 0) {
                v.cpuUser = MinDouble(100.0, user * 100.0 / (double)total);
                v.cpuKernel = MinDouble(100.0, (kern > idle ? kern - idle : 0) * 100.0 / (double)total);
                v.cpu = MinDouble(100.0, v.cpuUser + v.cpuKernel);
            }
        }
        g_prevIdle = i.QuadPart;
        g_prevKernel = k.QuadPart;
        g_prevUser = u.QuadPart;
        g_cpuPrimed = true;
    }

    // Effective clock: Windows exposes it through PDH "Processor Performance",
    // but that means a PDH query. The registry base clock is free, so report it.
    wcsncpy(v.cpuName, g_cpuName, 63);
    // {cpu.ghz} is the clock actually in use; {cpu.ghz.max} is the nominal one. Only
    // the former costs a PDH query, and only when a row references it.
    v.cpuName[63] = L'\0';
    v.cores = g_cpuCores;
    v.threads = g_cpuThreads;
    v.cpuGhzMax = g_cpuGhzMax;
    v.cpuGhz = g_cpuGhzMax;
    if (wantEffectiveClock) {
        CpuClockEnsure();
        double effective = 0;
        if (CpuClockRead(g_cpuGhzMax, &effective)) v.cpuGhz = effective;
    }
}

// ---------------------------------------------------------------------------
// Memory
// ---------------------------------------------------------------------------
static void ReadMemory(Values &v) {
    MEMORYSTATUSEX m = {};
    m.dwLength = sizeof(m);
    if (!GlobalMemoryStatusEx(&m)) return;
    const double gb = 1073741824.0;
    v.memTotalGb = m.ullTotalPhys / gb;
    double freeGb = m.ullAvailPhys / gb;
    v.memFreeGb = freeGb;
    v.memUsedGb = v.memTotalGb - freeGb;
    v.memPercent = (double)m.dwMemoryLoad;
    ULONGLONG used = m.ullTotalPageFile - m.ullAvailPageFile;
    v.memCommit = m.ullTotalPageFile > 0 ? used * 100.0 / (double)m.ullTotalPageFile : 0;
}

// ---------------------------------------------------------------------------
// Network
// ---------------------------------------------------------------------------
typedef DWORD(WINAPI *GetIfTable2Fn)(PMIB_IF_TABLE2 *);
static GetIfTable2Fn g_getIfTable2 = nullptr;
static bool g_netPrimed = false;
static ULONGLONG g_prevRx = 0, g_prevTx = 0, g_prevNetTick = 0;

static void ReadNetwork(Values &v) {
    if (!g_getIfTable2) return;
    PMIB_IF_TABLE2 table = nullptr;
    if (g_getIfTable2(&table) != NO_ERROR || !table) return;

    ULONGLONG rx = 0, tx = 0;
    int nics = 0;
    for (ULONG i = 0; i < table->NumEntries; i++) {
        const MIB_IF_ROW2 &row = table->Table[i];
        if (row.OperStatus != IfOperStatusUp) continue;
        if (row.Type == IF_TYPE_SOFTWARE_LOOPBACK) continue;
        if (row.Type == IF_TYPE_TUNNEL) continue;
        nics++;
        rx += row.InOctets;
        tx += row.OutOctets;
    }
    FreeMibTable(table);

    v.netNics = nics;
    v.netTotalBytes = (double)(rx + tx);

    ULONGLONG now = GetTickCount64();
    if (g_netPrimed && now > g_prevNetTick) {
        double seconds = (now - g_prevNetTick) / 1000.0;
        v.netDownBps = rx >= g_prevRx ? (rx - g_prevRx) / seconds : 0;
        v.netUpBps = tx >= g_prevTx ? (tx - g_prevTx) / seconds : 0;
    }
    g_prevRx = rx;
    g_prevTx = tx;
    g_prevNetTick = now;
    g_netPrimed = true;
}

// ---------------------------------------------------------------------------
// GPU: NVML first (exact, and cheap per sample), PDH counters as fallback.
//
// Both are expensive to *start* (NVML ~22 MB resident, PDH ~6 MB plus a ~550 ms
// first call), so neither is touched unless a row references {gpu...}.
// ---------------------------------------------------------------------------
typedef int nvmlReturn_t;
typedef void *nvmlDevice_t;
struct nvmlUtilization_t { unsigned int gpu; unsigned int memory; };
struct nvmlMemory_t { unsigned long long total, free, used; };
typedef nvmlReturn_t(WINAPI *nvmlInitFn)(void);
typedef nvmlReturn_t(WINAPI *nvmlShutdownFn)(void);
typedef nvmlReturn_t(WINAPI *nvmlDeviceGetHandleByIndexFn)(unsigned int, nvmlDevice_t *);
typedef nvmlReturn_t(WINAPI *nvmlDeviceGetTemperatureFn)(nvmlDevice_t, int, unsigned int *);
typedef nvmlReturn_t(WINAPI *nvmlDeviceGetUtilizationRatesFn)(nvmlDevice_t, nvmlUtilization_t *);
typedef nvmlReturn_t(WINAPI *nvmlDeviceGetMemoryInfoFn)(nvmlDevice_t, nvmlMemory_t *);
typedef nvmlReturn_t(WINAPI *nvmlDeviceGetNameFn)(nvmlDevice_t, char *, unsigned int);
typedef nvmlReturn_t(WINAPI *nvmlDeviceGetFanSpeedFn)(nvmlDevice_t, unsigned int *);
typedef nvmlReturn_t(WINAPI *nvmlDeviceGetPowerUsageFn)(nvmlDevice_t, unsigned int *);

struct GpuBackend {
    enum Kind { None, Nvml, Pdh } kind = None;

    // NVML
    HMODULE nvmlModule = nullptr;
    nvmlDevice_t device = nullptr;
    nvmlDeviceGetTemperatureFn getTemp = nullptr;
    nvmlDeviceGetUtilizationRatesFn getUtil = nullptr;
    nvmlDeviceGetMemoryInfoFn getMemory = nullptr;
    nvmlDeviceGetNameFn getName = nullptr;
    nvmlDeviceGetFanSpeedFn getFan = nullptr;
    nvmlDeviceGetPowerUsageFn getPower = nullptr;
    nvmlShutdownFn shutdown = nullptr;

    // PDH
    PDH_HQUERY query = nullptr;
    PDH_HCOUNTER engineCounter = nullptr;
    PDH_HCOUNTER memCounter = nullptr;

    bool Start(Values &v);
    void Sample(Values &v);
};

// Loads nvml.dll, working around a real-world driver-install defect.
//
// Some NVIDIA installs leave nvml.dll in System32 but never place its dependency
// nvml_loader.dll there, so a plain LoadLibrary("nvml.dll") fails with
// ERROR_MOD_NOT_FOUND (126). The loader does exist in the driver store, so it is
// located and preloaded first; once it is already mapped, nvml.dll's import
// resolves. Verified against a machine showing exactly that split.
static HMODULE LoadNvmlWithDependencies() {
    HMODULE module = ((HMODULE(WINAPI *)(LPCWSTR))LoadLibraryW)(L"nvml.dll");
    if (module) return module;

    // Already mapped with the right name? Then the failure is something else.
    if (GetModuleHandleW(L"nvml_loader.dll")) return nullptr;

    WCHAR searchRoot[MAX_PATH] = L"C:\\Windows\\System32\\DriverStore\\FileRepository";
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE,
                      L"SOFTWARE\\NVIDIA Corporation\\Global\\NVSMI", 0, KEY_READ,
                      &key) == ERROR_SUCCESS) {
        WCHAR installPath[MAX_PATH] = {};
        DWORD size = sizeof(installPath);
        DWORD type = 0;
        if (RegQueryValueExW(key, L"InstallPath", nullptr, &type, (LPBYTE)installPath,
                             &size) == ERROR_SUCCESS && installPath[0]) {
            wcsncpy(searchRoot, installPath, MAX_PATH - 1);
        }
        RegCloseKey(key);
    }

    // A bounded breadth-first sweep for nvml_loader.dll. Depth 3 is enough for
    // both the driver store layout and an NVSMI install directory.
    struct Node { WCHAR path[MAX_PATH]; int depth; };
    static Node queue[64];
    int head = 0, tail = 0;
    wcsncpy(queue[tail].path, searchRoot, MAX_PATH - 1);
    queue[tail].path[MAX_PATH - 1] = L'\0';
    queue[tail].depth = 0;
    tail++;

    HMODULE loader = nullptr;
    while (head < tail && !loader) {
        Node node = queue[head++];
        WCHAR pattern[MAX_PATH];
        swprintf(pattern, MAX_PATH, L"%s\\nvml_loader.dll", node.path);
        loader = ((HMODULE(WINAPI *)(LPCWSTR))LoadLibraryW)(pattern);
        if (loader) break;
        if (node.depth >= 3 || tail >= 64) continue;

        WCHAR childPattern[MAX_PATH];
        swprintf(childPattern, MAX_PATH, L"%s\\*", node.path);
        WIN32_FIND_DATAW data;
        HANDLE find = FindFirstFileW(childPattern, &data);
        if (find == INVALID_HANDLE_VALUE) continue;
        do {
            if (!(data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) continue;
            if (data.cFileName[0] == L'.') continue;
            if (tail >= 64) break;
            swprintf(queue[tail].path, MAX_PATH, L"%s\\%s", node.path, data.cFileName);
            queue[tail].depth = node.depth + 1;
            tail++;
        } while (FindNextFileW(find, &data));
        FindClose(find);
    }

    if (loader) {
        Wh_Log(L"NVML: preloaded nvml_loader.dll to satisfy nvml.dll's import");
        module = ((HMODULE(WINAPI *)(LPCWSTR))LoadLibraryW)(L"nvml.dll");
    }
    return module;
}

static bool GpuStartNvml(GpuBackend &b, Values &v) {
    b.nvmlModule = LoadNvmlWithDependencies();
    if (!b.nvmlModule) {
        // Report both numbers: 126 with the module present means a missing
        // dependency, 2 means the file itself is absent.
        Wh_Log(L"NVML: could not load nvml.dll, error=%lu, already-in-system32=%d",
               GetLastError(),
               GetFileAttributesW(L"C:\\Windows\\System32\\nvml.dll") != INVALID_FILE_ATTRIBUTES);
        return false;
    }
    auto init = (nvmlInitFn)GetProcAddress(b.nvmlModule, "nvmlInit_v2");
    auto count = (nvmlReturn_t(WINAPI *)(unsigned int *))GetProcAddress(b.nvmlModule, "nvmlDeviceGetCount_v2");
    auto handle = (nvmlDeviceGetHandleByIndexFn)GetProcAddress(b.nvmlModule, "nvmlDeviceGetHandleByIndex_v2");
    b.getTemp = (nvmlDeviceGetTemperatureFn)GetProcAddress(b.nvmlModule, "nvmlDeviceGetTemperature");
    b.getUtil = (nvmlDeviceGetUtilizationRatesFn)GetProcAddress(b.nvmlModule, "nvmlDeviceGetUtilizationRates");
    b.getMemory = (nvmlDeviceGetMemoryInfoFn)GetProcAddress(b.nvmlModule, "nvmlDeviceGetMemoryInfo");
    b.getName = (nvmlDeviceGetNameFn)GetProcAddress(b.nvmlModule, "nvmlDeviceGetName");
    b.getFan = (nvmlDeviceGetFanSpeedFn)GetProcAddress(b.nvmlModule, "nvmlDeviceGetFanSpeed");
    b.getPower = (nvmlDeviceGetPowerUsageFn)GetProcAddress(b.nvmlModule, "nvmlDeviceGetPowerUsage");
    b.shutdown = (nvmlShutdownFn)GetProcAddress(b.nvmlModule, "nvmlShutdown");

    if (!init || !count || !handle || !b.getUtil) {
        Wh_Log(L"NVML: exports missing (init=%d count=%d handle=%d util=%d)",
               init ? 1 : 0, count ? 1 : 0, handle ? 1 : 0, b.getUtil ? 1 : 0);
        return false;
    }
    nvmlReturn_t rc = init();
    if (rc != 0) {
        Wh_Log(L"NVML: nvmlInit_v2 failed, rc=%d", (int)rc);
        return false;
    }
    unsigned int devices = 0;
    rc = count(&devices);
    if (rc != 0 || devices == 0) {
        Wh_Log(L"NVML: device count rc=%d devices=%u", (int)rc, devices);
        return false;
    }
    rc = handle(0, &b.device);
    if (rc != 0) {
        Wh_Log(L"NVML: GetHandleByIndex(0) failed, rc=%d", (int)rc);
        return false;
    }

    if (b.getName) {
        char name[96] = {};
        if (b.getName(b.device, name, sizeof(name)) == 0 && name[0]) {
            MultiByteToWideChar(CP_UTF8, 0, name, -1, v.gpuName, 63);
        }
    }
    b.kind = GpuBackend::Nvml;
    Wh_Log(L"NVML ready: %s (%u device(s))", v.gpuName, devices);
    return true;
}

static bool GpuStartPdh(GpuBackend &b) {
    if (PdhOpenQueryW(nullptr, 0, &b.query) != ERROR_SUCCESS) return false;
    if (PdhAddEnglishCounterW(b.query, L"\\GPU Engine(*)\\Utilization Percentage", 0,
                              &b.engineCounter) != ERROR_SUCCESS) {
        PdhCloseQuery(b.query);
        b.query = nullptr;
        return false;
    }
    // VRAM is optional; a failure here must not disable utilisation.
    PdhAddEnglishCounterW(b.query, L"\\GPU Adapter Memory(*)\\Dedicated Usage", 0, &b.memCounter);
    // Counters need two collections before the first value is meaningful.
    PdhCollectQueryData(b.query);
    b.kind = GpuBackend::Pdh;
    return true;
}

// NVML is preferred: one call pair reads temperature and both utilisation axes,
// and it needs no PDH query (which costs ~550 ms to start and never returns its
// ~6 MB). PDH is the fallback for non-NVIDIA adapters.
bool GpuBackend::Start(Values &v) {
    if (GpuStartNvml(*this, v)) return true;
    Wh_Log(L"NVML unavailable; falling back to PDH GPU counters (may take ~500 ms)");
    return GpuStartPdh(*this);
}

// Reads the busiest engine of the selected adapter, mirroring Task Manager.
static double PdhReadMaxEngine(PDH_HQUERY query, PDH_HCOUNTER counter) {
    DWORD bufferSize = 0;
    DWORD itemCount = 0;
    if (PdhGetFormattedCounterArrayW(counter, PDH_FMT_DOUBLE, &bufferSize, &itemCount, nullptr)
        != PDH_MORE_DATA) {
        return 0;
    }
    BYTE *buffer = (BYTE *)malloc(bufferSize);
    if (!buffer) return 0;
    double best = 0;
    if (PdhGetFormattedCounterArrayW(counter, PDH_FMT_DOUBLE, &bufferSize, &itemCount,
                                     (PPDH_FMT_COUNTERVALUE_ITEM_W)buffer) == ERROR_SUCCESS) {
        auto *items = (PPDH_FMT_COUNTERVALUE_ITEM_W)buffer;
        for (DWORD i = 0; i < itemCount; i++) {
            if (!PdhValueUsable(items[i].FmtValue.CStatus)) continue;
            double value = items[i].FmtValue.doubleValue;
            if (value > best) best = value;
        }
    }
    free(buffer);
    return MinDouble(100.0, best);
}

void GpuBackend::Sample(Values &v) {
    if (kind == Nvml) {
        nvmlUtilization_t util = {};
        if (getUtil && getUtil(device, &util) == 0) {
            v.gpu = util.gpu;
            v.gpuMem = util.memory;
            v.gpuValid = true;
        }
        if (getTemp) {
            unsigned int celsius = 0;
            if (getTemp(device, 0, &celsius) == 0) {
                v.gpuTemp = celsius;
                v.gpuTempValid = true;
            }
        }
        if (getMemory) {
            nvmlMemory_t mem = {};
            if (getMemory(device, &mem) == 0) {
                v.vramUsedGb = mem.used / 1073741824.0;
                v.vramTotalGb = mem.total / 1073741824.0;
                v.vramUsedValid = true;
                v.vramTotalValid = true;
            }
        }
        if (getFan) {
            unsigned int fan = 0;
            if (getFan(device, &fan) == 0) {
                v.gpuFan = fan;
                v.gpuFanValid = true;
            }
        }
        if (getPower) {
            unsigned int mw = 0;
            if (getPower(device, &mw) == 0) {
                v.gpuPower = mw / 1000.0;
                v.gpuPowerValid = true;
            }
        }
    } else if (kind == Pdh) {
        if (PdhCollectQueryData(query) == ERROR_SUCCESS) {
            v.gpu = PdhReadMaxEngine(query, engineCounter);
            v.gpuValid = true;
            if (memCounter) {
                DWORD bufferSize = 0, itemCount = 0;
                if (PdhGetFormattedCounterArrayW(memCounter, PDH_FMT_LARGE, &bufferSize,
                                                 &itemCount, nullptr) == PDH_MORE_DATA) {
                    BYTE *buffer = (BYTE *)malloc(bufferSize);
                    if (buffer) {
                        if (PdhGetFormattedCounterArrayW(memCounter, PDH_FMT_LARGE, &bufferSize,
                                                         &itemCount,
                                                         (PPDH_FMT_COUNTERVALUE_ITEM_W)buffer) == ERROR_SUCCESS) {
                            auto *items = (PPDH_FMT_COUNTERVALUE_ITEM_W)buffer;
                            ULONGLONG best = 0;
                            for (DWORD i = 0; i < itemCount; i++) {
                                if (!PdhValueUsable(items[i].FmtValue.CStatus)) continue;
                                ULONGLONG value = (ULONGLONG)items[i].FmtValue.largeValue;
                                if (value > best) best = value;
                            }
                            v.vramUsedGb = best / 1073741824.0;
                            v.vramUsedValid = true;
                        }
                        free(buffer);
                    }
                }
            }
        }
    }
}

// ---------------------------------------------------------------------------
// Rendering the rows
// ---------------------------------------------------------------------------
static void FormatBytes(double bytes, WCHAR *out, size_t count) {
    static const WCHAR *units[] = {L"B", L"KB", L"MB", L"GB", L"TB", L"PB"};
    int unit = 0;
    double value = bytes;
    while (value >= 1024.0 && unit < 5) {
        value /= 1024.0;
        unit++;
    }
    if (unit == 0) swprintf(out, count, L"%.0f %s", value, units[unit]);
    else if (value < 10.0) swprintf(out, count, L"%.2f %s", value, units[unit]);
    else swprintf(out, count, L"%.1f %s", value, units[unit]);
}

static void FormatSpeed(double bps, WCHAR *out, size_t count) {
    WCHAR bytes[32];
    FormatBytes(bps, bytes, 32);
    swprintf(out, count, L"%s/s", bytes);
}

// Returns a pointer to a pre-formatted display string, or nullptr when unknown.
// Numbers that can be reformatted are handled by LookupNumber instead.
static const WCHAR *LookupRaw(const WCHAR *name, int len, const Values &v, WCHAR *scratch, size_t scratchCount) {
#define MATCH(literal) (len == (int)(sizeof(literal) / sizeof(WCHAR)) - 1 && _wcsnicmp(name, literal, len) == 0)
    if (MATCH(L"cpu.name")) return v.cpuName;
    if (MATCH(L"gpu.name")) return v.gpuName;
    if (MATCH(L"time")) return v.timeText;
    if (MATCH(L"date")) return v.dateText;
    if (MATCH(L"uptime")) return v.uptimeText;
    if (MATCH(L"gpu.vram.total")) {
        // No VRAM total on this machine (the PDH fallback cannot read it): the row must
        // show "--", not "0.0". Returning nullptr lets ResolveToken fall through to
        // LookupNumber, which answers with the same validity flag.
        if (!v.vramTotalValid) return nullptr;
        swprintf(scratch, scratchCount, L"%.1f", v.vramTotalGb);
        return scratch;
    }
    if (MATCH(L"cpu.cores")) { swprintf(scratch, scratchCount, L"%d", v.cores); return scratch; }
    if (MATCH(L"cpu.threads")) { swprintf(scratch, scratchCount, L"%d", v.threads); return scratch; }
    if (MATCH(L"net.nics")) { swprintf(scratch, scratchCount, L"%d", v.netNics); return scratch; }
    if (MATCH(L"net.down")) { FormatSpeed(v.netDownBps, scratch, scratchCount); return scratch; }
    if (MATCH(L"net.up")) { FormatSpeed(v.netUpBps, scratch, scratchCount); return scratch; }
    if (MATCH(L"net.total")) { FormatBytes(v.netTotalBytes, scratch, scratchCount); return scratch; }
#undef MATCH
    return nullptr;
}

// Numeric variables, so a format spec can set precision and padding on them.
// Distinct from LookupRaw because formatting has to happen before the digits are
// fixed: once "12%" is a string it is too late to ask for two decimals.
static bool LookupNumber(const WCHAR *name, int len, const Values &v, double &out) {
#define MATCH(literal) (len == (int)(sizeof(literal) / sizeof(WCHAR)) - 1 && _wcsnicmp(name, literal, len) == 0)
    if (MATCH(L"cpu")) { out = v.cpu; return true; }
    if (MATCH(L"cpu.user")) { out = v.cpuUser; return true; }
    if (MATCH(L"cpu.kernel")) { out = v.cpuKernel; return true; }
    if (MATCH(L"cpu.ghz")) { out = v.cpuGhz; return true; }
    if (MATCH(L"cpu.ghz.max")) { out = v.cpuGhzMax; return true; }
    if (MATCH(L"cpu.cores")) { out = v.cores; return true; }
    if (MATCH(L"cpu.threads")) { out = v.threads; return true; }
    if (MATCH(L"mem")) { out = v.memPercent; return true; }
    if (MATCH(L"mem.used")) { out = v.memUsedGb; return true; }
    if (MATCH(L"mem.total")) { out = v.memTotalGb; return true; }
    if (MATCH(L"mem.free")) { out = v.memFreeGb; return true; }
    if (MATCH(L"mem.commit")) { out = v.memCommit; return true; }
    if (MATCH(L"net.nics")) { out = v.netNics; return true; }
    if (MATCH(L"net.down.bps")) { out = v.netDownBps; return true; }
    if (MATCH(L"net.up.bps")) { out = v.netUpBps; return true; }
    if (MATCH(L"mod.ws")) { out = v.modWsMb; return true; }
    if (MATCH(L"mod.cpu")) { out = v.modCpuMs; return true; }
    // GPU values are only meaningful once a backend reported something.
    if (MATCH(L"gpu")) { out = v.gpuValid ? v.gpu : 0; return v.gpuValid; }
    if (MATCH(L"gpu.mem")) { out = v.gpuValid ? v.gpuMem : 0; return v.gpuValid; }
    if (MATCH(L"gpu.temp")) { out = v.gpuTemp; return v.gpuTempValid; }
    if (MATCH(L"gpu.fan")) { out = v.gpuFan; return v.gpuFanValid; }
    if (MATCH(L"gpu.power")) { out = v.gpuPower; return v.gpuPowerValid; }
    if (MATCH(L"gpu.vram.used")) { out = v.vramUsedGb; return v.vramUsedValid; }
    if (MATCH(L"gpu.vram.total")) { out = v.vramTotalGb; return v.vramTotalValid; }
#undef MATCH
    return false;
}

// One format spec: digits | int | pct | wN | padC
static void ApplySpecToken(const WCHAR *spec, int specLen, WCHAR *value, size_t valueCount) {
    if (specLen <= 0) return;
    WCHAR formatted[128];

    if (specLen == 1 && spec[0] >= L'0' && spec[0] <= L'9') {
        // Digit count, applied to the value as it stands. This is the only spec
        // that works on the pre-formatted form of a numeric variable.
        swprintf(formatted, 128, L"%.*f", spec[0] - L'0', _wtof(value));
        wcsncpy(value, formatted, valueCount - 1);
        value[valueCount - 1] = L'\0';
    } else if (_wcsnicmp(spec, L"int", specLen) == 0) {
        swprintf(formatted, 128, L"%.0f", _wtof(value));
        wcsncpy(value, formatted, valueCount - 1);
        value[valueCount - 1] = L'\0';
    } else if (_wcsnicmp(spec, L"pct", specLen) == 0) {
        swprintf(formatted, 128, L"%.0f%%", _wtof(value));
        wcsncpy(value, formatted, valueCount - 1);
        value[valueCount - 1] = L'\0';
    } else if ((spec[0] == L'w' || spec[0] == L'W') && specLen > 1) {
        int width = _wtoi(spec + 1);
        if (width > 0 && width < 64) {
            size_t current = wcslen(value);
            if ((int)current < width) {
                // Padded on the left, which is what makes numbers line up in a
                // column. The fill character comes from a later padX token.
                size_t pad = (size_t)width - current;
                WCHAR fill = L' ';
                for (int i = 0; i < (int)pad && i < 63; i++) formatted[i] = fill;
                formatted[pad < 63 ? (int)pad : 63] = L'\0';
                wcsncat(formatted, value, 127 - wcslen(formatted) - 1);
                wcsncpy(value, formatted, valueCount - 1);
                value[valueCount - 1] = L'\0';
            }
        }
    } else if ((spec[0] == L'p' || spec[0] == L'P') && specLen > 1 &&
               (spec[1] == L'a' || spec[1] == L'A')) {
        WCHAR fill = specLen > 3 ? spec[3] : L' ';   // pad_ / pad0
        wchar_t *space = wcschr(value, L' ');
        if (space) {
            for (WCHAR *c = value; c < space; c++) *c = fill;
        }
    }
}

// Numeric view of one token: the built-in table first, then an external value
// that happens to parse as a number. This is what makes {ext.music.progress|0}
// and [[ring:{ext.music.progress}]] work without a second spec path.
static bool ResolveNumeric(const WCHAR *name, int len, const Values &v, double &out) {
    if (LookupNumber(name, len, v, out)) return true;
    if (!ExtIsExtToken(name, len)) return false;
    const WCHAR *text = nullptr;
    if (ExtLookup(name, len, &text) != ER_VALUE || !text) return false;
    return ExtParseNumber(text, out);
}

// Applies a |-separated spec chain, e.g. {cpu|0|w3|pad0}.
//
// When the variable is numeric the chain runs on the number first, so
// {mem.used|2} really does give two decimals rather than zero. Variables that
// are only available pre-formatted (speeds, byte totals, clock text) take the
// digit spec applied to their string form.
static void ApplySpec(const WCHAR *spec, int specLen, const WCHAR *name, int nameLen,
                      const Values &v, WCHAR *value, size_t valueCount) {
    if (specLen <= 0) return;

    // Split the chain and note whether it starts with a digit count.
    const WCHAR *tokens[8];
    int lengths[8];
    int tokenCount = 0;
    const WCHAR *p = spec;
    int remaining = specLen;
    while (remaining > 0 && tokenCount < 8) {
        const WCHAR *bar = p;
        int span = 0;
        while (span < remaining && bar[span] != L'|') span++;
        while (span > 0 && bar[span - 1] == L' ') span--;
        tokens[tokenCount] = bar;
        lengths[tokenCount] = span;
        tokenCount++;
        p = bar + span;
        remaining -= span;
        if (remaining > 0) {
            p++;
            remaining--;
        }
    }

    double number = 0;
    bool numeric = ResolveNumeric(name, nameLen, v, number);
    int firstToken = 0;
    if (numeric && tokenCount > 0 && lengths[0] == 1 && tokens[0][0] >= L'0' && tokens[0][0] <= L'9') {
        swprintf(value, valueCount, L"%.*f", tokens[0][0] - L'0', number);
        firstToken = 1;
    }

    for (int i = firstToken; i < tokenCount; i++) {
        ApplySpecToken(tokens[i], lengths[i], value, valueCount);
        // A pad token only rewrites existing spaces, so it has to run after the
        // width token that created them. Chained specs preserve that order.
    }
}

// The StringFormat every row's text is drawn and measured with.
//
// Factored out so the paint path and the regression test share ONE definition: the
// test asserting "no run wraps" against its own copy of this setup would keep passing
// while the panel wrapped, which is a test that proves nothing. The flag is the whole
// fix for the wrap, so it lives here and nowhere else.
static GpStringFormat *CreateRowStringFormat() {
    GpStringFormat *format = nullptr;
    if (g_gdiplus.CreateStringFormat(kStringFormatNoWrap, 0, &format) != 0 || !format) {
        return nullptr;
    }
    if (g_gdiplus.SetStringFormatAlign(format, kAlignNear) != 0 ||
        g_gdiplus.SetStringFormatLineAlign(format, kAlignCenter) != 0) {
        g_gdiplus.DeleteStringFormat(format);
        return nullptr;
    }
    return format;
}

// The panel's intro transform: a scale about the panel's centre, expressed as the
// matrix GDI+ is given. Factored out because the ripple used to be painted outside
// this transform while the icon was painted inside it, and matrix.dy grows with the
// panel height -- so the two only agreed on a single row.
struct PanelTransform {
    float scale;
    float dx;
    float dy;
    bool active;
};

static PanelTransform ComputePanelTransform(float width, float height, double introScale,
                                            bool introActive) {
    PanelTransform t = {};
    t.active = introActive && introScale > 0.001 &&
               (introScale < 0.999 || introScale > 1.001);
    t.scale = t.active ? (float)introScale : 1.0f;
    t.dx = (1.0f - t.scale) * width / 2.0f;
    t.dy = (1.0f - t.scale) * height / 2.0f;
    return t;
}

// Right edge available to text on one row, and the width the text should occupy.
//
// This is factored out because getting it wrong is invisible in a compile and
// subtle on screen: a zero here makes GDI+ clip the entire row away, which is
// what made every text-only row disappear. It is covered by _layouttest.cpp.
struct RowTextMetrics {
    float textRight;
    float textWidth;
};

static RowTextMetrics ComputeRowTextMetrics(float panelWidth, int ringCount, int ringSize,
                                            int ringRightOffset, bool inlineFlow,
                                            float leftInset = 12.0f) {
    RowTextMetrics m;
    const float inset = leftInset;
    m.textRight = panelWidth - 12.0f;
    if (!inlineFlow && ringCount > 0) {
        float ringsWidth = (float)ringCount * ((float)ringSize + 4.0f);
        m.textRight = panelWidth - (float)ringRightOffset - ringsWidth;
    }
    m.textWidth = m.textRight - inset;
    if (m.textWidth < 0.0f) m.textWidth = 0.0f;
    return m;
}

// How much room an *inline* text run may use, measured from where it starts.
//
// A right-parked row hands the text whatever is left after the gauges; an inline
// row has to work out its own limit, and it used to work it out by measuring the
// run and then passing that same number as the budget -- so FitTextToWidth could
// never shorten anything (its first test is "does it already fit?"), which meant a
// row using [[ring:...]] was never truncated: the text ran to the bitmap edge with
// no ellipsis and pushed its own gauge off the panel. The README promises the
// opposite ("the part that does not fit is cut and marked with ...").
//
// The rule is "the run plus the gauges still to come must fit the row", which is
// the same relation the parked path gets from ComputeRowTextMetrics; the gauges
// keep their width so they stay on screen, and the text is what gives way.
// Factored out so that relation is assertable instead of implied by the paint loop.
static float ComputeInlineTextLimit(float textRight, float cursor, int ringsAfter, int ringSize) {
    if (ringsAfter < 0) ringsAfter = 0;
    const float limit = textRight - cursor - (float)ringsAfter * ((float)ringSize + 4.0f);
    return limit > 0.0f ? limit : 0.0f;
}

// The floor both the trim budget and the DrawString box use. A zero-width box makes GDI+
// drop the whole run -- that is the other half of the "every row after the first
// disappeared" bug -- so the floor lives in one function that both call sites ask,
// instead of the same expression being written out twice (the regression test used to
// copy the expression, which meant it tested its own copy; see AGENTS 7.8).
static float LayoutWidthFloor(float width) {
    return width > kMinLayoutWidth ? width : kMinLayoutWidth;
}

// Colour of a ring gauge. Precedence, highest first:
//
//   1. the per-ring :COLOR field in [[ring:VALUE:MAX:THICK:COLOR]]
//   2. the RingColor setting, when Theme is Custom
//   3. the theme's border colour
//
// Factored out so the precedence is testable rather than implied by wherever the
// expression happens to sit in the paint loop.
static DWORD ResolveRingColor(const RingSpec &ring, const ModSettings &settings) {
    if (ring.hasColor) return ring.color;
    if (settings.ringColorUserSet) return settings.ringColor;
    return settings.border;
}

// Width of a text run, in pixels, including its trailing spaces.
//
// GDI+ MeasureString deliberately ignores trailing spaces (normal typographic
// behaviour), which is wrong for a layout that positions a gauge immediately
// after the text: "9%  " must advance by the two spaces or the gauge lands on
// top of the '%'. Appending a sentinel glyph and subtracting its own width is
// the portable way to get the true advance.
//
// The sentinel is U+2502 BOX DRAWINGS LIGHT VERTICAL, chosen because it is
// effectively never present in a status row and is not whitespace, so it
// terminates the trailing-space run that GDI+ would otherwise trim.
//
// exactLength >= 0 measures that prefix and stops there, ignoring anything after it.
// FitTextToWidth needs this: measuring a prefix by copying it into a shorter buffer
// would let the sentinel absorb the following whitespace, so "ab" and "ab      "
// would report the same width and a width-ordered search would have no order to
// rely on.
static float MeasureAdvance(GpGraphics *graphics, GpFont *font, GpStringFormat *format,
                            const WCHAR *text, float boxHeight, int exactLength = -1) {
    static const WCHAR kSentinel = 0x2502;

    WCHAR probe[644];
    int length = exactLength >= 0 ? exactLength : (int)wcslen(text);
    if (length > 640) length = 640;
    wcsncpy(probe, text, length);
    probe[length] = kSentinel;
    probe[length + 1] = L'\0';

    RectF box(0.0f, 0.0f, 4000.0f, boxHeight);
    RectF bounds = {};
    INT codepoints = 0, lines = 0;
    float withSentinel = 0.0f;
    if (g_gdiplus.MeasureString(graphics, probe, -1, font, &box, format, &bounds,
                                &codepoints, &lines) == 0) {
        withSentinel = bounds.Width;
    }

    WCHAR justSentinel[2] = {kSentinel, L'\0'};
    float sentinelOnly = 0.0f;
    bounds = {};
    if (g_gdiplus.MeasureString(graphics, justSentinel, -1, font, &box, format, &bounds,
                                &codepoints, &lines) == 0) {
        sentinelOnly = bounds.Width;
    }

    float advance = withSentinel - sentinelOnly;
    if (advance < 0.0f) advance = 0.0f;
    return advance;
}

// How many characters of a run fit inside `budget` pixels.
//
// `width` answers "how wide is the first count characters", and a prefix only ever
// gets wider as characters are added, so the longest fitting prefix is bisected
// rather than scanned. Factored out of FitTextToWidth with the measurement injected
// so the search itself is assertable in the regression test, which has no GDI+ or
// font to measure with.
static int FitCharacterCount(int length, float budget, float (*width)(int, void *),
                             void *context) {
    if (length <= 0 || budget <= 0.0f || !width) return 0;
    int low = 0, high = length;
    while (low < high) {
        int mid = low + (high - low + 1) / 2;
        if (width(mid, context) <= budget) low = mid;
        else high = mid - 1;
    }
    return low;
}

// How many leading characters to keep when a run is too wide, reserving one
// character for the ellipsis. Returns 0 when only the ellipsis would fit, which the
// caller renders as "keep the raw prefix" instead: a lone ellipsis carries no
// information, and a run trimmed to nothing is indistinguishable from an empty row.
static int FitCharsWithEllipsis(int length, float budget, float ellipsisWidth,
                                float (*width)(int, void *), void *context) {
    if (length <= 0 || budget <= 0.0f) return 0;
    if (ellipsisWidth <= 0.0f || ellipsisWidth >= budget) return 0;
    int keep = FitCharacterCount(length, budget - ellipsisWidth, width, context);
    if (keep >= length) return 0;   // nothing was actually shortened
    return keep;
}

// Shortens a run in place until it can be drawn inside `available` pixels.
//
// Needed because GDI+ does not clip text to the layout box: it either wraps (which
// kStringFormatNoWrap now forbids) or simply draws past the box. A row packs several
// segments -- text, ring, text -- into one row width, and every segment is drawn in
// its own box, so a segment that runs long would draw across the gauge that follows
// it. Trimming here also shows the reader that something was cut, instead of
// overflowing silently.
//
// The head is kept and an ellipsis is appended, which is the order the readings are
// written in ("CPU ... 12%  RAM ... 73%"): dropping the head would remove the labels
// and leave bare numbers with no way to tell which is which.
//
// Returns true when the text was shortened.
static bool FitTextToWidth(GpGraphics *graphics, GpFont *font, GpStringFormat *format,
                           WCHAR *text, size_t capacity, float available, float boxHeight) {
    if (!text || capacity == 0 || available <= 0.0f) return false;

    int length = (int)wcslen(text);
    if (length <= 0) return false;
    if (MeasureAdvance(graphics, font, format, text, boxHeight) <= available) return false;

    // Width of the ellipsis on its own.
    //
    // This used to be cached "once per process", on the claim that "the font is fixed for
    // the lifetime of the panel". It is not: the painter rebuilds the font from the
    // settings on every frame, so after a font-size change the cache still held the width
    // for the *old* size and every trimmed run overshot its budget by the difference.
    // Measuring it here costs one extra MeasureString next to the binary search below,
    // which is not a price worth a stale global.
    const float ellipsisWidth = MeasureAdvance(graphics, font, format, L"\x2026", boxHeight);

    // A lambda captures nothing it could pass as a C callback, so the measurement is
    // reached through a small context struct instead.
    struct PrefixMeasure {
        GpGraphics *graphics;
        GpFont *font;
        GpStringFormat *format;
        const WCHAR *text;
        float boxHeight;
    };
    PrefixMeasure measure = {graphics, font, format, text, boxHeight};
    auto prefixWidth = [](int count, void *raw) -> float {
        PrefixMeasure *m = (PrefixMeasure *)raw;
        return MeasureAdvance(m->graphics, m->font, m->format, m->text, m->boxHeight, count);
    };

    int keep = FitCharsWithEllipsis(length, available, ellipsisWidth, prefixWidth, &measure);
    if (keep > 0 && (size_t)keep + 2 <= capacity) {
        text[keep] = L'\x2026';
        text[keep + 1] = L'\0';
        return true;
    }

    // No room for an ellipsis: keep the widest prefix that fits on its own.
    keep = FitCharacterCount(length, available, prefixWidth, &measure);
    if (keep < length) {
        text[keep] = L'\0';
        return true;
    }
    return false;
}

// True when a name is a variable this mod knows about, whether or not the machine
// can currently supply a value for it. LookupNumber answering "no" is not enough
// to conclude "unknown token": GPU temperature is a known variable that simply has
// no sensor on some machines, and it must render as "--" rather than being left
// on screen as literal "{gpu.temp}" text.
static bool IsKnownNumberVariable(const WCHAR *name, int len) {
#define MATCH(literal) (len == (int)(sizeof(literal) / sizeof(WCHAR)) - 1 && _wcsnicmp(name, literal, len) == 0)
    return MATCH(L"cpu") || MATCH(L"cpu.user") || MATCH(L"cpu.kernel") || MATCH(L"cpu.ghz") ||
           MATCH(L"cpu.ghz.max") || MATCH(L"cpu.cores") || MATCH(L"cpu.threads") ||
           MATCH(L"mem") || MATCH(L"mem.used") || MATCH(L"mem.total") || MATCH(L"mem.free") ||
           MATCH(L"mem.commit") || MATCH(L"net.nics") || MATCH(L"net.down.bps") ||
           MATCH(L"net.up.bps") || MATCH(L"mod.ws") || MATCH(L"mod.cpu") ||
           MATCH(L"gpu") || MATCH(L"gpu.mem") || MATCH(L"gpu.temp") || MATCH(L"gpu.fan") ||
           MATCH(L"gpu.power") || MATCH(L"gpu.vram.used") || MATCH(L"gpu.vram.total");
#undef MATCH
}

// Outcome of resolving one token.
enum LookupResult {
    LR_UNKNOWN,    // not a variable: keep the source text so a typo is obvious
    LR_RESOLVED,   // value written to the caller's buffer
    LR_UNAVAILABLE // known variable, but this machine cannot supply it
};

// Resolves {name} or {name|spec} into out. Numeric variables apply the digit spec
// to the number itself, which is why the spec is passed down rather than applied
// to an already-formatted string.
static LookupResult ResolveToken(const WCHAR *name, int nameLen, const WCHAR *spec, int specLen,
                                 const Values &v, WCHAR *out, size_t outCount) {
    WCHAR scratch[64];

    // External values first: their names only exist at run time, and the two
    // failure modes are deliberately different. Malformed -> unknown, so the
    // literal text stays visible and a typo is obvious; well formed but absent or
    // expired -> unavailable, which renders as "--".
    if (ExtIsExtToken(name, nameLen)) {
        const WCHAR *text = nullptr;
        ExtResult result = ExtLookup(name, nameLen, &text);
        ExtLogStatus(name, nameLen, result == ER_VALUE ? 1 : (result == ER_EXPIRED ? 0 : -1));
        if (result == ER_NONE) return LR_UNKNOWN;
        if (result == ER_EXPIRED) return LR_UNAVAILABLE;
        wcsncpy(out, text ? text : L"", outCount - 1);
        out[outCount - 1] = L'\0';
        if (specLen > 0) ApplySpec(spec, specLen, name, nameLen, v, out, outCount);
        return LR_RESOLVED;
    }

    const WCHAR *raw = LookupRaw(name, nameLen, v, scratch, sizeof(scratch) / sizeof(WCHAR));
    if (raw) {
        wcsncpy(out, raw, outCount - 1);
        out[outCount - 1] = L'\0';
        if (specLen > 0) ApplySpec(spec, specLen, name, nameLen, v, out, outCount);
        return LR_RESOLVED;
    }

    double number = 0;
    if (LookupNumber(name, nameLen, v, number)) {
        swprintf(out, outCount, L"%.0f", number);
        if (specLen > 0) ApplySpec(spec, specLen, name, nameLen, v, out, outCount);
        return LR_RESOLVED;
    }

    // Known but not available on this machine (for example {gpu.temp} when the GPU
    // collector has no temperature sensor). The caller shows "--".
    if (IsKnownNumberVariable(name, nameLen)) return LR_UNAVAILABLE;

    return LR_UNKNOWN;
}

// Expands every {token|spec} in source into out. Shared by the panel renderer
// and by the ring scanner, so both always agree on what a row says.
static void ExpandTokens(const WCHAR *source, const Values &v, WCHAR *out, size_t outCount) {
    size_t written = 0;
    const WCHAR *p = source;
    while (*p && written + 1 < outCount) {
        if (*p != L'{') {
            out[written++] = *p++;
            continue;
        }
        const WCHAR *end = wcschr(p + 1, L'}');
        if (!end) {
            out[written++] = *p++;
            continue;
        }
        const WCHAR *pipe = p + 1;
        while (pipe < end && *pipe != L'|') pipe++;
        int nameLen = (int)(pipe - (p + 1));
        int specLen = (*pipe == L'|') ? (int)(end - pipe - 1) : 0;

        // Sized for the longest external value the settings allow, not 128: a pushed
        // value used to be silently cut to 127 characters on its way into a row, so a
        // long value lost its tail with no ellipsis at all, even though "External value
        // length" can be set to 512. ExtSanitize marks the *stored* value with an
        // ellipsis when the provider exceeds that setting; this buffer must not add a
        // second, invisible cap on top of it.
        WCHAR value[kExtMaxValueChars + 1];
        LookupResult result = ResolveToken(p + 1, nameLen, pipe + 1, specLen, v, value,
                                           kExtMaxValueChars + 1);
        if (result == LR_UNKNOWN) {
            // Not a variable at all: keep it visible so a typo is obvious.
            size_t span = (size_t)(end - p + 1);
            if (written + span < outCount) {
                wcsncpy(out + written, p, span);
                written += span;
            }
        } else {
            // LR_UNAVAILABLE renders as "--", matching the rest of the mod.
            const WCHAR *text = result == LR_RESOLVED ? value : L"--";
            size_t span = wcslen(text);
            if (written + span < outCount) {
                wcscpy(out + written, text);
                written += span;
            }
        }
        p = end + 1;
    }
    out[written] = L'\0';
}

// ---------------------------------------------------------------------------
// Row layout
//
// A row is an alternating sequence of text and gauges, taken from the markers
// ExpandTokensAndRings leaves behind. Only slice offsets are stored, which keeps
// the sampler thread cheap and lets the painter measure text with a real
// Graphics instead of guessing at glyph widths.
// ---------------------------------------------------------------------------
static const int kMaxSegments = 12;

struct TextSeg {
    int offset;
    int length;
    // Which [[font:...]] chain this run belongs to, as a 1-based index into the settings'
    // tagFonts, or 0 for "no tag". It is carried per segment rather than per row because a
    // row may switch family mid-sentence.
    int tagChain = 0;
    // This run's own font size, or 0 for "the row's size". A [[size:NN]] run may start and
    // end anywhere in the text, so -- like the family -- the size has to live per segment.
    int runFontSize = 0;
};

struct RowState {
    WCHAR text[kMaxRows][640];
    RingSpec rings[kMaxRows][kMaxRingsPerRow];
    int ringCount[kMaxRows];
    TextSeg segments[kMaxRows][kMaxSegments];
    int segmentCount[kMaxRows];
    bool inlineRing[kMaxRows];
};

static RowState g_rowState;
static int g_publishedRows = 0;

// Private markers that remember where each gauge was written. They are stripped
// before anything is measured or drawn.
static const WCHAR kRingMarkStart = 0xE000;
static const WCHAR kRingMarkEnd = 0xE001;
// A [[font:...]] run is marked the same way: the start marker carries the chain index as a
// digit and is terminated by kRingMarkEnd (the payload terminator is shared, because all it
// means is "the marker ends here"), and the end marker stands alone. Encoding the tag as an
// index is what keeps the sampler thread cheap: it never re-validates a family name, and the
// painter never parses markup -- stripping the markers happens in exactly one place.
static const WCHAR kFontMarkStart = 0xE002;
static const WCHAR kFontMarkEnd = 0xE003;
// A [[size:NN]] run. Unlike the font marker this one carries no table index: the size is
// written into the marker itself as two decimal digits (09..48), which needs no registry and
// no lookup -- the painter can read the number straight back out of the string.
static const WCHAR kSizeMarkStart = 0xE004;
static const WCHAR kSizeMarkEnd = 0xE005;

// Expands tokens, swaps each ring token for a marker, and returns the gauges in
// source order.
static void ExpandTokensAndRings(const WCHAR *source, const Values &v,
                                 const ModSettings &settings, WCHAR *out, size_t outCount,
                                 RingSpec *rings, int *ringCount, int maxRings) {
    *ringCount = 0;
    size_t written = 0;
    const WCHAR *p = source;

    while (*p && written + 4 < outCount) {
        if (p[0] == L'[' && p[1] == L'[') {
            const WCHAR *close = wcsstr(p + 2, L"]]");
            if (close) {
                const WCHAR *body = p + 2;
                int bodyLen = (int)(close - body);
                if (bodyLen > 5 && _wcsnicmp(body, L"ring:", 5) == 0) {
                    // The ring's own VALUE / MAX can be tokens, so expand the body
                    // before parsing it.
                    WCHAR rawBody[192];
                    WCHAR bodyExpanded[192];
                    int rawLen = MinInt(bodyLen - 5, 191);
                    wcsncpy(rawBody, body + 5, rawLen);
                    rawBody[rawLen] = L'\0';
                    ExpandTokens(rawBody, v, bodyExpanded, 192);
                    bool drawn = false;
                    if (*ringCount < maxRings) {
                        RingSpec spec;
                        if (ParseRing(bodyExpanded, (int)wcslen(bodyExpanded), v, settings, spec)) {
                            out[written++] = kRingMarkStart;
                            out[written++] = (WCHAR)(L'0' + *ringCount);
                            out[written++] = kRingMarkEnd;
                            rings[*ringCount] = spec;
                            (*ringCount)++;
                            drawn = true;
                        }
                    }
                    if (drawn) {
                        p = close + 2;
                        continue;
                    }
                    // Nothing was drawn -- the body did not parse, or the row already has all
                    // the gauges it may have. Consuming the text here would delete the very
                    // characters that explain why no gauge appeared, so it falls through and is
                    // copied as ordinary text (which is what ParseRing's own contract says).
                }
                // [[font:Name]] / [[font:A;B]] opens a run in another family, [[/font]]
                // closes it. The name list was parsed and registered by ReadSettings, so all
                // this has to do is find it by its canonical text -- a tag that is not in
                // the table (mis-spelt, uninstalled, past the chain limit, or simply never
                // registered because the settings changed under it) is left in the text and
                // draws literally. That is the tolerance rule: a broken tag must never make
                // a row disappear (docs/font-and-row-metrics-design.md 3).
                if (bodyLen > 5 && _wcsnicmp(body, L"font:", 5) == 0) {
                    FontChain candidate;
                    int over = 0;
                    ParseFontChainList(body + 5, close, candidate, &over);
                    WCHAR key[kMaxFontKeyChars];
                    FontChainKey(candidate, key, kMaxFontKeyChars);
                    const int chain = FontTagIndexOf(settings.tagFonts, settings.tagFontCount, key);
                    if (chain >= 0 && written + 4 < outCount) {
                        out[written++] = kFontMarkStart;
                        out[written++] = (WCHAR)(L'1' + chain);
                        out[written++] = kRingMarkEnd;
                        p = close + 2;
                        continue;
                    }
                    // Not registered: mis-spelt, uninstalled, past the chain limit, or the
                    // settings changed under this row. It falls through and is copied as
                    // ordinary text -- eating it here would hide the only visible clue that the
                    // tag did nothing.
                }
                // The closing tag carries no payload, and its index does not matter: the runs
                // are properly nested by construction, so an end marker inside a run can only
                // close that run.
                if (bodyLen == 5 && _wcsnicmp(body, L"/font", 5) == 0) {
                    if (written + 2 < outCount) out[written++] = kFontMarkEnd;
                    p = close + 2;
                    continue;
                }
                // [[size:NN]] / [[/size]]: the same mechanism as a font run, for the font
                // size. A size tag at the very start of the row is a *row* property and was
                // already consumed by ReadSettings, so anything that reaches this point is a
                // run inside the text and is marked as one.
                if (bodyLen > 5 && _wcsnicmp(body, L"size:", 5) == 0) {
                    int size = 0;
                    if (ParseInlineSize(body + 5, close, &size) && written + 5 < outCount) {
                        out[written++] = kSizeMarkStart;
                        out[written++] = (WCHAR)(L'0' + size / 10);
                        out[written++] = (WCHAR)(L'0' + size % 10);
                        out[written++] = kRingMarkEnd;
                        p = close + 2;
                        continue;
                    }
                    // Not a size this code can draw (a word, an empty value, a number past
                    // the clamp): leave it as text rather than eating it.
                }
                if (bodyLen == 5 && _wcsnicmp(body, L"/size", 5) == 0) {
                    if (written + 2 < outCount) out[written++] = kSizeMarkEnd;
                    p = close + 2;
                    continue;
                }
            }
        }
        if (*p == L'{') {
            const WCHAR *end = wcschr(p + 1, L'}');
            if (end) {
                WCHAR token[320];
                int span = MinInt((int)(end - p) + 1, 319);
                wcsncpy(token, p, span);
                token[span] = L'\0';
                WCHAR expanded[kExtMaxValueChars + 1];
                ExpandTokens(token, v, expanded, kExtMaxValueChars + 1);
                // Copy with a per-character bounds check, mirroring ExpandTokens.
                // A single "written + 1 < outCount" guard before the whole run
                // would let a long expansion overrun the destination.
                for (const WCHAR *c = expanded; *c && written + 1 < outCount; c++)
                    out[written++] = *c;
                p = end + 1;
                continue;
            }
        }
        out[written++] = *p++;
    }
    out[written] = L'\0';
}

// Splits the marked string into text / ring segments. A ring segment is encoded
// as offset = -1 with length = ring index.
static void BuildRowLayout(const WCHAR *marked, int ringCount, RowState &state, int row) {
    state.segmentCount[row] = 0;
    state.ringCount[row] = ringCount;

    int ringIndex = 0;
    int textStart = 0;
    int position = 0;
    size_t out = 0;
    bool sawText = false;
    // The chain the *next* text segment belongs to. Text before the first tag is chain 0,
    // which is the global fallback list.
    int tagChain = 0;
    // And the size the next text segment is drawn at, 0 = the row's own size.
    int runFontSize = 0;

    // A text run ends at a marker, and every marker below flushes the run in progress before
    // moving on. That is what keeps a segment from spanning two tags -- the run that follows
    // a tag has to be a segment of its own, or the segment would have no single family or
    // size to be drawn in.
    auto flushText = [&]() {
        if (position > textStart && state.segmentCount[row] < kMaxSegments - 1) {
            TextSeg &seg = state.segments[row][state.segmentCount[row]++];
            seg.offset = textStart;
            seg.length = position - textStart;
            seg.tagChain = tagChain;
            seg.runFontSize = runFontSize;
            textStart = position;
        }
    };

    for (const WCHAR *p = marked; *p; p++) {
        if (*p == kRingMarkStart) {
            flushText();
            if (ringIndex < ringCount && state.segmentCount[row] < kMaxSegments) {
                TextSeg &seg = state.segments[row][state.segmentCount[row]++];
                seg.offset = -1;
                seg.length = ringIndex;
                if (sawText) state.inlineRing[row] = true;
            }
            ringIndex++;
            // Skip the digits and the end marker that follow.
            while (p[1] && p[1] != kRingMarkEnd) p++;
            if (p[1] == kRingMarkEnd) p++;
            continue;
        }
        if (*p == kFontMarkStart) {
            flushText();
            // The digit is the chain index; anything unexpected leaves the run in the main
            // font, which is the safe answer for a marker this code did not write.
            if (p[1] >= L'1' && p[1] <= (WCHAR)(L'0' + kMaxFontChains)) {
                tagChain = (int)(p[1] - L'0');
            }
            while (p[1] && p[1] != kRingMarkEnd) p++;
            if (p[1] == kRingMarkEnd) p++;
            continue;
        }
        if (*p == kFontMarkEnd) {
            flushText();
            tagChain = 0;
            continue;
        }
        if (*p == kSizeMarkStart) {
            flushText();
            // Two decimal digits, high first. Anything unexpected leaves the run at the
            // row's own size, which is the safe answer for a marker this code did not write.
            if (p[1] >= L'0' && p[1] <= L'9' && p[2] >= L'0' && p[2] <= L'9') {
                runFontSize = ClampInt(((int)(p[1] - L'0')) * 10 + (int)(p[2] - L'0'),
                                       kMinRowFontSize, kMaxRowFontSize);
            }
            while (p[1] && p[1] != kRingMarkEnd) p++;
            if (p[1] == kRingMarkEnd) p++;
            continue;
        }
        if (*p == kSizeMarkEnd) {
            flushText();
            // Back to the row's own size. Runs deliberately do not nest: [[/size]] always
            // returns to the row, the same way [[/font]] always returns to the global chain.
            runFontSize = 0;
            continue;
        }
        if (*p == kRingMarkEnd) continue;
        state.text[row][out++] = *p;
        position++;
        if (*p != L' ' && *p != L'\t') sawText = true;
    }
    if (position > textStart && state.segmentCount[row] < kMaxSegments) {
        TextSeg &seg = state.segments[row][state.segmentCount[row]++];
        seg.offset = textStart;
        seg.length = position - textStart;
        seg.tagChain = tagChain;
        seg.runFontSize = runFontSize;
    }
    state.text[row][out] = L'\0';
}

// ---------------------------------------------------------------------------
// Ring gauges
// ---------------------------------------------------------------------------
// Parses "VALUE[:MAX[:THICK[:COLOUR]]]", expanding {variable} tokens in the
// first three fields. Returns false when any numeric field is unusable, so the
// caller can fall back to showing the raw text instead of a wrong gauge.
static bool ParseRing(const WCHAR *body, int bodyLen, const Values &v,
                      const ModSettings &settings, RingSpec &out) {
    if (bodyLen <= 0) return false;

    WCHAR buffer[192];
    int len = bodyLen < 191 ? bodyLen : 191;
    wcsncpy(buffer, body, len);
    buffer[len] = L'\0';

    WCHAR *fields[5] = {};
    int fieldCount = 0;
    fields[fieldCount++] = buffer;
    for (WCHAR *p = buffer; *p && fieldCount < 5; p++) {
        if (*p == L':') {
            *p = L'\0';
            fields[fieldCount++] = p + 1;
        }
    }

    // A field is either a number or a single {token}; anything else is an error.
    auto resolve = [&](const WCHAR *field, double &number) -> bool {
        if (!field || !*field) return false;
        const WCHAR *text = field;
        const WCHAR *trimmed = field;
        while (*trimmed == L' ') trimmed++;
        if (*trimmed == L'{') {
            const WCHAR *end = wcschr(trimmed + 1, L'}');
            if (!end) return false;
            int nameLen = (int)(end - trimmed - 1);
            WCHAR scratch[64];
            const WCHAR *raw = LookupRaw(trimmed + 1, nameLen, v, scratch, 64);
            if (!raw && ExtIsExtToken(trimmed + 1, nameLen)) {
                // A gauge can be driven by an external value too. The caller holds
                // g_extLock while expanding, so this read is consistent.
                const WCHAR *extText = nullptr;
                if (ExtLookup(trimmed + 1, nameLen, &extText) == ER_VALUE) raw = extText;
            }
            if (!raw) return false;
            text = raw;
        }
        WCHAR *stop = nullptr;
        double parsed = wcstod(text, &stop);
        if (stop == text) return false;
        number = parsed;
        return true;
    };

    double value = 0, max = 100;
    if (!resolve(fields[0], value)) return false;
    if (fieldCount > 1) {
        if (!resolve(fields[1], max)) return false;
    }
    if (max <= 0) return false;

    out.value = value;
    out.max = max;

    if (fieldCount > 2 && fields[2] && *fields[2]) {
        double thickness = 0;
        if (resolve(fields[2], thickness) && thickness >= 1 && thickness <= 16)
            out.thickness = (int)thickness;
        else
            out.thickness = settings.ringThickness;
    } else {
        out.thickness = settings.ringThickness;
    }

    if (fieldCount > 3 && fields[3] && *fields[3]) {
        const WCHAR *colorText = fields[3];
        while (*colorText == L' ') colorText++;
        if (*colorText == L'#') colorText++;
        WCHAR *stop = nullptr;
        unsigned long long parsed = wcstoull(colorText, &stop, 16);
        if (stop != colorText) {
            out.color = (DWORD)parsed;
            out.hasColor = true;
        }
    }
    return true;
}

static void DrawRing(GpGraphics *graphics, float centerX, float centerY, float radius,
                     float thickness, double fraction, DWORD trackColor, DWORD valueColor) {
    if (fraction < 0) fraction = 0;
    if (fraction > 1) fraction = 1;

    GpPen *trackPen = nullptr;
    if (g_gdiplus.CreatePen1(trackColor, thickness, 0, &trackPen) == 0 && trackPen) {
        // Track: a full circle, so a zero-value gauge still reads as a gauge.
        g_gdiplus.DrawEllipse(graphics, trackPen, centerX - radius, centerY - radius,
                              radius * 2.0f, radius * 2.0f);
        g_gdiplus.DeletePen(trackPen);
    }
    if (fraction <= 0.0) return;

    GpPen *valuePen = nullptr;
    if (g_gdiplus.CreatePen1(valueColor, thickness, 0, &valuePen) != 0 || !valuePen) return;

    // Value arc: 12 o'clock, clockwise, stroked as a polyline (GDI+ has no arc).
    const float start = -1.57079633f;  // -90 degrees
    const float sweep = (float)(fraction * 6.28318531);
    int segments = (int)(kRingSegments * fraction) + 2;
    if (segments > kRingSegments * 2) segments = kRingSegments * 2;

    POINTF *points = (POINTF *)malloc(sizeof(POINTF) * (segments + 1));
    if (points) {
        for (int i = 0; i <= segments; i++) {
            float angle = start + sweep * ((float)i / (float)segments);
            points[i].x = centerX + radius * (float)cos(angle);
            points[i].y = centerY + radius * (float)sin(angle);
        }
        g_gdiplus.DrawLines(graphics, valuePen, points, segments + 1);
        free(points);
    }
    g_gdiplus.DeletePen(valuePen);
}

// ---------------------------------------------------------------------------
// Monitors
// ---------------------------------------------------------------------------
struct MonitorEntry {
    RECT rect;
    bool primary;
};
static MonitorEntry g_monitors[kMaxMonitors];
static int g_monitorCount = 0;

static BOOL CALLBACK MonitorEnumProc(HMONITOR monitor, HDC, LPRECT, LPARAM) {
    if (g_monitorCount >= kMaxMonitors) return TRUE;
    MONITORINFOEXW mi = {};
    mi.cbSize = sizeof(mi);
    if (GetMonitorInfoW(monitor, &mi)) {
        g_monitors[g_monitorCount].rect = mi.rcMonitor;
        g_monitors[g_monitorCount].primary = (mi.dwFlags & MONITORINFOF_PRIMARY) != 0;
        g_monitorCount++;
    }
    return TRUE;
}

static void RefreshMonitors() {
    g_monitorCount = 0;
    EnumDisplayMonitors(nullptr, nullptr, MonitorEnumProc, 0);
}

// Highest on the virtual desktop wins; ties go to the primary.
static int TopmostMonitorIndex() {
    if (g_monitorCount <= 0) return -1;
    int best = 0;
    for (int i = 1; i < g_monitorCount; i++) {
        if (g_monitors[i].rect.top < g_monitors[best].rect.top) best = i;
        else if (g_monitors[i].rect.top == g_monitors[best].rect.top &&
                 g_monitors[i].primary && !g_monitors[best].primary) best = i;
    }
    return best;
}

static int PrimaryMonitorIndex() {
    for (int i = 0; i < g_monitorCount; i++)
        if (g_monitors[i].primary) return i;
    return g_monitorCount > 0 ? 0 : -1;
}

// ---------------------------------------------------------------------------
// Mod-level state
//
// Concurrency contract:
//   loader thread  - WhTool_ModInit / SettingsChanged / Uninit
//   HUD thread     - owns g_window, paints, pumps its own messages
//   sampler thread - collects and publishes the row arrays and g_values
//
// g_settings is written by the loader thread and read by the sampler thread, so
// every access goes through g_settingsLock. A version counter lets the sampler
// notice a row change and re-evaluate which collectors it needs.
// ---------------------------------------------------------------------------
static ModSettings g_settings;
static CRITICAL_SECTION g_settingsLock;
static volatile LONG g_settingsVersion = 0;
static LONG g_samplerSeenVersion = -1;

static ULONG_PTR g_gdiplusToken = 0;
static HWND g_window = nullptr;
static HANDLE g_hudThread = nullptr;
static DWORD g_hudThreadId = 0;
static HANDLE g_hudReady = nullptr;
static HANDLE g_samplerThread = nullptr;
static volatile LONG g_stopSampler = 0;
static volatile LONG g_refreshMs = 1000;

// Display state.
//
// Three independent booleans, which is what makes the two click behaviours work:
//   g_visible     - is the panel hidden entirely? A click flips this in Toggle mode.
//   g_modeProximity - is the panel on the mouse? A click flips this in Proximity mode.
//   g_expanded    - is it drawn at full height, or collapsed into its tab?
//
// Collapsed is NOT hidden: the collapsed state is a small always-clickable tab at
// the top edge. Otherwise a hidden panel could never be clicked back, which is
// exactly the trap the previous version fell into.
static const int kTabHeight = 8;
// Distance from the monitor's top edge to the window's top edge. Shared by
// PlaceWindow and CursorInHotZone so the hot zone always begins at the panel's
// real top rather than at the monitor edge.
static const int kPanelTopOffset = 6;

static bool g_visible = true;             // Toggle mode: shown vs hidden
static bool g_modeProximity = true;       // Proximity mode active?
static bool g_expanded = true;            // painted at full height
static DWORD g_hideAt = 0;                // proximity linger deadline
static int g_visibleRows = 0;
static int g_panelHeight = 0;             // expanded height
// Resting height of the window: what PlaceWindow and WM_NCHITTEST use. Set only
// by ComputeGeometry, so both agree with each other at all times.
static int g_displayHeight = 0;
// Resting width. It is the panel width normally and the card's target width while a
// card is up, so window placement and the hit test read one number instead of each
// deriving their own (design 8: the same rule that g_displayHeight already enforces
// for height). Only the HUD thread writes it.
static int g_restWidth = 0;
// Height the last frame actually painted. The intro animation drives this, and it
// is deliberately separate from g_displayHeight: writing the animated height back
// into the geometry value made the window and its hit-test rectangle shrink to
// tab size mid-animation, which read as the panel collapsing the moment it opened.
static int g_paintedHeight = 0;
static int g_anchorIndex = -1;
static DWORD g_lastTopmostAssert = 0;

static unsigned long long g_perfBaseTick = 0, g_perfBaseCpu100ns = 0;
static unsigned long long g_sampleCount = 0, g_paintCount = 0;
// Repaints caused by a pushed value re-expanding the rows, as opposed to a real
// sample. They are counted apart because they must not cost the same: see
// RepublishRows. Printed by the periodic [perf] line, which is the only way to see
// from the outside whether a chatty publisher is driving the panel.
static unsigned long long g_republishCount = 0;

static unsigned long long CpuTime100ns() {
    FILETIME c, e, k, u;
    if (!GetProcessTimes(GetCurrentProcess(), &c, &e, &k, &u)) return 0;
    return ((unsigned long long)k.dwHighDateTime << 32 | k.dwLowDateTime) +
           ((unsigned long long)u.dwHighDateTime << 32 | u.dwLowDateTime);
}

// ---------------------------------------------------------------------------
// Custom icon image
//
// GDI+ decodes PNG, JPEG, BMP, GIF and TIFF through GdipLoadImageFromFile, so any
// of those work. SVG does NOT: GDI+ has no SVG support, and adding it would mean
// writing an XML parser plus an SVG path-data interpreter (arcs have to become
// beziers, because GDI+ has no arc primitive) and guessing at stroke/fill
// semantics. That is a large amount of unverifiable code for a 23 px glyph, so an
// .svg path is rejected with a clear log message rather than silently ignored.
//
// The decoded image is cached because this is on the paint path: re-reading and
// re-decoding a file every frame at 16 ms intervals would be visible.
// ---------------------------------------------------------------------------
// One decoded image per icon slot, so each row can carry its own file. The decoded
// image is cached because this is on the paint path: re-reading and re-decoding a
// file every frame at 16 ms intervals would be visible.
//
// The decoded source is cached, and a second, resampled copy at the exact plate
// size is cached alongside it. Drawing the source every frame would make GDI+
// resample the whole file once per frame, which for a large image is both slow and
// aliased (a naive single-step shrink skips source pixels whenever the ratio drops
// below 0.5).
static GpImage *g_iconImage[kMaxRows] = {};
static WCHAR g_iconImagePath[kMaxRows][MAX_PATH] = {};
// Bounded diagnostic counter: prints what the paint path decided for the first few
// frames after a change, then stops.
static int g_iconDrawLogs = 0;
// The resampled copy actually drawn, and the size it was built for. The plate
// diameter follows FontSize, so changing the font has to invalidate this.
static GpImage *g_iconScaled[kMaxRows] = {};
static int g_iconScaledFor[kMaxRows] = {};
// A path that already failed once, per slot. Without this, a typo'd path would be
// re-read from disk and re-logged on every painted frame.
static WCHAR g_iconFailedPath[kMaxRows][MAX_PATH] = {};

static bool PathEndsWith(const WCHAR *path, const WCHAR *suffix) {
    size_t pathLen = wcslen(path), suffixLen = wcslen(suffix);
    if (suffixLen > pathLen) return false;
    return _wcsicmp(path + pathLen - suffixLen, suffix) == 0;
}

static void ReleaseSlotImage(int slot) {
    if (slot < 0 || slot >= kMaxRows) return;
    if (g_iconScaled[slot]) {
        g_gdiplus.DisposeImage(g_iconScaled[slot]);
        g_iconScaled[slot] = nullptr;
    }
    g_iconScaledFor[slot] = 0;
    if (g_iconImage[slot]) {
        g_gdiplus.DisposeImage(g_iconImage[slot]);
        g_iconImage[slot] = nullptr;
    }
    g_iconImagePath[slot][0] = L'\0';
}

// Plans the resampling chain for one image: a sequence of halving steps followed by
// a final step to the exact target box, aspect ratio preserved.
//
// Factored out of the drawing code so the reduction plan is testable. A single
// large reduction step makes the resampler emit only a few samples of the source,
// which loses detail and aliases; halving first keeps every source pixel
// contributing. `steps` counts the halving intermediates, `final` is the last entry.
struct DownscalePlan {
    int count = 0;          // entries filled in `sizes`
    int steps = 0;          // how many are halving intermediates
    struct { int w, h; } sizes[16] = {};
};

static DownscalePlan PlanDownscale(int srcW, int srcH, int target) {
    DownscalePlan plan = {};
    if (srcW <= 0 || srcH <= 0 || target <= 0) return plan;

    int w = srcW, h = srcH;
    int guard = 0;
    while (guard++ < 12) {
        if (w <= target * 2 && h <= target * 2) break;
        // Halve both axes. That alone keeps the aspect ratio -- what distorted a 2:1
        // source into 23x17 was clamping the two axes *independently*, and the
        // proportional repair that replaced it had a fixed point: whenever the short
        // axis was already at or below the target, the repair scaled it back up to the
        // target, the "never grow" clamp below put it straight back, and the step
        // changed nothing. A 200x20 banner therefore did twelve 1:1 resamples and then
        // one 173x reduction -- the aliasing this plan exists to prevent.
        //
        // The short axis is allowed to fall below the target here: it is an
        // intermediate, and for a 200:1 source no chain of half-steps keeps both axes
        // at or above 23. Only the long axis is what the final fit lands on the box.
        int nw = w / 2, nh = h / 2;
        if (nw < 1) nw = 1;
        if (nh < 1) nh = 1;
        if (nw == w && nh == h) break;   // a 1 px axis: nothing left to halve
        w = nw;
        h = nh;
        if (plan.count < 15) {
            plan.sizes[plan.count].w = w;
            plan.sizes[plan.count].h = h;
            plan.count++;
        }
    }
    plan.steps = plan.count;

    // Final step to the exact box, aspect ratio preserved.
    float fit = (float)target / (float)(w > h ? w : h);
    int fw = (int)((float)w * fit + 0.5f);
    int fh = (int)((float)h * fit + 0.5f);
    if (fw < 1) fw = 1;
    if (fh < 1) fh = 1;
    if (plan.count < 16) {
        plan.sizes[plan.count].w = fw;
        plan.sizes[plan.count].h = fh;
        plan.count++;
    }
    return plan;
}

// Reports what a bitmap actually contains, by sampling a grid of pixels. Used to
// tell "the file has no colour" apart from "the renderer lost the colour", which
// look identical on screen when the plate behind the icon is light.
static void LogBitmapContent(const WCHAR *label, GpImage *image) {
    if (!image || !g_gdiplus.BitmapGetPixel) return;
    UINT w = 0, h = 0;
    if (g_gdiplus.GetImageWidth) g_gdiplus.GetImageWidth(image, &w);
    if (g_gdiplus.GetImageHeight) g_gdiplus.GetImageHeight(image, &h);
    if (w == 0 || h == 0) return;

    DWORD sumR = 0, sumG = 0, sumB = 0, sumA = 0;
    int samples = 0, opaque = 0;
    // A 5x5 grid is enough to characterise the image and costs 25 calls.
    for (int gy = 0; gy < 5; gy++) {
        for (int gx = 0; gx < 5; gx++) {
            INT x = (INT)((gx + 0.5) * w / 5);
            INT y = (INT)((gy + 0.5) * h / 5);
            DWORD px = 0;
            if (g_gdiplus.BitmapGetPixel((GpBitmap *)image, x, y, &px) != 0) continue;
            DWORD a = (px >> 24) & 0xFF;
            sumR += (px >> 16) & 0xFF;
            sumG += (px >> 8) & 0xFF;
            sumB += px & 0xFF;
            sumA += a;
            samples++;
            if (a > 200) opaque++;
        }
    }
    if (samples == 0) return;
    DBG(L"%s: %ux%u sampled RGBA avg = %u,%u,%u,%u over %d px (%d nearly opaque)",
           label, w, h, sumR / samples, sumG / samples, sumB / samples, sumA / samples,
           samples, opaque);
}

// Resamples `source` down to fit a `target` pixel box, preserving aspect ratio.
//
// Halves repeatedly before the final resize. A single large reduction step makes
// GDI+ output only a few samples of the source -- the text-book aliasing case --
// whereas each halving keeps every source pixel contributing. The final step is
// then within 2x, where bicubic is well behaved. This is the standard mipmap
// approach, and it runs once per loaded file, not per frame.
static GpImage *DownscaleToFit(GpImage *source, int target) {
    UINT w = 0, h = 0;
    if (g_gdiplus.GetImageWidth) g_gdiplus.GetImageWidth(source, &w);
    if (g_gdiplus.GetImageHeight) g_gdiplus.GetImageHeight(source, &h);
    if (w == 0 || h == 0 || target <= 0) return nullptr;

    // Guard the intermediate allocations: a 20000 px image would otherwise build
    // full-size halving steps before shrinking.
    if (w > 4096 || h > 4096) return nullptr;

    DownscalePlan plan = PlanDownscale((int)w, (int)h, target);
    if (plan.count == 0) return nullptr;

    GpImage *current = source;
    bool ownsCurrent = false;
    for (int i = 0; i < plan.count; i++) {
        int nw = plan.sizes[i].w;
        int nh = plan.sizes[i].h;

        GpBitmap *next = nullptr;
        if (!g_gdiplus.CreateBitmapFromScan0 || !g_gdiplus.GetImageGraphicsContext) break;
        if (g_gdiplus.CreateBitmapFromScan0(nw, nh, 0, kPixelFormat32bppPARGB, nullptr,
                                            &next) != 0 || !next) break;

        GpGraphics *ng = nullptr;
        bool ok = false;
        if (g_gdiplus.GetImageGraphicsContext((GpImage *)next, &ng) == 0 && ng) {
            if (g_gdiplus.SetInterpolationMode) {
                g_gdiplus.SetInterpolationMode(ng, kInterpolationHighQualityBicubic);
            }
            ok = g_gdiplus.DrawImageRect(ng, current, 0.0f, 0.0f, (float)nw, (float)nh) == 0;
        }
        if (!ok) {
            g_gdiplus.DisposeImage((GpImage *)next);
            break;
        }

        if (ownsCurrent && current) g_gdiplus.DisposeImage(current);
        current = (GpImage *)next;
        ownsCurrent = true;
    }

    if (!ownsCurrent) return nullptr;   // nothing was produced
    return current;
}

// Returns the image to draw for this slot: resampled to the plate size, or the
// decoded source when no resampling was needed.
//
// target is the plate diameter in pixels, which follows FontSize. The resampled
// copy is rebuilt when that size changes, so a font-size change does not leave a
// stale bitmap at the wrong resolution.
static GpImage *IconImageFor(int slot, const WCHAR *path, int target, bool *failed) {
    *failed = false;
    if (slot < 0 || slot >= kMaxRows) return nullptr;
    if (!path || !path[0]) return nullptr;

    if (g_iconImage[slot] && _wcsicmp(g_iconImagePath[slot], path) == 0) {
        if (g_iconScaled[slot] && g_iconScaledFor[slot] == target) return g_iconScaled[slot];
        if (!g_iconScaled[slot]) {
            // Already decided this file needs no resampling.
            UINT w = 0, h = 0;
            if (g_gdiplus.GetImageWidth) g_gdiplus.GetImageWidth(g_iconImage[slot], &w);
            if (g_gdiplus.GetImageHeight) g_gdiplus.GetImageHeight(g_iconImage[slot], &h);
            if ((int)w <= target && (int)h <= target) return g_iconImage[slot];
        }
        // Target size changed: rebuild the resampled copy.
        if (g_iconScaled[slot]) {
            g_gdiplus.DisposeImage(g_iconScaled[slot]);
            g_iconScaled[slot] = nullptr;
        }
        g_iconScaled[slot] = DownscaleToFit(g_iconImage[slot], target);
        g_iconScaledFor[slot] = g_iconScaled[slot] ? target : 0;
        if (g_iconScaled[slot]) return g_iconScaled[slot];
        // Resampling failed; fall back to the source rather than dropping the icon.
        UINT w2 = 0, h2 = 0;
        if (g_gdiplus.GetImageWidth) g_gdiplus.GetImageWidth(g_iconImage[slot], &w2);
        if (g_gdiplus.GetImageHeight) g_gdiplus.GetImageHeight(g_iconImage[slot], &h2);
        if ((int)w2 <= target && (int)h2 <= target) return g_iconImage[slot];
        *failed = true;
        return nullptr;
    }
    if (g_iconFailedPath[slot][0] && _wcsicmp(g_iconFailedPath[slot], path) == 0) {
        *failed = true;
        return nullptr;
    }

    // Settings changed (or first call): drop this slot's old images and load anew.
    ReleaseSlotImage(slot);
    g_iconFailedPath[slot][0] = L'\0';

    if (PathEndsWith(path, L".svg")) {
        Wh_Log(L"Icon%d: SVG is not supported (GDI+ has no SVG decoder); "
               L"using the built-in glyph. Export the icon as PNG instead.",
               slot + 1);
        *failed = true;
        wcsncpy(g_iconFailedPath[slot], path, MAX_PATH - 1);
        return nullptr;
    }

    GpImage *image = nullptr;
    if (!g_gdiplus.LoadImageFromFile || g_gdiplus.LoadImageFromFile(path, &image) != 0 || !image) {
        Wh_Log(L"Icon%d: could not load \"%s\"; using the built-in glyph", slot + 1, path);
        *failed = true;
        wcsncpy(g_iconFailedPath[slot], path, MAX_PATH - 1);
        return nullptr;
    }

    UINT w = 0, h = 0;
    if (g_gdiplus.GetImageWidth) g_gdiplus.GetImageWidth(image, &w);
    if (g_gdiplus.GetImageHeight) g_gdiplus.GetImageHeight(image, &h);

    g_iconImage[slot] = image;
    wcsncpy(g_iconImagePath[slot], path, MAX_PATH - 1);
    g_iconImagePath[slot][MAX_PATH - 1] = L'\0';

    if ((int)w <= target && (int)h <= target) {
        // Smaller than the plate: drawing it directly never upsamples, and the
        // caller scales it, so there is nothing to precompute.
        if (g_iconDrawLogs < 4) LogBitmapContent(L"Icon source (as-is)", image);
        Wh_Log(L"Icon%d: loaded \"%s\" (%ux%u), used as-is (plate is %d px)",
               slot + 1, path, w, h, target);
        return image;
    }

    g_iconScaled[slot] = DownscaleToFit(image, target);
    g_iconScaledFor[slot] = g_iconScaled[slot] ? target : 0;
    if (g_iconScaled[slot]) {
        UINT sw = 0, sh = 0;
        if (g_gdiplus.GetImageWidth) g_gdiplus.GetImageWidth(g_iconScaled[slot], &sw);
        if (g_gdiplus.GetImageHeight) g_gdiplus.GetImageHeight(g_iconScaled[slot], &sh);
        // Sampling the resampled copy is what distinguishes "the file really is
        // white" from "resampling destroyed the colours".
        LogBitmapContent(L"Icon resampled", g_iconScaled[slot]);
        Wh_Log(L"Icon%d: loaded \"%s\" (%ux%u) -> resampled to %ux%u for a %d px plate",
               slot + 1, path, w, h, sw, sh, target);
        return g_iconScaled[slot];
    }

    Wh_Log(L"Icon%d: loaded \"%s\" (%ux%u); resampling failed, drawing the source",
           slot + 1, path, w, h);
    return image;
}

static void DisposeIconImage() {
    for (int i = 0; i < kMaxRows; i++) {
        ReleaseSlotImage(i);
        g_iconFailedPath[i][0] = L'\0';
    }
    g_iconDrawLogs = 0;   // report again after a settings change
}

// ---------------------------------------------------------------------------
// Left icon plate
//
// The original capsule has a light disc at its left end with a dark glyph on it
// (Hud.IconPlate #E9E7E4 over Hud.BoltDark #141313). It is the most recognisable
// thing about the HUD, so the mod reproduces the plate and draws its own glyphs
// as plain primitives: a chip, a memory module, a platter, a link and a fan.
// ---------------------------------------------------------------------------
static void PaintIconGlyph(GpGraphics *graphics, int style, float cx, float cy,
                           float size, GpSolidFill *fill) {
    const float h = size / 2.0f;
    GpBrush *brush = (GpBrush *)fill;
    switch (style) {
    case 1: {   // CPU: chip body with a die window, pins on all four sides
        float body = size * 0.58f, die = size * 0.24f, pin = size * 0.09f;
        g_gdiplus.FillRectangle(graphics, brush, cx - body / 2, cy - body / 2, body, body);
        g_gdiplus.FillRectangle(graphics, brush, cx - die / 2, cy - die / 2, die, die);
        for (int i = -1; i <= 1; i++) {
            float along = (float)i * size * 0.20f;
            g_gdiplus.FillRectangle(graphics, brush, cx + along - pin / 2, cy - h, pin, h - body / 2);
            g_gdiplus.FillRectangle(graphics, brush, cx + along - pin / 2, cy + body / 2, pin, h - body / 2);
            g_gdiplus.FillRectangle(graphics, brush, cx - h, cy + along - pin / 2, h - body / 2, pin);
            g_gdiplus.FillRectangle(graphics, brush, cx + body / 2, cy + along - pin / 2, h - body / 2, pin);
        }
        break;
    }
    case 2: {   // Memory: a filled module with one notch, kept chunky so it stays
                // legible at the plate's ~23 px size.
        float body = size * 0.74f, thickness = size * 0.40f;
        g_gdiplus.FillRectangle(graphics, brush, cx - body / 2, cy - thickness / 2,
                                body, thickness);
        float notch = size * 0.10f;
        g_gdiplus.FillRectangle(graphics, (GpBrush *)fill, cx - notch / 2, cy - thickness / 2,
                                notch, thickness);
        break;
    }
    case 3: {   // Disk: platter outline with a spindle
        float radius = size * 0.44f;
        GpPath *ring = nullptr;
        g_gdiplus.CreatePath(0, &ring);
        if (ring) {
            g_gdiplus.AddPathArc(ring, cx - radius, cy - radius, radius * 2, radius * 2, 0, 359.9f);
            g_gdiplus.ClosePathFigure(ring);
            GpSolidFill *plate = nullptr;
            g_gdiplus.CreateSolidFill(kPlateLight, &plate);
            if (plate) {
                g_gdiplus.FillPath(graphics, (GpBrush *)plate, ring);
                g_gdiplus.DeleteBrush((GpBrush *)plate);
            }
            g_gdiplus.DeletePath(ring);
        }
        float hub = size * 0.16f;
        GpSolidFill *hubFill = nullptr;
        g_gdiplus.CreateSolidFill(kGlyphDark, &hubFill);
        if (hubFill) {
            g_gdiplus.FillRectangle(graphics, (GpBrush *)hubFill, cx - hub / 2, cy - hub / 2, hub, hub);
            g_gdiplus.DeleteBrush((GpBrush *)hubFill);
        }
        break;
    }
    case 4: {   // Network: two ascending bars plus a drop
        float bar = size * 0.16f;
        g_gdiplus.FillRectangle(graphics, brush, cx - size * 0.30f, cy, bar, size * 0.30f);
        g_gdiplus.FillRectangle(graphics, brush, cx - size * 0.07f, cy - size * 0.16f, bar, size * 0.46f);
        g_gdiplus.FillRectangle(graphics, brush, cx + size * 0.16f, cy - size * 0.32f, bar, size * 0.62f);
        break;
    }
    case 5: {   // GPU: shroud with a fan
        float body = size * 0.64f;
        g_gdiplus.FillRectangle(graphics, brush, cx - body / 2, cy - body / 2, body, body);
        GpSolidFill *inner = nullptr;
        g_gdiplus.CreateSolidFill(kPlateLight, &inner);
        if (inner) {
            float radius = size * 0.19f;
            GpPath *fan = nullptr;
            g_gdiplus.CreatePath(0, &fan);
            if (fan) {
                g_gdiplus.AddPathArc(fan, cx - radius, cy - radius, radius * 2, radius * 2, 0, 359.9f);
                g_gdiplus.ClosePathFigure(fan);
                g_gdiplus.FillPath(graphics, (GpBrush *)inner, fan);
                g_gdiplus.DeletePath(fan);
            }
            g_gdiplus.DeleteBrush((GpBrush *)inner);
        }
        break;
    }
    default:
        break;
    }
}

// Returns the plate diameter for the current settings, or 0 when no icon is on.
// A custom image counts as an icon even when no built-in glyph was chosen, so
// setting only a path is enough to get a plate.
// Diameter of the plate for one row, or 0 when that row has no icon.
//
// The diameter follows the **global** Font size, never a row's own size (neither
// `Row N font size` nor a `[[size:…]]` run). The plate is a panel element, not a piece of
// the row's text: making it grow with the row's font meant that setting one row to a larger
// size also redrew its icon larger, which is not what the row asked for, and it moved the
// text column of every row around with it. `slot` therefore only decides *whether* there is
// a plate.
static float IconPlateDiameter(const ModSettings &settings, int slot) {
    if (ResolveIconStyle(settings, slot) <= 0 && !ResolveIconPath(settings, slot)[0]) {
        return 0.0f;
    }
    return (float)settings.fontSize * (float)kIconPlateRatio;
}

// The plate diameter to reserve room for in the shared text column: the widest plate any row
// will draw. Every plate is the same size, so this only has to cope with the rows that have
// **no** plate at all -- the first row may well be one of those (`Row 1 icon = None`), and
// reading the diameter off row 0 specifically would then inset the text by nothing while the
// rows below it still draw a disc under it.
static float MaxIconPlateDiameter(const ModSettings &settings) {
    float widest = 0.0f;
    for (int i = 0; i < kMaxRows; i++) {
        const float diameter = IconPlateDiameter(settings, i);
        if (diameter > widest) widest = diameter;
    }
    return widest;
}

// True when at least one row will have a plate. Asked through the same function the
// painter uses for each row, so "is the plate margin needed" and "does this row get a
// plate" cannot answer differently -- and an explicit `None` on every row is not a
// configuration that wants a margin.
static bool AnyIconConfigured(const ModSettings &settings) {
    for (int i = 0; i < kMaxRows; i++) {
        if (IconPlateDiameter(settings, i) > 0.0f) return true;
    }
    return false;
}

// Centre of the icon plate for one row, in the panel's own (untransformed)
// coordinates. All plates share a size, so the X offset is the same for every row;
// only the vertical position varies.
static void IconPlateCenterForRow(const ModSettings &settings, int slot, float rowTop,
                                  float rowHeight, float *outX, float *outY) {
    float diameter = IconPlateDiameter(settings, slot);
    *outX = kIconLeftMargin + diameter / 2.0f;
    *outY = rowTop + rowHeight / 2.0f;
}

// ---------------------------------------------------------------------------
// Plate and ring geometry.
//
// The plate and the rings around it are one object as far as the eye is concerned,
// so they are derived here together rather than by two code paths that each work
// out "where is the icon" for themselves. They differ in size on purpose -- the
// plate is the disc, the rings are larger and expand outward from it -- but they
// share exactly one centre, and that centre is what the rings are built around.
// ---------------------------------------------------------------------------
struct IconGeometry {
    bool present = false;
    float centerX = 0.0f;
    float centerY = 0.0f;
    float plateRadius = 0.0f;   // the icon disc; the rings start from this radius
};

// rowTop is the row's own offset, so one call describes exactly one plate and its
// rings. Passing the row offset matters: a fixed offset would put every row's rings
// on row 1.
static IconGeometry ComputeIconGeometry(const ModSettings &settings, int slot,
                                        float rowTop) {
    IconGeometry g = {};
    float diameter = IconPlateDiameter(settings, slot);
    if (diameter <= 0.0f) return g;
    g.present = true;
    // The row's own height: a row with a taller font is a taller box, and the plate belongs
    // in the middle of the box it is drawn in, not in the middle of the default one.
    IconPlateCenterForRow(settings, slot, rowTop, (float)RowHeightPx(settings, slot),
                          &g.centerX, &g.centerY);
    g.plateRadius = diameter / 2.0f;
    // Rings are larger than the plate and measured from this radius (RingRadiusAt), so
    // the innermost ring starts at the plate's edge rather than on top of the glyph.
    return g;
}

// Ring radius for a given expansion progress, measured from the shared centre.
//
// The ring starts AT the plate's edge and grows outward from there, so its
// innermost state is a ring grazing the plate rather than a smaller circle sitting
// on top of the glyph. Starting below the plate radius is what made the rings look
// concentric with the glyph instead of with the plate.
static float RingRadiusAt(const IconGeometry &geometry, int ringIndex, double progress) {
    double endScale = kRippleScaleEnd[ringIndex < 3 ? ringIndex : 2];
    double outer = geometry.plateRadius * endScale;
    return (float)Lerp(geometry.plateRadius, outer, progress);
}

// The size a cached icon image is drawn at.
//
// The intro scales the whole plate in, so the image has to follow it. It did not: the
// scale was applied only inside the branch for images *larger* than the plate, and a
// cached image is already exactly plate-sized, so a custom icon merely faded in while its
// plate grew. The "never upsample past the natural size" rule caps the base size; the
// animation multiplies that cap, it is not a second cap.
static void ComputeIconImageDrawSize(int imageW, int imageH, float plateRadius,
                                     float iconScale, float *outW, float *outH) {
    float w = (float)imageW;
    float h = (float)imageH;
    const float cap = plateRadius * 2.0f;
    if (w > cap && w > 0.0f) {
        const float k = cap / w;
        w = cap;
        h *= k;
    }
    const float scale = iconScale > 0.0f ? iconScale : 0.0f;
    if (outW) *outW = w * scale;
    if (outH) *outH = h * scale;
}

// Draws one plate, at the centre and radius the shared geometry reports.
static void PaintIconPlate(GpGraphics *graphics, const ModSettings &settings,
                           const IntroFrame &frame, int slot, const IconGeometry &geometry) {
    if (!geometry.present) return;
    float scale = (float)(frame.iconScale > 0.0 ? frame.iconScale : 0.0);
    if (scale <= 0.001f) return;
    float radius = geometry.plateRadius * scale;
    float centerX = geometry.centerX;
    float centerY = geometry.centerY;

    // Circular plate. GDI+ has no arc primitive, so the disc is filled from a
    // closed two-arc path and the glyph is drawn inside the same transform.
    BYTE alpha = (BYTE)(Clamp01(frame.iconAlpha) * 255.0 + 0.5);
    GpPath *disc = nullptr;
    g_gdiplus.CreatePath(0, &disc);
    if (!disc) return;
    g_gdiplus.AddPathArc(disc, centerX - radius, centerY - radius, radius * 2, radius * 2, 0, 359.9f);
    g_gdiplus.ClosePathFigure(disc);

    GpSolidFill *plate = nullptr;
    g_gdiplus.CreateSolidFill((kPlateLight & 0x00FFFFFF) | ((DWORD)alpha << 24), &plate);
    if (plate) {
        g_gdiplus.FillPath(graphics, (GpBrush *)plate, disc);
        g_gdiplus.DeleteBrush((GpBrush *)plate);
    }
    g_gdiplus.DeletePath(disc);

    GpSolidFill *glyph = nullptr;
    g_gdiplus.CreateSolidFill((kGlyphDark & 0x00FFFFFF) | ((DWORD)alpha << 24), &glyph);

    // A custom image replaces the drawn glyph but keeps the plate, so the icon
    // stays visually anchored.
    //
    // The image arrives already resampled to the plate's pixel size, so this only
    // has to place it: the drawn size is the cached size times the intro's icon
    // scale, and nothing is resampled per frame. When the plate is large enough to
    // hold the source unchanged, the source is used and this never upsamples it
    // past its natural size.
    bool imageFailed = false;
    // The image may belong to a different cache slot than this row: rows that resolve to
    // the same file share one slot, so it is decoded once (see ResolveIconSlot). The
    // glyph fallback is still this row's own, so a row that said `None` draws nothing.
    const int imageSlot = ResolveIconSlot(settings, slot);
    const WCHAR *iconPath = imageSlot >= 0 ? ResolveIconPath(settings, slot) : L"";
    int target = (int)(geometry.plateRadius * 2.0f + 0.5f);
    GpImage *image = imageSlot >= 0 ? IconImageFor(imageSlot, iconPath, target, &imageFailed)
                                    : nullptr;
    if (g_iconDrawLogs < 8) {
        g_iconDrawLogs++;
        DBG(L"Icon%d draw: plate diameter target=%d  image=%s  cache slot=%d  built-in glyph=%d",
               slot + 1, target, image ? L"custom" : L"none", imageSlot,
               ResolveIconStyle(settings, slot));
    }
    if (image) {
        UINT iw = 0, ih = 0;
        if (g_gdiplus.GetImageWidth) g_gdiplus.GetImageWidth(image, &iw);
        if (g_gdiplus.GetImageHeight) g_gdiplus.GetImageHeight(image, &ih);
        if (iw > 0 && ih > 0) {
            // Same size rule as the glyph path below: base size capped at the plate, then
            // the intro's scale on top of it (see ComputeIconImageDrawSize).
            float dw = 0.0f, dh = 0.0f;
            ComputeIconImageDrawSize((int)iw, (int)ih, geometry.plateRadius, scale, &dw, &dh);
            g_gdiplus.DrawImageRect(graphics, image, centerX - dw / 2.0f,
                                    centerY - dh / 2.0f, dw, dh);
        }
    } else if (glyph) {
        PaintIconGlyph(graphics, ResolveIconStyle(settings, slot), centerX, centerY,
                       geometry.plateRadius * 2.0f * scale, glyph);
    }
    if (glyph) g_gdiplus.DeleteBrush((GpBrush *)glyph);
}

// Concentric rings expanding outward from the plate, from the original's Ripple
// animation: radii 1.5x / 2.0x / 2.5x, peak opacity 0.50 / 0.50 / 0.60.
//
// The rings are centred on the very same point as the plate, taken from the shared
// geometry rather than recomputed. The original also lifted its ripple host by
// 16 px (RippleRise), which made sense for a full-screen HUD whose icon travels
// while the layout settles; here it would push the rings off the plate they are
// supposed to be coming out of, so it is intentionally not applied.
static void PaintRipples(GpGraphics *graphics, const ModSettings &settings,
                         const IntroFrame &frame, int slot, const IconGeometry &geometry) {
    if (!geometry.present) return;   // rings come out of a plate, so no plate means none
    if (settings.rippleCount <= 0 || frame.rippleProgress <= 0.0) return;

    int count = MinInt(settings.rippleCount, 3);
    for (int i = 0; i < count; i++) {
        double progress = frame.rippleProgress;
        // Stagger: each ring is slightly behind the previous one.
        if (i > 0) {
            double lag = 0.12 * i;
            progress = Clamp01((progress - lag) / (1.0 - lag));
        }
        if (progress <= 0.0) continue;

        double opacity = progress < 0.5 ? Lerp(0.0, kRipplePeakOpacity[i], progress / 0.5)
                                        : Lerp(kRipplePeakOpacity[i], 0.0, (progress - 0.5) / 0.5);
        float radius = RingRadiusAt(geometry, i, progress);
        if (radius < 1.0f) continue;

        BYTE alpha = (BYTE)(Clamp01(opacity) * 255.0 + 0.5);
        DWORD color = (g_settings.border & 0x00FFFFFF) | ((DWORD)alpha << 24);

        GpPen *pen = nullptr;
        g_gdiplus.CreatePen1(color, 2.0f, 0, &pen);
        if (pen) {
            // Centred on the plate's centre exactly: same X and same Y, no offset.
            g_gdiplus.DrawEllipse(graphics, pen, geometry.centerX - radius,
                                  geometry.centerY - radius,
                                  radius * 2.0f, radius * 2.0f);
            g_gdiplus.DeletePen(pen);
        }
    }
}

// ---------------------------------------------------------------------------
// Painting
// ---------------------------------------------------------------------------
static void BuildRoundedRect(GpPath *path, float x, float y, float w, float h, float r) {
    float d = r * 2.0f;
    g_gdiplus.AddPathArc(path, x, y, d, d, 180.0f, 90.0f);
    g_gdiplus.AddPathArc(path, x + w - d, y, d, d, 270.0f, 90.0f);
    g_gdiplus.AddPathArc(path, x + w - d, y + h - d, d, d, 0.0f, 90.0f);
    g_gdiplus.AddPathArc(path, x, y + h - d, d, d, 90.0f, 90.0f);
    g_gdiplus.ClosePathFigure(path);
}

// Defined further down, but the collapsing frame has to resize the window to match
// the animated height, so it is declared here.
static void PlaceWindow(HWND hwnd);

// ---------------------------------------------------------------------------
// M3: card geometry, regions, hit testing and dismissal
// ---------------------------------------------------------------------------
// One source of truth for where the card is (design 8 / M3): the window placement, the
// hit test and the painter all derive from these functions, and none of them computes a
// rectangle of its own. Coordinates are whatever space the caller works in -- screen
// space for the hit test, bitmap space for the painter -- because only the *relation*
// between the card and its parts has to be shared.
struct PanelRect {
    int x;
    int y;
    int w;
    int h;
};

static PanelRect ComputeIslandRect(const IslandMetrics &m, const ModSettings &cfg,
                                   const PanelRect &monitor, int top, int height) {
    PanelRect out = {};
    out.w = ComputeIslandCardWidth(m, cfg, monitor.w);
    out.x = monitor.x + (monitor.w - out.w) / 2;
    out.y = top;
    out.h = height;
    return out;
}

// The close affordance: a square at the card's right edge, centred on its first line.
//
// The button may not be larger than the card it belongs to. The clamps below are what
// keeps it inside, but when the button is *taller* than the card no position satisfies
// both clamps at once: the old fixed 22 px square against a single 18 px row landed at
// y = -2, the top clamp lifted it to the card's edge, and the bottom clamp then pushed it
// straight back out of the card (measured: y = card.y - 4) -- a click target outside the
// panel, over whatever is behind it. Shrinking is the only way both clamps can hold.
static const int kIslandCloseSize = 22;

static PanelRect ComputeIslandCloseRect(const PanelRect &card, int rowHeight) {
    PanelRect out = {};
    int size = kIslandCloseSize;
    if (size > card.w) size = card.w;
    if (size > card.h) size = card.h;
    if (size < 1) size = 1;
    out.w = size;
    out.h = size;
    out.x = card.x + card.w - size - 8;
    out.y = card.y + (rowHeight - size) / 2;
    if (out.x + out.w > card.x + card.w) out.x = card.x + card.w - out.w;
    if (out.x < card.x) out.x = card.x;
    if (out.y < card.y) out.y = card.y;
    if (out.y + out.h > card.y + card.h) out.y = card.y + card.h - out.h;
    return out;
}

enum IslandRegion {
    IR_NONE = 0,
    IR_CARD,
    IR_CLOSE,
};

struct IslandHitRegion {
    IslandRegion id;
    PanelRect rect;
};

// Regions are written back to front: later entries sit on top, and the hit test returns
// the last match. That ordering *is* the rule "what is visible is what is clickable" --
// the card accepts everything, and the button wins inside itself.
static int BuildIslandRegions(const PanelRect &card, int rowHeight, IslandHitRegion *out,
                              int cap) {
    if (!out || cap < 2) return 0;
    out[0].id = IR_CARD;
    out[0].rect = card;
    out[1].id = IR_CLOSE;
    out[1].rect = ComputeIslandCloseRect(card, rowHeight);
    return 2;
}

static IslandRegion IslandHitTest(const IslandHitRegion *regions, int count, int x, int y) {
    for (int i = count - 1; i >= 0; i--) {
        const PanelRect &r = regions[i].rect;
        if (x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h) return regions[i].id;
    }
    return IR_NONE;
}

// Closing a card early takes the same road the ttl expiry takes, so the fade and the
// state machine cannot diverge. Idempotent on purpose: a second click on the button must
// not restart the fade.
static bool IslandDismissable(const IslandView &v) {
    return v.state == IS_POPPING || v.state == IS_CARD;
}

static bool IslandDismiss(IslandView *v, DWORD now) {
    if (!IslandDismissable(*v)) return false;
    v->state = IS_COLLAPSING;
    v->phaseStarted = now;
    return true;
}

// What a click does while a card is up. Pure, so the rule is assertable rather than
// buried in WindowProc.
//
// The button consumes the click whether or not there is still a fade to start: the old
// code demanded IslandDismiss() == true, so a *second* click on the × during the fade
// fell straight through to the panel's own click behaviour and toggled the panel -- which
// reads as "the × closed the panel". The card body consumes as well, for the same reason:
// clicking the card to read it must not toggle the panel either. Only a click outside the
// card belongs to the panel.
enum IslandClickAction { ICA_PANEL = 0, ICA_CONSUMED, ICA_DISMISS };

static IslandClickAction IslandClickActionFor(IslandRegion hit, bool dismissable) {
    if (hit == IR_CLOSE) return dismissable ? ICA_DISMISS : ICA_CONSUMED;
    if (hit == IR_CARD) return ICA_CONSUMED;
    return ICA_PANEL;
}

// ---------------------------------------------------------------------------
// Frame clock and hover: both are kept off unless something needs them
// ---------------------------------------------------------------------------
// The HUD tick is a *decision*, not a habit: the cheap cursor poll unless something is
// actually animating. Two animations share this clock (the panel's height intro and the
// island's phases), so the decision lives in one place. Escalating the tick for the
// whole lifetime of a card -- which is what a hover highlight would tempt you into --
// would buy 16 ms wake-ups for a card that is sitting still doing nothing.
static bool IslandPhaseAnimating(const IslandView &v) {
    return v.state == IS_POPPING || v.state == IS_COLLAPSING;
}

static int DesiredTickMs() {
    return (g_intro.active || IslandPhaseAnimating(g_islandView)) ? 16 : kCursorPollMs;
}

static int g_tickMs = kCursorPollMs;

// Only touch the timer when the answer changed: a SetTimer per tick would be a call per
// tick for no reason.
static void ApplyTickRate(HWND hwnd) {
    const int want = DesiredTickMs();
    if (want == g_tickMs) return;
    g_tickMs = want;
    if (hwnd) SetTimer(hwnd, kTimerTick, (UINT)want, nullptr);
}

// Hover is driven by the mouse, not by the clock. WM_MOUSEMOVE costs nothing while the
// pointer is still, and WM_MOUSELEAVE (via TrackMouseEvent, registered only while a card
// is up) closes it. The rectangle is the one the click already uses -- the region table
// -- so the highlight and the hit target cannot drift apart.
static bool g_islandCloseHot = false;
static bool g_islandTrackingLeave = false;

// ---------------------------------------------------------------------------
// Font fallback: choosing a family, and remembering what was decided
//
// Everything here is only ever reached when RowNeedsFontPath() said so. The unconfigured
// panel builds its one font and never calls into this section at all -- no glyph query, no
// cache lookup, no second GDI+ object (docs/font-and-row-metrics-design.md 1).
//
// Both caches are static and fixed-size, in keeping with the no-allocation discipline: the
// glyph answers are keyed by (family, character) and the fonts by (family, size).
// ---------------------------------------------------------------------------
static const int kCoverCacheSlots = 256;
static const int kFontCacheSlots = 8;

// Resolved glyph coverage: does `family` have a glyph for `character`?
struct GlyphCoverSlot {
    DWORD familyHash;
    WCHAR character;
    bool covered;
    bool used;
};

static GlyphCoverSlot g_coverCache[kCoverCacheSlots];
static int g_coverCacheNext = 0;      // round-robin victim; the probe is a plain cache
static HDC g_coverDc = nullptr;
static HFONT g_coverFont = nullptr;
static DWORD g_coverFontHash = 0;
// Bounded diagnostic: how many times the *real* query had to run. The test host reads it to
// prove the cache works, and the [perf] line reports it on a real machine, where "did the
// fallback path start costing anything" is the question worth answering.
static DWORD g_coverQueryCount = 0;

static DWORD HashFamilyName(const WCHAR *name) {
    DWORD hash = 2166136261u;                       // FNV-1a, case-insensitive
    for (; name && *name; name++) {
        WCHAR c = *name;
        if (c >= L'A' && c <= L'Z') c = (WCHAR)(c - L'A' + L'a');
        hash ^= (DWORD)c;
        hash *= 16777619u;
    }
    return hash;
}

// True when the family has a glyph for that character.
//
// GGI_MARK_NONEXISTING_GLYPHS is what makes this answer a *coverage* answer: without it GDI
// returns the font's default glyph index for characters it has nothing for, and every font
// would look like it covers everything.
static bool QueryGlyphCovered(const WCHAR *family, WCHAR ch) {
    if (!family || !*family) return false;
    if (!g_coverDc) {
        g_coverDc = CreateCompatibleDC(nullptr);
        if (!g_coverDc) return false;
    }
    const DWORD hash = HashFamilyName(family);
    if (hash != g_coverFontHash || !g_coverFont) {
        HFONT replacement = CreateFontW(
            0, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_TT_ONLY_PRECIS,
            CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY, DEFAULT_PITCH, family);
        if (!replacement) return false;
        if (g_coverFont) DeleteObject(g_coverFont);
        g_coverFont = replacement;
        g_coverFontHash = hash;
    }
    HGDIOBJ previous = SelectObject(g_coverDc, g_coverFont);
    WORD glyph = 0;
    const DWORD mapped = GetGlyphIndicesW(g_coverDc, &ch, 1, &glyph, GGI_MARK_NONEXISTING_GLYPHS);
    SelectObject(g_coverDc, previous);
    g_coverQueryCount++;
    return mapped != GDI_ERROR && glyph != 0xFFFF;
}

// Cached front end for QueryGlyphCovered. A linear scan of 256 slots is cheaper than the DC
// work by orders of magnitude, and the working set is tiny: a handful of families times the
// distinct non-ASCII characters on screen.
static bool FontFamilyCovers(const WCHAR *family, WCHAR ch) {
    const DWORD hash = HashFamilyName(family);
    for (int i = 0; i < kCoverCacheSlots; i++) {
        const GlyphCoverSlot &slot = g_coverCache[i];
        if (slot.used && slot.familyHash == hash && slot.character == ch) return slot.covered;
    }
    const bool covered = QueryGlyphCovered(family, ch);
    GlyphCoverSlot &slot = g_coverCache[g_coverCacheNext];
    g_coverCacheNext = (g_coverCacheNext + 1) % kCoverCacheSlots;
    slot.familyHash = hash;
    slot.character = ch;
    slot.covered = covered;
    slot.used = true;
    return covered;
}

// The family that should draw one run, or null for the main font.
//
// The two chains mean different things, so they are asked in different ways:
//
//   * A [[font:...]] tag is an *instruction*. For a run of ASCII its first name wins outright
//     with no coverage query at all -- that is what makes the common override free, and every
//     font can be assumed to draw ASCII. Above ASCII the tag's names are asked about in order
//     (cheaply: the answers are cached), because a family with no glyph for the run would only
//     produce boxes, and a tag that names several families clearly expects the choice to be
//     made for it.
//   * The global "Fallback fonts" list is a *safety net* for characters the main family
//     cannot draw. So the main family is asked first, and a run that is entirely ASCII never
//     reaches the list at all. That is the performance gate in its final form: an English
//     panel with a long fallback list still pays nothing for it.
//
// The coverage question is injected so every branch is assertable without a font.
static const WCHAR *PickFontFamily(const WCHAR *run, int len, const WCHAR *mainFamily,
                                   const FontChain &explicitChain, const FontChain &fallback,
                                   bool (*covers)(const WCHAR *family, WCHAR ch)) {
    if (!run || len <= 0 || !covers) return nullptr;

    bool aboveAscii = false;
    for (int i = 0; i < len; i++) {
        if (run[i] > 0x7F) {
            aboveAscii = true;
            break;
        }
    }

    auto coversRun = [&](const WCHAR *family) {
        for (int i = 0; i < len; i++) {
            if (!covers(family, run[i])) return false;
        }
        return true;
    };

    if (explicitChain.count > 0) {
        if (!aboveAscii) return explicitChain.names[0];
        for (int i = 0; i < explicitChain.count; i++) {
            if (coversRun(explicitChain.names[i])) return explicitChain.names[i];
        }
    }

    if (fallback.count == 0 || !aboveAscii) return nullptr;
    if (mainFamily && *mainFamily && coversRun(mainFamily)) return nullptr;
    for (int i = 0; i < fallback.count; i++) {
        if (coversRun(fallback.names[i])) return fallback.names[i];
    }
    return nullptr;
}

// A GpFont for (family, size), created on first use and kept. Rebuilt rather than reused
// across a settings change -- ReleaseFontResources drops the lot, the same way the icon
// caches are dropped when the icon settings change.
struct FontCacheSlot {
    WCHAR family[kMaxFontFamilyChars];
    int size;
    GpFontFamily *fontFamily;
    GpFont *font;
    unsigned int lastUsed;
    bool used;
};

static FontCacheSlot g_fontCache[kFontCacheSlots];
static unsigned int g_fontCacheClock = 0;

static GpFont *AcquireRowFont(GpGraphics *graphics, const WCHAR *family, int size) {
    if (!graphics || !family || !*family) return nullptr;

    // An unused slot always wins over the least recently used one: evicting a live font to
    // make room while an empty slot sits next to it would mean rebuilding it on the next
    // frame, which is the whole cost this cache exists to avoid.
    FontCacheSlot *empty = nullptr;
    FontCacheSlot *lru = nullptr;
    for (int i = 0; i < kFontCacheSlots; i++) {
        FontCacheSlot &slot = g_fontCache[i];
        if (slot.used && slot.size == size && _wcsicmp(slot.family, family) == 0) {
            slot.lastUsed = ++g_fontCacheClock;
            return slot.font;   // null is a remembered failure, not a retry
        }
        if (!slot.used) {
            if (!empty) empty = &slot;
        } else if (!lru || slot.lastUsed < lru->lastUsed) {
            lru = &slot;
        }
    }

    FontCacheSlot *free = empty ? empty : lru;
    if (!free) return nullptr;
    if (free->font) {
        g_gdiplus.DeleteFont(free->font);
        free->font = nullptr;
    }
    if (free->fontFamily) {
        g_gdiplus.DeleteFontFamily(free->fontFamily);
        free->fontFamily = nullptr;
    }

    GpFontFamily *created = nullptr;
    g_gdiplus.CreateFontFamilyFromName(family, nullptr, &created);
    GpFont *font = nullptr;
    if (created) g_gdiplus.CreateFont(created, (float)size, 0, 0, &font);
    if (!font && created) {
        g_gdiplus.DeleteFontFamily(created);
        created = nullptr;
    }

    free->used = true;
    free->size = size;
    free->lastUsed = ++g_fontCacheClock;
    wcsncpy(free->family, family, kMaxFontFamilyChars - 1);
    free->family[kMaxFontFamilyChars - 1] = L'\0';
    free->fontFamily = created;
    free->font = font;      // may be null: the failed lookup is cached too
    return font;
}

// Drops every font object and the GDI coverage DC. Called on a settings change (the sizes and
// families may all be different) and on unload, in both cases *before* GDI+ shuts down.
static void ReleaseFontResources() {
    for (int i = 0; i < kFontCacheSlots; i++) {
        if (g_fontCache[i].font) g_gdiplus.DeleteFont(g_fontCache[i].font);
        if (g_fontCache[i].fontFamily) g_gdiplus.DeleteFontFamily(g_fontCache[i].fontFamily);
        g_fontCache[i] = {};
    }
    for (int i = 0; i < kCoverCacheSlots; i++) g_coverCache[i] = {};
    g_coverCacheNext = 0;
    if (g_coverFont) {
        DeleteObject(g_coverFont);
        g_coverFont = nullptr;
    }
    g_coverFontHash = 0;
    if (g_coverDc) {
        DeleteDC(g_coverDc);
        g_coverDc = nullptr;
    }
}

static void PaintPanel(HWND hwnd) {
    // Card mode: a static width, applied here and now. The width used to animate, and
    // that is what flickered: resizing from the timer left one frame in which the window
    // was wider than the bitmap about to be submitted, and even squeezing the animation
    // into this function still resized the window mid-frame. The card now appears and
    // disappears at its own width, the way the panel appears and disappears at its own
    // height -- the fade below is what carries the transition.
    const bool cardUp = g_settings.islandEnabled && g_islandCardValid &&
                        g_islandView.state != IS_IDLE;
    int width = g_settings.panelWidth;
    if (cardUp) {
        IslandMetrics metrics = {};
        metrics.titleChars = (int)wcslen(g_islandCard.title);
        metrics.bodyChars = (int)wcslen(g_islandCard.body);
        int monitorWidth = 0;
        HMONITOR monitor = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
        MONITORINFO info = {};
        info.cbSize = sizeof(info);
        if (monitor && GetMonitorInfoW(monitor, &info)) {
            monitorWidth = (int)(info.rcMonitor.right - info.rcMonitor.left);
        }
        // The width comes from the shared geometry function -- the same one the hit test's
        // rectangles are built from -- so the card cannot be drawn at one size and
        // accepted at another. Only the width transfers into bitmap space: the bitmap the
        // painter is about to fill *is* the card, so its local origin is (0,0) by
        // definition, while the function's x centres it inside the monitor.
        const PanelRect monitorRect = {0, 0, monitorWidth, 0};
        width = ComputeIslandRect(metrics, g_settings, monitorRect, 0, 0).w;
    }
    // Resize before the bitmap exists, never after: inside one frame the window and the
    // layered blit are then always the same size, so no band can be left behind.
    if (width != g_restWidth) {
        g_restWidth = width;
        PlaceWindow(hwnd);
    }
    // The card's own fade, driven by its phase: in while popping, out while collapsing.
    // Without it the text sat at full opacity and then vanished when the machine reached
    // Idle, which reads as a flash instead of a transition.
    double cardAlpha = 1.0;
    if (cardUp) {
        const DWORD elapsed = GetTickCount() - g_islandView.phaseStarted;
        const DWORD span = g_islandView.state == IS_COLLAPSING ? kIslandCollapseMs : kIslandPopMs;
        double t = span == 0 ? 1.0 : (double)elapsed / (double)span;
        if (t > 1.0) t = 1.0;
        cardAlpha = g_islandView.state == IS_COLLAPSING ? 1.0 - t : t;
        if (cardAlpha < 0.0) cardAlpha = 0.0;
        if (cardAlpha > 1.0) cardAlpha = 1.0;
    }
    bool collapsed = !g_expanded;

    // The intro drives the *painted* height. g_displayHeight is left alone: it is
    // the resting geometry that ComputeGeometry owns, and letting the animation
    // write to it made the hit-test rectangle shrink to tab size mid-animation,
    // which read as the panel collapsing as it opened.
    //
    // The two directions need different window handling:
    //
    //   arriving  the window already sits at the resting height and the panel
    //             grows into it, so nothing has to move
    //   leaving   the panel shrinks away, so the window has to follow it down or
    //             it would hold a transparent band below a shrinking panel
    //
    // Whether the intro is still running comes from g_intro.active, not from a
    // second cue calculation: that second sample could report "finished" while
    // EvaluateIntro had not, and the height then jumped backwards on the last frame.
    IntroFrame intro = EvaluateIntro();
    const bool introRunning = g_intro.active;
    int height = g_displayHeight;
    if (introRunning && !intro.expanding) {
        height = (int)(intro.height + 0.5);
        if (height < kTabHeight) height = kTabHeight;
        // SetExpanded already shrank the window to the resting (tab) height, so the
        // frame has to grow it back to the animated height. Without this the
        // collapsing frame would be a full-height bitmap clipped into an 8 px
        // window.
        if (height != g_displayHeight) {
            g_displayHeight = height;
            PlaceWindow(hwnd);
        }
    } else if (introRunning) {
        height = (int)(intro.height + 0.5);
        if (height < 1) height = 1;
    }
    g_paintedHeight = height;
    if (width <= 0 || height <= 0) return;

    HDC screenDc = GetDC(nullptr);
    HDC memDc = CreateCompatibleDC(screenDc);

    BITMAPINFO bmi = {};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = width;
    bmi.bmiHeader.biHeight = -height;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    void *bits = nullptr;
    HBITMAP dib = CreateDIBSection(screenDc, &bmi, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!dib || !bits) {
        DeleteDC(memDc);
        ReleaseDC(nullptr, screenDc);
        return;
    }
    HGDIOBJ oldBitmap = SelectObject(memDc, dib);

    GpBitmap *bitmap = nullptr;
    if (g_gdiplus.CreateBitmapFromScan0(width, height, width * 4, kPixelFormat32bppPARGB,
                                        (BYTE *)bits, &bitmap) == 0 && bitmap) {
        GpGraphics *graphics = nullptr;
        if (g_gdiplus.GetImageGraphicsContext((GpImage *)bitmap, &graphics) == 0 && graphics) {
            g_gdiplus.SetSmoothingMode(graphics, kSmoothingAntiAlias);
            g_gdiplus.SetTextRenderingHint(graphics, kTextHintAntiAliasGridFit);

            // Panel transform. The original scales the whole capsule about its
            // centre with a BackOut overshoot, so the panel does the same: build
            // the panel in local coordinates and let GDI+ apply the transform.
            const bool introActive = g_intro.active;
            float panelRadius = (float)MinInt(g_settings.cornerRadius, MinInt(width, height) / 2);
            PanelTransform transform = ComputePanelTransform((float)width, (float)height,
                                                             intro.scale, introActive);
            float scale = transform.scale;
            bool transformed = transform.active;

            GpMatrix matrix;
            matrix.m11 = scale; matrix.m12 = 0.0f;
            matrix.m21 = 0.0f;   matrix.m22 = scale;
            matrix.dx = transform.dx;
            matrix.dy = transform.dy;
            if (transformed) {
                // MatrixOrderPrepend == 0: the scale applies about the panel centre.
                g_gdiplus.MultiplyWorldTransform(graphics, &matrix, 0);
            }

            GpPath *panelPath = nullptr;
            g_gdiplus.CreatePath(0, &panelPath);
            BuildRoundedRect(panelPath, 0.5f, 0.5f, (float)width - 1.0f,
                             (float)height - 1.0f, panelRadius);

            GpSolidFill *background = nullptr;
            g_gdiplus.CreateSolidFill(g_settings.background, &background);
            if (background && panelPath) g_gdiplus.FillPath(graphics, (GpBrush *)background, panelPath);

            // One inset for every row, sized by the widest plate in the panel (they are all
            // the same size now, but the first row may have no plate at all).
            float iconDiameter = MaxIconPlateDiameter(g_settings);
            bool iconsPresent = AnyIconConfigured(g_settings);

            // The border doubles as the state indicator: it dims while collapsed,
            // so the current state is readable without any tooltip.
            DWORD borderColor = collapsed ? g_settings.borderCollapsed : g_settings.border;
            if (g_settings.borderThickness > 0) {
                GpPen *pen = nullptr;
                float penWidth = (float)g_settings.borderThickness;
                g_gdiplus.CreatePen1(borderColor, penWidth, 0, &pen);
                if (pen && panelPath) g_gdiplus.DrawPath(graphics, pen, panelPath);
                if (pen) g_gdiplus.DeletePen(pen);
            }

            if (collapsed) {
                // Nothing but the tab: this is the clickable grip that brings the
                // panel back, so it must stay on screen and hit-testable.
                GpSolidFill *gripBrush = nullptr;
                // Tertiary tier: the grip should read as a hint, not a stripe.
                g_gdiplus.CreateSolidFill(WithOpacity(borderColor, kTextTertiaryOpacity),
                                          &gripBrush);
                float gripWidth = (float)MinInt(56, width / 4);
                float gripHeight = 4.0f;
                if (gripBrush) {
                    float gripX = (width - gripWidth) / 2.0f;
                    float gripY = (height - gripHeight) / 2.0f;
                    GpPath *gripPath = nullptr;
                    g_gdiplus.CreatePath(0, &gripPath);
                    BuildRoundedRect(gripPath, gripX, gripY, gripWidth, gripHeight, gripHeight / 2.0f);
                    if (gripPath) {
                        g_gdiplus.FillPath(graphics, (GpBrush *)gripBrush, gripPath);
                        g_gdiplus.DeletePath(gripPath);
                    }
                    g_gdiplus.DeleteBrush((GpBrush *)gripBrush);
                }
            } else {
                GpFontFamily *family = nullptr;
                g_gdiplus.CreateFontFamilyFromName(g_settings.fontFamily, nullptr, &family);
                GpFont *font = nullptr;
                if (family) g_gdiplus.CreateFont(family, (float)g_settings.fontSize, 0, 0, &font);
                GpSolidFill *textBrush = nullptr;
                g_gdiplus.CreateSolidFill(g_settings.text, &textBrush);
                GpStringFormat *format = CreateRowStringFormat();

                EnterCriticalSection(&g_valuesLock);
                int rows = MinInt(g_publishedRows, g_visibleRows);
                // Diagnostic budget. It repeats for the first few paints on
                // purpose: a debug-output viewer is usually started after the mod
                // has already painted, so a once-only dump is easy to miss.
                // Remove this block once the layout is confirmed.
                //
                // It is decremented once per paint and merely *read* by the
                // per-segment logging below, so a single paint emits its whole
                // picture instead of running out mid-row.
                static int dumpBudget = 8;
                if (dumpBudget > 0) {
                    dumpBudget--;
                    DBG(L"Paint#%d rows=%d published=%d visibleRows=%d collapsed=%d size=%dx%d",
                           8 - dumpBudget, rows, g_publishedRows, g_visibleRows,
                           collapsed ? 1 : 0, width, height);
                    for (int d = 0; d < kMaxRows; d++) {
                        DBG(L"  row%d segs=%d textLen=%d inline=%d text=\"%ls\"",
                               d, g_rowState.segmentCount[d],
                               (int)wcslen(g_rowState.text[d]),
                               g_rowState.inlineRing[d] ? 1 : 0,
                               g_rowState.text[d]);
                    }
                }
                bool logDraws = dumpBudget > 0;

                // Text and rings fade in together once the shape has settled, and
                // fade out first when collapsing. The intro reports both.
                float contentAlpha = (float)intro.contentAlpha;
                if (contentAlpha < 0.01f) contentAlpha = 0.0f;
                // The icon plate claims the left margin; text starts after it.
                float leftInset = 12.0f;
                if (iconsPresent) {
                    leftInset = kIconLeftMargin + iconDiameter + (float)kIconContentGap;
                }

                // The cost gate, read from the settings rather than decided per frame: the
                // global list is one flag, the per-row half is one bool per row (see
                // RowNeedsFontPath). With both false the loop below never calls into the
                // fallback section at all.
                const bool globalFontPath = g_settings.fallbackFonts.count > 0;

                for (int r = 0; r < rows; r++) {
                    const bool rowFontPath =
                        RowNeedsFontPath(r < kMaxRows && g_settings.rowFontTag[r], globalFontPath);
                    // This row's own box: rows are not interchangeable any more, so the top
                    // edge is the sum of the rows above it and everything inside the row --
                    // the plate, the gauges, the text -- is placed against this height.
                    const int rowFontSizePx = RowFontSizePx(g_settings, r);
                    const int rowHeightPx = RowHeightPx(g_settings, r);
                    const float rowHeight = (float)rowHeightPx;
                    const float top = (float)RowTopPx(g_settings, r);
                    const float centerY = top + rowHeight / 2.0f;
                    float cursor = leftInset;

                    // The main font for this row. A row that inherits the global size keeps
                    // using the one font built above, so the default configuration allocates
                    // nothing extra and draws exactly as it did before rows had sizes.
                    GpFont *rowMainFont = font;
                    if (rowFontSizePx != g_settings.fontSize) {
                        GpFont *sized = AcquireRowFont(graphics, g_settings.fontFamily, rowFontSizePx);
                        if (sized) rowMainFont = sized;
                    }

                    // One plate per row, at that row's vertical centre, drawn before
                    // the row's text so the text never sits under it. Each row uses
                    // its own IconN / IconNPath.
                    //
                    // The plate and its rings are built from one geometry value, so
                    // there is no way for the rings to end up beside the plate.
                    if (iconsPresent) {
                        IconGeometry geometry = ComputeIconGeometry(g_settings, r, top);
                        if (geometry.present) {
                            if (introActive && intro.rippleProgress > 0.0f) {
                                PaintRipples(graphics, g_settings, intro, r, geometry);
                            }
                            PaintIconPlate(graphics, g_settings, intro, r, geometry);
                        }
                    }

                    int ringCount = g_rowState.ringCount[r];
                    bool inlineFlow = g_rowState.inlineRing[r];

                    // Ahead of an inline gauge the text is drawn normally, and the
                    // gauge is laid down wherever the text ended. In the other
                    // layout the gauges are parked at the right edge and the text
                    // is confined to whatever space is left.
                    RowTextMetrics metrics = ComputeRowTextMetrics(
                        (float)width, ringCount, g_settings.ringSize,
                        g_settings.ringRightOffset, inlineFlow, leftInset);

                    float parkX = 0.0f;
                    if (!inlineFlow && ringCount > 0) {
                        // Centre of the first parked gauge.
                        parkX = metrics.textRight + (float)g_settings.ringSize / 2.0f;
                    }

                    for (int s = 0; s < g_rowState.segmentCount[r]; s++) {
                        const TextSeg &seg = g_rowState.segments[r][s];
                        if (seg.offset < 0) {
                            // A gauge.
                            int ringIndex = seg.length;
                            if (ringIndex < 0 || ringIndex >= ringCount) continue;
                            const RingSpec &ring = g_rowState.rings[r][ringIndex];
                            float thickness = (float)ring.thickness;
                            float outer = (float)g_settings.ringSize;
                            float ringRadius = (outer - thickness) / 2.0f;
                            if (ringRadius < 3.0f) ringRadius = 3.0f;

                            float centerX;
                            if (inlineFlow) {
                                centerX = cursor + outer / 2.0f;
                                cursor = centerX + outer / 2.0f + 4.0f;
                            } else {
                                centerX = parkX;
                                parkX += outer + 4.0f;
                            }

                            double fraction = ring.max > 0 ? ring.value / ring.max : 0.0;
                            if (fraction < 0) fraction = 0;
                            if (fraction > 1) fraction = 1;
                            DWORD valueColor = ResolveRingColor(ring, g_settings);
                            // Content fades in as one unit during the intro, so
                            // the rings inherit the same alpha as the text.
                            if (contentAlpha < 1.0f) {
                                valueColor = WithOpacity(valueColor, contentAlpha);
                            }
                            // The track is the same hue at the secondary opacity
                            // tier, which is how the original dims a surface rather
                            // than introducing a second colour.
                            DWORD trackColor = WithOpacity(valueColor, kTextSecondaryOpacity);

                            DrawRing(graphics, centerX, centerY, ringRadius, thickness,
                                     fraction, trackColor, valueColor);
                            continue;
                        }

                        if (!font || !textBrush || seg.length <= 0) continue;
                        WCHAR slice[640];
                        int length = MinInt(seg.length, 639);
                        wcsncpy(slice, g_rowState.text[r] + seg.offset, length);
                        slice[length] = L'\0';

                        // This run's size: its own [[size:NN]], or the row's size. The font is
                        // chosen from it before anything is measured, so the fit is computed
                        // with the font the run will actually be drawn in.
                        const int segFontSize = seg.runFontSize > 0 ? seg.runFontSize : rowFontSizePx;
                        GpFont *segMainFont = rowMainFont;
                        if (segFontSize != rowFontSizePx) {
                            GpFont *sized = AcquireRowFont(graphics, g_settings.fontFamily, segFontSize);
                            if (sized) segMainFont = sized;
                        }

                        // Fallback fonts: choose this run's family *before* measuring it, so the
                        // fit is computed with the font it will actually be drawn in. The choice
                        // is made from the whole run, not from whatever survives the trim: the row
                        // says which family it wants, not how much of it fits.
                        GpFont *runFont = segMainFont;
                        if (rowFontPath) {
                            const FontChain &tagChain =
                                seg.tagChain > 0 ? g_settings.tagFonts[seg.tagChain - 1].chain
                                                 : kNoFontChain;
                            const WCHAR *picked =
                                PickFontFamily(slice, length, g_settings.fontFamily, tagChain,
                                               g_settings.fallbackFonts, FontFamilyCovers);
                            if (picked && _wcsicmp(picked, g_settings.fontFamily) != 0) {
                                GpFont *fallbackFont =
                                    AcquireRowFont(graphics, picked, segFontSize);
                                if (fallbackFont) runFont = fallbackFont;
                            }
                        }

                        // How much room this run may use. Right-parked gauges leave the
                        // text whatever they do not need; an inline run has to count the
                        // gauges that still follow it, or a long run pushes them off the
                        // panel (and FitTextToWidth gets a budget it can never exceed,
                        // so the run was never trimmed at all -- see
                        // ComputeInlineTextLimit).
                        float limit = metrics.textWidth;
                        if (inlineFlow) {
                            int ringsAfter = 0;
                            for (int k = s + 1; k < g_rowState.segmentCount[r]; k++) {
                                if (g_rowState.segments[r][k].offset < 0) ringsAfter++;
                            }
                            limit = ComputeInlineTextLimit(metrics.textRight, cursor, ringsAfter,
                                                           g_settings.ringSize);
                        }
                        float budget = LayoutWidthFloor(limit);

                        // The run's own width is what an inline gauge is placed after
                        // (including the spaces written before it); a parked run is
                        // drawn in its whole budget instead.
                        float advance = inlineFlow
                                            ? MeasureAdvance(graphics, runFont, format, slice,
                                                             rowHeight)
                                            : budget;

                        // Trim to the space this segment is actually allowed to
                        // occupy. GDI+ does not clip text to the layout box, so
                        // without this a long run would be drawn straight across the
                        // gauge that follows it. The fit is measured through the same
                        // helper used for the advance, so a fitted run costs exactly
                        // what it reports.
                        bool fitted = FitTextToWidth(graphics, runFont, format, slice,
                                                     sizeof(slice) / sizeof(slice[0]),
                                                     budget, rowHeight);
                        if (fitted) {
                            advance = MeasureAdvance(graphics, runFont, format, slice, rowHeight);
                        }

                        float drawWidth = LayoutWidthFloor(advance);
                        RectF layout(cursor, top, drawWidth, rowHeight);
                        if (logDraws) {
                            DBG(L"    draw row=%d seg=%d x=%.1f y=%.1f w=%.1f "
                                   L"adv=%.1f inline=%d fitted=%d \"%ls\"",
                                   r, s, layout.X, layout.Y,
                                   layout.Width, advance, inlineFlow ? 1 : 0,
                                   fitted ? 1 : 0, slice);
                        }
                        // Text alpha tracks the intro so the numbers fade in with
                        // the rings rather than appearing before them.
                        if (textBrush) {
                            g_gdiplus.SetSolidFillColor(textBrush,
                                contentAlpha < 1.0f
                                    ? WithOpacity(g_settings.text, contentAlpha)
                                    : g_settings.text);
                        }
                        g_gdiplus.DrawString(graphics, slice, -1, runFont, &layout, format,
                                             (GpBrush *)textBrush);
                        if (inlineFlow) cursor += advance;
                    }
                }
                LeaveCriticalSection(&g_valuesLock);

                // --- island card (M2) ------------------------------------------
                // A parallel drawing branch, not a RowState variant (design 7.4). It
                // paints over the rows, so while a card is up the card is what is
                // visible; the rows are untouched and come straight back afterwards.
                //
                // Still drawn in the panel's own geometry: the width animation channel
                // and ComputeIslandRect are the next step, and they need the resting
                // geometry to become a rectangle first (design 8).
                if (cardUp && font && format && textBrush && background) {
                    const float pad = 10.0f;
                    // The card is its own geometry, and deliberately the *global* row height:
                    // it is not one of the rows, it covers them, and a card whose line height
                    // changed with the rows underneath would resize while it was fading out.
                    // Its own line height is what BuildIslandRegions and ComputeIslandCloseRect
                    // are given as well, so the drawn card and the clickable card agree.
                    const float lineHeight = (float)g_settings.rowHeight;
                    // Cover with the panel's own background, inside the same rounded
                    // path, so nothing rectangular pokes out of the capsule corners. It
                    // fades with the card, which cross-fades it against the rows beneath.
                    g_gdiplus.SetSolidFillColor(
                        background, WithOpacity(g_settings.background, cardAlpha));
                    if (panelPath) g_gdiplus.FillPath(graphics, (GpBrush *)background, panelPath);

                    const DWORD accent = WithOpacity(
                        ResolveSeverityColor(g_islandCard.severity, g_settings.text), cardAlpha);
                    const IslandCardLayout plan = PlanIslandCard(g_islandCard);
                    const float textX = pad + 12.0f;
                    // Leave room for the close affordance: GDI+ does not clip to the
                    // layout box, so text would happily run underneath it.
                    float textWidth =
                        (float)width - textX - pad - (float)(kIslandCloseSize + 6);
                    if (textWidth < kMinLayoutWidth) textWidth = kMinLayoutWidth;

                    // Accent bar: severity, and the only thing that carries colour.
                    g_gdiplus.SetSolidFillColor(textBrush, accent);
                    float barHeight = lineHeight * (float)plan.lines - 6.0f;
                    if (barHeight < 6.0f) barHeight = 6.0f;
                    g_gdiplus.FillRectangle(graphics, (GpBrush *)textBrush, pad, pad + 3.0f,
                                            3.0f, barHeight);

                    // The rows' rule, applied here too: measure and ellipsise to the
                    // budget, because GDI+ would otherwise draw straight past the box.
                    WCHAR fitted[kIslandBodyChars];
                    wcsncpy(fitted, g_islandCard.title, kIslandBodyChars - 1);
                    fitted[kIslandBodyChars - 1] = L'\0';
                    FitTextToWidth(graphics, font, format, fitted, kIslandBodyChars, textWidth,
                                   lineHeight);
                    g_gdiplus.SetSolidFillColor(
                        textBrush, WithOpacity(g_settings.text, cardAlpha));
                    RectF titleRect(textX, pad, textWidth, lineHeight);
                    g_gdiplus.DrawString(graphics, fitted, -1, font, &titleRect, format,
                                         (GpBrush *)textBrush);
                    if (plan.lines > 1) {
                        wcsncpy(fitted, g_islandCard.body, kIslandBodyChars - 1);
                        fitted[kIslandBodyChars - 1] = L'\0';
                        FitTextToWidth(graphics, font, format, fitted, kIslandBodyChars,
                                       textWidth, lineHeight);
                        g_gdiplus.SetSolidFillColor(
                            textBrush,
                            WithOpacity(g_settings.text, kTextSecondaryOpacity * cardAlpha));
                        RectF bodyRect(textX, pad + lineHeight, textWidth, lineHeight);
                        g_gdiplus.DrawString(graphics, fitted, -1, font, &bodyRect,
                                             format, (GpBrush *)textBrush);
                    }
                    // The close affordance, drawn from the same rectangle the hit test
                    // accepts: an x, which is the glyph the merge badge already uses, so
                    // no new drawing surface is needed.
                    {
                        PanelRect localCard = {0, 0, width, height};
                        const PanelRect closeRect =
                            ComputeIslandCloseRect(localCard, g_settings.rowHeight);
                        // Hover feedback: only ever a repaint when the state flips (the
                        // window procedure decides that), never a per-frame one.
                        if (g_islandCloseHot && cardAlpha > 0.0) {
                            g_gdiplus.SetSolidFillColor(
                                textBrush, WithOpacity(g_settings.text, 0.12 * cardAlpha));
                            g_gdiplus.FillRectangle(graphics, (GpBrush *)textBrush,
                                                    (float)closeRect.x, (float)closeRect.y,
                                                    (float)closeRect.w, (float)closeRect.h);
                        }
                        g_gdiplus.SetSolidFillColor(
                            textBrush,
                            WithOpacity(g_settings.text,
                                        (g_islandCloseHot ? 1.0 : kTextSecondaryOpacity) *
                                            cardAlpha));
                        RectF closeBox((float)closeRect.x, (float)closeRect.y,
                                       (float)closeRect.w, (float)closeRect.h);
                        g_gdiplus.DrawString(graphics, L"\x00D7", -1, font, &closeBox, format,
                                             (GpBrush *)textBrush);
                    }
                    if (plan.hasBadge) {
                        // The merge counter from design 7.3, drawn in the accent colour.
                        g_gdiplus.SetSolidFillColor(textBrush, accent);
                        RectF badgeRect(
                            (float)width - 46.0f - (float)(kIslandCloseSize + 6), pad, 38.0f,
                            lineHeight);
                        g_gdiplus.DrawString(graphics, plan.badge, -1, font, &badgeRect, format,
                                             (GpBrush *)textBrush);
                    }
                }

                if (format) g_gdiplus.DeleteStringFormat(format);
                if (textBrush) g_gdiplus.DeleteBrush((GpBrush *)textBrush);
                if (font) g_gdiplus.DeleteFont(font);
                if (family) g_gdiplus.DeleteFontFamily(family);
            }

            if (background) g_gdiplus.DeleteBrush((GpBrush *)background);
            if (panelPath) g_gdiplus.DeletePath(panelPath);
            g_gdiplus.DeleteGraphics(graphics);
        }
        g_gdiplus.DisposeImage((GpImage *)bitmap);
    }

    // Analytic alpha mask: the panel body is opaque, the corners are rounded.
    // A monochrome mask drawn by GDI+ keeps the corner antialiasing clean and the
    // text fully opaque, which per-pixel ARGB text rasterisation does not.
    {
        float r = (float)MinInt(g_settings.cornerRadius, MinInt(width, height) / 2);
        BYTE *pixels = (BYTE *)bits;
        for (int y = 0; y < height; y++) {
            for (int x = 0; x < width; x++) {
                float px = (float)x + 0.5f, py = (float)y + 0.5f;
                float dx = 0.0f, dy = 0.0f;
                if (px < r) dx = r - px;
                else if (px > width - r) dx = px - (width - r);
                if (py < r) dy = r - py;
                else if (py > height - r) dy = py - (height - r);

                float coverage = 1.0f;
                if (dx > 0.0f && dy > 0.0f) {
                    float dist = (float)sqrt((double)(dx * dx + dy * dy));
                    coverage = r + 0.5f - dist;
                    if (coverage < 0.0f) coverage = 0.0f;
                    if (coverage > 1.0f) coverage = 1.0f;
                }
                BYTE alpha = (BYTE)(coverage * 255.0f + 0.5f);
                BYTE *p = pixels + ((size_t)y * width + x) * 4;
                if (alpha == 0) {
                    p[0] = p[1] = p[2] = p[3] = 0;
                } else {
                    p[3] = alpha;
                    if (p[0] > alpha) p[0] = alpha;
                    if (p[1] > alpha) p[1] = alpha;
                    if (p[2] > alpha) p[2] = alpha;
                }
            }
        }
    }

    POINT src = {0, 0};
    SIZE size = {width, height};
    RECT rect;
    GetWindowRect(hwnd, &rect);
    POINT dst = {rect.left, rect.top};
    BLENDFUNCTION blend = {};
    blend.BlendOp = AC_SRC_OVER;
    blend.SourceConstantAlpha = 255;
    blend.AlphaFormat = AC_SRC_ALPHA;
    UpdateLayeredWindow(hwnd, screenDc, &dst, &size, memDc, &src, 0, &blend, ULW_ALPHA);
    g_paintCount++;

    SelectObject(memDc, oldBitmap);
    DeleteObject(dib);
    DeleteDC(memDc);
    ReleaseDC(nullptr, screenDc);
}

// ---------------------------------------------------------------------------
// Placement and visibility
// ---------------------------------------------------------------------------
static void ComputeGeometry() {
    g_visibleRows = g_settings.rowCount;
    if (g_visibleRows < 1) g_visibleRows = 1;
    // Rows are not all the same height any more, so the panel is the sum of theirs rather
    // than a multiplication. The floor is still the collapsed tab: the panel never disappears.
    g_panelHeight = PanelRowsHeight(g_settings, g_visibleRows);
    g_displayHeight = g_expanded ? g_panelHeight : kTabHeight;

    RefreshMonitors();
    g_anchorIndex = g_settings.monitorMode == 1 ? PrimaryMonitorIndex() : TopmostMonitorIndex();
}

// The window always sits at the anchor monitor's top-centre and always occupies
// exactly the height it is currently painted at, so the hit-test rectangle and
// the visible panel can never disagree.
// Defined just below; declared here because PlaceWindow re-establishes the
// topmost state every time it moves the window.
static bool IsTopmost(HWND hwnd);
static bool ForceTopmost(HWND hwnd);

static void PlaceWindow(HWND hwnd) {
    if (g_anchorIndex < 0 || g_anchorIndex >= g_monitorCount) return;
    const RECT &rect = g_monitors[g_anchorIndex].rect;
    // A zero here would mean a zero-width window, so the resting width falls back to
    // the configured panel width until UpdateIsland sets a card's target.
    if (g_restWidth <= 0) g_restWidth = g_settings.panelWidth;
    int x = rect.left + (rect.right - rect.left - g_restWidth) / 2;
    int y = rect.top + kPanelTopOffset;

    if (g_visible) {
        SetWindowPos(hwnd, HWND_TOPMOST, x, y, g_restWidth, g_displayHeight,
                     SWP_NOACTIVATE | SWP_SHOWWINDOW);
        // PlaceWindow is where the geometry is decided, so the topmost state is
        // re-established here rather than assumed to have survived the move.
        ForceTopmost(hwnd);
        g_lastTopmostAssert = GetTickCount();
    } else {
        SetWindowPos(hwnd, HWND_TOPMOST, x, y, g_restWidth, g_displayHeight,
                     SWP_NOACTIVATE);
        ForceTopmost(hwnd);
        ShowWindow(hwnd, SW_HIDE);
    }
}

// Is the window actually flagged topmost right now?
static bool IsTopmost(HWND hwnd) {
    return (GetWindowLongPtrW(hwnd, GWL_EXSTYLE) & WS_EX_TOPMOST) != 0;
}

// Force the window into the topmost band and to the front of it.
//
// Two mechanisms, because one is not always enough:
//   * SetWindowLongPtr(WS_EX_TOPMOST) sets the style bit directly. SetWindowPos
//     normally sets it as a side effect, but the bit is what the compositor
//     actually consults, so it is written explicitly.
//   * SetWindowPos(HWND_TOPMOST, HWND_TOP) then raises the window within the
//     topmost band. HWND_TOPMOST alone only re-inserts it at the top of its
//     current band, which is not the same as being in front of every other
//     topmost window.
static bool ForceTopmost(HWND hwnd) {
    if (IsTopmost(hwnd)) {
        SetWindowPos(hwnd, HWND_TOP, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOOWNERZORDER);
        return true;
    }

    LONG_PTR ex = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
    SetWindowLongPtrW(hwnd, GWL_EXSTYLE, ex | WS_EX_TOPMOST);
    SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOOWNERZORDER |
                 SWP_FRAMECHANGED);
    return IsTopmost(hwnd);
}

// Topmost can be lost to another process raising itself, to a full-screen app, or
// to the shell rebuilding the z-order.
//
// Measured behaviour of this mechanism (see _topprobe.cpp): the style bit is set
// correctly and a topmost window keeps its place in the topmost band even when
// another window is raised to topmost afterwards. What a periodic re-assert buys
// is therefore not correctness but *latency*: between the moment another topmost
// window is raised and the next re-assert, the panel is genuinely behind it. A
// one-second gap is very visible; ~100 ms is not.
static const DWORD kTopmostIntervalMs = 100;

static void ReassertTopmost(HWND hwnd) {
    if (!g_visible) return;
    DWORD now = GetTickCount();
    bool topmost = IsTopmost(hwnd);
    // Lost the bit entirely: recover at once. Otherwise just refresh the order.
    if (topmost && now - g_lastTopmostAssert < kTopmostIntervalMs) return;
    g_lastTopmostAssert = now;

    bool ok = ForceTopmost(hwnd);
    // Log only genuine losses, not the routine refresh, so the log stays useful.
    if (!topmost || !ok) {
        DBG(L"Topmost lost and re-asserted: was=%d now=%d setpos=%d err=%lu",
               topmost ? 1 : 0, IsTopmost(hwnd) ? 1 : 0, ok ? 1 : 0, GetLastError());
    }
}

// Is the pointer inside the hot zone of the anchor monitor's top edge?
static bool CursorInHotZone() {
    if (g_anchorIndex < 0) return false;
    POINT cursor;
    if (!GetCursorPos(&cursor)) return false;
    const RECT &rect = g_monitors[g_anchorIndex].rect;

    int monitorWidth = rect.right - rect.left;
    int zoneWidth = MaxInt(160, monitorWidth / 3);
    int centerX = rect.left + monitorWidth / 2;
    // The zone spans the panel's top edge and extends downward far enough to cover
    // the whole panel plus the configured margin, so a pointer resting anywhere on
    // the panel keeps it open.
    //
    // It starts at the window's actual top (rect.top + kPanelTopOffset), not the
    // monitor's top: the window is drawn 6 px below the monitor edge, so anchoring
    // the zone at rect.top left the panel's upper pixels outside its own hot zone.
    int panelTop = rect.top + kPanelTopOffset;
    int depth = g_settings.hotZoneDepth + (g_expanded ? g_panelHeight : 0);

    return cursor.x >= centerX - zoneWidth / 2 && cursor.x <= centerX + zoneWidth / 2 &&
           cursor.y >= panelTop - kPanelTopOffset && cursor.y <= panelTop + depth;
}

static void SetVisible(HWND hwnd, bool visible) {
    if (g_visible == visible) return;
    g_visible = visible;
    PlaceWindow(hwnd);
}

// Kicks off the arrival or departure animation. The height animates from wherever
// the panel currently is to where it is going, so the same code covers expand and
// collapse; the direction also selects the duration.
static void StartIntro(HWND hwnd, double fromHeight, double toHeight) {
    // Clear first: a stale active flag from a previous run would make the caller's
    // very next paint take the "intro finished" path and show one frame of fully
    // visible content at rest scale, which reads as a flash.
    g_intro.active = false;
    if (g_settings.introStyle == 2 || g_settings.introMs <= 0) {
        return;
    }
    const bool expanding = toHeight >= fromHeight;
    g_intro.active = true;
    g_intro.startedAt = GetTickCount();
    g_intro.durationMs = (double)g_settings.introMs *
                         (expanding ? 1.0 : kCollapseDurationRatio);
    g_intro.fromHeight = fromHeight;
    g_intro.toHeight = toHeight;
    g_intro.style = g_settings.introStyle;
    g_intro.rippleCount = g_settings.rippleCount;
    // Run the 40 ms tick as a frame clock while the animation lasts, then drop it
    // back so an idle panel is not repainting continuously.
    ApplyTickRate(hwnd);
}

// Expand/collapse. Both states stay on screen, so the panel can always be clicked
// back; collapsing only shrinks it to the tab.
static void SetExpanded(HWND hwnd, bool expanded) {
    if (g_expanded == expanded) return;
    double fromHeight = (double)g_displayHeight;
    g_expanded = expanded;
    ComputeGeometry();
    double toHeight = (double)g_displayHeight;
    StartIntro(hwnd, fromHeight, toHeight);
    // Growing: the window goes to the expanded resting size at once and the content grows
    // into it, which is intended -- there is nothing under a growing panel to hide.
    //
    // Shrinking: it must NOT go to the tab height here. Doing so and then growing it back
    // inside PaintPanel's collapse branch -- which has to, or the expanded bitmap would be
    // clipped into an 8 px window -- leaves one slice in which the window has no matching
    // bitmap at all, and that slice is the flash on collapse. The per-frame height follow
    // in the painter brings the window down, together with the content.
    if (expanded) PlaceWindow(hwnd);
    PaintPanel(hwnd);
}

// Proximity: expand on approach, collapse after a linger once the pointer leaves.
static void UpdateProximity(HWND hwnd) {
    if (!g_modeProximity) return;
    DWORD now = GetTickCount();
    bool inside = CursorInHotZone();
    if (inside) {
        g_hideAt = now + g_settings.hideDelayMs;
        if (!g_visible) SetVisible(hwnd, true);
        if (!g_expanded) SetExpanded(hwnd, true);
    } else if (g_expanded && now >= g_hideAt) {
        // Log where the cursor actually was, so a collapse caused by pointer
        // tracking can be told apart from one caused by stale geometry.
        POINT cursor = {};
        GetCursorPos(&cursor);
        const RECT &rect = g_monitors[g_anchorIndex].rect;
        int depth = g_settings.hotZoneDepth + (g_expanded ? g_panelHeight : 0);
        DBG(L"Proximity: collapsing. cursor=(%ld,%ld) monitor.top=%ld depth=%d "
               L"panel=%dx%d painted=%d",
               cursor.x, cursor.y, rect.top, depth, g_settings.panelWidth,
               g_displayHeight, g_paintedHeight);
        SetExpanded(hwnd, false);
    }
}

// Drives the intro animation. While one is running the tick doubles as a frame
// clock; when it finishes the tick drops back to the cheap cursor-poll rate so an
// idle panel is not repainting 60 times a second.
static void UpdateIntro(HWND hwnd) {
    if (!g_intro.active) return;
    if (IntroCue() >= 1.0) {
        const bool wasExpanding = g_intro.toHeight >= g_intro.fromHeight;
        g_intro.active = false;
        // Not unconditionally back to the cheap poll: an island phase may still be
        // animating, and it shares this clock.
        ApplyTickRate(hwnd);
        if (!wasExpanding) {
            // The collapse drove g_displayHeight down frame by frame; put it back to
            // the resting value so the window and WM_NCHITTEST agree with the
            // painted result, instead of leaving it one frame short.
            double target = g_intro.toHeight;
            g_expanded = false;
            ComputeGeometry();
            if ((double)g_displayHeight != target) PlaceWindow(hwnd);
        }
        PaintPanel(hwnd);   // one last frame at rest
        return;
    }
    PaintPanel(hwnd);
}

// ---------------------------------------------------------------------------
// Window procedure
// ---------------------------------------------------------------------------
// Drives the island from the HUD's frame clock. The visual layer (card geometry, the
// width animation channel, painting the card) hangs off this same call in the next
// step; the machine itself already runs, so its transitions are real and logged
// rather than waiting for the drawing to exist.
static void UpdateIsland() {
    if (!g_settings.islandEnabled) {
        // Turning the island off has to stop its phase too. Nothing drives the machine
        // while the feature is off, so a phase caught mid-flight stayed POPPING or
        // COLLAPSING for the rest of the session: IslandPhaseAnimating() stayed true,
        // ApplyTickRate pinned the tick at 16 ms, and the stale card could not even be
        // dismissed (the click path is gated on the same switch).
        if (g_islandView.state != IS_IDLE) {
            if (g_islandLogs < 8) {
                Wh_Log(L"EXT island: disabled mid-phase (%s); dropping it",
                       IslandStateName(g_islandView.state));
                g_islandLogs++;
            }
            g_islandView.state = IS_IDLE;
            g_islandView.showing = ES_INFO;
            g_islandCardValid = false;
        }
        return;
    }
    if (!g_islandLockReady) return;

    IslandEvent local[kIslandQueueMax];
    int count = 0;
    EnterCriticalSection(&g_islandLock);
    // Stale events are dropped here, under the lock, before the snapshot: what the
    // state machine sees is then never a queue of things that already happened.
    g_islandCount = IslandDropStale(g_islandQueue, g_islandCount, GetTickCount(), &g_islandDrops);
    count = g_islandCount;
    for (int i = 0; i < count; i++) local[i] = g_islandQueue[i];
    LeaveCriticalSection(&g_islandLock);

    const DWORD now = GetTickCount();
    bool phaseDone = false;
    if (g_islandView.state == IS_POPPING) phaseDone = (now - g_islandView.phaseStarted) >= kIslandPopMs;
    else if (g_islandView.state == IS_COLLAPSING) phaseDone = (now - g_islandView.phaseStarted) >= kIslandCollapseMs;

    int pop = -1;
    const IslandState was = g_islandView.state;
    const IslandState next = StepIsland(&g_islandView, local, count, now, phaseDone,
                                        (DWORD)g_settings.extEventMs, &pop);

    if (pop >= 0 && pop < count) {
        EnterCriticalSection(&g_islandLock);
        g_islandCardValid = IslandTakeMatching(g_islandQueue, &g_islandCount, local[pop]);
        if (g_islandCardValid) g_islandCard = local[pop];
        LeaveCriticalSection(&g_islandLock);
    }
    // A card has to be readable, so while one is up the proximity collapse is
    // deferred: the collapse is timer-driven off g_hideAt, so pushing that deadline
    // is all it takes. Normal proximity behaviour resumes on the tick after the card.
    // Only while the card is on its way in or on screen. Forcing the panel open while
    // the card is collapsing would expand the panel and then collapse it again one tick
    // later, which is exactly the short flicker that was still visible on the way out.
    const bool cardVisible = g_islandCardValid && (g_islandView.state == IS_POPPING ||
                                                  g_islandView.state == IS_CARD);
    if (cardVisible) {
        if (g_modeProximity) g_hideAt = now + g_settings.hideDelayMs;
        if (!g_expanded && g_window) SetExpanded(g_window, true);
    }
    // Once the machine is idle again nothing is on screen, so the card is dropped: a
    // stale one would otherwise be one state transition away from being drawn again.
    if (next == IS_IDLE && pop < 0) g_islandCardValid = false;

    if (next != was && g_islandLogs < 8) {
        Wh_Log(L"EXT island: %s -> %s (queued=%d, showing=%d%s)", IslandStateName(was),
               IslandStateName(next), count, (int)g_islandView.showing,
               g_islandCardValid ? L" card=yes" : L" card=no");
        g_islandLogs++;
    }
}

static LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_TIMER:
        if (wParam == kTimerTick) {
            // A pushed value goes stale on its own schedule, not on the refresh
            // interval. This reads one atomic and, once the deadline has passed,
            // asks for another sample -- the sampler is the only thread that can
            // turn an expired value into "--".
            LONG deadline = InterlockedCompareExchange(&g_extDeadlineMs, 0, 0);
            if (deadline && (LONG)(GetTickCount() - (DWORD)deadline) >= 0) {
                InterlockedExchange(&g_extDeadlineMs, 0);   // clear first: never spin
                InterlockedExchange(&g_extDirty, 1);
            }
            UpdateProximity(hwnd);
            UpdateIntro(hwnd);
            UpdateIsland();
            // The island's phases share this clock -- the height intro is not the only
            // animation -- so the rate is decided here, once, for both. And once the
            // machine is idle nothing may stay hot or stay tracked, or the next card
            // would open with a highlight it never earned and no leave notification.
            if (g_islandView.state == IS_IDLE) {
                g_islandCloseHot = false;
                g_islandTrackingLeave = false;
            }
            ApplyTickRate(hwnd);
            ReassertTopmost(hwnd);
        } else if (wParam == kTimerPerf) {
            unsigned long long cpuMs = (CpuTime100ns() - g_perfBaseCpu100ns) / 10000ULL;
            PROCESS_MEMORY_COUNTERS_EX mem = {};
            mem.cb = sizeof(mem);
            GetProcessMemoryInfo(GetCurrentProcess(), (PROCESS_MEMORY_COUNTERS *)&mem, sizeof(mem));
            unsigned long long elapsed = GetTickCount64() - g_perfBaseTick;
            DBG(L"[perf] up=%llus rows=%d groups=0x%X samples=%llu republish=%llu paints=%llu "
                   L"glyphq=%lu cpu=%llums cpu/sample=%.3fms ws=%.1fMB private=%.1fMB "
                   L"visible=%d expanded=%d prox=%d",
                   elapsed / 1000ULL, g_visibleRows, g_activeGroups, g_sampleCount, g_republishCount,
                   g_paintCount, (unsigned long)g_coverQueryCount,
                   cpuMs, g_sampleCount ? (double)cpuMs / (double)g_sampleCount : 0.0,
                   mem.WorkingSetSize / 1048576.0, mem.PrivateUsage / 1048576.0,
                   g_visible ? 1 : 0, g_expanded ? 1 : 0, g_modeProximity ? 1 : 0);
        }
        return 0;

    case kMsgRatesReady:
        if (g_visible) PaintPanel(hwnd);
        return 0;

    case kMsgApplySettings: {
        // The loader thread posted this after writing g_settings, so the values
        // needed below are read fresh out of it (see PushSettings).
        //
        // The panel is always hit-testable so it can always be clicked; only the
        // rounded-corner test decides which pixels actually take the click.
        LONG_PTR ex = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
        ex &= ~WS_EX_TRANSPARENT;
        SetWindowLongPtrW(hwnd, GWL_EXSTYLE, ex);

        ComputeGeometry();
        // The icon file may have changed, so drop the decoded copy before the
        // next paint re-reads it.
        DisposeIconImage();
        // Fonts are cached the same way and for the same reason: a size or a family may have
        // changed, and a stale GpFont would draw the old metrics. This runs on the HUD thread,
        // which is the only thread that ever touches these caches.
        ReleaseFontResources();
        PlaceWindow(hwnd);
        PaintPanel(hwnd);
        // Publishing a new version is what tells the sampler to re-evaluate which
        // collector groups are needed.
        InterlockedIncrement(&g_settingsVersion);
        return 0;
    }

    case WM_MOUSEMOVE: {
        // Hover costs a message, not a wake-up: with the pointer still, this never runs.
        // Only while a card is up is there anything to highlight, and TrackMouseEvent is
        // registered then and only then, so an idle panel receives neither message.
        if (!g_settings.islandEnabled || !g_islandCardValid ||
            g_islandView.state == IS_IDLE) {
            break;
        }
        RECT wr = {};
        GetWindowRect(hwnd, &wr);
        PanelRect card = {wr.left, wr.top, wr.right - wr.left, wr.bottom - wr.top};
        IslandHitRegion regions[2];
        const int regionCount = BuildIslandRegions(card, g_settings.rowHeight, regions, 2);
        const bool hot =
            IslandHitTest(regions, regionCount, wr.left + (int)(short)LOWORD(lParam),
                          wr.top + (int)(short)HIWORD(lParam)) == IR_CLOSE;
        if (!g_islandTrackingLeave) {
            TRACKMOUSEEVENT track = {};
            track.cbSize = sizeof(track);
            track.dwFlags = TME_LEAVE;
            track.hwndTrack = hwnd;
            g_islandTrackingLeave = TrackMouseEvent(&track) != FALSE;
        }
        if (hot != g_islandCloseHot) {
            g_islandCloseHot = hot;
            PaintPanel(hwnd);          // the only repaint a hover ever causes
        }
        return 0;
    }

    case WM_MOUSELEAVE:
        g_islandTrackingLeave = false;
        if (g_islandCloseHot) {
            g_islandCloseHot = false;
            PaintPanel(hwnd);
        }
        return 0;

    case WM_LBUTTONUP:
        // The card's close affordance comes first: while a card is up it owns the click,
        // and the panel's own click behaviour (toggle / mode switch) must not also fire.
        // Everything here is on the HUD thread, which is the only thread that touches the
        // island view.
        if (g_settings.islandEnabled && g_islandCardValid &&
            g_islandView.state != IS_IDLE) {
            RECT wr = {};
            GetWindowRect(hwnd, &wr);
            PanelRect card = {wr.left, wr.top, wr.right - wr.left, wr.bottom - wr.top};
            IslandHitRegion regions[2];
            const int regionCount =
                BuildIslandRegions(card, g_settings.rowHeight, regions, 2);
            const int screenX = wr.left + (int)(short)LOWORD(lParam);
            const int screenY = wr.top + (int)(short)HIWORD(lParam);
            const IslandRegion hit = IslandHitTest(regions, regionCount, screenX, screenY);
            switch (IslandClickActionFor(hit, IslandDismissable(g_islandView))) {
            case ICA_DISMISS:
                IslandDismiss(&g_islandView, GetTickCount());
                PaintPanel(hwnd);
                return 0;
            case ICA_CONSUMED:
                // The card swallows the click; the panel's own behaviour must not run.
                return 0;
            default:
                break;
            }
        }
        if (g_settings.clickToggle) {
            // Toggle mode: a click is the only way in and out. Opening also pins
            // the panel, so it stays until the next click instead of being driven
            // back down by the mouse.
            if (g_expanded) {
                SetExpanded(hwnd, false);
            } else {
                g_modeProximity = false;
                SetExpanded(hwnd, true);
                SetVisible(hwnd, true);
            }
        } else {
            // Proximity mode: a click switches how the panel is driven, which is a
            // display mode rather than a visibility setting. Switching back to
            // mouse mode collapses it, which is the visible confirmation.
            g_modeProximity = !g_modeProximity;
            if (g_modeProximity) {
                g_hideAt = GetTickCount() + g_settings.hideDelayMs;
                SetExpanded(hwnd, false);
            } else {
                SetExpanded(hwnd, true);
            }
            SetVisible(hwnd, true);
        }
        DBG(L"Click: %s, display mode = %s",
               g_settings.clickToggle ? L"toggle" : L"mode switch",
               g_modeProximity ? L"proximity" : L"always");
        return 0;

    case WM_NCHITTEST: {
        // Rounded-corner aware: outside the panel, let the click fall through.
        POINT cursor;
        GetCursorPos(&cursor);
        RECT rect;
        GetWindowRect(hwnd, &rect);
        int x = cursor.x - rect.left;
        int y = cursor.y - rect.top;
        float r = (float)MinInt(g_settings.cornerRadius,
                                MinInt(g_restWidth, g_displayHeight) / 2);
        float dx = 0.0f, dy = 0.0f;
        if (x < r) dx = r - x;
        else if (x > g_restWidth - r) dx = x - (g_restWidth - r);
        if (y < r) dy = r - y;
        else if (y > g_displayHeight - r) dy = y - (g_displayHeight - r);
        bool inside = true;
        if (dx > 0.0f && dy > 0.0f) {
            inside = (dx * dx + dy * dy) <= r * r;
        }
        return inside ? HTCLIENT : HTTRANSPARENT;
    }
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}

// ---------------------------------------------------------------------------
// HUD thread
// ---------------------------------------------------------------------------
static DWORD WINAPI HudThreadProc(LPVOID) {
    static const WCHAR kClassName[] = L"Endfield.Info.Hud.2";
    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = kClassName;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    if (!RegisterClassExW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        Wh_Log(L"RegisterClassExW failed: %lu", GetLastError());
        SetEvent(g_hudReady);
        return 1;
    }

    // The window is never transparent to input: it must always be clickable, and
    // WM_NCHITTEST decides which pixels take the click. It is also never hidden,
    // so the collapsed tab can always be clicked back open.
    // WS_EX_TOPMOST is set at creation as well as through SetWindowPos, because
    // the style bit is what the compositor consults.
    LONG_PTR exStyle = WS_EX_LAYERED | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_TOPMOST;

    ComputeGeometry();
    HWND hwnd = CreateWindowExW(exStyle, kClassName, L"Endfield Info Windhawk", WS_POPUP,
                                0, 0, g_restWidth, g_displayHeight,
                                nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    if (!hwnd) {
        Wh_Log(L"CreateWindowExW failed: %lu", GetLastError());
        SetEvent(g_hudReady);
        return 1;
    }
    g_window = hwnd;

    PlaceWindow(hwnd);
    ShowWindow(hwnd, SW_SHOWNOACTIVATE);
    ForceTopmost(hwnd);
    SetTimer(hwnd, kTimerTick, kCursorPollMs, nullptr);
    SetTimer(hwnd, kTimerPerf, 30000, nullptr);
    PaintPanel(hwnd);
    {
        RECT placed = {};
        GetWindowRect(hwnd, &placed);
        Wh_Log(L"Window ready: topmost=%d exstyle=0x%llX rect=%ld,%ld %ldx%ld",
               IsTopmost(hwnd) ? 1 : 0,
               (unsigned long long)GetWindowLongPtrW(hwnd, GWL_EXSTYLE),
               placed.left, placed.top, placed.right - placed.left,
               placed.bottom - placed.top);
    }
    // Start expanded only in the mouse-driven mode; in Toggle mode the panel
    // begins as a tab so the collapsed state is discoverable.
    if (g_modeProximity) {
        g_hideAt = GetTickCount() + g_settings.hideDelayMs;
    } else {
        SetExpanded(hwnd, false);
    }
    SetEvent(g_hudReady);

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        if (msg.message == kMsgQuit && msg.hwnd == nullptr) break;
        if (msg.message == kMsgIslandEvent && msg.hwnd == nullptr) {
            // A thread message, not a window message: the pipe thread must not hold
            // (or need) the window handle, which may already be gone (design R5).
            g_islandWakes++;
            int queued = 0;
            if (g_islandLockReady) {
                EnterCriticalSection(&g_islandLock);
                queued = g_islandCount;
                LeaveCriticalSection(&g_islandLock);
            }
            if (g_islandWakes <= 4) {
                Wh_Log(L"EXT island: wake %u, queued=%d", g_islandWakes, queued);
            }
            continue;               // thread messages are not dispatched
        }
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    KillTimer(hwnd, kTimerTick);
    KillTimer(hwnd, kTimerPerf);
    DestroyWindow(hwnd);
    g_window = nullptr;
    return 0;
}

// ---------------------------------------------------------------------------
// Sampler
// ---------------------------------------------------------------------------
static GpuBackend g_gpu;
static bool g_gpuStartAttempted = false;

static void FillSystemFields(Values &v) {
    SYSTEMTIME st;
    GetLocalTime(&st);
    swprintf(v.timeText, 16, L"%02d:%02d:%02d", st.wHour, st.wMinute, st.wSecond);
    swprintf(v.dateText, 16, L"%04d-%02d-%02d", st.wYear, st.wMonth, st.wDay);
    unsigned long long secs = GetTickCount64() / 1000ULL;
    swprintf(v.uptimeText, 24, L"%llud %02llu:%02llu", secs / 86400,
             (secs % 86400) / 3600, (secs % 3600) / 60);

    PROCESS_MEMORY_COUNTERS_EX mem = {};
    mem.cb = sizeof(mem);
    GetProcessMemoryInfo(GetCurrentProcess(), (PROCESS_MEMORY_COUNTERS *)&mem, sizeof(mem));
    v.modWsMb = mem.WorkingSetSize / 1048576.0;
    v.modCpuMs = (CpuTime100ns() - g_perfBaseCpu100ns) / 10000.0;
}

// Field by field on purpose: RingSpec carries padding, so memcmp would call two
// equal gauges different whenever the padding bytes differ.
static bool RingSpecsEqual(const RingSpec &a, const RingSpec &b) {
    return a.value == b.value && a.max == b.max && a.thickness == b.thickness &&
           a.color == b.color && a.hasColor == b.hasColor;
}

// Everything the painter reads for one set of rows. Segments are derived from the
// text and the gauges, but they are compared too: a change that only moves where a
// gauge sits inside a sentence is still a change.
static bool RowStateEqual(const RowState &a, const RowState &b, int count) {
    for (int r = 0; r < count; r++) {
        if (a.segmentCount[r] != b.segmentCount[r]) return false;
        if (a.ringCount[r] != b.ringCount[r]) return false;
        if (a.inlineRing[r] != b.inlineRing[r]) return false;
        if (wcscmp(a.text[r], b.text[r]) != 0) return false;
        for (int i = 0; i < a.segmentCount[r]; i++) {
            if (a.segments[r][i].offset != b.segments[r][i].offset) return false;
            if (a.segments[r][i].length != b.segments[r][i].length) return false;
            // Two rows can carry identical text with different tags around it: the markers are
            // stripped, so without this the family or the size could change without a repaint.
            if (a.segments[r][i].tagChain != b.segments[r][i].tagChain) return false;
            if (a.segments[r][i].runFontSize != b.segments[r][i].runFontSize) return false;
        }
        for (int i = 0; i < a.ringCount[r]; i++) {
            if (!RingSpecsEqual(a.rings[r][i], b.rings[r][i])) return false;
        }
    }
    return true;
}

// Publishes the rows and reports whether anything actually changed, so the caller
// can skip a repaint when nothing did. That matters because a pushed value arrives
// on its own schedule: the same value repeated, or a sample whose numbers came out
// identical, used to cost a full layered-window repaint for nothing.
static bool PublishRows(const Values &v, const ModSettings &settings) {
    int count = settings.rowCount;
    if (count > kMaxRows) count = kMaxRows;

    // Build everything off-lock, then swap it in.
    WCHAR marked[kMaxRows][640];
    RingSpec rings[kMaxRows][kMaxRingsPerRow];
    int ringCounts[kMaxRows] = {};

    // Expanding reads the external value table, so the whole expansion runs under
    // g_extLock: a row can never contain a half-written entry. The lock is
    // released before g_valuesLock is taken -- the two are never nested, which is
    // what keeps the lock graph acyclic (docs/external-data-island-design.md 5.6.1).
    bool lockedExt = g_extLockReady;
    if (lockedExt) EnterCriticalSection(&g_extLock);
    for (int r = 0; r < count; r++) {
        ExpandTokensAndRings(settings.rows[r], v, settings, marked[r], 640,
                             rings[r], &ringCounts[r], kMaxRingsPerRow);
    }
    if (lockedExt) {
        // Publish when the first live value goes stale, so "--" appears then and
        // not at the next scheduled sample. A repaint alone could not do it:
        // g_rowState already holds expanded text, so only a re-expansion can turn
        // an expired value into "--".
        InterlockedExchange(&g_extDeadlineMs, (LONG)ExtNextDeadline(g_ext, kMaxExtProviders));
        LeaveCriticalSection(&g_extLock);
    }

    // Lay the new rows out into a scratch copy first. The comparison has to run
    // against BuildRowLayout's output, not against `marked`: it strips the ring
    // markers, so the stored text and the marked text are never equal once a row
    // carries a gauge.
    RowState staged = {};
    for (int r = 0; r < count; r++) {
        // BuildRowLayout records where each gauge sits inside the text flow, which
        // is what lets a ring be drawn mid-sentence rather than only at the edge.
        BuildRowLayout(marked[r], ringCounts[r], staged, r);
        for (int i = 0; i < ringCounts[r]; i++) staged.rings[r][i] = rings[r][i];
    }

    EnterCriticalSection(&g_valuesLock);
    const bool changed = (count != g_publishedRows) || !RowStateEqual(staged, g_rowState, count);
    g_publishedRows = count;
    if (changed) {
        for (int r = 0; r < count; r++) {
            g_rowState.segmentCount[r] = staged.segmentCount[r];
            g_rowState.ringCount[r] = staged.ringCount[r];
            g_rowState.inlineRing[r] = staged.inlineRing[r];
            wcscpy(g_rowState.text[r], staged.text[r]);
            for (int i = 0; i < staged.segmentCount[r]; i++) {
                g_rowState.segments[r][i] = staged.segments[r][i];
            }
            for (int i = 0; i < staged.ringCount[r]; i++) {
                g_rowState.rings[r][i] = staged.rings[r][i];
            }
        }
    }
    g_values = v;
    LeaveCriticalSection(&g_valuesLock);
    return changed;
}

static bool SamplerIteration() {
    // Take a consistent snapshot of the settings, and notice row changes so the
    // collector set can be re-derived.
    ModSettings settings;
    EnterCriticalSection(&g_settingsLock);
    settings = g_settings;
    LONG version = InterlockedCompareExchange(&g_settingsVersion, 0, 0);
    LeaveCriticalSection(&g_settingsLock);

    if (version != g_samplerSeenVersion) {
        g_samplerSeenVersion = version;
        // The CPU identity fields are written here and nowhere else. The loader
        // thread used to refresh them on a settings change, which raced with this
        // thread reading them (docs/external-data-island-design.md 5.6.3 R1b).
        ReadCpuStatic();
        // A row edit changes which external names are wanted, so say so in the log.
        ExtLogRowReferences(settings);
        EnterCriticalSection(&g_valuesLock);
        g_publishedRows = 0;
        LeaveCriticalSection(&g_valuesLock);
    }

    unsigned groups = RequiredGroups(settings);

    // External providers start lazily, and only when a row actually references
    // {ext.*}: the same shape as the GPU backends below, and it is what keeps
    // "off" free of cost.
    const bool extConsumed = (groups & G_EXT) != 0 && settings.externalData;
    if (extConsumed && !g_extStartAttempted) {
        g_extStartAttempted = true;
        ExtStart(settings);
    }
    if (extConsumed) {
        // Cheap every-pass call: starts the configured publisher, supervises it with a
        // bounded restart policy, and stops it the moment the command is cleared.
        ExtPublisherTick();
    } else {
        // Nothing consumes {ext.*} any more -- the switch is off, or the last row that
        // referenced it is gone -- so the publisher this mod started has to go with it.
        // It used to keep running (the tick was simply not called any more), which means
        // a bridge doing memory reads or network calls kept doing them after the user
        // turned the feature off, until the mod was unloaded. Stop() is idempotent and
        // also closes the job object, so it runs on every pass either way; the log line
        // is emitted only when there really was a process. No assertion can cover this
        // (it is a real CreateProcess), so the log is the defence -- same as the
        // nMaxInstances case in AGENTS §6.
        if (ExtPublisherAlive()) {
            Wh_Log(L"EXT publisher: nothing consumes {ext.*} any more; stopping it");
        }
        ExtPublisherStop();
    }

    Values v;
    FillSystemFields(v);

    if (groups & G_CPU) {
        bool wantEffectiveClock = false;
        for (int r = 0; r < settings.rowCount && !wantEffectiveClock; r++) {
            wantEffectiveClock = RowUsesEffectiveClock(settings.rows[r]);
        }
        ReadCpu(v, wantEffectiveClock);
    }
    if (groups & G_MEM) ReadMemory(v);
    if (groups & G_NET) ReadNetwork(v);
    if (groups & G_GPU) {
        // Starting a GPU backend is the expensive step: NVML costs ~22 MB and PDH
        // costs ~550 ms plus ~6 MB. Do it once, on this thread, and only because a
        // row genuinely referenced a GPU variable.
        if (g_gpu.kind == GpuBackend::None && !g_gpuStartAttempted) {
            g_gpuStartAttempted = true;
            if (!g_gpu.Start(v)) Wh_Log(L"No GPU backend available (NVML and PDH both failed)");
        }
        g_gpu.Sample(v);
    }
    g_activeGroups = groups;
    g_sampleCount++;
    return PublishRows(v, settings);
}

// The "a pushed value arrived" path: re-expand the rows against the values already
// on hand and nothing else.
//
// This exists because waking the sampler used to mean re-reading every counter.
// The sampler's wait is interrupted by g_extDirty so a pushed value does not wait a
// whole refresh interval -- but running a full SamplerIteration for it made the
// panel's *system* rows follow the publisher's rate instead of the configured one:
// with a bridge pushing title, lyric and position a few times a second, CPU,
// memory, network (and NVML, when a row asks for {gpu...}) were being sampled
// several times a second, and every one of those samples repainted. The pushed
// value only ever changes the text of the rows, so only the text is rebuilt.
//
// Returns true when the rows changed, i.e. when the change is worth a repaint.
static bool RepublishRows() {
    ModSettings settings;
    EnterCriticalSection(&g_settingsLock);
    settings = g_settings;
    LeaveCriticalSection(&g_settingsLock);

    Values v;
    EnterCriticalSection(&g_valuesLock);
    v = g_values;
    LeaveCriticalSection(&g_valuesLock);

    const bool changed = PublishRows(v, settings);
    if (changed) {
        // Bounded diagnostic: enough to see a chatty publisher in the log, without
        // a line per pushed value. The [perf] line carries both totals as well.
        if ((++g_republishCount % 64) == 0) {
            Wh_Log(L"EXT republish n=%llu samples=%llu (a pushed value republishes, it does not re-sample)",
                   g_republishCount, g_sampleCount);
        }
    }
    return changed;
}

// The sampler's wake policy, kept pure so the rule can be asserted instead of being
// implied by a thread loop: an external wake is cheap (republish the rows), unless
// the refresh interval is due as well -- then the counters get re-read, or a
// publisher pushing values continuously would starve the system rows of samples
// entirely, which is the opposite failure of the one this whole path fixes.
static bool SamplerWakeIsLight(bool extWake, DWORD now, DWORD lastSampleAt, LONG refreshMs) {
    if (!extWake) return false;
    return (LONG)(now - lastSampleAt) < refreshMs;
}

static DWORD WINAPI SamplerThread(LPVOID) {
    // Prime CPU/network deltas so the first published sample is meaningful.
    SamplerIteration();
    DWORD lastSampleAt = GetTickCount();
    LONG waited = 0;
    while (InterlockedCompareExchange(&g_stopSampler, 0, 0) == 0) {
        // Two reasons to wake, and they must not cost the same:
        //   * the refresh interval elapsed -> re-read the system counters;
        //   * a pushed value arrived       -> only the rows' text can have changed.
        // `waited` deliberately survives an external wake, so a publisher cannot push
        // the scheduled sample further and further away; SamplerWakeIsLight is the belt
        // for that braces (a burst can consume the whole interval).
        bool extWake = false;
        while (waited < g_refreshMs) {
            if (InterlockedCompareExchange(&g_stopSampler, 0, 0) != 0) break;
            // A pushed value must not wait a whole refresh interval to appear.
            // Test-and-clear, so a burst of SETs costs exactly one extra pass
            // and one repaint rather than one per message.
            if ((g_activeGroups & G_EXT) &&
                InterlockedCompareExchange(&g_extDirty, 0, 1) == 1) {
                extWake = true;
                break;
            }
            Sleep(50);
            waited += 50;
        }
        if (InterlockedCompareExchange(&g_stopSampler, 0, 0) != 0) break;

        if (SamplerWakeIsLight(extWake, GetTickCount(), lastSampleAt, g_refreshMs)) {
            if (RepublishRows() && g_window) PostMessage(g_window, kMsgRatesReady, 0, 0);
            continue;
        }
        if (SamplerIteration() && g_window) PostMessage(g_window, kMsgRatesReady, 0, 0);
        lastSampleAt = GetTickCount();
        waited = 0;
    }
    return 0;
}

// ---------------------------------------------------------------------------
// External data: the pipe server
//
// One worker thread per pipe instance, so several providers stay connected at
// once. Every operation is overlapped and every wait also waits on the stop
// event: that is the only way Uninit can end these threads deterministically
// instead of closing a handle under a blocked ReadFile. The stop order this
// depends on is written down in docs/external-data-island-design.md 5.6.4.
// ---------------------------------------------------------------------------
static const DWORD kExtProto = 1;
static const WCHAR kExtPipeName[] = L"\\\\.\\pipe\\endfield-info-windhawk";
// Overlapped operations are waited for with a timeout, never INFINITE. A
// completion that never arrives must not park a pipe thread forever: with a
// timeout the loop can notice, ask the pipe whether the data is really there, and
// either keep waiting (a quiet client is normal) or recover and say so. 500 ms is
// short enough that ExtStop's join still finishes promptly.
static const DWORD kExtIoTimeoutMs = 500;

enum ExtCmd { EC_NONE = 0, EC_HELLO, EC_SET, EC_DEL, EC_EVENT, EC_BYE };

struct ExtCommand {
    ExtCmd cmd = EC_NONE;
    const WCHAR *provider = nullptr; int providerLen = 0;
    const WCHAR *key = nullptr;      int keyLen = 0;
    const WCHAR *ttl = nullptr;      int ttlLen = 0;
    const WCHAR *text = nullptr;     int textLen = 0;
    // EVENT only.
    ExtSeverity severity = ES_INFO;
    const WCHAR *title = nullptr;    int titleLen = 0;
    const WCHAR *body = nullptr;     int bodyLen = 0;
};

struct ExtSession {
    WCHAR    provider[kExtProviderChars + 1] = {};
    int      slot = -1;                  // -1 until HELLO
    DWORD    windowStart = 0;            // rate limit window
    int      windowCount = 0;
    unsigned dropped = 0;
    unsigned setLogs = 0;
    // Reply bookkeeping: how many replies in a row have been cancelled after the timeout,
    // and whether that has been taken as "this client does not read them" (see
    // ExtReplyTimeoutsAfter).
    unsigned replyTimeouts = 0;
    bool     repliesAbandoned = false;
};

static bool ExtIsSpace(WCHAR c) {
    return c == L' ' || c == L'\t' || c == L'\r' || c == L'\n';
}

static bool ExtToken(const WCHAR **cursor, const WCHAR **start, int *len) {
    const WCHAR *p = *cursor;
    while (ExtIsSpace(*p)) p++;
    if (!*p) { *cursor = p; return false; }
    const WCHAR *s = p;
    while (*p && !ExtIsSpace(*p)) p++;
    *start = s;
    *len = (int)(p - s);
    *cursor = p;
    return true;
}

// Returns 0 for "handled or ignorable", otherwise the error code to answer with.
// A blank line and a '#' comment are ignored silently, because that is what a
// human typing at the pipe produces.
static int ExtParseLine(const WCHAR *line, ExtCommand &out) {
    out = ExtCommand();
    const WCHAR *cursor = line;
    const WCHAR *word = nullptr;
    int wordLen = 0;
    if (!ExtToken(&cursor, &word, &wordLen)) return 0;
    if (word[0] == L'#') return 0;

    if (wordLen == 3 && _wcsnicmp(word, L"BYE", 3) == 0) { out.cmd = EC_BYE; return 0; }
    if (wordLen == 5 && _wcsnicmp(word, L"HELLO", 5) == 0) {
        const WCHAR *proto = nullptr;
        int protoLen = 0;
        if (!ExtToken(&cursor, &proto, &protoLen)) return 400;
        if (protoLen != 1 || proto[0] != (WCHAR)(L'0' + kExtProto)) return 400;
        if (!ExtToken(&cursor, &out.provider, &out.providerLen)) return 400;
        // The optional glyph/badge token is accepted and ignored until the event
        // milestone needs it, so a client written against the protocol does not
        // get an error for sending it.
        const WCHAR *glyph = nullptr;
        int glyphLen = 0;
        ExtToken(&cursor, &glyph, &glyphLen);
        out.cmd = EC_HELLO;
        return 0;
    }
    if (wordLen == 3 && _wcsnicmp(word, L"SET", 3) == 0) {
        if (!ExtToken(&cursor, &out.provider, &out.providerLen)) return 400;
        if (!ExtToken(&cursor, &out.key, &out.keyLen)) return 400;
        if (!ExtToken(&cursor, &out.ttl, &out.ttlLen)) return 400;
        while (ExtIsSpace(*cursor)) cursor++;
        out.text = cursor;                       // the rest of the line, spaces and all
        out.textLen = (int)wcslen(cursor);
        out.cmd = EC_SET;
        return 0;
    }
    if (wordLen == 3 && _wcsnicmp(word, L"DEL", 3) == 0) {
        if (!ExtToken(&cursor, &out.provider, &out.providerLen)) return 400;
        if (!ExtToken(&cursor, &out.key, &out.keyLen)) return 400;
        out.cmd = EC_DEL;
        return 0;
    }
    if (wordLen == 5 && _wcsnicmp(word, L"EVENT", 5) == 0) {
        const WCHAR *sev = nullptr;
        int sevLen = 0;
        if (!ExtToken(&cursor, &out.provider, &out.providerLen)) return 400;
        if (!ExtToken(&cursor, &sev, &sevLen)) return 400;
        if (!ExtParseSeverity(sev, sevLen, &out.severity)) return 400;
        if (!ExtToken(&cursor, &out.ttl, &out.ttlLen)) return 400;
        if (!ExtToken(&cursor, &out.title, &out.titleLen)) return 400;
        while (ExtIsSpace(*cursor)) cursor++;
        out.body = cursor;                       // the rest of the line, spaces and all
        out.bodyLen = (int)wcslen(cursor);
        out.cmd = EC_EVENT;
        return 0;
    }
    return 400;
}

static bool ExtParseTtl(const WCHAR *ttl, int len, DWORD fallback, DWORD *out) {
    if (len == 1 && ttl[0] == L'-') { *out = fallback; return true; }
    if (len <= 0 || len > 7) return false;
    DWORD value = 0;
    for (int i = 0; i < len; i++) {
        if (ttl[i] < L'0' || ttl[i] > L'9') return false;
        value = value * 10 + (DWORD)(ttl[i] - L'0');
    }
    *out = value;
    return true;
}

// Waits for a posted overlapped operation while staying responsive to the stop
// event. 1 = completed, 0 = frame too long, -1 = stopped or failed, -2 = still
// pending after the timeout (the caller decides whether that is normal).
static int ExtWaitIo(HANDLE pipe, OVERLAPPED *ov, DWORD *bytes) {
    HANDLE waits[2] = { ov->hEvent, g_extStopEvent };
    DWORD count = g_extStopEvent ? 2 : 1;
    DWORD which = WaitForMultipleObjects(count, waits, FALSE, kExtIoTimeoutMs);
    if (which == WAIT_TIMEOUT) return -2;
    if (count == 2 && which == WAIT_OBJECT_0 + 1) {
        // Cancel, then WAIT for the cancellation: returning while the kernel
        // still owns this OVERLAPPED would let it write to dead stack.
        CancelIoEx(pipe, ov);
        DWORD ignored = 0;
        GetOverlappedResult(pipe, ov, &ignored, TRUE);
        return -1;
    }
    if (which != WAIT_OBJECT_0) return -1;
    DWORD got = 0;
    if (GetOverlappedResult(pipe, ov, &got, FALSE)) {
        if (bytes) *bytes = got;
        return 1;
    }
    return GetLastError() == ERROR_MORE_DATA ? 0 : -1;
}

// Which branch of the accept sequence ran. The mod's own self-test and a foreign
// client can take different paths through ConnectNamedPipe, and a difference there is
// a lead -- this is the last part of the accept path that was invisible.
static void ExtConnectLog(const WCHAR *branch) {
    if (g_extConnectLogs < 8) {
        Wh_Log(L"EXT connect: %s", branch);
        g_extConnectLogs++;
    }
}

static bool ExtConnect(HANDLE pipe, OVERLAPPED *ov) {
    ResetEvent(ov->hEvent);
    if (ConnectNamedPipe(pipe, ov)) {
        ExtConnectLog(L"sync");
        return true;
    }
    DWORD err = GetLastError();
    // A client that connected between CreateNamedPipe and ConnectNamedPipe makes
    // this call fail with ERROR_PIPE_CONNECTED. That is success, and treating it
    // as failure is the classic bug in this sequence.
    if (err == ERROR_PIPE_CONNECTED) {
        ExtConnectLog(L"already connected");
        return true;
    }
    if (err != ERROR_IO_PENDING) {
        ExtConnectLog(L"failed");
        if (g_extReadErrLogs < 4) {
            Wh_Log(L"EXT connect failed: %lu", err);
            g_extReadErrLogs++;
        }
        return false;
    }
    // Nobody connected yet. Waiting here is this thread's whole job, so a timeout
    // must not be read as failure -- it just means "still listening".
    for (;;) {
        DWORD bytes = 0;
        int wait = ExtWaitIo(pipe, ov, &bytes);
        if (wait == 1) {
            ExtConnectLog(L"pending, then a client arrived");
            return true;
        }
        if (wait != -2) {
            ExtConnectLog(L"wait failed");
            return false;
        }
    }
}

// 1 = one frame read, 0 = the frame is larger than the buffer, -1 = stopped or broken.
//
// The wait is bounded (kExtIoTimeoutMs) and a timeout is not an error. What happens
// next depends on what the pipe says:
//   * bytes queued with no completion -> the completion was lost; log it and re-post
//     at once (a cancelled read consumes nothing, so the bytes are still there);
//   * an empty buffer -> normally just a quiet client, so the same pending read keeps
//     waiting -- but re-post every few seconds anyway as a safety net, because
//     PeekNamedPipe is the only witness that the bytes are not there. The outer loop
//     is unbounded on purpose: a quiet provider must never be disconnected for being
//     quiet, and a re-post costs one cancel plus two I/O calls.
static int ExtReadFrame(HANDLE pipe, OVERLAPPED *ov, char *buffer, DWORD cap, DWORD *got) {
    for (;;) {
        *got = 0;
        ResetEvent(ov->hEvent);
        if (ReadFile(pipe, buffer, cap, got, ov)) return 1;
        DWORD err = GetLastError();
        // Message mode reports an over-long frame this way instead of truncating it:
        // this is the over-long-frame detector, not a generic failure.
        if (err == ERROR_MORE_DATA) return 0;
        if (err != ERROR_IO_PENDING) {
            // "result=-1" on its own says nothing; the code says whether the client
            // left (109 ERROR_BROKEN_PIPE) or something else went wrong.
            if (g_extReadErrLogs < 4) {
                Wh_Log(L"EXT read failed: %lu", err);
                g_extReadErrLogs++;
            }
            return -1;
        }

        DWORD started = GetTickCount();
        bool warned = false;
        for (;;) {
            int wait = ExtWaitIo(pipe, ov, got);
            if (wait != -2) return wait;

            DWORD available = 0;
            if (!PeekNamedPipe(pipe, nullptr, 0, nullptr, &available, nullptr)) {
                if (g_extReadErrLogs < 4) {
                    Wh_Log(L"EXT peek failed: %lu", GetLastError());
                    g_extReadErrLogs++;
                }
                return -1;
            }
            const bool empty = (available == 0);
            const DWORD elapsed = GetTickCount() - started;

            if (empty && !warned && elapsed >= 2000 && g_extStallLogs < 4) {
                // Nothing queued yet. If that goes on, say so once: a connected client
                // whose bytes never arrive must not look like a mystery.
                Wh_Log(L"EXT: no frame for 2s and the pipe buffer is empty "
                       L"(nothing this instance can see)");
                g_extStallLogs++;
                warned = true;
            }
            // Queued bytes with no completion is a lost completion: re-post at once.
            if (!empty && g_extStallLogs < 4) {
                Wh_Log(L"EXT: read completion never arrived with %lu bytes queued; "
                       L"re-posting", available);
                g_extStallLogs++;
            }
            // An empty buffer is normally just a quiet client -- but re-post every few
            // seconds anyway. PeekNamedPipe is the only witness that the bytes are not
            // there, and a cancelled read consumes nothing, so if it ever under-reports
            // this recovers the frame instead of waiting forever for a completion that
            // was already delivered.
            if (!empty || elapsed >= 4000) {
                CancelIoEx(pipe, ov);
                DWORD ignored = 0;
                GetOverlappedResult(pipe, ov, &ignored, TRUE);
                break;                         // re-post; queued bytes are untouched
            }
        }
    }
    return -1;
}

// A client that never reads its replies fills the out buffer, and every reply write from
// then on is cancelled after the full I/O timeout -- 500 ms of the pipe thread's time, per
// frame, for a reply nobody will ever read. That is not hypothetical: the NetEase bridge
// publishes about four values a second and never reads. Its out buffer held ~1300 replies,
// so five minutes in, each frame began to cost half a second; the server fell behind the
// publisher, and because two values for one key are then applied one timeout apart, the
// panel visibly alternated between them (the song title with and without its artist, the
// lyric on the right line and on the frozen one). Ignoring the *outcome* of the write was
// already deliberate; ignoring its *cost* was the bug.
//
// Two consecutive timeouts is enough to conclude the client is not reading. A client that
// does read -- the reference clients do -- never even reaches one, because it drains the
// buffer long before it fills.
static const unsigned kExtReplyTimeoutLimit = 2;

// The reply bookkeeping as a pure state transition, so the rule above is asserted instead
// of being inferred from a pipe: the new consecutive-timeout count, and whether that count
// means this client has stopped reading.
static unsigned ExtReplyTimeoutsAfter(int ioResult, unsigned previous, bool *abandon) {
    if (abandon) *abandon = false;
    if (ioResult == 1) return 0;          // it went out: the client is reading
    if (ioResult != -2) return previous;  // a hard failure says nothing about reading
    const unsigned next = previous + 1;
    if (abandon) *abandon = next >= kExtReplyTimeoutLimit;
    return next;
}

static void ExtWriteLine(HANDLE pipe, OVERLAPPED *ov, const WCHAR *text, ExtSession *ses) {
    // A client that is not reading gets no further replies (see the note above), so its
    // frames cost nothing but the read that carries them.
    if (ses && ses->repliesAbandoned) return;

    char utf8[96];
    int n = WideCharToMultiByte(CP_UTF8, 0, text, -1, utf8, (int)sizeof(utf8) - 2, nullptr, nullptr);
    if (n <= 1) return;
    utf8[n - 1] = '\n';                     // replace the terminator: one line per reply
    ResetEvent(ov->hEvent);
    DWORD written = 0;
    int io = 1;
    if (!WriteFile(pipe, utf8, (DWORD)n, &written, ov)) {
        io = GetLastError() == ERROR_IO_PENDING ? ExtWaitIo(pipe, ov, &written) : -1;
    }
    if (io == -2) {
        // The OVERLAPPED has to be left clean for the next operation, whichever it is.
        CancelIoEx(pipe, ov);
        DWORD ignored = 0;
        GetOverlappedResult(pipe, ov, &ignored, TRUE);
    }
    if (!ses) return;
    bool abandon = false;
    ses->replyTimeouts = ExtReplyTimeoutsAfter(io, ses->replyTimeouts, &abandon);
    if (abandon && !ses->repliesAbandoned) {
        ses->repliesAbandoned = true;
        Wh_Log(L"EXT client is not reading its replies (%u in a row, %d ms each): no more "
               L"replies on this connection; its frames are now read as fast as they arrive",
               ses->replyTimeouts, kExtIoTimeoutMs);
    }
}

static void ExtReplyErr(HANDLE pipe, OVERLAPPED *ov, int code, ExtSession *ses) {
    WCHAR text[24];
    swprintf(text, 24, L"ERR %d", code);
    ExtWriteLine(pipe, ov, text, ses);
}

// What this pipe actually is, as the OS sees it. Message mode is what the protocol
// assumes, and curInstances is how a *second* server process holding instances of
// the same name would show up -- such a process would steal clients while this one
// waits for bytes that never arrive. Bounded: a few lines per connection.
static void ExtLogPipeState(HANDLE pipe) {
    DWORD state = 0;
    DWORD curInstances = 0;
    DWORD flags = 0;
    DWORD outSize = 0;
    DWORD inSize = 0;
    DWORD maxInstances = 0;
    GetNamedPipeHandleStateW(pipe, &state, &curInstances, nullptr, nullptr, nullptr, 0);
    GetNamedPipeInfo(pipe, &flags, &outSize, &inSize, &maxInstances);
    Wh_Log(L"EXT: pipe flags=0x%lx curInstances=%lu maxInstances=%lu out=%lu in=%lu",
           flags, curInstances, maxInstances, outSize, inSize);
}

static void ExtLogClient(HANDLE pipe) {
    ULONG pid = 0;
    if (!GetNamedPipeClientProcessId(pipe, &pid)) return;
    WCHAR path[MAX_PATH] = L"?";
    HANDLE proc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (proc) {
        DWORD len = MAX_PATH;
        if (!QueryFullProcessImageNameW(proc, 0, path, &len)) path[0] = L'\0';
        CloseHandle(proc);
    }
    // Diagnostic only. This is NOT authentication: the trust boundary is "same
    // user". A name in the log is what makes a badly behaved provider
    // identifiable after the fact.
    Wh_Log(L"EXT: client pid=%lu image=%s", pid, path);
}

static const WCHAR *ExtCmdName(ExtCmd cmd) {
    switch (cmd) {
    case EC_HELLO: return L"HELLO";
    case EC_SET: return L"SET";
    case EC_DEL: return L"DEL";
    case EC_EVENT: return L"EVENT";
    case EC_BYE: return L"BYE";
    default: return L"?";
    }
}

// Every refusal is logged with its code and the command it came from. A silent
// refusal is indistinguishable from "the client never sent anything", which is
// exactly the confusion this caused once: the client saw ERR 405 while the log
// showed nothing but a successful connect.
static int ExtRefuse(HANDLE pipe, OVERLAPPED *ov, int code, const ExtCommand &cmd,
                     ExtSession &ses) {
    Wh_Log(L"EXT err code=%d cmd=%s provider=%s keyLen=%d", code, ExtCmdName(cmd.cmd),
           ses.provider[0] ? ses.provider : (cmd.provider ? L"(unregistered)" : L"(none)"),
           cmd.keyLen);
    ExtReplyErr(pipe, ov, code, &ses);
    return code;
}

static int ExtHandleLine(HANDLE pipe, OVERLAPPED *ov, const WCHAR *line, ExtSession &ses) {
    ExtCommand cmd;
    int code = ExtParseLine(line, cmd);
    if (code == 0 && cmd.cmd == EC_NONE) return 0;
    if (code != 0) return ExtRefuse(pipe, ov, code, cmd, ses);
    if (cmd.cmd == EC_BYE) {
        Wh_Log(L"EXT bye provider=%s", ses.provider[0] ? ses.provider : L"(none)");
        ExtWriteLine(pipe, ov, L"OK bye", &ses);
        return 0;
    }

    // One snapshot per command, taken under the lock the loader writes with.
    // Nothing is cached in a global, so a saved setting applies from the next
    // command on and there is no second copy that can drift.
    ModSettings snap;
    EnterCriticalSection(&g_settingsLock);
    snap = g_settings;
    LeaveCriticalSection(&g_settingsLock);

    if (!snap.externalData) return ExtRefuse(pipe, ov, 405, cmd, ses);
    if (!ExtNameValid(cmd.provider, cmd.providerLen, true)) return ExtRefuse(pipe, ov, 400, cmd, ses);
    if (!ExtProviderAllowed(snap, cmd.provider, cmd.providerLen)) {
        return ExtRefuse(pipe, ov, 405, cmd, ses);
    }

    if (cmd.cmd == EC_HELLO) {
        EnterCriticalSection(&g_extLock);
        ses.slot = ExtFindProvider(cmd.provider, cmd.providerLen, true, snap.extMaxProviders);
        LeaveCriticalSection(&g_extLock);
        if (ses.slot < 0) return ExtRefuse(pipe, ov, 403, cmd, ses);
        wcsncpy(ses.provider, cmd.provider, cmd.providerLen);
        ses.provider[cmd.providerLen] = L'\0';
        Wh_Log(L"EXT hello provider=%s slot=%d", ses.provider, ses.slot);
        ExtWriteLine(pipe, ov, L"OK", &ses);
        return 0;
    }

    if (ses.slot < 0) return ExtRefuse(pipe, ov, 401, cmd, ses);   // SET/DEL/EVENT before HELLO
    if (cmd.cmd == EC_EVENT) {
        DWORD ttl = 0;
        if (!ExtParseTtl(cmd.ttl, cmd.ttlLen, (DWORD)snap.extEventMs, &ttl)) {
            return ExtRefuse(pipe, ov, 400, cmd, ses);
        }
        if (ttl == 0) {
            // "No duration" is not an event. Saying so beats queueing something that
            // would have to vanish the instant it appeared.
            if (g_extStallLogs < 4) {
                Wh_Log(L"EXT event dropped reason=ttl0");
                g_extStallLogs++;
            }
            ExtWriteLine(pipe, ov, L"OK", &ses);
            return 0;
        }
        IslandEvent ev = {};
        wcsncpy(ev.provider, ses.provider, kExtProviderChars);
        ev.provider[kExtProviderChars] = L'\0';
        ExtSanitize(cmd.title ? cmd.title : L"", ev.title, kIslandTitleChars);
        ExtSanitize(cmd.body ? cmd.body : L"", ev.body, kIslandBodyChars);
        ev.severity = cmd.severity;
        ev.ttlMs = ttl;
        ev.repeat = 1;
        ev.queuedAt = GetTickCount();
        int queued = 0;
        if (g_islandLockReady) {
            EnterCriticalSection(&g_islandLock);
            g_islandCount = IslandEnqueue(g_islandQueue, g_islandCount, snap.extEventQueue, ev,
                                          &g_islandDrops);
            queued = g_islandCount;
            g_islandAccepted++;
            LeaveCriticalSection(&g_islandLock);
        }
        // Counts and lengths only: an event's text is content and never goes to the log.
        Wh_Log(L"EXT event provider=%s sev=%d ttl=%lu titleLen=%d bodyLen=%d queue=%d drops=%u",
               ev.provider, (int)ev.severity, ttl, (int)wcslen(ev.title), (int)wcslen(ev.body),
               queued, g_islandDrops);
        // Wake the HUD through its thread, never through the window handle: this
        // thread cannot know whether the window still exists (design 5.6.3 R5).
        if (g_hudThreadId) PostThreadMessageW(g_hudThreadId, kMsgIslandEvent, 0, 0);
        ExtWriteLine(pipe, ov, L"OK", &ses);
        return 0;
    }
    if (!ExtNameValid(cmd.key, cmd.keyLen, false)) return ExtRefuse(pipe, ov, 400, cmd, ses);

    WCHAR key[kExtKeyChars + 1];
    wcsncpy(key, cmd.key, cmd.keyLen);
    key[cmd.keyLen] = L'\0';

    if (cmd.cmd == EC_DEL) {
        ExtInvalidate(ses.slot, key);
        InterlockedExchange(&g_extDirty, 1);   // show "--" without waiting a whole interval
        ExtWriteLine(pipe, ov, L"OK", &ses);
        return 0;
    }

    DWORD now = GetTickCount();
    if (now - ses.windowStart >= 1000) {
        ses.windowStart = now;
        ses.windowCount = 0;
    }
    if (++ses.windowCount > snap.extMaxUpdates) {
        ses.dropped++;
        if (ses.dropped == 1 || (ses.dropped % 64) == 0)
            Wh_Log(L"EXT drop reason=rate provider=%s count=%u", ses.provider, ses.dropped);
        ExtReplyErr(pipe, ov, 404, &ses);
        return 404;                            // dropped, connection stays open
    }

    DWORD ttl = 0;
    if (!ExtParseTtl(cmd.ttl, cmd.ttlLen, (DWORD)snap.extValueTtlMs, &ttl)) {
        return ExtRefuse(pipe, ov, 400, cmd, ses);
    }
    if (ttl == 0) {                            // "publish nothing" is a delete
        ExtInvalidate(ses.slot, key);
        InterlockedExchange(&g_extDirty, 1);
        ExtWriteLine(pipe, ov, L"OK", &ses);
        return 0;
    }

    ExtStoreValue(ses.slot, key, cmd.text, ttl, snap.extMaxValue);
    InterlockedExchange(&g_extDirty, 1);       // the sampler merges bursts
    // Bounded diagnostic. The key names what is on screen and is the useful part;
    // the value itself is never logged.
    if (ses.setLogs < 4 || (ses.setLogs % 256) == 0)
        Wh_Log(L"EXT set provider=%s key=%s chars=%d ttl=%lu", ses.provider, key, cmd.textLen, ttl);
    ses.setLogs++;
    ExtWriteLine(pipe, ov, L"OK", &ses);
    return 0;
}

static void ExtServeClient(HANDLE pipe, OVERLAPPED *ov, ExtSession &ses) {
    char frame[kExtMaxLineBytes];
    // 4 KB of UTF-8 can decode to at most that many UTF-16 units, so the wide
    // buffer is sized from the byte cap rather than from half of it.
    WCHAR line[kExtMaxLineBytes + 1];
    int frames = 0;
    int reads = 0;
    for (;;) {
        if (InterlockedCompareExchange(&g_extStop, 0, 0)) break;
        DWORD got = 0;
        int read = ExtReadFrame(pipe, ov, frame, (DWORD)sizeof(frame), &got);
        // Bounded per-connection trace of the read outcomes: this is the line that
        // says whether frames arrive at all, which is what separates "connected but
        // nothing happened" from "nothing was ever sent".
        if (++reads <= 4) Wh_Log(L"EXT read#%d result=%d got=%lu", reads, read, got);
        if (read < 0) break;
        if (read == 0) {
            Wh_Log(L"EXT err code=402: frame larger than %d bytes", kExtMaxLineBytes);
            ExtReplyErr(pipe, ov, 402, &ses);
            break;
        }
        if (got == 0) {
            // A zero-length message is legal and carries nothing. Looping silently
            // here would look exactly like a stall, so say it once.
            if (g_extStallLogs < 4) {
                Wh_Log(L"EXT: zero-length frame ignored");
                g_extStallLogs++;
            }
            continue;
        }
        // The first frames of each connection are logged so "the client connected
        // but nothing happened" can be told apart from "nothing arrived at all".
        if (++frames <= 2) Wh_Log(L"EXT recv bytes=%lu", got);
        int wide = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, frame, (int)got, line,
                                       kExtMaxLineBytes);
        if (wide <= 0) {
            Wh_Log(L"EXT err code=400: frame is not valid UTF-8 (bytes=%lu)", got);
            ExtReplyErr(pipe, ov, 400, &ses);
            continue;
        }
        line[wide] = L'\0';
        ExtHandleLine(pipe, ov, line, ses);
    }
    Wh_Log(L"EXT client done: frames=%d sets=%u dropped=%u", frames, ses.setLogs, ses.dropped);
}

// The pipe's DACL: SYSTEM, Administrators and the user this process runs as.
//
// The user's SID has to be spelled out. "CO" (creator owner) is a placeholder
// that is only substituted in *inheritable* ACEs on container objects; in a
// pipe's object DACL it is compared literally, matches nobody, and every client
// -- including the user who started the mod -- gets ERROR_ACCESS_DENIED. That is
// the bug this function used to have, and the regression test builds a pipe from
// this descriptor and connects to it so it cannot come back.
static PSECURITY_DESCRIPTOR ExtPipeSecurityDescriptor() {
    WCHAR sidText[200] = L"";
    HANDLE token = nullptr;
    if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) {
        BYTE info[256] = {};
        DWORD size = 0;
        if (GetTokenInformation(token, TokenUser, info, sizeof(info), &size)) {
            LPWSTR text = nullptr;
            if (ConvertSidToStringSidW(((TOKEN_USER *)info)->User.Sid, &text)) {
                wcsncpy(sidText, text, 199);
                sidText[199] = L'\0';
                LocalFree(text);
            }
        }
        CloseHandle(token);
    }
    if (!sidText[0]) {
        // Falling back to the process default DACL errs on the permissive side:
        // it grants this user by construction instead of locking them out.
        Wh_Log(L"EXT: could not resolve the process user SID (%lu); using the "
               L"process default DACL", GetLastError());
        return nullptr;
    }

    // Built by concatenation rather than swprintf: what "%s" means in a wide
    // format depends on __USE_MINGW_ANSI_STDIO, and this particular string is a
    // security descriptor. It must not depend on which stdio the toolchain picked.
    WCHAR sddl[256];
    wcscpy(sddl, L"D:(A;;GA;;;SY)(A;;GA;;;BA)(A;;GA;;;");
    size_t used = wcslen(sddl);
    wcsncat(sddl, sidText, sizeof(sddl) / sizeof(sddl[0]) - used - 2);
    wcsncat(sddl, L")", 1);
    PSECURITY_DESCRIPTOR sd = nullptr;
    if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl, SDDL_REVISION_1, &sd,
                                                              nullptr)) {
        Wh_Log(L"EXT: could not build the pipe DACL from \"%s\" (%lu); using the "
               L"process default", sddl, GetLastError());
        return nullptr;
    }
    // The granted SID is the discriminator to look at if a client is ever refused.
    Wh_Log(L"EXT: pipe DACL grants SYSTEM, Administrators and %s", sidText);
    return sd;
}

static DWORD WINAPI ExtPipeThread(LPVOID) {
    OVERLAPPED ov = {};
    ov.hEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (!ov.hEvent) { Wh_Log(L"EXT: CreateEvent failed: %lu", GetLastError()); return 1; }

    PSECURITY_DESCRIPTOR sd = ExtPipeSecurityDescriptor();
    SECURITY_ATTRIBUTES sa = {};
    sa.nLength = sizeof(sa);
    sa.lpSecurityDescriptor = sd;
    sa.bInheritHandle = FALSE;

    while (!InterlockedCompareExchange(&g_extStop, 0, 0)) {
        // nMaxInstances must be the number of worker threads, because each thread
        // creates exactly one instance. Passing 1 here made the first thread the
        // only one that could ever create a pipe and killed the other three with
        // ERROR_PIPE_BUSY (231) -- so the server silently served one client while
        // claiming four instances.
        HANDLE pipe = CreateNamedPipeW(kExtPipeName,
                                       PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED,
                                       PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT |
                                           PIPE_REJECT_REMOTE_CLIENTS,
                                       kExtInstances, 8192, 8192, 0, &sa);
        if (pipe == INVALID_HANDLE_VALUE) {
            DWORD err = GetLastError();
            if (err == ERROR_PIPE_BUSY) {
                // Every instance is taken right now. That is transient -- a client
                // will disconnect -- so wait for one instead of retiring this thread
                // for the rest of the session.
                if (g_extBusyLogs < 4) {
                    Wh_Log(L"EXT: all %d instances are busy; waiting for one to free up",
                           kExtInstances);
                    g_extBusyLogs++;
                }
                for (int waited = 0; waited < 200; waited += 50) {
                    if (InterlockedCompareExchange(&g_extStop, 0, 0)) break;
                    Sleep(50);
                }
                continue;
            }
            Wh_Log(L"EXT: CreateNamedPipe failed: %lu", err);
            break;
        }
        if (!ExtConnect(pipe, &ov)) { CloseHandle(pipe); break; }
        ExtLogClient(pipe);
        if (g_extConnLogs < 4) {
            g_extConnLogs++;
            ExtLogPipeState(pipe);
        }
        ExtSession ses;                        // per connection: provider + rate window
        ExtServeClient(pipe, &ov, ses);
        // No FlushFileBuffers before disconnecting: it blocks until the client has
        // read everything, and a client that never reads must not be able to pin
        // this thread.
        DisconnectNamedPipe(pipe);
        CloseHandle(pipe);
    }
    if (sd) LocalFree(sd);
    CloseHandle(ov.hEvent);
    return 0;
}

// A one-shot self-test: the mod connects to its own pipe and publishes a value.
//
// The pipe read path cannot be exercised in a sandbox (a client needs write access,
// which confined environments refuse), so the machine running the mod proves it
// instead. If the server logs the self-test's HELLO/SET, the transport works and a
// missing value is the client's doing; if it does not, the read path is at fault and
// its own logs (recv / read result / the 2s watchdog) say where. Bounded: 10 x 50 ms
// of connection retries, once per load.
static void ExtSelfTest() {
    HANDLE client = INVALID_HANDLE_VALUE;
    DWORD lastErr = 0;
    for (int attempt = 0; attempt < 10; attempt++) {
        client = CreateFileW(kExtPipeName, GENERIC_READ | GENERIC_WRITE, 0, nullptr,
                             OPEN_EXISTING, 0, nullptr);
        if (client != INVALID_HANDLE_VALUE) break;
        lastErr = GetLastError();
        Sleep(50);
    }
    if (client == INVALID_HANDLE_VALUE) {
        Wh_Log(L"EXT: self-test could not connect to its own pipe (err=%lu)", lastErr);
        return;
    }
    // Two separate writes: one message per line is what the protocol relies on, so
    // that is the part worth testing rather than a single byte stream. The TTL is
    // explicit and long: this value is meant to be *looked at* afterwards through a
    // row like "{ext.selftest.ok}", so it must outlive the moment of startup.
    const char line1[] = "HELLO 1 selftest\n";
    const char line2[] = "SET selftest ok 600000 - pipe self-test\n";
    DWORD written = 0;
    BOOL sent1 = WriteFile(client, line1, (DWORD)sizeof(line1) - 1, &written, nullptr);
    BOOL sent2 = WriteFile(client, line2, (DWORD)sizeof(line2) - 1, &written, nullptr);
    CloseHandle(client);
    Wh_Log(L"EXT: self-test sent 2 frames (wrote=%d/%d, ttl=600000); the {ext.selftest.ok} "
           L"row should read \"pipe self-test\" for ten minutes", sent1 ? 1 : 0,
           sent2 ? 1 : 0);
}

// ---------------------------------------------------------------------------
// External publisher: the mod starts the publisher the user configured
// ---------------------------------------------------------------------------
// {ext.*} only has values if something publishes them. The user types a command line in
// the settings; the mod starts it, restarts it a bounded number of times if it dies, and
// makes sure it cannot outlive the mod. A job object with KILL_ON_JOB_CLOSE reaps the whole
// tree -- TerminateProcess on the child alone cannot reach grandchildren the script itself
// started.
//
// It deliberately does NOT start or restart any third-party application the user already
// uses (the music client, say): that would change how their software starts, which is not
// ours to change. Only the command written in the setting is started.
static HANDLE g_pubJob = nullptr;
static HANDLE g_pubProc = nullptr;
static DWORD g_pubRestarts = 0;
// When the current attempt was started, so a run that stays up can earn the restart
// budget back (see PublisherEarnedBackRestarts).
static DWORD g_pubStartedAt = 0;
static DWORD g_pubLastTry = 0;
static DWORD g_pubLastRead = 0;
static WCHAR g_pubCmd[512] = L"";
static bool g_pubGaveUp = false;

static const DWORD kPubMaxRestarts = 5;
static const DWORD kPubBackoffMs = 5000;
static const DWORD kPubSettingRecheckMs = 10000;
// A publisher that stays up this long has proved the failures were not systematic, so
// its restart budget is restored. Without this the budget was a *lifetime* counter:
// five starts ever -- including the harmless ones a user causes by restarting the
// music client, or by killing the publisher by hand -- permanently disabled it, and
// the only cure was reloading the mod. Symptom in the field: the song title and lyric
// stop updating for good, and the log says "gave up after 5 restarts".
static const DWORD kPubHealthyMs = 60000;
// 看门狗：目标进程的扫描周期（**不每拍做** ✗ —— 建进程快照不便宜 ✓）
static const DWORD kPubWatchScanMs = 5000;
static WCHAR g_pubWatch[128] = L"";   // 要观察的进程名（空 = 不做看门狗，维持旧行为 ✓）
static bool g_pubWatchFound = false;
static DWORD g_pubLastScan = 0;
static bool g_pubWatchLogged = false;

// Pure: split a command line the way CreateProcessW does when lpApplicationName is null.
// A leading quoted token may contain spaces; an unquoted one ends at the first space.
// Everything after the executable is the argument string, kept verbatim (no shell).
static bool ExtParsePublisherCommand(const WCHAR *cmd, WCHAR *exe, size_t exeChars,
                                     WCHAR *args, size_t argsChars) {
    if (exeChars) exe[0] = 0;
    if (argsChars) args[0] = 0;
    if (!cmd || !exeChars) return false;
    const WCHAR *p = cmd;
    while (*p == L' ' || *p == L'\t') p++;
    size_t n = 0;
    if (*p == L'"') {
        p++;
        while (*p && *p != L'"') {
            if (n + 1 < exeChars) exe[n++] = *p;
            p++;
        }
        if (*p == L'"') p++;
    } else {
        while (*p && *p != L' ' && *p != L'\t') {
            if (n + 1 < exeChars) exe[n++] = *p;
            p++;
        }
    }
    exe[n] = 0;
    if (n == 0) return false;
    while (*p == L' ' || *p == L'\t') p++;
    size_t m = 0;
    if (argsChars) {
        while (*p) {
            if (m + 1 < argsChars) args[m++] = *p;
            p++;
        }
        args[m] = 0;
    }
    return true;
}

static bool ExtPublisherAlive() {
    if (!g_pubProc) return false;
    DWORD code = 0;
    if (!GetExitCodeProcess(g_pubProc, &code)) {
        CloseHandle(g_pubProc);
        g_pubProc = nullptr;
        return false;
    }
    if (code == STILL_ACTIVE) return true;
    CloseHandle(g_pubProc);
    g_pubProc = nullptr;
    return false;
}

static void ExtPublisherStop() {
    if (g_pubProc) {
        TerminateProcess(g_pubProc, 0);
        CloseHandle(g_pubProc);
        g_pubProc = nullptr;
    }
    if (g_pubJob) {
        CloseHandle(g_pubJob);   // KILL_ON_JOB_CLOSE: nothing of ours outlives the mod
        g_pubJob = nullptr;
    }
}

static void ExtPublisherStart(const WCHAR *exe, const WCHAR *args) {
    WCHAR cmdline[768];
    if (args && *args) wsprintfW(cmdline, L"\"%s\" %s", exe, args);
    else wsprintfW(cmdline, L"\"%s\"", exe);

    STARTUPINFOW si;
    PROCESS_INFORMATION pi;
    ZeroMemory(&si, sizeof(si));
    ZeroMemory(&pi, sizeof(pi));
    si.cb = sizeof(si);

    // CREATE_NO_WINDOW: a publisher must not flash a console. SUSPENDED so the child cannot
    // run away before it is inside the job object.
    if (!CreateProcessW(nullptr, cmdline, nullptr, nullptr, FALSE,
                        CREATE_NO_WINDOW | CREATE_SUSPENDED, nullptr, nullptr, &si, &pi)) {
        Wh_Log(L"EXT publisher: CreateProcess failed for '%s' (error=%lu)", exe, GetLastError());
        return;
    }

    if (!g_pubJob) {
        g_pubJob = CreateJobObjectW(nullptr, nullptr);
        if (g_pubJob) {
            JOBOBJECT_EXTENDED_LIMIT_INFORMATION li;
            ZeroMemory(&li, sizeof(li));
            li.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
            if (!SetInformationJobObject(g_pubJob, JobObjectExtendedLimitInformation, &li,
                                        sizeof(li))) {
                Wh_Log(L"EXT publisher: SetInformationJobObject failed (error=%lu)",
                       GetLastError());
            }
        }
    }
    if (g_pubJob && !AssignProcessToJobObject(g_pubJob, pi.hProcess)) {
        Wh_Log(L"EXT publisher: AssignProcessToJobObject failed (error=%lu)", GetLastError());
    }

    ResumeThread(pi.hThread);
    CloseHandle(pi.hThread);
    g_pubProc = pi.hProcess;
    Wh_Log(L"EXT publisher: started pid=%lu '%s'", pi.dwProcessId, cmdline);
}

// 看门狗（用户设计 2026-10-13）：把"触发"与"信息收集"分开 —— mod 只**观察**某个进程在不在跑，
// 在跑才启动用户配置的发布器，退出就把它收掉。**绝不启动/重启那个程序本身** ✓。
// 名字比较是纯函数（有断言 ✓）：大小写不敏感 ✓、带不带 .exe 都认 ✓、空名字一律不匹配 ✓。
static bool ExtProcessNameMatches(const WCHAR *imageName, const WCHAR *want) {
    if (!imageName || !want || !*imageName || !*want) return false;
    const WCHAR *a = imageName;
    const WCHAR *b = want;
    for (;;) {
        WCHAR ca = *a;
        WCHAR cb = *b;
        if (ca >= L'A' && ca <= L'Z') ca = (WCHAR)(ca - L'A' + L'a');
        if (cb >= L'A' && cb <= L'Z') cb = (WCHAR)(cb - L'A' + L'a');
        if (cb == 0) {
            if (ca == 0) return true;
            // 用户写 cloudmusic、实际映像名是 cloudmusic.exe → 认 ✓
            if (ca == L'.' && (a[1] == L'e' || a[1] == L'E') && (a[2] == L'x' || a[2] == L'X') &&
                (a[3] == L'e' || a[3] == L'E') && a[4] == 0)
                return true;
            return false;
        }
        if (ca == 0) {
            // 反向：用户写 cloudmusic.exe、而映像名恰好没有后缀 → 也认 ✓
            // （断言第一次跑就把这一条抓出来了 ✗ —— 只做单向匹配是不够的 ✓）
            if (cb == L'.' && (b[1] == L'e' || b[1] == L'E') && (b[2] == L'x' || b[2] == L'X') &&
                (b[3] == L'e' || b[3] == L'E') && b[4] == 0)
                return true;
            return false;
        }
        if (ca != cb) return false;
        a++;
        b++;
    }
}

// 目标进程在不在跑。用 kernel32 的 toolhelp（**不加新库依赖** ✓）；调用方按 kPubWatchScanMs 限频 ✓。
// 这几项**自己声明**而不是引 <tlhelp32.h> ✓ —— 与 GDI+ / pdh 常量同一种做法：不动包含顺序、
// 不新增依赖 ✓（符号都在 kernel32 里 ✓）。
#define TH32CS_SNAPPROCESS 0x00000002
struct ExtProcessEntry32W {
    DWORD dwSize;
    DWORD cntUsage;
    DWORD th32ProcessID;
    ULONG_PTR th32DefaultHeapID;
    DWORD th32ModuleID;
    DWORD cntThreads;
    DWORD th32ParentProcessID;
    LONG pcPriClassBase;
    DWORD dwFlags;
    WCHAR szExeFile[MAX_PATH];
};
extern "C" __declspec(dllimport) HANDLE WINAPI CreateToolhelp32Snapshot(DWORD, DWORD);
extern "C" __declspec(dllimport) BOOL WINAPI Process32FirstW(HANDLE, ExtProcessEntry32W *);
extern "C" __declspec(dllimport) BOOL WINAPI Process32NextW(HANDLE, ExtProcessEntry32W *);

static bool ExtProcessRunning(const WCHAR *name) {
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return false;
    ExtProcessEntry32W pe;
    ZeroMemory(&pe, sizeof(pe));
    pe.dwSize = sizeof(pe);
    bool found = false;
    if (Process32FirstW(snap, &pe)) {
        do {
            if (ExtProcessNameMatches(pe.szExeFile, name)) {
                found = true;
                break;
            }
        } while (Process32NextW(snap, &pe));
    }
    CloseHandle(snap);
    return found;
}

// Called on every sampler pass; cheap. The settings are re-read every 10 s so an edit takes
// effect, the restart policy is bounded, and nothing is ever started twice.
// The restart budget is for crash loops, not for a lifetime tally. Pure, so the rule
// is assertable: a dead process has earned nothing, a process we never started has
// earned nothing, and only a run that has actually stayed up counts.
static bool PublisherEarnedBackRestarts(bool alive, DWORD now, DWORD startedAt) {
    if (!alive || startedAt == 0) return false;
    return (DWORD)(now - startedAt) >= kPubHealthyMs;
}

static void ExtPublisherTick() {
    DWORD now = GetTickCount();
    if (g_pubLastRead == 0 || now - g_pubLastRead > kPubSettingRecheckMs) {
        g_pubLastRead = now ? now : 1;
        PCWSTR value = Wh_GetStringSetting(L"ExternalPublisherCommand");
        if (value) {
            if (wcscmp(value, g_pubCmd) != 0) {
                ExtPublisherStop();          // a changed command line replaces the old one
                g_pubRestarts = 0;
                g_pubGaveUp = false;
                wcsncpy(g_pubCmd, value, 511);
                g_pubCmd[511] = 0;
            }
            Wh_FreeStringSetting(value);
        } else if (g_pubCmd[0]) {
            g_pubCmd[0] = 0;
            ExtPublisherStop();
        }
        PCWSTR watch = Wh_GetStringSetting(L"ExternalPublisherWatchProcess");
        g_pubWatch[0] = 0;
        if (watch) {
            wcsncpy(g_pubWatch, watch, 127);
            g_pubWatch[127] = 0;
            Wh_FreeStringSetting(watch);
        }
        g_pubWatchFound = false;
        g_pubLastScan = 0;                   // 改完立刻重新判定一次 ✓
    }

    // 看门狗：只在目标进程运行时才让发布器活着 ✓（目标没了就收掉并重置计数 ✓，不重扫不刷屏 ✓）
    if (g_pubWatch[0]) {
        if (g_pubLastScan == 0 || now - g_pubLastScan > kPubWatchScanMs) {
            g_pubLastScan = now ? now : 1;
            g_pubWatchFound = ExtProcessRunning(g_pubWatch);
        }
        if (!g_pubWatchFound) {
            if (ExtPublisherAlive()) {
                if (!g_pubWatchLogged) {
                    g_pubWatchLogged = true;
                    Wh_Log(L"EXT publisher: watched process '%s' is not running; stopping",
                           g_pubWatch);
                }
                ExtPublisherStop();
                g_pubRestarts = 0;
                g_pubGaveUp = false;
            }
            return;
        }
        g_pubWatchLogged = false;
    }

    if (!g_pubCmd[0]) return;

    // A healthy run gives the budget back before the "still alive" shortcut, so an
    // exhausted budget does not stay exhausted for the rest of the session.
    if (PublisherEarnedBackRestarts(ExtPublisherAlive(), now, g_pubStartedAt)) {
        if (g_pubRestarts || g_pubGaveUp) {
            Wh_Log(L"EXT publisher: up for %lus, restart budget restored (%lu spent)",
                   (unsigned long)((now - g_pubStartedAt) / 1000), g_pubRestarts);
        }
        g_pubRestarts = 0;
        g_pubGaveUp = false;
    }

    if (ExtPublisherAlive()) return;

    if (g_pubRestarts >= kPubMaxRestarts) {
        if (!g_pubGaveUp) {
            g_pubGaveUp = true;
            Wh_Log(L"EXT publisher: gave up after %lu restarts in a row; {ext.*} values "
                   L"will go stale (a run that survives %lus restores the budget)",
                   g_pubRestarts, kPubHealthyMs / 1000);
        }
        return;
    }
    if (g_pubLastTry && now - g_pubLastTry < kPubBackoffMs) return;
    g_pubLastTry = now ? now : 1;

    WCHAR exe[512];
    WCHAR args[512];
    if (!ExtParsePublisherCommand(g_pubCmd, exe, 512, args, 512)) {
        if (!g_pubGaveUp) {
            g_pubGaveUp = true;
            Wh_Log(L"EXT publisher: the command line has no executable");
        }
        return;
    }
    g_pubRestarts++;
    g_pubStartedAt = now ? now : 1;
    ExtPublisherStart(exe, args);
}

static void ExtStart(const ModSettings &snap) {
    InitializeCriticalSection(&g_extLock);
    g_extLockReady = true;
    // The island queue's lock is a second, independent one: the HUD takes it to read
    // an event, the pipe threads take it to enqueue one, and neither ever holds it
    // together with g_extLock (design 5.6.1). It is torn down in the uninit path,
    // after the HUD is gone -- not here, because ExtStop runs while the HUD is still
    // alive.
    InitializeCriticalSection(&g_islandLock);
    g_islandLockReady = true;
    g_extStopEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!g_extStopEvent) {
        Wh_Log(L"EXT: CreateEvent failed: %lu; external values stay unavailable",
               GetLastError());
        return;
    }
    // One bounded line saying what this server will accept. Without it a refused
    // provider (405) is invisible: the client sees an error code, the log says
    // nothing, and the panel just shows "--".
    Wh_Log(L"EXT: limits ttl=%d maxValue=%d providers=%d updates/s=%d allowlist=\"%s\"",
           snap.extValueTtlMs, snap.extMaxValue, snap.extMaxProviders, snap.extMaxUpdates,
           snap.extProviders[0] ? snap.extProviders : L"(any)");
    ExtLogRowReferences(snap);
    for (int i = 0; i < kExtInstances; i++) {
        g_extThreads[i] = CreateThread(nullptr, 0, ExtPipeThread, nullptr, 0, nullptr);
        if (!g_extThreads[i]) {
            Wh_Log(L"EXT: pipe thread %d could not start: %lu", i, GetLastError());
            break;
        }
        g_extThreadCount++;
    }
    Wh_Log(L"EXT: serving %d pipe instance(s) on %s", g_extThreadCount, kExtPipeName);
    // The instances exist now, so prove the whole path once on this machine.
    ExtSelfTest();
}

static void ExtStop() {
    // The publisher goes first: a writer must stop before the thing it writes into.
    ExtPublisherStop();
    InterlockedExchange(&g_extStop, 1);
    if (g_extStopEvent) SetEvent(g_extStopEvent);
    if (g_extThreadCount > 0) {
        WaitForMultipleObjects((DWORD)g_extThreadCount, g_extThreads, TRUE, 3000);
        for (int i = 0; i < g_extThreadCount; i++) {
            CloseHandle(g_extThreads[i]);
            g_extThreads[i] = nullptr;
        }
        g_extThreadCount = 0;
    }
    if (g_extStopEvent) {
        CloseHandle(g_extStopEvent);
        g_extStopEvent = nullptr;
    }
}

// ---------------------------------------------------------------------------
// Loader-thread settings plumbing
// ---------------------------------------------------------------------------
static void PushSettings(int updateMs) {
    // No allocation: the HUD thread reads g_settings under the same lock the
    // loader thread writes it with. That keeps this DLL free of any C++ runtime
    // import (new/delete is what pulls in libc++.dll), so it stays a ~176 KB
    // self-contained mod (measured 180736 B on 2026-10-14) instead of a ~590 KB
    // statically-linked one.
    // The refresh interval comes from the caller's copy rather than from the
    // global, so it cannot disagree with the settings that were just swapped in.
    InterlockedExchange(&g_refreshMs, updateMs);
    HWND window = g_window;
    if (!window) return;
    PostMessageW(window, kMsgApplySettings, 0, 0);
}

// ---------------------------------------------------------------------------
// Tool mod callbacks
// ---------------------------------------------------------------------------
BOOL WhTool_ModInit() {
    InitializeCriticalSection(&g_valuesLock);
    InitializeCriticalSection(&g_settingsLock);
    g_perfBaseTick = GetTickCount64();
    g_perfBaseCpu100ns = CpuTime100ns();

    if (!g_gdiplus.Load()) return FALSE;
    GdiplusStartupInput startupInput;
    if (g_gdiplus.Startup(&g_gdiplusToken, &startupInput, nullptr) != 0) {
        Wh_Log(L"GdiplusStartup failed");
        return FALSE;
    }

    // Network is cheap to prepare; the GPU backends are deliberately left alone
    // here and started later, on the sampler thread, only if a row needs them.
    HMODULE iphlp = ((HMODULE(WINAPI *)(LPCWSTR))LoadLibraryW)(L"iphlpapi.dll");
    if (iphlp) g_getIfTable2 = (GetIfTable2Fn)GetProcAddress(iphlp, "GetIfTable2");

    // Fonts must be enumerated before the settings are read, because reading them
    // validates the configured family against the installed list.
    EnumerateFonts();
    ReadSettings(g_settings);
    InterlockedExchange(&g_refreshMs, g_settings.updateMs);
    ReadCpuStatic();   // identity fields are static and cost nothing to keep

    {
        int families = 0;
        for (const WCHAR *p = g_fontList; *p; p++)
            if (*p == L';') families++;
        Wh_Log(L"%d installed font families found; using '%s'", families + 1,
               g_settings.fontFamily);
    }

    // Report the palette that actually won. Resolving two colour sources silently
    // is exactly what made "why is my theme ignored" hard to answer.
    {
        const WCHAR *themeName = g_settings.themeIndex >= 0 && g_settings.themeIndex < kThemeCount
                                     ? kThemes[g_settings.themeIndex].name
                                     : L"Custom";
        Wh_Log(L"Theme=%s  bg=%08X border=%08X collapsed=%08X text=%08X accent=%08X",
               themeName, g_settings.background, g_settings.border,
               g_settings.borderCollapsed, g_settings.text, g_settings.accent);
    }

    // In Proximity mode the mouse drives expansion; in Toggle mode the panel
    // starts collapsed and a click opens it.
    g_modeProximity = !g_settings.clickToggle;
    g_expanded = g_modeProximity;

    g_hudReady = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!g_hudReady) {
        Wh_Log(L"CreateEvent failed: %lu", GetLastError());
        return FALSE;
    }
    g_hudThread = CreateThread(nullptr, 0, HudThreadProc, nullptr, 0, &g_hudThreadId);
    if (!g_hudThread) {
        Wh_Log(L"CreateThread (HUD) failed: %lu", GetLastError());
        return FALSE;
    }
    WaitForSingleObject(g_hudReady, 5000);
    if (!g_window) {
        Wh_Log(L"HUD window was not created");
        return FALSE;
    }

    g_samplerThread = CreateThread(nullptr, 0, SamplerThread, nullptr, 0, nullptr);
    if (!g_samplerThread) {
        Wh_Log(L"CreateThread (sampler) failed: %lu", GetLastError());
        return FALSE;
    }

    Wh_Log(L"Started: rows=%d clickAction=%s monitor=%d update=%dms panel=%dx%d rings=%d/%d",
           g_settings.rowCount, g_settings.clickToggle ? L"toggle" : L"proximity",
           g_settings.monitorMode, g_settings.updateMs, g_settings.panelWidth,
           g_panelHeight, g_settings.ringSize, g_settings.ringThickness);
    return TRUE;
}

void WhTool_ModSettingsChanged() {
    // Read into a local, then swap it in under the lock the sampler copies it
    // with. Writing g_settings in place left the sampler free to copy a
    // half-applied configuration for one interval (docs/... 5.6.3 R1).
    ModSettings fresh;
    ReadSettings(fresh);
    EnterCriticalSection(&g_settingsLock);
    g_settings = fresh;
    LeaveCriticalSection(&g_settingsLock);
    // ReadCpuStatic() is deliberately not called here: it writes the CPU identity
    // fields that the sampler thread reads, so it now runs on that thread when it
    // notices the new settings version.
    PushSettings(fresh.updateMs);
    Wh_Log(L"Settings applied: rows=%d clickAction=%s", fresh.rowCount,
           fresh.clickToggle ? L"toggle" : L"proximity");
}

// True when the values critical section may be deleted: only once the sampler thread is
// really gone. The wait used to be fire-and-forget -- its result was thrown away and the
// lock was deleted anyway, which is undefined behaviour (freeing a lock another thread can
// still be inside) in exactly the case the wait exists to catch. This runs on the way out
// of the process, so leaving the lock alone is the safe answer.
static bool ValuesLockCanBeReleased(DWORD waitResult) {
    return waitResult == WAIT_OBJECT_0;
}

void WhTool_ModUninit() {
    InterlockedExchange(&g_stopSampler, 1);
    // Stop the pipe server first: it writes the external value table and the
    // sampler (the reader) is still running. Stopping the reader first would
    // leave a writer racing a lock that is about to be deleted
    // (docs/external-data-island-design.md 5.6.4).
    ExtStop();
    if (g_samplerThread) {
        const DWORD samplerWait = WaitForSingleObject(g_samplerThread, 2000);
        CloseHandle(g_samplerThread);
        g_samplerThread = nullptr;
        if (!ValuesLockCanBeReleased(samplerWait)) {
            Wh_Log(L"sampler thread did not stop within 2s (wait=%lu); leaving the values "
                   L"lock to the process exit", (unsigned long)samplerWait);
            g_valuesLockUsable = false;
        }
    }
    if (g_hudThreadId) PostThreadMessageW(g_hudThreadId, kMsgQuit, 0, 0);
    if (g_hudThread) {
        WaitForSingleObject(g_hudThread, 3000);
        CloseHandle(g_hudThread);
        g_hudThread = nullptr;
        g_hudThreadId = 0;
    }
    if (g_hudReady) {
        CloseHandle(g_hudReady);
        g_hudReady = nullptr;
    }
    if (g_gpu.query) {
        PdhCloseQuery(g_gpu.query);
        g_gpu.query = nullptr;
    }
    if (g_gpu.shutdown && g_gpu.kind == GpuBackend::Nvml) g_gpu.shutdown();
    // Must happen before GDI+ shuts down, or the cached image leaks the decoder's
    // resources.
    DisposeIconImage();
    // Same deadline for the cached fonts and the coverage DC: they are GDI+ / GDI objects and
    // both are gone the moment GDI+ is.
    ReleaseFontResources();
    if (g_gdiplusToken) {
        g_gdiplus.Shutdown(g_gdiplusToken);
        g_gdiplusToken = 0;
    }
    if (g_valuesLockUsable) DeleteCriticalSection(&g_valuesLock);
    DeleteCriticalSection(&g_settingsLock);
    // Only now: the pipe threads and the sampler are both joined, so nothing can
    // be inside the external lock any more.
    if (g_islandLockReady) {
        DeleteCriticalSection(&g_islandLock);
        g_islandLockReady = false;
        Wh_Log(L"EXT island: %u event(s) accepted, %u dropped, %u wake(s)",
               g_islandAccepted, g_islandDrops, g_islandWakes);
    }
    if (g_extLockReady) {
        DeleteCriticalSection(&g_extLock);
        g_extLockReady = false;
    }
    Wh_Log(L"Unloaded");
}

// ===========================================================================
// Windhawk tool mod boilerplate.
//
// Runs this mod in a dedicated windhawk.exe process instead of injecting it into
// another process. Structure from:
// https://github.com/ramensoftware/windhawk/wiki/Mods-as-tools:-Running-mods-in-a-dedicated-process
//
// Only WhTool_ModInit / WhTool_ModSettingsChanged / WhTool_ModUninit are
// supported for tool mods.
// ===========================================================================
static bool g_isToolModProcessLauncher = false;
static HANDLE g_toolModProcessMutex = nullptr;

static void WINAPI EntryPoint_Hook() {
    DBG(L">");
    ExitThread(0);
}

BOOL Wh_ModInit() {
    DWORD sessionId;
    if (ProcessIdToSessionId(GetCurrentProcessId(), &sessionId) && sessionId == 0) return FALSE;

    bool isExcluded = false, isToolModProcess = false, isCurrentToolModProcess = false;
    int argc = 0;
    LPWSTR *argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (!argv) {
        Wh_Log(L"CommandLineToArgvW failed");
        return FALSE;
    }
    for (int i = 1; i < argc; i++) {
        if (wcscmp(argv[i], L"-service") == 0 || wcscmp(argv[i], L"-service-start") == 0 ||
            wcscmp(argv[i], L"-service-stop") == 0) {
            isExcluded = true;
            break;
        }
    }
    for (int i = 1; i < argc - 1; i++) {
        if (wcscmp(argv[i], L"-tool-mod") == 0) {
            isToolModProcess = true;
            if (wcscmp(argv[i + 1], WH_MOD_ID) == 0) isCurrentToolModProcess = true;
            break;
        }
    }
    LocalFree(argv);

    if (isExcluded) return FALSE;

    if (isCurrentToolModProcess) {
        g_toolModProcessMutex = CreateMutexW(nullptr, TRUE, L"windhawk-tool-mod_" WH_MOD_ID);
        if (!g_toolModProcessMutex) {
            Wh_Log(L"CreateMutex failed");
            ExitProcess(1);
        }
        if (GetLastError() == ERROR_ALREADY_EXISTS) {
            Wh_Log(L"Tool mod already running");
            ExitProcess(1);
        }
        if (!WhTool_ModInit()) ExitProcess(1);

        IMAGE_DOS_HEADER *dosHeader = (IMAGE_DOS_HEADER *)GetModuleHandleW(nullptr);
        IMAGE_NT_HEADERS *ntHeaders = (IMAGE_NT_HEADERS *)((BYTE *)dosHeader + dosHeader->e_lfanew);
        void *entryPoint = (BYTE *)dosHeader + ntHeaders->OptionalHeader.AddressOfEntryPoint;
        Wh_SetFunctionHook(entryPoint, (void *)EntryPoint_Hook, nullptr);
        return TRUE;
    }

    if (isToolModProcess) return FALSE;

    g_isToolModProcessLauncher = true;
    return TRUE;
}

void Wh_ModAfterInit() {
    if (!g_isToolModProcessLauncher) return;

    WCHAR currentProcessPath[MAX_PATH];
    if (GetModuleFileNameW(nullptr, currentProcessPath, MAX_PATH) == 0) {
        Wh_Log(L"GetModuleFileName failed");
        return;
    }
    WCHAR commandLine[MAX_PATH + 2 + (sizeof(L" -tool-mod \"" WH_MOD_ID "\"") / sizeof(WCHAR))];
    swprintf(commandLine, MAX_PATH + 2 + (sizeof(L" -tool-mod \"" WH_MOD_ID "\"") / sizeof(WCHAR)),
             L"\"%s\" -tool-mod \"%s\"", currentProcessPath, WH_MOD_ID);

    STARTUPINFOW si = {};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_FORCEOFFFEEDBACK;
    PROCESS_INFORMATION pi = {};
    if (!CreateProcessW(currentProcessPath, commandLine, nullptr, nullptr, FALSE,
                        NORMAL_PRIORITY_CLASS, nullptr, nullptr, &si, &pi)) {
        Wh_Log(L"CreateProcess failed: %lu", GetLastError());
        return;
    }
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
}

void Wh_ModSettingsChanged() {
    if (g_isToolModProcessLauncher) return;
    WhTool_ModSettingsChanged();
}

void Wh_ModUninit() {
    if (g_isToolModProcessLauncher) return;
    WhTool_ModUninit();
    ExitProcess(0);
}
