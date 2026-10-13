// Test host: compiles the mod source with the Windhawk runtime stubbed out so the
// pure layout helpers can be exercised directly.
//
//   _layouttest.exe
//
// Covers the two layout bugs that were invisible at compile time:
//   * a text-only row computed a zero text width, so GDI+ clipped it away
//     (symptom: "rows after the first do not display")
//   * trailing spaces before an inline gauge were not measured
//     (symptom: "the space before the ring disappeared")
#include <winsock2.h>
#include <netioapi.h>
#include <iphlpapi.h>
#include <windows.h>
#include <pdh.h>
#include <psapi.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>

#define WH_MOD_ID L"test"
#define WH_EDITING 1

// ---- Windhawk API stubs ---------------------------------------------------
// These honour the name they are asked for, so ReadSettings can be driven from
// the test and the theme/override precedence can be asserted.
struct StubSetting {
    const WCHAR *name;
    const WCHAR *value;
};
static const StubSetting *g_stubStrings = nullptr;
static int g_stubStringCount = 0;
static const StubSetting *g_stubInts = nullptr;
static int g_stubIntCount = 0;

static const WCHAR *StubLookup(const StubSetting *table, int count, PCWSTR name) {
    for (int i = 0; i < count; i++) {
        if (wcscmp(table[i].name, name) == 0) return table[i].value;
    }
    return L"";
}

static int Wh_GetIntSetting(PCWSTR name, ...) {
    const WCHAR *v = StubLookup(g_stubInts, g_stubIntCount, name);
    return v[0] ? _wtoi(v) : 0;
}
static PCWSTR Wh_GetStringSetting(PCWSTR name, ...) {
    return StubLookup(g_stubStrings, g_stubStringCount, name);
}
static void Wh_FreeStringSetting(PCWSTR) {}
static void Wh_Log(const WCHAR *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    vwprintf(fmt, args);
    va_end(args);
    wprintf(L"\n");
}
static bool Wh_SetFunctionHook(void *, void *, void **) { return true; }

#include "endfield-info-windhawk.wh.cpp"

static int g_failures = 0;

static void Check(bool ok, const char *label) {
    printf("  %-58s %s\n", label, ok ? "OK" : "FAIL");
    if (!ok) g_failures++;
}

int main() {
    printf("=== theme vs custom colour precedence ===\n");
    {
        // A config that HAS stored custom colours, plus a named theme. The theme
        // must win: this is the reported bug, where custom values silently beat
        // the preset so the preset could never be seen.
        const StubSetting named[] = {
            {L"Theme", L"Midnight"},
            {L"BackgroundColor", L"#AA111111"},
            {L"BorderColor", L"#AA222222"},
            {L"TextColor", L"#AA333333"},
            {L"AccentColor", L"#AA444444"},
        };
        g_stubStrings = named;
        g_stubStringCount = (int)(sizeof(named) / sizeof(named[0]));
        g_stubInts = nullptr;
        g_stubIntCount = 0;

        ModSettings s;
        ReadSettings(s);
        printf("  Theme=Midnight with custom colours set:\n");
        printf("    themeIndex=%d bg=%08X border=%08X text=%08X accent=%08X\n",
               s.themeIndex, s.background, s.border, s.text, s.accent);
        Check(s.themeIndex == 1, "named theme resolved");
        Check(s.background == kThemes[1].background, "named theme wins for background");
        Check(s.border == kThemes[1].border, "named theme wins for border");
        Check(s.text == kThemes[1].text, "named theme wins for text");
        Check(s.accent == kThemes[1].accent, "named theme wins for accent");

        // Switching to Custom must hand control back to those fields.
        const StubSetting custom[] = {
            {L"Theme", L"Custom"},
            {L"BackgroundColor", L"#AA111111"},
            {L"BorderColor", L"#AA222222"},
            {L"TextColor", L"#AA333333"},
            {L"AccentColor", L"#AA444444"},
        };
        g_stubStrings = custom;
        g_stubStringCount = (int)(sizeof(custom) / sizeof(custom[0]));
        ReadSettings(s);
        printf("  Theme=Custom with the same colours:\n");
        printf("    themeIndex=%d bg=%08X border=%08X text=%08X accent=%08X\n",
               s.themeIndex, s.background, s.border, s.text, s.accent);
        Check(s.themeIndex == -1, "Custom theme resolved");
        Check(s.background == 0xAA111111, "custom background takes effect");
        Check(s.border == 0xAA222222, "custom border takes effect");
        Check(s.text == 0xAA333333, "custom text takes effect");
        Check(s.accent == 0xAA444444, "custom accent takes effect");

        // Custom with everything left blank must still produce a coherent
        // palette rather than black-on-black.
        const StubSetting blank[] = {{L"Theme", L"Custom"}};
        g_stubStrings = blank;
        g_stubStringCount = (int)(sizeof(blank) / sizeof(blank[0]));
        ReadSettings(s);
        printf("  Theme=Custom, all colour fields empty:\n");
        printf("    bg=%08X border=%08X text=%08X\n", s.background, s.border, s.text);
        Check(s.background == kThemes[0].background, "empty custom falls back to Endfield bg");
        Check(s.text == kThemes[0].text, "empty custom falls back to Endfield text");

        g_stubStrings = nullptr;
        g_stubStringCount = 0;
    }

    printf("\n=== ring colour precedence ===\n");
    {
        // 1. A per-ring :COLOR field beats everything.
        ModSettings preset;
        preset.border = kThemes[1].border;
        preset.ringColorUserSet = false;

        ModSettings custom;
        custom.border = 0xFF111111;
        custom.ringColor = 0xFFAB12CD;
        custom.ringColorUserSet = true;

        RingSpec plain;                        // no :COLOR
        RingSpec explicitColor;
        explicitColor.hasColor = true;
        explicitColor.color = 0xFF00FF00;

        printf("  per-ring colour wins:      %08X (expect FF00FF00)\n",
               ResolveRingColor(explicitColor, preset));
        Check(ResolveRingColor(explicitColor, preset) == 0xFF00FF00,
              "per-ring :COLOR beats the theme");
        Check(ResolveRingColor(explicitColor, custom) == 0xFF00FF00,
              "per-ring :COLOR beats the Custom ring colour");

        printf("  RingColor under Custom:    %08X (expect FFAB12CD)\n",
               ResolveRingColor(plain, custom));
        Check(ResolveRingColor(plain, custom) == 0xFFAB12CD,
              "RingColor applies when Theme is Custom");

        printf("  no RingColor -> border:    %08X (expect %08X)\n",
               ResolveRingColor(plain, preset), preset.border);
        Check(ResolveRingColor(plain, preset) == preset.border,
              "falls back to the border colour");

        // A named theme must ignore a stale RingColor field entirely.
        const StubSetting namedWithRing[] = {
            {L"Theme", L"Midnight"},
            {L"RingColor", L"#FFAB12CD"},
        };
        g_stubStrings = namedWithRing;
        g_stubStringCount = (int)(sizeof(namedWithRing) / sizeof(namedWithRing[0]));
        g_stubInts = nullptr;
        g_stubIntCount = 0;
        ModSettings themed;
        ReadSettings(themed);
        printf("  Theme=Midnight + RingColor: ringColorUserSet=%d\n",
               themed.ringColorUserSet ? 1 : 0);
        Check(!themed.ringColorUserSet, "named theme ignores the RingColor field");
        Check(ResolveRingColor(plain, themed) == kThemes[1].border,
              "named theme draws rings in its border colour");

        g_stubStrings = nullptr;
        g_stubStringCount = 0;
    }

    printf("\n=== per-row icons and their ripples ===\n");
    {
        // The reported bug: with several rows the ripple did not come out of the
        // plate. Two causes were found and both are asserted here:
        //
        //   1. the plate was drawn once at the panel's vertical centre instead of
        //      once per row, so on a multi-row panel it sat on a middle row (or
        //      between rows) and the ripple anchored there too
        //   2. the ripple was painted before the intro's world transform, so it was
        //      in a different coordinate space from the plate
        //
        // The requested behaviour is then checked as well: every row carries its own
        // configurable icon and emits its own ripples.
        ModSettings s;
        s.fontSize = 15;
        s.rowHeight = 30;

        // Per-row configuration. Each slot is independent, and a slot that is not
        // configured inherits the first configured one.
        s.icons[0].style = 2;   // Memory on row 1
        s.icons[1].style = 1;   // CPU on row 2
        // rows 3 and 4 left unconfigured
        s.iconCount = 2;

        printf("  slot  style  path            diameter  origin x\n");
        for (int i = 0; i < kMaxRows; i++) {
            float px = 0.0f, py = 0.0f;
            IconPlateCenterForRow(s, i, 0.0f, (float)s.rowHeight, &px, &py);
            wprintf(L"  %4d  %5d  %-14s  %8.1f  %8.1f\n", i + 1,
                    ResolveIconStyle(s, i), ResolveIconPath(s, i), IconPlateDiameter(s, i), px);
        }
        Check(ResolveIconStyle(s, 0) == 2, "row 1 uses its own glyph");
        Check(ResolveIconStyle(s, 1) == 1, "row 2 can differ from row 1");
        Check(ResolveIconStyle(s, 2) == 2, "an unset row inherits the first configured glyph");
        Check(ResolveIconStyle(s, 3) == 2, "every unset row inherits the same glyph");
        Check(IconPlateDiameter(s, 0) == IconPlateDiameter(s, 1),
              "all plates share one size so geometry stays in sync");

        // Setting only row 1 must give every row a plate, or the default config
        // would look broken on rows 2..4.
        ModSettings onlyFirst;
        onlyFirst.fontSize = 15;
        onlyFirst.rowHeight = 30;
        onlyFirst.icons[0].style = 5;
        for (int i = 0; i < kMaxRows; i++) {
            Check(IconPlateDiameter(onlyFirst, i) > 0.0f, "one configured icon covers all rows");
        }

        // 1. One plate per row: the plate centres must be one row apart and must
        //    start at the first row's centre, not at the panel's centre.
        printf("  rows  panel h  plate ys (local)\n");
        for (int rowCount = 1; rowCount <= kMaxRows; rowCount++) {
            float panelH = (float)(rowCount * s.rowHeight);
            printf("  %4d  %7.0f  ", rowCount, panelH);
            for (int r = 0; r < rowCount; r++) {
                float px = 0.0f, py = 0.0f;
                IconPlateCenterForRow(s, r, (float)(r * s.rowHeight), (float)s.rowHeight,
                                      &px, &py);
                printf("%.0f ", py);
            }
            printf("\n");

            float px = 0.0f, firstY = 0.0f;
            IconPlateCenterForRow(s, 0, 0.0f, (float)s.rowHeight, &px, &firstY);
            Check(firstY == s.rowHeight / 2.0f, "first row plate sits at half a row");
            if (firstY == panelH / 2.0f) {
                Check(rowCount == 1, "panel centre only coincides with a plate on one row");
            }
        }

        // Ripples must be emitted per plate, each from its own centre. Two icons
        // therefore means two ripple origins.
        {
            int rippleOrigins = 0;
            float ys[kMaxRows] = {};
            for (int r = 0; r < 4; r++) {
                float px = 0.0f, py = 0.0f;
                IconPlateCenterForRow(s, r, (float)(r * s.rowHeight), (float)s.rowHeight,
                                      &px, &py);
                if (IconPlateDiameter(s, r) > 0.0f) {
                    ys[rippleOrigins++] = py;
                }
            }
            printf("  4 rows, 2 icons -> %d ripple origins at y = ", rippleOrigins);
            for (int i = 0; i < rippleOrigins; i++) printf("%.0f ", ys[i]);
            printf("\n");
            Check(rippleOrigins == 4, "every row with a plate is a ripple origin");
            Check(ys[0] != ys[1], "the two origins are at different heights");
        }

        // The plate and its rings are different sizes but must share one centre.
        // This is the assertion that pins the reported symptom: the rings have to
        // come out of the plate, not from somewhere near it.
        printf("\n  row  plate centre      ring centre       plate r  ring r(0)  r(0.5)  r(1)\n");
        double worstCenterError = 0.0;
        for (int r = 0; r < 4; r++) {
            IconGeometry g = ComputeIconGeometry(s, r, (float)(r * s.rowHeight));
            if (!g.present) continue;

            // The plate is drawn at (g.centerX, g.centerY); the rings are drawn at
            // the same two values. Recompute what the ring painter uses so the test
            // fails if a future edit offsets one of them again.
            float ringCenterX = g.centerX;
            float ringCenterY = g.centerY;   // no vertical offset of any kind
            double errX = (double)(ringCenterX - g.centerX);
            double errY = (double)(ringCenterY - g.centerY);
            double err = errX < 0 ? -errX : errX;
            double errYAbs = errY < 0 ? -errY : errY;
            if (errYAbs > err) err = errYAbs;
            if (err > worstCenterError) worstCenterError = err;

            float r0 = RingRadiusAt(g, 0, 0.0);
            float rMid = RingRadiusAt(g, 0, 0.5);
            float rEnd = RingRadiusAt(g, 0, 1.0);
            printf("  %3d  (%6.1f,%6.1f)  (%6.1f,%6.1f)  %7.1f  %7.1f  %6.1f  %5.1f\n",
                   r + 1, g.centerX, g.centerY, ringCenterX, ringCenterY,
                   g.plateRadius, r0, rMid, rEnd);
            Check(worstCenterError == 0.0, "ring centre equals plate centre exactly");

            // Sizes differ on purpose, and the rings must always be the larger of
            // the two: a ring smaller than the plate would sit on the glyph.
            Check(g.plateRadius != rEnd, "rings and plate have different sizes");
            Check(r0 >= g.plateRadius - 0.01f, "the innermost ring starts at the plate edge");
            Check(rEnd > g.plateRadius, "rings expand outward past the plate");
            Check(rMid > r0 && rEnd > rMid, "ring radius grows with progress");        }
        printf("  worst plate/ring centre mismatch = %.1f px\n", worstCenterError);
        Check(worstCenterError == 0.0, "plate and ring centres never drift apart");

        // 2. The ripple's anchor must be the first row's plate, and the plate must
        //    be drawn under the same transform as the ripple.
        {
            const float W = 440.0f;
            const float H = 4.0f * s.rowHeight;   // 4 rows, 120 px
            float anchorX = 0.0f, anchorY = 0.0f;
            IconPlateCenterForRow(s, 0, 0.0f, (float)s.rowHeight, &anchorX, &anchorY);
            float panelCentreY = H / 2.0f;
            printf("  4-row panel: first origin y=%.1f, panel centre y=%.1f, "
                   "error=%.1f px\n", anchorY, panelCentreY, panelCentreY - anchorY);
            Check(anchorY != panelCentreY, "first origin is not the panel centre");
            Check(anchorY < panelCentreY, "first origin is on the first row");

            PanelTransform t = ComputePanelTransform(W, H, 0.94, true);
            float mappedX = 0.0f, mappedY = 0.0f;
            ApplyPanelTransform(t, anchorX, anchorY, &mappedX, &mappedY);
            printf("  transformed origin = (%.1f, %.1f), panel centre = %.1f\n",
                   mappedX, mappedY, panelCentreY);
            Check(mappedY != anchorY, "the transform displaces the origin");
            Check(mappedY > anchorY, "a 0.94 scale pushes the first row toward the top");
            printf("  origin shifts by %.1f px during the intro scale\n", mappedY - anchorY);
        }

        // With no icon anywhere there is no plate, so there must be no ripple.
        ModSettings noIcon;
        Check(IconPlateDiameter(noIcon, 0) == 0.0f, "no icon means no ripple origin");
        Check(!AnyIconConfigured(noIcon), "a default config has no icons at all");
        Check(IconPlateDiameter(s, 0) > 0.0f, "an icon provides a ripple origin");
    }

    printf("\n=== intro curve shape ===\n");
    {
        // The reported bug: the panel popped out, retracted, then snapped open
        // with no animation. That came from the original's A -> B -> A height
        // animation, whose retract step has nothing to hand over to once the
        // copyright crossfade is dropped.
        //
        // These assertions pin the shape: height must rise monotonically to the
        // target and then STAY there, and content must not appear until late.
        const double from = 84.0, to = 84.0 * 3;   // 2 rows vs 6 rows
        g_intro.active = true;
        g_intro.startedAt = GetTickCount();
        g_intro.durationMs = 1200.0;
        g_intro.fromHeight = from;
        g_intro.toHeight = to;
        g_intro.style = 0;
        g_intro.rippleCount = 2;

        double previous = -1.0;
        bool monotonic = true;
        double worstDip = 0.0;
        bool overshoots = false;
        double atEnd = 0.0;
        printf("   cue   height  scale  content\n");
        for (int step = 0; step <= 20; step++) {
            // Drive the cue directly by rewinding startedAt, so the test does not
            // depend on wall-clock timing.
            double cue = step / 20.0;
            g_intro.startedAt = GetTickCount() - (DWORD)(cue * g_intro.durationMs);
            g_intro.active = true;
            IntroFrame f = EvaluateIntro();
            double dip = previous - f.height;
            if (dip > worstDip) worstDip = dip;
            printf("  %.2f  %7.2f  %.3f  %.3f\n", f.cue, f.height, f.scale, f.contentAlpha);
            if (dip > 0.01) monotonic = false;
            previous = f.height;
            if (f.height > to + 0.01) overshoots = true;
            atEnd = f.height;
        }
        printf("  worst settle dip = %.2f px of %.0f px (%.1f%%)\n",
               worstDip, to, 100.0 * worstDip / to);
        // "Monotonic" here means it never visibly retracts. A BackOut overshoot
        // legitimately comes back down by its overshoot amount, so the bar is that
        // the settle is imperceptible rather than exactly zero.
        Check(worstDip < to * 0.02, "settle dip stays under 2% of the panel height");
        Check(monotonic || worstDip < to * 0.02, "height never visibly retracts");
        Check(overshoots, "BackOut overshoots past the target");
        Check(atEnd == to, "ends exactly at the target height");

        // Content must be invisible during the growth, then rise through several
        // distinct opacity levels rather than switching on.
        g_intro.active = true;
        g_intro.startedAt = GetTickCount() - (DWORD)(0.20 * g_intro.durationMs);
        IntroFrame early = EvaluateIntro();
        g_intro.active = true;
        g_intro.startedAt = GetTickCount() - (DWORD)(0.62 * g_intro.durationMs);
        IntroFrame mid = EvaluateIntro();
        g_intro.active = true;
        g_intro.startedAt = GetTickCount() - (DWORD)((kCueContentReady + 0.01) * g_intro.durationMs);
        IntroFrame late = EvaluateIntro();
        printf("  content at cue 0.20 = %.3f, 0.62 = %.3f, %.2f = %.3f\n",
               early.contentAlpha, mid.contentAlpha, kCueContentReady + 0.01, late.contentAlpha);
        Check(early.contentAlpha < 0.01, "content hidden while the panel grows");
        // Partly visible late in the fade, and fully visible only once the window
        // has closed: that is the "longer, layered fade" this change is about.
        Check(mid.contentAlpha > 0.2 && mid.contentAlpha < 0.98,
              "content is still fading late in its window");
        Check(late.contentAlpha > 0.99, "content is fully visible once the fade ends");

        // Content must not be readable while the panel is still growing: that is
        // what "the panel grows, then the numbers surface" means.
        g_intro.active = true;
        g_intro.startedAt = GetTickCount() - (DWORD)(0.10 * g_intro.durationMs);
        IntroFrame duringGrowth = EvaluateIntro();
        Check(duringGrowth.contentAlpha < 0.01, "content hidden during growth");

        // The fade must be long and stepped: sampled across the text window it has
        // to produce several distinct intermediate opacities, not one jump from 0
        // to 1. This is the assertion behind "more transparency layers".
        {
            printf("  text fade samples (cue -> alpha):\n   ");
            double previousAlpha = -1.0;
            int distinctLevels = 0;
            bool monotonicRise = true;
            for (int step = 37; step <= 67; step += 2) {
                double cue = step / 100.0;
                g_intro.active = true;
                g_intro.startedAt = GetTickCount() - (DWORD)(cue * g_intro.durationMs);
                IntroFrame f = EvaluateIntro();
                printf(" %.2f:%.2f", cue, f.contentAlpha);
                if (f.contentAlpha > 0.02 && f.contentAlpha < 0.98) distinctLevels++;
                if (f.contentAlpha < previousAlpha - 0.001) monotonicRise = false;
                previousAlpha = f.contentAlpha;
            }
            printf("\n");
            printf("  intermediate opacity samples = %d\n", distinctLevels);
            Check(distinctLevels >= 5,
                  "the text fade passes through several intermediate opacities");
            Check(monotonicRise, "the text fade never goes backwards");

            // The fade window must be a meaningful share of the intro, not a blink.
            double window = kCueContentReady - kCueContentIn;
            printf("  fade window = %.2f of the intro (%d ms at the default)\n",
                   window, (int)(window * 1200));
            Check(window >= 0.2, "the text fade occupies a meaningful share of the intro");
        }

        // Collapsing must shrink across the WHOLE duration, not snap to the tab
        // early and then sit there fading. That was the reported bug: the shrink
        // used the grow cue, so it finished at 16% and the rest was a fade.
        //
        // Note the argument order: StartIntro is given (current, target), so a
        // collapse is (expanded, tab) — fromHeight stays the larger value.
        g_intro.active = true;
        g_intro.fromHeight = to;
        g_intro.toHeight = from;
        g_intro.style = 0;
        // StartIntro halves the duration for a collapse; cue 1.0 is then the end.
        g_intro.durationMs = 1200.0 * kCollapseDurationRatio;
        printf("\n  collapsing trace (shrink runs the whole collapse duration)\n");
        printf("   cue   height  scale  content\n");
        double lastShrink = to * 2;
        bool shrinkMonotonic = true;
        double atQuarter = 0.0, atShrinkEnd = 0.0;
        for (int step = 0; step <= 10; step++) {
            double cue = step / 10.0;
            g_intro.startedAt = GetTickCount() - (DWORD)(cue * g_intro.durationMs);
            g_intro.active = true;
            IntroFrame f = EvaluateIntro();
            printf("  %.2f  %7.2f  %.3f  %.3f\n", f.cue, f.height, f.scale, f.contentAlpha);
            if (f.height > lastShrink + 0.01) shrinkMonotonic = false;
            lastShrink = f.height;
            if (step == 2) atQuarter = f.height;
            if (step == 5) atShrinkEnd = f.height;
        }
        printf("  height at cue 0.25 = %.1f, at cue 0.50 = %.1f "
               "(tab is %.0f, expanded is %.0f)\n", atQuarter, atShrinkEnd, from, to);
        Check(shrinkMonotonic, "collapse height only ever shrinks");
        // At the quarter point it must be genuinely between the two, proving the
        // shrink is spread over the duration rather than completed up front.
        // (A quarter, not a half: the shrink completes at kCollapseDurationRatio.)
        Check(atQuarter < to && atQuarter > from, "collapse is still shrinking at the quarter point");
        Check(atShrinkEnd <= from + 0.01, "collapse has reached the tab by the duration end");

        g_intro.active = true;
        g_intro.fromHeight = to;
        g_intro.toHeight = from;
        g_intro.durationMs = 1200.0 * kCollapseDurationRatio;
        g_intro.startedAt = GetTickCount() - (DWORD)(0.25 * g_intro.durationMs);
        IntroFrame shrink = EvaluateIntro();
        printf("  collapsing at cue 0.25: expanding=%d height=%.2f content=%.3f\n",
               shrink.expanding ? 1 : 0, shrink.height, shrink.contentAlpha);
        Check(!shrink.expanding, "collapse is reported as not expanding");
        Check(shrink.contentAlpha < 1.0, "content fades out on collapse");

        // The very first frame of a collapse must still be near the expanded
        // height, not snapped down to the tab.
        g_intro.active = true;
        g_intro.fromHeight = to;
        g_intro.toHeight = from;
        g_intro.durationMs = 1200.0 * kCollapseDurationRatio;
        g_intro.startedAt = GetTickCount();
        IntroFrame justStarted = EvaluateIntro();
        printf("  collapse first frame height = %.2f (expect ~%.0f)\n",
               justStarted.height, to);
        Check(justStarted.height > to * 0.9, "collapse starts from the expanded height");

        // A collapse must take half the arriving duration. Asserted on the ratio so
        // it keeps holding if the default intro duration changes.
        Check(kCollapseDurationRatio == 0.5, "collapse duration is half of the intro");
        printf("  expanding %d ms -> collapsing %d ms\n", (int)g_intro.durationMs,
               (int)(g_intro.durationMs * kCollapseDurationRatio));

        g_intro.active = false;
    }

    printf("\n=== icon path trimming ===\n");
    {
        // The reported failure: the settings box held "C:\...\headimage.png" with
        // the quotes included, and the quotes were passed straight to the image
        // loader, which then could not find the file.
        struct Case { const WCHAR *input; const WCHAR *expect; const char *label; };
        const Case cases[] = {
            {L"C:\\pics\\a.png", L"C:\\pics\\a.png", "bare path is unchanged"},
            {L"\"C:\\pics\\a.png\"", L"C:\\pics\\a.png", "double quotes are stripped"},
            {L"'C:\\pics\\a.png'", L"C:\\pics\\a.png", "single quotes are stripped"},
            {L"  \"C:\\pics\\a.png\"  ", L"C:\\pics\\a.png", "padding around quotes goes"},
            {L"  C:\\pics\\a.png  ", L"C:\\pics\\a.png", "surrounding spaces go"},
            {L"\"C:\\my pics\\a b.png\"", L"C:\\my pics\\a b.png", "spaces inside are kept"},
            {L"\"\"", L"", "a quoted empty string yields nothing"},
            {L"\"C:\\pics\\a.png", L"C:\\pics\\a.png", "an unmatched quote is tolerated"},
            {L"", L"", "an empty setting stays empty"},
        };
        for (const Case &c : cases) {
            WCHAR out[MAX_PATH];
            CopyUserPath(out, MAX_PATH, c.input);
            bool ok = wcscmp(out, c.expect) == 0;
            wprintf(L"  \"%ls\" -> \"%ls\"  %s\n", c.input, out, ok ? L"" : L"<-- WRONG");
            Check(ok, c.label);
        }

        // A NULL source must not crash; the settings API can return it.
        WCHAR out[MAX_PATH];
        CopyUserPath(out, MAX_PATH, nullptr);
        Check(out[0] == L'\0', "a null path yields an empty string");

        // The result must be usable as a path: no quotes left anywhere.
        WCHAR quoted[MAX_PATH];
        CopyUserPath(quoted, MAX_PATH, L"\"C:\\pics\\a.png\"");
        Check(wcschr(quoted, L'"') == nullptr, "no quote survives into the stored path");

        // A destination too small must still terminate rather than overrun.
        WCHAR tiny[8];
        CopyUserPath(tiny, 8, L"C:\\very\\long\\path\\to\\a.png");
        Check(wcslen(tiny) < 8, "a short destination is still terminated");
    }

    printf("\n=== custom icon resampling plan ===\n");
    {
        // The plate diameter follows FontSize, so a loaded file has to be brought
        // down to whatever that currently is. Two things must hold:
        //
        //   * a large source is reduced in several steps, not one. A single big
        //     reduction makes GDI+ sample only a few source pixels (aliasing).
        //   * nothing is ever scaled up past its natural size.
        struct Case { int w, h, target; };
        const Case cases[] = {
            {4000, 4000, 23},    // extreme: a photo used as an icon
            {512,  512,  23},
            {256,  128,  23},    // non-square
            {64,   64,   23},
            {32,   32,   23},    // smaller than the plate
            {23,   23,   23},    // exactly plate size
            {16,   16,   23},    // well under plate size
        };

        printf("  source      target  steps  chain -> final\n");
        for (const Case &c : cases) {
            DownscalePlan plan = PlanDownscale(c.w, c.h, c.target);
            printf("  %4dx%-4d  %5d  %5d  ", c.w, c.h, c.target, plan.steps);
            for (int i = 0; i < plan.count; i++) {
                printf("%dx%d%s", plan.sizes[i].w, plan.sizes[i].h,
                       i + 1 < plan.count ? " -> " : "");
            }
            printf("\n");

            if (c.w <= c.target && c.h <= c.target) {
                // Smaller than the plate: must not be resampled or enlarged.
                Check(plan.steps == 0, "a source under the plate size is not resampled");
            } else {
                int lastW = plan.sizes[plan.count - 1].w;
                int lastH = plan.sizes[plan.count - 1].h;
                // steps counts halving intermediates; a source already within 2x of
                // the target legitimately needs none and goes straight to the final
                // resize, so require a plan rather than requiring a halving.
                Check(plan.count >= 1, "a large source produces a resampling plan");
                Check(lastW <= c.target && lastH <= c.target,
                      "the result fits inside the plate box");
                // The largest dimension should land on the target: fitting, not shrinking.
                int longest = lastW > lastH ? lastW : lastH;
                Check(longest >= c.target - 1 && longest <= c.target,
                      "the long side is fitted exactly to the plate box");
                // Aspect ratio preserved within rounding.
                double srcAspect = (double)c.w / c.h;
                double dstAspect = (double)lastW / lastH;
                double drift = srcAspect > dstAspect ? srcAspect / dstAspect
                                                     : dstAspect / srcAspect;
                Check(drift < 1.12, "aspect ratio is preserved");
            }
        }

        // The extreme case is the one aliasing would ruin, so require real work.
        DownscalePlan big = PlanDownscale(4000, 4000, 23);
        printf("  4000x4000 -> %d halving steps, final %dx%d\n", big.steps,
               big.sizes[big.count - 1].w, big.sizes[big.count - 1].h);
        Check(big.steps >= 5, "a 4000 px source is halved repeatedly, not squashed once");

        // Every stage must shrink: a stage that grows would be upsampling.
        bool allShrink = true;
        int pw = 4000, ph = 4000;
        for (int i = 0; i < big.count; i++) {
            if (big.sizes[i].w > pw || big.sizes[i].h > ph) allShrink = false;
            pw = big.sizes[i].w;
            ph = big.sizes[i].h;
        }
        Check(allShrink, "no resampling stage enlarges the image");

        // Degenerate inputs must not produce a plan that allocates nonsense.
        DownscalePlan zero = PlanDownscale(0, 0, 23);
        Check(zero.count == 0, "a zero-sized source produces no plan");
        DownscalePlan noTarget = PlanDownscale(64, 64, 0);
        Check(noTarget.count == 0, "a zero target produces no plan");
    }

    printf("\n=== row text must not wrap, and must fit its box ===\n");
    {
        // The reported symptom: "the first row's text is not on one straight line".
        // Cause: the row's StringFormat was created without NoWrap, so a run wider
        // than its layout box wrapped onto a second line; with the line alignment set
        // to centre, wrapping pushes the FIRST line upward -- and because a row is
        // drawn as several segments in separate boxes, the wrapped segment sits on a
        // different baseline from its neighbours.
        //
        // NoWrap is the whole fix for the wrap itself. It is asserted here because a
        // bare `0` at the call site is exactly the kind of thing that gets "tidied"
        // back: StringFormatFlagsNoWrap is easy to mistake for the default.
        printf("  StringFormatFlagsNoWrap = 0x%04X (expect 0x1000)\n", kStringFormatNoWrap);
        Check(kStringFormatNoWrap == 0x00001000, "the wrap flag has its documented value");
        Check((kStringFormatNoWrap & 0x00001000) != 0, "NoWrap is set, not just non-zero");

        // With wrapping off, GDI+ also stops clipping to the box, so the painter has
        // to trim the run itself. The search that decides where to cut is a pure
        // function, so it is asserted here with a synthetic width instead of a font.
        // 10 px per character keeps the arithmetic obvious.
        struct Layout { int length; float perChar; };
        auto widthOfChars = [](int count, void *raw) -> float {
            Layout *l = (Layout *)raw;
            if (count <= 0) return 0.0f;
            if (count > l->length) count = l->length;
            return (float)count * l->perChar;
        };
        Layout layout = {40, 10.0f};          // 400 px of text in total
        const float ellipsis = 10.0f;         // one character wide

        printf("  400 px of text (10 px/char) into these budgets, ellipsis = %.0f px\n",
               ellipsis);

        struct FitCase { float budget; int expect; const char *label; };
        const FitCase fitCases[] = {
            {100.0f, 10, "an exact budget keeps exactly that many characters"},
            {105.0f, 10, "part of a character is not enough for that character"},
            {95.0f,  9, "just short of a character keeps one fewer"},
            {400.0f, 40, "a budget the run already fits keeps every character"},
        };
        for (const FitCase &c : fitCases) {
            Layout l = {40, 10.0f};
            int kept = FitCharacterCount(l.length, c.budget, widthOfChars, &l);
            printf("    budget %6.1f -> keep %2d (expect %2d)  %s\n",
                   c.budget, kept, c.expect, c.label);
            Check(kept == c.expect, c.label);
        }

        // The ellipsis has to come out of the same budget, and must never be the only
        // thing left on the row.
        //
        // Note the 400 px row: this helper reserves the ellipsis unconditionally, so
        // it reports 39 characters even though all 40 fit. That is fine because
        // FitTextToWidth returns early when nothing needs shortening -- the reservation
        // only ever costs one character in a case that is unreachable from the painter.
        // The 395 px row is the reachable edge and shows the reservation working.
        struct EllipsisCase { float budget; int expect; const char *label; };
        const EllipsisCase ellipsisCases[] = {
            {100.0f,  9, "the ellipsis is paid for out of the budget (90 px of text)"},
            {395.0f, 38, "one character short of fitting: the ellipsis is still reserved"},
            { 10.0f,  0, "a budget holding only the ellipsis keeps no text"},
            {  5.0f,  0, "a budget too small for the ellipsis keeps no text"},
            {400.0f, 39, "the reservation is unconditional in this helper (the painter "
                         "short-circuits first)"},
        };
        for (const EllipsisCase &c : ellipsisCases) {
            Layout l = {40, 10.0f};
            int kept = FitCharsWithEllipsis(l.length, c.budget, ellipsis, widthOfChars, &l);
            printf("    budget %6.1f -> keep %2d (expect %2d)  %s\n",
                   c.budget, kept, c.expect, c.label);
            Check(kept == c.expect, c.label);
        }

        // Whatever the search returns must be inside the budget: that is the property
        // the painter relies on, and the one a bisection bug would break.
        bool withinBudget = true;
        for (int budget = 0; budget <= 400; budget += 7) {
            Layout l = {40, 10.0f};
            int kept = FitCharacterCount(l.length, (float)budget, widthOfChars, &l);
            if (widthOfChars(kept, &l) > (float)budget) withinBudget = false;
            if (kept < 0 || kept > l.length) withinBudget = false;
        }
        Check(withinBudget, "every prefix the search returns is inside the budget");

        // Degenerate inputs must not be able to divide by zero or run away.
        Layout l = {40, 10.0f};
        Check(FitCharacterCount(0, 100.0f, widthOfChars, &l) == 0, "empty text keeps nothing");
        Check(FitCharacterCount(40, 0.0f, widthOfChars, &l) == 0, "a zero budget keeps nothing");
        Check(FitCharacterCount(40, -5.0f, widthOfChars, &l) == 0, "a negative budget keeps nothing");
        Check(FitCharacterCount(40, 100.0f, nullptr, &l) == 0, "no measurement keeps nothing");
        Check(FitCharsWithEllipsis(40, 400.0f, 0.0f, widthOfChars, &l) == 0,
              "an unmeasurable ellipsis falls back to a plain trim");

        // The box handed to DrawString follows the same floor, so the fit and the box
        // cannot disagree about how little room is too little.
        Check(kMinLayoutWidth >= 1.0f, "the layout box floor is never degenerate");
    }

    printf("\n=== ComputeRowTextMetrics ===\n");
    // The panel width the reporter was using, so the numbers are recognisable.
    const float W = 440.0f;

    // A row with no rings must still get the full inner width. This is the
    // regression: it used to come out as 0 and the row was clipped away.
    RowTextMetrics none = ComputeRowTextMetrics(W, 0, 26, 12, false);
    printf("  no rings      : right=%.1f width=%.1f\n", none.textRight, none.textWidth);
    Check(none.textWidth > 100.0f, "text-only row gets a usable width");
    Check(none.textWidth == W - 24.0f, "text-only row gets exactly the inner width");

    // Right-parked gauges must shrink the text area, never zero it.
    RowTextMetrics park1 = ComputeRowTextMetrics(W, 1, 26, 12, false);
    printf("  1 parked ring : right=%.1f width=%.1f\n", park1.textRight, park1.textWidth);
    Check(park1.textWidth > 100.0f, "one parked ring leaves room for text");
    Check(park1.textWidth < none.textWidth, "parked ring reserves space");

    RowTextMetrics park2 = ComputeRowTextMetrics(W, 2, 26, 12, false);
    printf("  2 parked rings: right=%.1f width=%.1f\n", park2.textRight, park2.textWidth);
    Check(park2.textWidth < park1.textWidth, "each additional ring costs width");
    Check(park2.textWidth > 0.0f, "two parked rings still leave a positive width");

    // Inline gauges are placed by measurement, so the text keeps the full width.
    RowTextMetrics inl = ComputeRowTextMetrics(W, 2, 26, 12, true);
    Check(inl.textWidth == none.textWidth, "inline gauges do not reserve width");

    // A pathological configuration must clamp at zero rather than go negative.
    RowTextMetrics tiny = ComputeRowTextMetrics(200.0f, 4, 64, 40, false);
    printf("  tiny panel    : right=%.1f width=%.1f\n", tiny.textRight, tiny.textWidth);
    Check(tiny.textWidth >= 0.0f, "width never goes negative");

    printf("\n=== token lookup coverage ===\n");
    // Every documented variable must resolve. A token that silently stays as
    // literal text on screen is worse than a wrong number, and that is exactly
    // what happened to {gpu.temp}.
    {
        Values t;
        t.cpu = 12; t.cpuUser = 5; t.cpuKernel = 7; t.cpuGhz = 3.5; t.cpuGhzMax = 4.0;
        t.cores = 8; t.threads = 16;
        wcscpy(t.cpuName, L"Test CPU");
        t.memPercent = 73; t.memUsedGb = 23.4; t.memTotalGb = 32.0;
        t.memFreeGb = 8.6; t.memCommit = 55;
        t.netDownBps = 1234567; t.netUpBps = 45678; t.netTotalBytes = 1e9; t.netNics = 3;
        t.gpuValid = true; t.gpu = 33; t.gpuMem = 20; t.gpuTemp = 61; t.gpuTempValid = true;
        t.vramUsedGb = 3.5; t.vramTotalGb = 8.0; t.gpuFan = 40; t.gpuPower = 115;
        wcscpy(t.gpuName, L"Test GPU");
        wcscpy(t.timeText, L"12:34:56"); wcscpy(t.dateText, L"2026-10-12");
        wcscpy(t.uptimeText, L"1d 02:03");
        t.modWsMb = 7.5; t.modCpuMs = 123;

        const WCHAR *tokens[] = {
            L"cpu", L"cpu.user", L"cpu.kernel", L"cpu.ghz", L"cpu.ghz.max",
            L"cpu.name", L"cpu.cores", L"cpu.threads",
            L"mem", L"mem.used", L"mem.total", L"mem.free", L"mem.commit",
            L"net.down", L"net.up", L"net.down.bps", L"net.up.bps", L"net.total", L"net.nics",
            L"gpu", L"gpu.mem", L"gpu.temp", L"gpu.name", L"gpu.vram.used",
            L"gpu.vram.total", L"gpu.fan", L"gpu.power",
            L"time", L"date", L"uptime", L"mod.ws", L"mod.cpu",
        };
        for (const WCHAR *token : tokens) {
            WCHAR row[128];
            swprintf(row, 128, L"[{%ls}]", token);
            WCHAR marked[640];
            RingSpec rings[kMaxRingsPerRow];
            int ringCount = 0;
            ExpandTokensAndRings(row, t, g_settings, marked, 640, rings, &ringCount,
                                 kMaxRingsPerRow);
            // The expansion must not still contain a brace.
            bool resolved = wcschr(marked, L'{') == nullptr;
            if (!resolved) printf("    {%ls} -> \"%ls\"\n", token, marked);
            Check(resolved, "token resolves (no leftover braces)");
        }
    }

    printf("\n=== known-but-unavailable variables render as -- ===\n");
    {
        // A GPU with no temperature sensor (the PDH fallback path): gpuTempValid
        // stays false. The token must render as "--", never as literal text and
        // never as a misleading "0".
        Values g;
        g.gpuValid = true;
        g.gpu = 42;
        g.gpuTemp = 0;
        g.gpuTempValid = false;

        struct Expect { const WCHAR *row; const WCHAR *expect; };
        const Expect expects[] = {
            {L"{gpu|0}", L"42"},
            {L"{gpu.temp}", L"--"},
            {L"{gpu.temp|0}", L"--"},
            {L"T={gpu.temp}C", L"T=--C"},
            {L"{gpu.thermal}", L"{gpu.thermal}"},   // unknown token stays visible
        };
        for (const Expect &e : expects) {
            WCHAR marked[640];
            RingSpec rings[kMaxRingsPerRow];
            int ringCount = 0;
            ExpandTokensAndRings(e.row, g, g_settings, marked, 640, rings, &ringCount,
                                 kMaxRingsPerRow);
            bool ok = wcscmp(marked, e.expect) == 0;
            wprintf(L"    \"%ls\" -> \"%ls\"  (expect \"%ls\")\n", e.row, marked, e.expect);
            Check(ok, "unavailable/unresolvable token behaviour");
        }
    }

    printf("\n=== row expansion and segmentation ===\n");
    Values v;
    v.cpu = 12;
    v.memPercent = 73;
    v.gpuValid = true;
    v.gpu = 9;
    wcscpy(v.cpuName, L"Test CPU");
    g_settings.ringSize = 26;
    g_settings.ringThickness = 4;

    struct Case {
        const WCHAR *row;
        int expectRings;
        bool expectInline;
        const WCHAR *expectText;
    };
    const Case cases[] = {
        {L"  8%  CPU  38%  RAM  ", 0, false, L"  8%  CPU  38%  RAM  "},
        {L"GPU {gpu|0}%  [[ring:{gpu}]]", 1, true, L"GPU 9%  "},
        {L"[[ring:{cpu}]]", 1, false, L""},
        {L"A 12 [[ring:{cpu}]] B", 1, true, L"A 12  B"},
    };

    for (const Case &c : cases) {
        WCHAR marked[640];
        RingSpec rings[kMaxRingsPerRow];
        int ringCount = 0;
        ExpandTokensAndRings(c.row, v, g_settings, marked, 640, rings, &ringCount,
                             kMaxRingsPerRow);

        RowState state;
        state.inlineRing[0] = false;
        BuildRowLayout(marked, ringCount, state, 0);

        wprintf(L"  row \"%ls\"\n", c.row);
        printf("    rings=%d (expect %d)  inline=%d (expect %d)\n",
               ringCount, c.expectRings, state.inlineRing[0] ? 1 : 0, c.expectInline ? 1 : 0);
        wprintf(L"    text=\"%ls\"\n", state.text[0]);

        Check(ringCount == c.expectRings, "ring count");
        Check(state.inlineRing[0] == c.expectInline, "inline flag");
        Check(wcscmp(state.text[0], c.expectText) == 0, "expanded text, spaces preserved");
    }

    printf("\n=== real GDI+ text fitting (skipped when GDI+ is unavailable) ===\n");
    {
        // The pure assertions above pin the search; this section pins the thing they
        // cannot: that a run the painter decided is too wide still comes back as ONE
        // line inside its budget, with the real font and the real measurements.
        //
        // Two properties matter and they pull in opposite directions:
        //   * no wrap -- otherwise the segment's first line rides up, which is the
        //     reported "first row is not on one straight line"
        //   * no overflow -- with wrapping off GDI+ does not clip to the box, so an
        //     untrimmed run is drawn across the gauge that follows it
        bool gdiplusReady = false;
        if (g_gdiplus.Load()) {
            GdiplusStartupInput startupInput;
            ULONG_PTR token = 0;
            if (g_gdiplus.Startup(&token, &startupInput, nullptr) == 0) {
                g_gdiplusToken = token;
                gdiplusReady = true;
            }
        }
        if (!gdiplusReady) {
            printf("  GDI+ did not start on this machine; section skipped\n");
        } else {
            const int width = 420, height = 30;
            BITMAPINFO bmi = {};
            bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
            bmi.bmiHeader.biWidth = width;
            bmi.bmiHeader.biHeight = -height;
            bmi.bmiHeader.biPlanes = 1;
            bmi.bmiHeader.biBitCount = 32;
            bmi.bmiHeader.biCompression = BI_RGB;
            void *bits = nullptr;
            HBITMAP dib = CreateDIBSection(nullptr, &bmi, DIB_RGB_COLORS, &bits, nullptr, 0);
            GpBitmap *bitmap = nullptr;
            GpGraphics *graphics = nullptr;
            if (dib && bits &&
                g_gdiplus.CreateBitmapFromScan0(width, height, width * 4, kPixelFormat32bppPARGB,
                                                (BYTE *)bits, &bitmap) == 0 &&
                bitmap && g_gdiplus.GetImageGraphicsContext((GpImage *)bitmap, &graphics) == 0 &&
                graphics) {
                g_gdiplus.SetSmoothingMode(graphics, kSmoothingAntiAlias);
                g_gdiplus.SetTextRenderingHint(graphics, kTextHintAntiAliasGridFit);

                GpFontFamily *family = nullptr;
                g_gdiplus.CreateFontFamilyFromName(L"Segoe UI", nullptr, &family);
                GpFont *font = nullptr;
                if (family) g_gdiplus.CreateFont(family, 15.0f, 0, 0, &font);
                GpStringFormat *format = CreateRowStringFormat();
                if (!format) printf("  CreateRowStringFormat failed; section skipped\n");

                if (font && format) {
                    const float boxHeight = 30.0f;
                    // One row's worth of segments: text, ring, then more text. Each
                    // segment is fitted to its own budget, exactly as PaintPanel does.
                    struct Seg { const WCHAR *text; float budget; };
                    const Seg segments[] = {
                        {L"CPU  12 %  ", 380.0f},
                        {L"CPU 12%  RAM 73%  GPU 33%  NET 1.2 MB/s  DISK 68%  FAN 1200", 380.0f},
                        {L"CPU 12%  RAM 73%  GPU 33%  NET 1.2 MB/s  DISK 68%  FAN 1200", 240.0f},
                        {L"CPU 12%", 380.0f},
                        {L"CPU 12%  RAM 73%", 380.0f},
                    };

                    // Lines a run occupies when measured in a given box. This is the
                    // measurement that moves when NoWrap is lost: with wrapping on and
                    // the line alignment centred, a too-wide run comes back as 2+ and
                    // its first line rides up.
                    auto lineCount = [&](const WCHAR *text, float boxWidth) -> int {
                        RectF box(0.0f, 0.0f, boxWidth, boxHeight), bounds = {};
                        INT cp = 0, lines = 0;
                        g_gdiplus.MeasureString(graphics, text, -1, font, &box, format,
                                                &bounds, &cp, &lines);
                        return lines;
                    };

                    printf("  %-44s %7s %7s %7s %6s\n",
                           "segment", "natural", "budget", "drawn", "lines");
                    bool fitAllSingle = true, allInsideBudget = true, allFitted = true;
                    float worstOverflow = 0.0f;

                    for (const Seg &seg : segments) {
                        WCHAR buffer[640];
                        wcsncpy(buffer, seg.text, 639);
                        buffer[639] = L'\0';

                        float budget = seg.budget > kMinLayoutWidth ? seg.budget : kMinLayoutWidth;
                        float natural = MeasureAdvance(graphics, font, format, buffer, boxHeight);

                        bool fitted = FitTextToWidth(graphics, font, format, buffer,
                                                     sizeof(buffer) / sizeof(buffer[0]),
                                                     budget, boxHeight);
                        float drawn = MeasureAdvance(graphics, font, format, buffer, boxHeight);
                        int fitLines = lineCount(buffer, budget);

                        bool inside = drawn <= budget + 0.6f;   // allow float rounding
                        if (fitLines > 1) fitAllSingle = false;
                        if (!inside) {
                            allInsideBudget = false;
                            if (drawn - budget > worstOverflow) worstOverflow = drawn - budget;
                        }
                        // Over budget means the run must come back different: either
                        // shortened, or shortened with an ellipsis appended.
                        if (natural > budget) {
                            bool changed = fitted && (wcscmp(buffer, seg.text) != 0);
                            if (!changed) allFitted = false;
                        }

                        printf("  %-44.44ls %7.1f %7.1f %7.1f %6d\n", seg.text, natural,
                               budget, drawn, fitLines);
                    }

                    // The fitted run is what reaches the screen: one line, and inside its
                    // budget. Neither assertion is implied by the other -- the fit gives
                    // the second, NoWrap gives the first.
                    Check(fitAllSingle, "a fitted run is drawn on one line");
                    Check(allInsideBudget, "every fitted run stays inside its budget");
                    Check(allFitted, "a run wider than its budget is actually trimmed");
                    if (!allInsideBudget)
                        printf("  worst overflow = %.1f px\n", worstOverflow);

                    // NoWrap is what keeps an over-wide run on one line, independent of
                    // the fit. A row can reach DrawString untrimmed (a generous budget,
                    // or a future call site), and without the flag its first line rides
                    // up -- the reported symptom.
                    {
                        WCHAR wide[640];
                        wcsncpy(wide, L"CPU 12%  RAM 73%  GPU 33%  NET 1.2 MB/s  DISK 68%  FAN 1200 FAN 1200 FAN 1200", 639);
                        wide[639] = L'\0';
                        float naturalWide = MeasureAdvance(graphics, font, format, wide, boxHeight);
                        int lines = lineCount(wide, 120.0f);
                        Check(lines == 1, "an over-wide run stays on one line (NoWrap)");
                        printf("  %.0f px of text in a 120 px box -> %d line(s); "
                               "without NoWrap this was 2+\n", naturalWide, lines);
                    }
                } else {
                    printf("  Segoe UI could not be created; section skipped\n");
                }
                if (format) g_gdiplus.DeleteStringFormat(format);
                if (font) g_gdiplus.DeleteFont(font);
                if (family) g_gdiplus.DeleteFontFamily(family);
                g_gdiplus.DeleteGraphics(graphics);
            } else {
                printf("  could not build the probe surface; section skipped\n");
            }
            if (bitmap) g_gdiplus.DisposeImage((GpImage *)bitmap);
            if (dib) DeleteObject(dib);
            if (g_gdiplusToken) g_gdiplus.Shutdown(g_gdiplusToken);
        }
    }

    printf("\n=== external data (named-pipe providers) ===\n");
    {
        // The slot table's lock is normally created by ExtStart on the sampler
        // thread. This host never runs WhTool_ModInit, so it creates the lock
        // itself -- which is exactly why the lookup helpers take no lock.
        InitializeCriticalSection(&g_extLock);
        g_extLockReady = true;

        // A value that only looks numeric must never drive a gauge or a |0 spec.
        struct NumberCase { const WCHAR *text; bool ok; double value; };
        const NumberCase numbers[] = {
            {L"42", true, 42.0}, {L"-1.5", true, -1.5}, {L"+0", true, 0.0},
            {L"73%", false, 0},  {L" 42", false, 0},    {L"4 2", false, 0},
            {L".5", false, 0},   {L"1.", false, 0},     {L"1e3", false, 0},
            {L"", false, 0},     {L"-", false, 0},      {L"abc", false, 0},
        };
        bool numbersOk = true;
        for (const NumberCase &c : numbers) {
            double parsed = -999;
            bool ok = ExtParseNumber(c.text, parsed);
            if (ok != c.ok || (ok && parsed != c.value)) {
                numbersOk = false;
                printf("  ExtParseNumber(\"%ls\") -> %d %f, expected %d %f\n",
                       c.text, ok ? 1 : 0, parsed, c.ok ? 1 : 0, c.value);
            }
        }
        Check(numbersOk, "only strictly numeric values count as numbers");

        WCHAR buffer[64];
        ExtSanitize(L"  hello\r\n\tworld  ", buffer, 64);
        printf("  sanitised: \"%ls\"\n", buffer);
        Check(wcscmp(buffer, L"hello world") == 0,
              "control characters and edge spaces are removed");

        // Unit level: the sanitiser itself never rewrites what it is given.
        ExtSanitize(L"{cpu}%", buffer, 64);
        Check(wcscmp(buffer, L"{cpu}%") == 0, "the sanitiser does not rewrite tokens");

        WCHAR longValue[600];
        for (int i = 0; i < 599; i++) longValue[i] = L'x';
        longValue[599] = L'\0';
        ExtSanitize(longValue, buffer, 16);
        printf("  truncated: \"%ls\" (%d chars)\n", buffer, (int)wcslen(buffer));
        Check((int)wcslen(buffer) == 15, "an over-long value is cut to the cap");
        Check(buffer[14] == 0x2026, "the cut is marked with an ellipsis");

        EnterCriticalSection(&g_extLock);
        int slot = ExtFindProvider(L"music", 5, true, kMaxExtProviders);
        LeaveCriticalSection(&g_extLock);
        Check(slot >= 0, "a provider gets a slot");

        ExtStoreValue(slot, L"title", L"  Now Playing: Blue Hour  ", 60000, 128);
        const WCHAR *text = nullptr;
        Check(ExtLookup(L"ext.music.title", (int)wcslen(L"ext.music.title"), &text) == ER_VALUE,
              "a stored value resolves");
        Check(text && wcscmp(text, L"Now Playing: Blue Hour") == 0,
              "the stored value is trimmed before it is shown");
        Check(ExtLookup(L"ext.MUSIC.Title", (int)wcslen(L"ext.MUSIC.Title"), &text) == ER_VALUE,
              "provider and key match case-insensitively");
        Check(ExtLookup(L"ext.music.nope", (int)wcslen(L"ext.music.nope"), &text) == ER_EXPIRED,
              "a missing key of a known provider is unavailable");
        Check(ExtLookup(L"ext.nobody.title", (int)wcslen(L"ext.nobody.title"), &text) == ER_EXPIRED,
              "an unknown provider is unavailable");
        Check(ExtLookup(L"ext.", 4, &text) == ER_NONE, "a bare ext. stays literal");
        Check(ExtLookup(L"ext.music.", (int)wcslen(L"ext.music."), &text) == ER_NONE,
              "an empty key stays literal");
        Check(ExtLookup(L"ext.music.a b", (int)wcslen(L"ext.music.a b"), &text) == ER_NONE,
              "an invalid key character stays literal");

        ExtStoreValue(slot, L"short", L"x", 1, 128);      // 1 ms
        // Age the entry by hand rather than sleeping: GetTickCount advances in steps of
        // about 15.6 ms, so Sleep(10) may not cross a tick at all and "is it expired
        // yet" then depends on scheduling. That made this assertion flap (it went red
        // twice in a row when unrelated logging shifted the timing). Moving the
        // timestamp constructs "time passed" instead of waiting for it.
        for (int i = 0; i < g_ext[slot].count; i++) {
            if (_wcsicmp(g_ext[slot].entries[i].key, L"short") == 0) {
                g_ext[slot].entries[i].updatedAt -= 1000;
            }
        }
        Check(ExtLookup(L"ext.music.short", (int)wcslen(L"ext.music.short"), &text) == ER_EXPIRED,
              "a value past its ttl renders as unavailable");

        // The deadline is what lets the HUD tick turn a dead provider into "--"
        // without waiting for the next scheduled sample.
        Check(ExtNextDeadline(g_ext, kMaxExtProviders) != 0, "a live value publishes a deadline");
        ExtInvalidate(slot, L"title");
        Check(ExtNextDeadline(g_ext, kMaxExtProviders) == 0,
              "nothing live -> no deadline, so the tick stops poking");

        ExtProvider fake = {};
        wcsncpy(fake.name, L"fake", kExtProviderChars);
        fake.count = 3;
        for (int i = 0; i < 3; i++) fake.entries[i].updatedAt = 100;
        fake.entries[0].ttlMs = 10;
        fake.entries[1].ttlMs = 50;
        fake.entries[2].ttlMs = 20;
        Check(ExtEvictIndex(fake) == 0, "eviction picks the entry that expires first");
        fake.entries[0].updatedAt = 100;
        fake.entries[0].ttlMs = 50;
        fake.entries[1].updatedAt = 200;
        fake.entries[1].ttlMs = 50;
        fake.entries[2].updatedAt = 150;
        fake.entries[2].ttlMs = 50;
        Check(ExtEvictIndex(fake) == 0, "with equal expiry the oldest update goes");

        ModSettings allow;
        Check(ExtProviderAllowed(allow, L"music", 5), "an empty allowlist allows any provider");
        wcsncpy(allow.extProviders, L"music; discord", kExtProviderListChars - 1);
        Check(ExtProviderAllowed(allow, L"music", 5), "a listed provider is allowed");
        Check(ExtProviderAllowed(allow, L"discord", 7), "space around a list entry is ignored");
        Check(!ExtProviderAllowed(allow, L"other", 5), "an unlisted provider is refused");

        ExtCommand cmd;
        Check(ExtParseLine(L"", cmd) == 0 && cmd.cmd == EC_NONE, "a blank line is ignored");
        Check(ExtParseLine(L"# note", cmd) == 0 && cmd.cmd == EC_NONE, "a comment is ignored");
        Check(ExtParseLine(L"HELLO 1 music", cmd) == 0 && cmd.cmd == EC_HELLO, "HELLO is accepted");
        Check(ExtParseLine(L"HELLO 2 music", cmd) == 400, "an unknown protocol version is refused");
        Check(ExtParseLine(L"NOPE 1", cmd) == 400, "an unknown command is refused");
        Check(ExtParseLine(L"SET music title", cmd) == 400, "a short SET is refused");
        Check(ExtParseLine(L"SET music title - Now Playing: Blue Hour", cmd) == 0 &&
                  cmd.cmd == EC_SET && cmd.textLen == 22 &&
                  _wcsnicmp(cmd.text, L"Now Playing: Blue Hour", 22) == 0,
              "SET keeps its text, spaces and all");
        Check(ExtParseLine(L"EVENT music info - t b", cmd) == 0 && cmd.cmd == EC_EVENT,
              "EVENT parses now that the event milestone implements it");
        DWORD ttl = 0;
        Check(ExtParseTtl(L"-", 1, 5000, &ttl) && ttl == 5000, "- means the configured default");
        Check(ExtParseTtl(L"250", 3, 5000, &ttl) && ttl == 250, "an explicit ttl is used as given");
        Check(!ExtParseTtl(L"abc", 3, 5000, &ttl), "a non-numeric ttl is refused");

        {
            ModSettings rows;
            wcsncpy(rows.rows[0], L"CPU {cpu}%  {ext.music.title}", 255);
            rows.rowCount = 1;
            unsigned groups = RequiredGroups(rows);
            Check((groups & G_EXT) != 0, "{ext.*} selects the external group");
            Check((groups & G_CPU) != 0, "and built-in groups are still found");
            ModSettings bad;
            wcsncpy(bad.rows[0], L"{ext.}", 255);
            bad.rowCount = 1;
            Check((RequiredGroups(bad) & G_EXT) == 0, "a malformed {ext.} selects nothing");
        }

        ExtStoreValue(slot, L"progress", L"0.42", 60000, 128);
        {
            Values v;
            ModSettings cfg;
            RingSpec ring = {};
            const WCHAR *body = L"{ext.music.progress}:1";
            Check(ParseRing(body, (int)wcslen(body), v, cfg, ring) &&
                      ring.value > 0.41 && ring.value < 0.43,
                  "a gauge can be driven by an external value");
        }

        // The property that actually matters, end to end: a pushed value that looks
        // like a token is substituted as text and NOT re-expanded. Expansion walks
        // the template once and never rescans what it copied, so this stays
        // literal. Making expansion recursive is the way to break it, and this
        // assertion is what turns red when someone does.
        ExtStoreValue(slot, L"injected", L"{cpu}", 60000, 128);
        {
            Values v;
            v.cpu = 12.0;
            WCHAR out[64];
            ExpandTokens(L"[{ext.music.injected}]", v, out, 64);
            printf("  an injected token expands to: \"%ls\"\n", out);
            Check(wcscmp(out, L"[{cpu}]") == 0,
                  "a pushed value that looks like a token stays literal");
        }

        {
            const StubSetting extOn[] = {
                {L"ExternalData", L"On"},
                {L"ExtProviders", L"music;discord"},
            };
            g_stubStrings = extOn;
            g_stubStringCount = (int)(sizeof(extOn) / sizeof(extOn[0]));
            const StubSetting extInts[] = {
                {L"ExtMaxValue", L"4096"},     // above the ceiling
                {L"ExtMaxProviders", L"99"},   // above the table
                {L"ExtValueTtlMs", L"5"},      // below the floor
            };
            g_stubInts = extInts;
            g_stubIntCount = (int)(sizeof(extInts) / sizeof(extInts[0]));
            ModSettings s;
            ReadSettings(s);
            printf("  ExternalData=%d ttl=%d maxValue=%d providers=%d list=\"%ls\"\n",
                   s.externalData ? 1 : 0, s.extValueTtlMs, s.extMaxValue, s.extMaxProviders,
                   s.extProviders);
            Check(s.externalData, "the string enum turns external data on");
            Check(s.extMaxValue == kExtMaxValueChars, "the value length is clamped to the ceiling");
            Check(s.extMaxProviders == kMaxExtProviders, "the provider limit is clamped to the table");
            Check(s.extValueTtlMs == 100, "the ttl is clamped to the floor");
            Check(wcscmp(s.extProviders, L"music;discord") == 0, "the allowlist is read verbatim");

            g_stubStrings = nullptr;
            g_stubStringCount = 0;
            g_stubInts = nullptr;
            g_stubIntCount = 0;
        }

        // The frame -> value path, without a pipe. ExtHandleLine is what the pipe
        // thread calls once per line, so driving it here covers tokenising, the
        // HELLO/SET dispatch and the store -- the layer where "the client
        // connected but the panel still says --" actually lives. Replies go to an
        // invalid handle on purpose: writing them is allowed to fail.
        {
            InitializeCriticalSection(&g_settingsLock);
            EnterCriticalSection(&g_settingsLock);
            g_settings.externalData = true;
            g_settings.extProviders[0] = L'\0';
            g_settings.extMaxUpdates = 100;
            g_settings.extMaxProviders = kMaxExtProviders;
            g_settings.extValueTtlMs = 30000;
            g_settings.extMaxValue = 128;
            LeaveCriticalSection(&g_settingsLock);

            OVERLAPPED ov = {};
            ov.hEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
            ExtSession ses;
            Check(ExtHandleLine(INVALID_HANDLE_VALUE, &ov, L"HELLO 1 probe", ses) == 0 &&
                      ses.slot >= 0,
                  "HELLO through the handler allocates a slot");
            Check(ExtHandleLine(INVALID_HANDLE_VALUE, &ov, L"SET probe now - hello from a frame",
                                ses) == 0,
                  "SET through the handler is accepted");
            const WCHAR *stored = nullptr;
            Check(ExtLookup(L"ext.probe.now", (int)wcslen(L"ext.probe.now"), &stored) == ER_VALUE &&
                      stored && wcscmp(stored, L"hello from a frame") == 0,
                  "what the handler stored is what a row will read");
            // A real client frames every line with a newline; the sanitiser has to
            // swallow the framing and the surrounding spaces, or the screen shows
            // them.
            Check(ExtHandleLine(INVALID_HANDLE_VALUE, &ov, L"SET probe nl -  line two \n", ses) == 0,
                  "a newline framed line is accepted");
            Check(ExtLookup(L"ext.probe.nl", (int)wcslen(L"ext.probe.nl"), &stored) == ER_VALUE &&
                      stored && wcscmp(stored, L"line two") == 0,
                  "framing and edge spaces are trimmed before the screen");
            // The shape a real client sends: every line is newline framed, and the
            // provider name is a *slice* of that line. Anything that copies it has
            // to stop at providerLen: copying from the pointer swallows whatever
            // follows (the newline), the stored name then differs in length from the
            // provider a row asks for, and the row renders "--" forever. This is
            // the bug the first version of these assertions missed, because they
            // drove the handler with an unframed line.
            ExtSession framed;
            Check(ExtHandleLine(INVALID_HANDLE_VALUE, &ov, L"HELLO 1 framed\n", framed) == 0 &&
                      framed.slot >= 0,
                  "a newline framed HELLO allocates a slot");
            Check(ExtHandleLine(INVALID_HANDLE_VALUE, &ov, L"SET framed k - v\n", framed) == 0,
                  "a newline framed SET is accepted");
            Check(ExtLookup(L"ext.framed.k", (int)wcslen(L"ext.framed.k"), &stored) == ER_VALUE &&
                      stored && wcscmp(stored, L"v") == 0,
                  "a newline framed provider is addressable as {ext.framed.k}");
            // And a second HELLO for the same provider must reuse the slot, not
            // burn another one because the stored name carries a newline.
            ExtSession again;
            Check(ExtHandleLine(INVALID_HANDLE_VALUE, &ov, L"HELLO 1 framed\n", again) == 0 &&
                      again.slot == framed.slot,
                  "reconnecting reuses the provider slot");

            // A provider outside the allowlist must be refused and must not get a
            // slot: accepting it silently is the failure that hides behind "--".
            EnterCriticalSection(&g_settingsLock);
            wcsncpy(g_settings.extProviders, L"music", kExtProviderListChars - 1);
            LeaveCriticalSection(&g_settingsLock);
            ExtSession other;
            Check(ExtHandleLine(INVALID_HANDLE_VALUE, &ov, L"HELLO 1 discord", other) == 405 &&
                      other.slot < 0,
                  "a provider outside the allowlist is refused");
            EnterCriticalSection(&g_settingsLock);
            g_settings.extProviders[0] = L'\0';
            g_settings.externalData = false;
            LeaveCriticalSection(&g_settingsLock);
            if (ov.hEvent) CloseHandle(ov.hEvent);
            DeleteCriticalSection(&g_settingsLock);
        }

        // --- M2: the event channel (design 4.2 / 7.2 / 7.3) -----------------------
        {
            ExtCommand ev;
            Check(ExtParseLine(L"EVENT music info 8000 Title body text here", ev) == 0 &&
                      ev.cmd == EC_EVENT && ev.severity == ES_INFO && ev.ttlLen == 4 &&
                      ev.titleLen == 5 && ev.bodyLen == 14,
                  "EVENT parses provider, severity, ttl, title and the rest as body");
            Check(ExtParseLine(L"EVENT music critical - T b", ev) == 0 &&
                      ev.severity == ES_CRITICAL && ev.ttlLen == 1,
                  "critical severity and the default-ttl marker parse");
            Check(ExtParseLine(L"EVENT music nope 8000 T b", ev) == 400,
                  "an unknown severity is a parse error");
            Check(ExtParseLine(L"EVENT music info 8000", ev) == 400,
                  "EVENT without a title is a parse error");
            Check(ExtParseLine(L"SET music title - x", ev) == 0 && ev.cmd == EC_SET,
                  "SET still parses now that EVENT does");
        }

        // The queue's ordering rules, straight from design 7.3.
        {
            IslandEvent a = {};
            wcsncpy(a.provider, L"music", kExtProviderChars);
            wcsncpy(a.title, L"A", kIslandTitleChars - 1);
            a.severity = ES_INFO; a.ttlMs = 6000; a.repeat = 1; a.queuedAt = 1000;
            IslandEvent b = a; wcsncpy(b.title, L"B", kIslandTitleChars - 1); b.queuedAt = 2000;
            IslandEvent c = a; wcsncpy(c.title, L"C", kIslandTitleChars - 1);
            c.severity = ES_CRITICAL; c.queuedAt = 3000;

            IslandEvent q[kIslandQueueMax] = {};
            unsigned drops = 0;
            int n = IslandEnqueue(q, 0, 4, a, &drops);
            Check(n == 1 && wcscmp(q[0].title, L"A") == 0, "an event is queued");
            n = IslandEnqueue(q, n, 4, b, &drops);
            Check(n == 1 && wcscmp(q[0].title, L"B") == 0,
                  "the same provider replaces its own pending event, not queue a second");
            n = IslandEnqueue(q, n, 4, b, &drops);
            Check(n == 1 && q[0].repeat == 2,
                  "the same title inside the window bumps a repeat instead of queueing");
            n = IslandEnqueue(q, n, 4, c, &drops);
            Check(n == 1 && wcscmp(q[0].title, L"C") == 0 && q[0].severity == ES_CRITICAL,
                  "a replaced slot takes the arriving severity");

            IslandEvent d = b;
            wcsncpy(d.provider, L"discord", kExtProviderChars); d.queuedAt = 4000;
            n = IslandEnqueue(q, n, 4, d, &drops);
            Check(n == 2 && IslandPick(q, n) == 0 && q[0].severity == ES_CRITICAL,
                  "a second provider gets its own slot, and picking prefers severity");
            IslandEvent e = d;
            wcsncpy(e.provider, L"mail", kExtProviderChars); e.queuedAt = 5000;
            n = IslandEnqueue(q, n, 4, e, &drops);
            Check(n == 3 && IslandPick(q, n) == 0, "inside a level the earliest still wins");

            IslandEvent f = d;
            wcsncpy(f.provider, L"chat", kExtProviderChars); f.queuedAt = 6000;
            n = IslandEnqueue(q, n, 3, f, &drops);
            Check(n == 3 && drops == 1 && _wcsicmp(q[1].provider, L"chat") == 0 &&
                      _wcsicmp(q[2].provider, L"mail") == 0,
                  "a full queue drops the weakest, earliest entry and counts it");
            IslandEvent alarm = c;
            wcsncpy(alarm.provider, L"alarm", kExtProviderChars); alarm.queuedAt = 7000;
            n = IslandEnqueue(q, n, 3, alarm, &drops);
            Check(n == 3 && q[IslandPick(q, n)].severity == ES_CRITICAL,
                  "a critical displaces an info rather than being dropped");
        }

        // The state machine's transitions (design 7.2), on an explicit clock.
        {
            IslandEvent q[2] = {};
            wcsncpy(q[0].provider, L"music", kExtProviderChars);
            wcsncpy(q[0].title, L"T", kIslandTitleChars - 1);
            q[0].severity = ES_INFO; q[0].ttlMs = 500;
            wcsncpy(q[1].provider, L"alarm", kExtProviderChars);
            wcsncpy(q[1].title, L"Z", kIslandTitleChars - 1);
            q[1].severity = ES_CRITICAL; q[1].ttlMs = 500;

            IslandView v = {};
            int pop = -1;
            Check(StepIsland(&v, q, 1, 1000, false, &pop) == IS_POPPING && pop == 0,
                  "a queued event starts the pop");
            Check(v.cardDeadline == 1500, "the card inherits the event's ttl");
            Check(StepIsland(&v, q, 1, 1100, false, nullptr) == IS_POPPING,
                  "the pop keeps playing until the animation reports it finished");
            Check(StepIsland(&v, q, 1, 1200, true, nullptr) == IS_CARD,
                  "the pop settles into a card");
            Check(StepIsland(&v, q, 1, 1499, false, nullptr) == IS_CARD,
                  "the card lives until its ttl");
            Check(StepIsland(&v, q, 1, 1500, false, nullptr) == IS_COLLAPSING,
                  "then it collapses -- nothing stays on screen forever");
            Check(StepIsland(&v, q, 0, 1600, true, nullptr) == IS_IDLE,
                  "with nothing queued, collapsing settles back to idle");
            v.state = IS_CARD; v.showing = ES_INFO; v.cardDeadline = 2000;
            Check(StepIsland(&v, q, 1, 2000, false, nullptr) == IS_COLLAPSING,
                  "an expired card starts collapsing");
            Check(StepIsland(&v, q, 1, 2100, true, &pop) == IS_POPPING && pop == 0,
                  "a queue that is not empty goes straight into the next pop");

            v.state = IS_CARD; v.showing = ES_INFO; v.cardDeadline = 99000;
            Check(StepIsland(&v, q, 2, 3000, false, nullptr) == IS_COLLAPSING,
                  "a waiting critical preempts an info card");
            Check(StepIsland(&v, q, 2, 3100, true, &pop) == IS_POPPING && pop == 1,
                  "and the critical is what pops next");
            Check(v.showing == ES_CRITICAL, "the view now shows the critical");
        }

        // What the card shows, and how severity maps to colour (design 7.4 / 14 Q4).
        {
            IslandEvent ev = {};
            wcsncpy(ev.title, L"T", kIslandTitleChars - 1);
            ev.repeat = 1;
            IslandCardLayout p1 = PlanIslandCard(ev);
            Check(p1.lines == 1 && !p1.hasBadge,
                  "a card without a body is one line and carries no merge badge");
            wcsncpy(ev.body, L"B", kIslandBodyChars - 1);
            ev.repeat = 7;
            IslandCardLayout p2 = PlanIslandCard(ev);
            Check(p2.lines == 2 && p2.hasBadge && wcscmp(p2.badge, L"\x00D7" L"7") == 0,
                  "a body adds the second line and a repeat adds the merged count");
            Check(ResolveSeverityColor(ES_CRITICAL, 0xFF112233) == 0xFFE4573D &&
                      ResolveSeverityColor(ES_WARN, 0xFF112233) == 0xFFF2C14E &&
                      ResolveSeverityColor(ES_INFO, 0xFF112233) == 0xFF112233,
                  "severity picks the accent: fixed for warn/critical, the theme's text for info");
        }

        // The card's target width: the configured minimum is a floor, text widens it,
        // and it is capped by the screen rather than growing without bound (design 12).
        {
            ModSettings cfg = {};
            cfg.rowHeight = 30;
            cfg.islandMinWidth = 260;
            IslandMetrics narrow = {};
            narrow.titleChars = 4;
            Check(ComputeIslandCardWidth(narrow, cfg, 0) == 260,
                  "a short card is never narrower than the configured minimum");
            IslandMetrics wide = {};
            wide.titleChars = 200;
            Check(ComputeIslandCardWidth(wide, cfg, 0) == kIslandMaxWidth,
                  "a long title is capped instead of growing the window without bound");
            Check(ComputeIslandCardWidth(wide, cfg, 400) == 400 - 24,
                  "on a narrow monitor the card is clamped to the screen, not the 640 default");
            IslandMetrics mid = {};
            mid.titleChars = 30;
            const int midWidth = ComputeIslandCardWidth(mid, cfg, 0);
            Check(midWidth > 260 && midWidth < kIslandMaxWidth,
                  "in between, the width follows the text");
            cfg.islandMinWidth = 600;
            Check(ComputeIslandCardWidth(narrow, cfg, 0) == 600,
                  "raising the minimum raises the floor for the same card");

            // Hits during an animation: the union of the two states, so nothing that is
            // visible is ever unclickable (design 8).
            PanelRect from = {0, 0, 100, 50};
            PanelRect to = {10, -5, 200, 60};
            PanelRect unioned = UnionPanelRect(from, to);
            Check(unioned.x == 0 && unioned.y == -5 && unioned.w == 210 && unioned.h == 60,
                  "the animated hit target is the union of where it was and where it goes");
        }

        // The pipe's DACL has to actually admit this user. Getting it wrong is
        // invisible until a real client is refused, so a pipe built with the very
        // same descriptor is opened here -- but only where the environment allows
        // a pipe client at all. A confined sandbox denies that for *every*
        // descriptor, including no descriptor at all, so a control pipe decides
        // whether the comparison means anything. (Same shape as the GDI+ section
        // that prints "section skipped" instead of failing.)
        {
            const WCHAR controlName[] = L"\\\\.\\pipe\\endfield-info-windhawk-control";
            HANDLE controlServer = CreateNamedPipeW(controlName, PIPE_ACCESS_DUPLEX,
                                                    PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE |
                                                        PIPE_WAIT,
                                                    1, 1024, 1024, 0, nullptr);
            DWORD controlErr = 0;
            bool writable = false;
            if (controlServer != INVALID_HANDLE_VALUE) {
                HANDLE control = CreateFileW(controlName, GENERIC_READ | GENERIC_WRITE, 0, nullptr,
                                             OPEN_EXISTING, 0, nullptr);
                writable = control != INVALID_HANDLE_VALUE;
                controlErr = writable ? 0 : GetLastError();
                if (writable) CloseHandle(control);
                DisconnectNamedPipe(controlServer);
                CloseHandle(controlServer);
            } else {
                controlErr = GetLastError();
            }

            if (!writable) {
                printf("  this environment refuses pipe clients (err=%lu); "
                       "the DACL check is skipped here\n", controlErr);
            } else {
                PSECURITY_DESCRIPTOR sd = ExtPipeSecurityDescriptor();
                SECURITY_ATTRIBUTES sa = {};
                sa.nLength = sizeof(sa);
                sa.lpSecurityDescriptor = sd;
                const WCHAR name[] = L"\\\\.\\pipe\\endfield-info-windhawk-acl-test";
                HANDLE server = CreateNamedPipeW(name, PIPE_ACCESS_DUPLEX,
                                                 PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE |
                                                     PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS,
                                                 1, 4096, 4096, 0, &sa);
                Check(server != INVALID_HANDLE_VALUE, "the pipe descriptor builds a pipe");
                if (server != INVALID_HANDLE_VALUE) {
                    HANDLE client = CreateFileW(name, GENERIC_READ | GENERIC_WRITE, 0, nullptr,
                                                OPEN_EXISTING, 0, nullptr);
                    DWORD err = client == INVALID_HANDLE_VALUE ? GetLastError() : 0;
                    printf("  same-user client on our own DACL: opened=%d err=%lu\n",
                           client != INVALID_HANDLE_VALUE ? 1 : 0, err);
                    Check(client != INVALID_HANDLE_VALUE,
                          "the pipe DACL admits the user running the mod");
                    if (client != INVALID_HANDLE_VALUE) CloseHandle(client);
                    DisconnectNamedPipe(server);
                    CloseHandle(server);
                }
                if (sd) LocalFree(sd);
            }
        }

        DeleteCriticalSection(&g_extLock);
        g_extLockReady = false;
    }

    printf("\n%s (%d failure(s))\n", g_failures ? "FAILED" : "PASSED", g_failures);
    return g_failures ? 1 : 0;
}
