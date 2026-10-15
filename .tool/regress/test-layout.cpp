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
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#define WH_MOD_ID L"test"

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

// The real Wh_GetStringSetting hands back a freshly allocated copy that the caller must
// release with Wh_FreeStringSetting (windhawk_api.h:230-245). A stub that returned a
// pointer into its own table could not tell a leak from a double free -- both were
// invisible, and both are easy to introduce. Every handout is recorded here, every free
// has to match one, and whatever is still outstanding at the end of main is a leak.
static const WCHAR *g_handedOut[64];
static int g_handedOutCount = 0;
static bool g_stringLedgerBroken = false;
static int g_stringsHandedOut = 0;

static WCHAR *StubDupString(const WCHAR *value) {
    const size_t chars = wcslen(value) + 1;
    WCHAR *copy = (WCHAR *)malloc(chars * sizeof(WCHAR));
    if (!copy) return nullptr;
    wcscpy(copy, value);
    if (g_handedOutCount < (int)(sizeof(g_handedOut) / sizeof(g_handedOut[0]))) {
        g_handedOut[g_handedOutCount++] = copy;
    } else {
        g_stringLedgerBroken = true;   // more outstanding strings than the ledger holds
    }
    g_stringsHandedOut++;
    return copy;
}

static PCWSTR Wh_GetStringSetting(PCWSTR name, ...) {
    return StubDupString(StubLookup(g_stubStrings, g_stubStringCount, name));
}

static void Wh_FreeStringSetting(PCWSTR value) {
    if (!value) return;                 // the real API tolerates a null release
    for (int i = 0; i < g_handedOutCount; i++) {
        if (g_handedOut[i] != value) continue;
        g_handedOut[i] = g_handedOut[--g_handedOutCount];
        free((void *)value);
        return;
    }
    printf("  Wh_FreeStringSetting on a pointer that was never handed out "
           "(double free, or a free of something else)\n");
    g_stringLedgerBroken = true;
}
static void Wh_Log(const WCHAR *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    vwprintf(fmt, args);
    va_end(args);
    wprintf(L"\n");
}
static bool Wh_SetFunctionHook(void *, void *, void **) { return true; }

// Two levels up: this host lives in .tool/regress/, and a quoted include is resolved
// against the including file first, so the product at the repository root is found
// regardless of the working directory the compiler is invoked from.
#include "../../endfield-info-windhawk.wh.cpp"

static int g_failures = 0;
static int g_skips = 0;

static void Check(bool ok, const char *label) {
    printf("  %-58s %s\n", label, ok ? "OK" : "FAIL");
    if (!ok) g_failures++;
}

// A check that cannot run in this environment. It is counted and printed, never silently
// skipped: "the check passed" and "the check never ran" used to look identical in the
// output, which is the trap AGENTS 3 warns about.
static void Skip(const char *label) {
    printf("  %-58s SKIP\n", label);
    g_skips++;
}

// One settings object per *site* in this file, in namespace-scope storage, reset on entry.
//
// ModSettings now carries a compiled markup program per row, which makes it about 33 KB, and
// keeping every block's copy on the stack put main's frame past the 1 MB stack this build is
// given -- and that failure is a stack overflow with no output at all, which reads like a clean
// run if you only grep for FAIL. The objects are namespace-scope rather than block-scope statics
// on purpose: a block-scope static of a type with a non-trivial default constructor needs
// __cxa_guard_acquire/release, which this build would have to import from libc++.dll, and the
// host is meant to link against nothing but the Win32 libraries. One object per site also means
// two blocks can never share one, however they nest; the host drives the product from one
// thread, so there is no sharing to worry about beyond that.
//
// The defaults live in an object of their own because a reset has to copy from *something*, and
// `x = ModSettings()` would materialise a 33 KB temporary in the frame -- the very cost this
// arrangement exists to avoid.
static const ModSettings kDefaultSettings = ModSettings();
static ModSettings g_markRow, g_theme, g_ringPreset, g_ringCustom, g_ringThemed, g_iconTag,
    g_iconRow, g_iconPlan, g_iconPlanSame, g_iconPlanRipple, g_iconRing, g_iconSize,
    g_fontChain, g_fontTagged, g_metricDefault, g_metricHuge, g_metricRow, g_sizeRun,
    g_sizeTagged, g_runCase, g_prefixRows, g_geometrySaved, g_geometry, g_planSaved, g_plan,
    g_extAllow, g_extRows, g_extBad, g_extCfg, g_extSettings, g_islandCfgA, g_islandCfgB,
    g_publish, g_programCompile, g_programRead, g_gapCompile, g_gapLayout, g_gapSaved, g_gapPlan,
    g_gapChecked;

#define LOCAL_SETTINGS(name, slot) ModSettings &name = slot; slot = kDefaultSettings

// ---- injectable font coverage ---------------------------------------------
// A fake glyph table plus the counter that makes the *number* of coverage questions
// assertable. "It picked the right family" is only half of the fallback contract; the other
// half is how much it had to ask to get there, and that is the half a regression would break
// silently -- the panel would still look right, just cost more every frame.
static int g_fakeQueries = 0;

static bool FakeCovers(const WCHAR *family, WCHAR ch) {
    g_fakeQueries++;
    if (_wcsicmp(family, L"LatinOnly") == 0) return ch < 0x80;
    if (_wcsicmp(family, L"Everything") == 0) return true;
    return false;
}

// A one- or two-name chain built without the parser, so the picking assertions do not depend
// on the parser being right.
static FontChain MakeChain(const WCHAR *first, const WCHAR *second) {
    FontChain chain;
    if (first) wcscpy(chain.names[chain.count++], first);
    if (second) wcscpy(chain.names[chain.count++], second);
    return chain;
}

// PickFontFamily hands back a pointer into whichever chain it chose, so results are compared
// by name. Comparing pointers would compare addresses of *different* chains that happen to
// hold the same string -- and the assertions that do that pass or fail for the wrong reason.
static bool FamilyIs(const WCHAR *picked, const WCHAR *name) {
    return picked && wcscmp(picked, name) == 0;
}

// ---- the row pipeline's two halves -----------------------------------------
//
// The product compiles a row's markup once, while the settings are read, and the publish path
// only instantiates that program against the current values. A test that wants to look at a row
// has to do the same two things, so this is the one place it does them: the raw text is put
// into a scratch copy of the settings (which is what ReadSettings does for real), compiled, and
// then instantiated. Going through one helper, rather than calling the compiler and the
// instantiator separately, is what keeps a test from pairing a program with text it was not
// compiled from -- InstantiateRowProgram reads the text out of the settings object itself.
static void MarkRow(const WCHAR *raw, const Values &v, const ModSettings &settings, WCHAR *out,
                    int cap, RingSpec *rings, int *ringCount, int maxRings) {
    LOCAL_SETTINGS(scratch, g_markRow);
    scratch = settings;
    wcsncpy(scratch.rows[0], raw, kRowTextCap - 1);
    scratch.rows[0][kRowTextCap - 1] = L'\0';
    CompileRowText(scratch, scratch.rows[0], scratch.rowProgram[0]);
    InstantiateRowProgram(scratch, 0, v, out, (size_t)cap, rings, ringCount, maxRings);
}

// Field by field, and only over the elements that were written: a program's arrays are
// oversized by design, so comparing the struct -- or the bytes past partCount -- would call two
// equal programs different for no reason. Every field is compared, so one changed attribute
// still reads as a difference.
static bool RowProgramEqual(const RowProgram &a, const RowProgram &b) {
    if (a.partCount != b.partCount || a.first != b.first) return false;
    for (int i = 0; i < a.partCount; i++) {
        const RowPart &x = a.parts[i];
        const RowPart &y = b.parts[i];
        if (x.kind != y.kind || x.tagChain != y.tagChain || x.runFontSize != y.runFontSize ||
            x.offset != y.offset || x.length != y.length || x.next != y.next ||
            x.failNext != y.failNext) {
            return false;
        }
    }
    return true;
}

// The n-th element of a program on its ordinary path, which is the order a publish emits them
// in. Elements are stored as a graph because a ring that fails has to resume in the middle of
// its own tag; following `next` is what turns that back into a sequence.
static const RowPart *ProgramElement(const RowProgram &p, int index) {
    int node = p.first;
    for (int i = 0; i < index && node >= 0 && node < p.partCount; i++) {
        node = p.parts[node].next;
    }
    if (node < 0 || node >= p.partCount) return nullptr;
    return &p.parts[node];
}

// Whether any element of a program is of this kind -- including the ones only a failing ring
// can reach. Used to compare a program against the cost gate that decides whether the row
// needs the multi-font path at all.
static bool ProgramHasKind(const RowProgram &p, RowPartKind kind) {
    for (int i = 0; i < p.partCount; i++) {
        if (p.parts[i].kind == kind) return true;
    }
    return false;
}

// Every TEXT template the ordinary path reaches, concatenated: what a program says when no
// {token} expands to anything. That is how "compiling did not expand the tokens" is read off a
// program directly, without instantiating it with values that would hide the answer.
static void LinearTemplate(const RowProgram &p, const WCHAR *raw, WCHAR *out, int cap) {
    int used = 0;
    for (int i = 0; ; i++) {
        const RowPart *part = ProgramElement(p, i);
        if (!part) break;
        if (part->kind != kPartText) continue;
        for (int j = 0; j < part->length && used + 1 < cap; j++) {
            out[used++] = raw[part->offset + j];
        }
    }
    out[used] = L'\0';
}

// A wide string with every non-printing character spelled out, so a marker mismatch can be read
// instead of guessed at. `%ls` prints a private-use code point as *nothing*, which is how a row
// that looked right in a log turned out not to be byte-equal to what was expected.
static void DumpWide(const char *label, const WCHAR *text) {
    printf("    %s = \"", label);
    for (const WCHAR *p = text; *p; p++) {
        if (*p >= 0x20 && *p < 0x7F) printf("%lc", (wint_t)*p);
        else printf("{%04X}", (unsigned)*p);
    }
    printf("\"\n");
}

// True when a marked string is exactly `expected`. On a mismatch both sides are printed with
// DumpWide, so a failure names the character that differs rather than showing two strings that
// look the same.
static bool MarkedEquals(const WCHAR *marked, const WCHAR *expected) {
    if (wcscmp(marked, expected) == 0) return true;
    DumpWide("actual  ", marked);
    DumpWide("expected", expected);
    return false;
}

// The pixel width a gap marker carries, in hundredths of a pixel, or -1 when the row has no gap
// marker. The marker is `kGapMarkStart + four decimal digits + kRingMarkEnd`; reading it back
// through one function keeps the test from spelling the digits out twice (and from getting them
// wrong once, which it did).
static int GapMarkerHundredths(const WCHAR *marked) {
    const WCHAR *at = wcschr(marked, kGapMarkStart);
    if (!at || at[1] < L'0' || at[1] > L'9' || at[2] < L'0' || at[2] > L'9' ||
        at[3] < L'0' || at[3] > L'9' || at[4] < L'0' || at[4] > L'9') {
        return -1;
    }
    return ((int)(at[1] - L'0')) * 1000 + ((int)(at[2] - L'0')) * 100 +
           ((int)(at[3] - L'0')) * 10 + (int)(at[4] - L'0');
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

        LOCAL_SETTINGS(s, g_theme);
        ReadSettings(s);
        printf("  Theme=Midnight with custom colours set:\n");
        printf("    themeIndex=%d bg=%08lX border=%08lX text=%08lX accent=%08lX\n",
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
        printf("    themeIndex=%d bg=%08lX border=%08lX text=%08lX accent=%08lX\n",
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
        printf("    bg=%08lX border=%08lX text=%08lX\n", s.background, s.border, s.text);
        Check(s.background == kThemes[0].background, "empty custom falls back to Endfield bg");
        Check(s.text == kThemes[0].text, "empty custom falls back to Endfield text");

        g_stubStrings = nullptr;
        g_stubStringCount = 0;
    }

    printf("\n=== ring colour precedence ===\n");
    {
        // 1. A per-ring :COLOR field beats everything.
        LOCAL_SETTINGS(preset, g_ringPreset);
        preset.border = kThemes[1].border;
        preset.ringColorUserSet = false;

        LOCAL_SETTINGS(custom, g_ringCustom);
        custom.border = 0xFF111111;
        custom.ringColor = 0xFFAB12CD;
        custom.ringColorUserSet = true;

        RingSpec plain;                        // no :COLOR
        RingSpec explicitColor;
        explicitColor.hasColor = true;
        explicitColor.color = 0xFF00FF00;

        printf("  per-ring colour wins:      %08lX (expect FF00FF00)\n",
               ResolveRingColor(explicitColor, preset));
        Check(ResolveRingColor(explicitColor, preset) == 0xFF00FF00,
              "per-ring :COLOR beats the theme");
        Check(ResolveRingColor(explicitColor, custom) == 0xFF00FF00,
              "per-ring :COLOR beats the Custom ring colour");

        printf("  RingColor under Custom:    %08lX (expect FFAB12CD)\n",
               ResolveRingColor(plain, custom));
        Check(ResolveRingColor(plain, custom) == 0xFFAB12CD,
              "RingColor applies when Theme is Custom");

        printf("  no RingColor -> border:    %08lX (expect %08lX)\n",
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
        LOCAL_SETTINGS(themed, g_ringThemed);
        ReadSettings(themed);
        printf("  Theme=Midnight + RingColor: ringColorUserSet=%d\n",
               themed.ringColorUserSet ? 1 : 0);
        Check(!themed.ringColorUserSet, "named theme ignores the RingColor field");
        Check(ResolveRingColor(plain, themed) == kThemes[1].border,
              "named theme draws rings in its border colour");

        g_stubStrings = nullptr;
        g_stubStringCount = 0;
    }

    printf("\n=== inline icons: the tag, the glyph and the ripple switch ===\n");
    {
        // An icon used to be a per-row *setting* drawn as a plate at the row's left edge
        // (Icon1..Icon4 / Icon1Path..Icon4Path / Custom icon file). Those settings are gone:
        // an icon is an element placed by a tag, exactly like [[ring:...]], and this section
        // is about the tag itself -- what it compiles to, and what it refuses to become.
        LOCAL_SETTINGS(s, g_iconTag);
        s.fontSize = 15;
        s.rowHeight = 30;

        // The tag is classified while compiling, so the glyph is already resolved here and
        // never re-validated per publish.
        const WCHAR iconRow[] = L"[[icon:Memory]]A";
        RowProgram iconProgram;
        CompileRowText(s, iconRow, iconProgram);
        const RowPart *i0 = ProgramElement(iconProgram, 0);
        const RowPart *i1 = ProgramElement(iconProgram, 1);
        printf("    \"%ls\" -> %d element(s)\n", iconRow, iconProgram.partCount);
        Check(i0 && i0->kind == kPartIcon, "[[icon:Memory]] compiles to an icon element");
        Check(i0 && (i0->glyphFlags & kIconGlyphMask) == 2, "and records the Memory glyph");
        Check(i0 && i0->length == 0, "a built-in glyph carries no path slice");
        Check(i1 && i1->kind == kPartText && i1->offset == (int)wcslen(L"[[icon:Memory]]"),
              "the text after the tag starts right after its ]]");

        // Every tag is one element, and the index the marker carries *is* that element, which
        // is what makes the glyph and the path reachable from a stripped row without any side
        // table to keep in sync.
        const WCHAR fileRow[] = L"[[icon:file=C:\\pics\\ssd.png]]B";
        RowProgram fileProgram;
        CompileRowText(s, fileRow, fileProgram);
        const RowPart *p0 = ProgramElement(fileProgram, 0);
        printf("    \"%ls\" -> %d element(s)\n", fileRow, fileProgram.partCount);
        Check(p0 && p0->kind == kPartIcon, "[[icon:file=...]] compiles to an icon element");
        Check(p0 && p0->length == (int)wcslen(L"C:\\pics\\ssd.png"),
              "and its path is a slice of the row's own text");
        Check(p0 && p0->offset > 0, "the slice points into the row, not at its start");

        // The ripple switch is per icon, and it shares its byte with the glyph.
        const WCHAR rippleRow[] = L"[[icon:Memory:ripple]]x[[icon:Disk:noripple]]y";
        RowProgram rippleProgram;
        CompileRowText(s, rippleRow, rippleProgram);
        const RowPart *r0 = ProgramElement(rippleProgram, 0);
        const RowPart *r1 = ProgramElement(rippleProgram, 1);
        const RowPart *r2 = ProgramElement(rippleProgram, 2);
        const RowPart *r3 = ProgramElement(rippleProgram, 3);
        Check(r0 && r0->kind == kPartIcon && (r0->glyphFlags & kIconGlyphMask) == 2,
              ":ripple keeps the glyph");
        Check(r0 && (r0->glyphFlags & kIconRippleOn) != 0, "and asks for the ripples");
        Check(r2 && r2->kind == kPartIcon && (r2->glyphFlags & kIconGlyphMask) == 3,
              "the second tag is its own icon (Disk)");
        Check(r2 && (r2->glyphFlags & kIconRippleOff) != 0, "with the ripples explicitly off");
        Check(r0 && r2 && (r0->glyphFlags & kIconRippleOff) == 0 &&
                  (r2->glyphFlags & kIconRippleOn) == 0,
              "neither override leaks into the other icon");
        Check(r1 && r1->kind == kPartText && r3 && r3->kind == kPartText,
              "and the text between them survives");

        // The glyph names, including the two synonyms the settings' own list used to print
        // (RAM / Net): a name the mapping function does not know is not an icon at all, and
        // the synonyms must not become that case.
        Check(IconStyleFromName(L"CPU") == 1, "CPU names the first glyph");
        Check(IconStyleFromName(L"Memory") == 2, "and Memory the second");
        Check(IconStyleFromName(L"RAM") == 2, "RAM is the same glyph as Memory");
        Check(IconStyleFromName(L"Network") == 4 && IconStyleFromName(L"Net") == 4,
              "Net is the same glyph as Network");
        Check(IconStyleFromName(L"GPU") == 5, "GPU the last one");
        Check(IconStyleFromName(L"Memory") == IconStyleFromName(L"memory"),
              "names are matched case-insensitively");
        Check(IconStyleFromName(L"ram") == 2, "including the synonyms");
        Check(IconStyleFromName(L"None") == 0 && IconStyleFromName(L"Bogus") == 0,
              "None and an unknown name both mean no glyph");

        // The tolerance rule, seen from this side: anything the compiler cannot make sense of
        // stays literal text, whole. A blank disc would be a silent lie; the visible
        // [[icon:...]] is the only clue about what the row asked for.
        struct BadTag {
            const WCHAR *row;
            const char *label;
        };
        const BadTag badTags[] = {
            {L"[[icon:Bogus]]x", "an unknown glyph stays literal text"},
            {L"[[icon:None]]x", "so does None: the no-icon name is not an icon"},
            {L"[[icon:]]x", "an empty body stays literal text"},
            {L"[[icon:file=]]x", "a file= with no path stays literal text"},
            {L"[[icon:file=\"\"]]x", "a path of nothing but quotes stays literal text"},
            {L"[[icon:Memory]x", "a missing ]] stays literal text"},
            {L"[[icon:Memory:sideways]]x", "an unknown modifier stays literal text"},
        };
        for (const BadTag &b : badTags) {
            RowProgram program;
            CompileRowText(s, b.row, program);
            WCHAR templateText[256];
            LinearTemplate(program, b.row, templateText, 256);
            wprintf(L"    \"%ls\" -> template \"%ls\"\n", b.row, templateText);
            Check(wcscmp(templateText, b.row) == 0 && program.partCount == 1 &&
                      program.parts[0].kind == kPartText,
                  b.label);
        }

        // A row with no tag pays nothing: one text element, and no icon element anywhere in
        // the program (including the tails only a failing ring could reach).
        const WCHAR plainProgramRow[] = L"CPU 12%  RAM 73%";
        RowProgram plainProgram;
        CompileRowText(s, plainProgramRow, plainProgram);
        Check(plainProgram.partCount == 1, "a row with no icon tag compiles to one element");
        Check(!ProgramHasKind(plainProgram, kPartIcon),
              "and contains no icon element at all: unconfigured costs nothing");

        // The size an icon is drawn at is the size *in effect where it appears*: its own
        // [[size:NN]] run, else the row's size. The diameter is that size times one ratio,
        // applied in one place.
        LOCAL_SETTINGS(sized, g_iconRow);
        sized.fontSize = 15;
        sized.rowHeight = 30;
        printf("    icon diameters: row size %d -> %.2f, run size 24 -> %.2f\n",
               RowFontSizePx(sized, 0), (double)IconDiameterForFontSize(15),
               (double)IconDiameterForFontSize(24));
        Check(ElementFontSizePx(sized, 0, 0) == 15, "an untagged icon takes the row's size");
        Check(ElementFontSizePx(sized, 0, 24) == 24, "an icon in a [[size:24]] run takes 24");
        Check(IconDiameterForFontSize(15) == 15.0f * (float)kIconPlateRatio,
              "the diameter is the size times one ratio (no second formula)");
        Check(IconDiameterForFontSize(24) > IconDiameterForFontSize(15),
              "so two sizes are two diameters: the icon follows its own size");
        Check(IconDiameterForFontSize(24) == IconDiameterForFontSize(24),
              "and the same size twice is the same diameter");
        Check(IconDiameterForFontSize(0) == 0.0f, "a zero size is a zero icon");

        // The ripple default is positional: the first icon of a row plays the intro ripples,
        // every later one stays quiet, and either can be overridden. That is the look the
        // per-row plate had, kept as the default.
        Check(IconElementRipples(0, true), "by default the first icon of a row ripples");
        Check(!IconElementRipples(0, false), "and a later icon does not");
        Check(IconElementRipples(kIconRippleOn, false), ":ripple overrides the default off");
        Check(!IconElementRipples(kIconRippleOff, true), ":noripple overrides the default on");

        // A tag with no text around it is still an icon element -- there is no "the icon needs
        // a text run" rule, and the tag written first is how the default layout looks.
        const WCHAR onlyIcon[] = L"x[[icon:Memory]]";
        RowProgram onlyIconProgram;
        CompileRowText(s, onlyIcon, onlyIconProgram);
        WCHAR onlyTemplate[256];
        LinearTemplate(onlyIconProgram, onlyIcon, onlyTemplate, 256);
        wprintf(L"    \"%ls\" -> template \"%ls\"\n", onlyIcon, onlyTemplate);
        Check(ProgramHasKind(onlyIconProgram, kPartIcon),
              "a tag at the very end of a row is still an icon");
        Check(wcscmp(onlyTemplate, L"x") == 0, "and it is not left in the text template");

        // An icon is an element of a row, so its box is the row's own box: the plan says where
        // the element starts and how wide it is, and the icon is centred in that row. The old
        // reported bug was a plate drawn once at the panel's centre, so its ripple came out of
        // a middle row; the equivalent statement now is that row r's icon centre is row r's
        // centre, and never the panel's.
        LOCAL_SETTINGS(boxed, g_iconPlanSame);
        boxed.fontSize = 15;
        boxed.rowHeight = 30;
        boxed.rowCount = 4;
        const float iconD = IconDiameterForFontSize(15);
        printf("  rows  panel h  icon centre y (local)  diameter\n");
        for (int rowCount = 1; rowCount <= kMaxRows; rowCount++) {
            const float panelH = (float)PanelRowsHeight(boxed, rowCount);
            printf("  %4d  %7.0f  ", rowCount, panelH);
            for (int r = 0; r < rowCount; r++) {
                const float rowTop = (float)RowTopPx(boxed, r);
                const float rowH = (float)RowHeightPx(boxed, r);
                float cx = 0.0f, cy = 0.0f;
                IconElementCenter(kRowTextLeftInset, iconD, rowTop, rowH, &cx, &cy);
                printf("%.0f ", cy);
                // The centre has to be the middle of *that* row, and the element has to fit
                // inside it. A "panel centre only coincides with an icon on one row" check
                // used to sit here whose condition was algebraically identical to its
                // conclusion -- it could not fail, and so proved nothing.
                Check(cy == rowTop + rowH / 2.0f, "each icon is centred in its own row");
                Check(cy - iconD / 2.0f >= rowTop - 0.01f &&
                          cy + iconD / 2.0f <= rowTop + rowH + 0.01f,
                      "and the whole icon fits inside that row");
                Check(cx - iconD / 2.0f == kRowTextLeftInset,
                      "the icon's left edge is where the row's flow put it");
            }
            printf("\n");
        }

        // Ripples are emitted per icon, from that icon's own centre. Counting the origins of
        // a 4-row panel is how "the ripples come from the wrong row" stays caught.
        {
            const float H = (float)PanelRowsHeight(boxed, kMaxRows);
            int rippleOrigins = 0;
            float ys[kMaxRows] = {};
            for (int r = 0; r < kMaxRows; r++) {
                const float rowTop = (float)RowTopPx(boxed, r);
                float cx = 0.0f, cy = 0.0f;
                IconElementCenter(kRowTextLeftInset, iconD, rowTop, (float)RowHeightPx(boxed, r),
                                  &cx, &cy);
                ys[rippleOrigins++] = cy;
            }
            printf("  4 rows -> %d ripple origins at y = ", rippleOrigins);
            for (int i = 0; i < rippleOrigins; i++) printf("%.0f ", ys[i]);
            printf("\n");
            Check(rippleOrigins == kMaxRows, "every row with an icon is a ripple origin");
            Check(ys[0] != ys[1], "two icons are at two different heights");
            const float panelCentreY = H / 2.0f;
            Check(ys[0] != panelCentreY, "the first origin is not the panel centre");
            Check(ys[0] < panelCentreY, "the first origin is on the first row");
        }

        // The icon and its rings differ in size on purpose and share exactly one centre, which
        // is what pins the reported symptom ("the ripple did not come out of the icon"): the
        // rings have to come out of the disc, not from somewhere near it.
        printf("\n  icon r  ring r(0)  r(0.5)  r(1)\n");
        {
            const float iconRadius = iconD / 2.0f;
            const float r0 = RingRadiusAt(iconRadius, 0, 0.0);
            const float rMid = RingRadiusAt(iconRadius, 0, 0.5);
            const float rEnd = RingRadiusAt(iconRadius, 0, 1.0);
            printf("  %6.1f  %8.1f  %6.1f  %5.1f\n", (double)iconRadius, (double)r0,
                   (double)rMid, (double)rEnd);
            // Sizes differ on purpose, and the rings must always be the larger of the two: a
            // ring smaller than the disc would sit on the glyph.
            Check(r0 != rEnd, "rings and the icon have different sizes");
            Check(r0 >= iconRadius - 0.01f, "the innermost ring starts at the icon's edge");
            Check(rEnd > iconRadius, "rings expand outward past the icon");
            Check(rMid > r0 && rEnd > rMid, "ring radius grows with progress");
            Check(rEnd == iconRadius * (float)kRippleScaleEnd[0],
                  "the outermost radius is the icon radius times the ripple scale");
            // The scale table is what the ring radii are built from, so a shorter one must not
            // be read past its end.
            Check(RingRadiusAt(iconRadius, 2, 1.0) == RingRadiusAt(iconRadius, 5, 1.0),
                  "a ring index past the table reads the last entry, not past the array");
        }

        // The intro scales the capsule **about its centre**, and the painter hands
        // these three fields straight to the GDI+ matrix, so the fields are what is
        // asserted here. (An ApplyPanelTransform() used to live in the mod for this
        // arithmetic; nothing but this test called it, so it is gone and the
        // properties are stated the way GDI+ maps a point: x*scale + dx.)
        {
            const float W = 440.0f;
            const float H = (float)PanelRowsHeight(boxed, kMaxRows);
            PanelTransform t = ComputePanelTransform(W, H, 0.94, true);
            Check(t.active && t.scale > 0.93f && t.scale < 0.95f,
                  "a mid-intro frame is an active 0.94 transform");
            Check(t.dx == (1.0f - t.scale) * W / 2.0f && t.dy == (1.0f - t.scale) * H / 2.0f,
                  "and its offset is built from the panel centre");
            Check(t.scale * (W / 2.0f) + t.dx == W / 2.0f &&
                      t.scale * (H / 2.0f) + t.dy == H / 2.0f,
                  "so the panel centre is the fixed point of the mapping");
            // Two frames that must not be transformed at all: a finished intro, and the
            // very first frame (scale 0, where GDI+ would collapse everything to a point).
            Check(!ComputePanelTransform(W, H, 1.0, true).active,
                  "a finished intro is not a transform");
            Check(!ComputePanelTransform(W, H, 0.0, true).active,
                  "and neither is the first frame");
            Check(!ComputePanelTransform(W, H, 0.94, false).active,
                  "nor is any frame once the intro is inactive");
            printf("  mid-intro transform: scale=%.3f dx=%.1f dy=%.1f (centre %.1f,%.1f)\n",
                   t.scale, t.dx, t.dy, W / 2.0f, H / 2.0f);
        }

        // No icon tag means no icon element, and therefore no ripple origin anywhere. Asked
        // through the same pipeline the painter reads (instantiate -> strip), not through a
        // second implementation of the tag rules.
        {
            struct RowCase {
                const WCHAR *row;
                int expectIcons;
            };
            const RowCase cases[] = {
                {L"CPU 12%  RAM 73%", 0},
                {L"[[icon:Memory]]CPU", 1},
                {L"CPU [[icon:CPU]] RAM [[icon:Disk]]", 2},
                {L"[[icon:Bogus]]CPU", 0},
            };
            for (const RowCase &c : cases) {
                WCHAR marked[640];
                RingSpec rings[kMaxRingsPerRow];
                int ringCount = 0;
                MarkRow(c.row, Values(), boxed, marked, 640, rings, &ringCount, kMaxRingsPerRow);
                RowState state = {};
                BuildRowLayout(marked, ringCount, state, 0);
                int icons = 0;
                for (int s = 0; s < state.segmentCount[0]; s++) {
                    if (state.segments[0][s].offset == kIconSegOffset) icons++;
                }
                wprintf(L"    \"%ls\" -> %d icon segment(s) (expect %d)\n", c.row, icons,
                        c.expectIcons);
                Check(icons == c.expectIcons, "the row's icon elements are the tags it wrote");
                if (c.expectIcons == 0) {
                    Check(state.segmentCount[0] == 1 &&
                              state.segments[0][0].offset == 0,
                          "and a row with no icon is one plain text segment");
                }
            }
        }
    }

    printf("\n=== gaps and alignment groups: the tags ===\n");
    {
        // `[[gap:...]]` and `[[left]]`/`[[center]]`/`[[right]]` ... `[[/...]]`. Two properties
        // are settled here, before any measurement: what the compiler makes of the markup, and
        // that a row with none of it is *unchanged* -- not "changed but rendered the same".
        LOCAL_SETTINGS(s, g_gapCompile);
        s.fontSize = 15;
        s.rowHeight = 30;
        s.ringSize = 26;
        s.ringThickness = 4;

        // --- the units ---
        // The unit decides the stored resolution: tenths of an em, hundredths of a pixel. That
        // is not cosmetic -- `[[gap:1.5]]` stored in pixels would stop scaling with the row, and
        // the assertion further down is what catches that.
        struct GapCase {
            const WCHAR *body;
            bool ok;
            unsigned char unit;
            int value;          // in the unit's own resolution
        };
        const GapCase gaps[] = {
            {L"1.5", true, kGapUnitEm, 150},
            {L"1", true, kGapUnitEm, 100},
            {L"12px", true, kGapUnitPx, 1200},
            {L"0", true, kGapUnitEm, 0},
            {L"0px", true, kGapUnitPx, 0},
            {L"1.555", true, kGapUnitEm, 155},   // sub-resolution digits are dropped
            {L"  2.5  ", true, kGapUnitEm, 250}, // padding around the tag is the writer's
            {L"3PX", true, kGapUnitPx, 300},     // the unit is case-insensitive
            {L"320.01", false, 0, 0},            // past the em limit
            {L"99.99px", true, kGapUnitPx, 9999},  // the widest gap the marker can write down
            {L"", false, 0, 0},
            {L"abc", false, 0, 0},
            {L"-1", false, 0, 0},
            {L"1.2.3", false, 0, 0},
            {L"px", false, 0, 0},
            {L"1pt", false, 0, 0},
            {L"1.5 px", false, 0, 0},            // only 'px' directly after the number
            {L"100px", false, 0, 0},             // past the pixel limit: four digits cannot hold it
            {L"999.6px", false, 0, 0},
        };
        for (const GapCase &c : gaps) {
            int value = -1;
            unsigned char unit = 0xFF;
            const bool ok = ParseGapTag(c.body, (int)wcslen(c.body), &value, &unit);
            const bool same = ok == c.ok && (!ok || (unit == c.unit && value == c.value));
            wprintf(L"    [[gap:%ls]] -> ok=%d unit=%d value=%d\n", c.body, ok ? 1 : 0,
                    (int)unit, value);
            Check(same, "the gap's body decides its unit and its value, or the tag is text");
        }

        // --- the compiled shapes ---
        struct ShapeCase {
            const WCHAR *row;
            bool gap;
            bool alignOpen;
            bool alignClose;
            bool literal;
        };
        const ShapeCase shapes[] = {
            {L"a[[gap:1.5]]b", true, false, false, false},
            {L"a[[gap:12px]]b", true, false, false, false},
            {L"[[gap:abc]]", false, false, false, true},
            {L"[[gap:]]", false, false, false, true},
            {L"a[[left]]b", false, true, false, false},
            {L"a[[center]]b[[/center]]c", false, true, true, false},
            {L"a[[right]]b[[/right]]", false, true, true, false},
            {L"a[[leftish]]b", false, false, false, true},
            {L"a[[/left]]b", false, false, true, false},
        };
        for (const ShapeCase &c : shapes) {
            wcsncpy(s.rows[0], c.row, kRowTextCap - 1);
            s.rows[0][kRowTextCap - 1] = L'\0';
            CompileRowText(s, s.rows[0], s.rowProgram[0]);
            const RowProgram &p = s.rowProgram[0];
            wprintf(L"    \"%ls\" -> %d element(s)\n", c.row, p.partCount);
            Check(ProgramHasKind(p, kPartGap) == c.gap, "a gap tag compiles to a gap element");
            Check(ProgramHasKind(p, kPartAlignOpen) == c.alignOpen,
                  "an alignment tag compiles to a group opener");
            Check(ProgramHasKind(p, kPartAlignClose) == c.alignClose,
                  "and a closing tag to a group closer");
            if (c.literal) {
                WCHAR templateText[640];
                LinearTemplate(p, s.rows[0], templateText, 640);
                Check(wcscmp(templateText, c.row) == 0,
                      "a tag this code cannot read stays in the text template");
            }
        }

        // --- the compiled element carries the unit and the size in effect ---
        {
            wcsncpy(s.rows[0], L"a[[gap:1.5]]b[[gap:12px]]c", kRowTextCap - 1);
            s.rows[0][kRowTextCap - 1] = L'\0';
            CompileRowText(s, s.rows[0], s.rowProgram[0]);
            const RowProgram &p = s.rowProgram[0];
            const RowPart *first = nullptr;
            const RowPart *second = nullptr;
            for (int i = 0; i < p.partCount; i++) {
                if (p.parts[i].kind != kPartGap) continue;
                if (!first) first = &p.parts[i];
                else if (!second) second = &p.parts[i];
            }
            Check(first && second, "both gaps compiled to elements");
            if (first && second) {
                Check((first->glyphFlags & kGapUnitMask) == kGapUnitEm,
                      "the first gap is in ems");
                Check((second->glyphFlags & kGapUnitMask) == kGapUnitPx,
                      "the second gap is in pixels");
                Check(first->gap == 150 && second->gap == 1200,
                      "each element keeps its own number, not a shared one");
            }
        }
        // A gap inside a [[size:24]] run has to record that run size, or it would scale with the
        // row and the whole point of the em unit would be lost on tagged rows.
        {
            wcsncpy(s.rows[0], L"[[size:24]]a[[gap:1]]b[[/size]]", kRowTextCap - 1);
            s.rows[0][kRowTextCap - 1] = L'\0';
            CompileRowText(s, s.rows[0], s.rowProgram[0]);
            const RowProgram &p = s.rowProgram[0];
            const RowPart *gap = nullptr;
            for (int i = 0; i < p.partCount && !gap; i++) {
                if (p.parts[i].kind == kPartGap) gap = &p.parts[i];
            }
            Check(gap && gap->runFontSize == 24,
                  "a gap in a [[size:NN]] run carries the run's size");
        }

        // --- the marked string: the unit is already resolved to pixels there ---
        {
            s.rowFontSize[0] = 10;
            s.rowHeightPx[0] = 30;
            s.rowMaxFontSize[0] = 10;
            s.rowCount = 1;

            WCHAR marked[640];
            RingSpec rings[kMaxRingsPerRow];
            int ringCount = 0;
            MarkRow(L"a[[gap:1.5]]b", Values(), s, marked, 640, rings, &ringCount, kMaxRingsPerRow);
            wprintf(L"    \"a[[gap:1.5]]b\" at 10 px -> \"%ls\"\n", marked);
            Check(wcschr(marked, kGapMarkStart) != nullptr, "a gap is marked in the row");
            // The marker's payload is the gap in hundredths of a pixel (15.00 px here). It is
            // written out and read back rather than spelled digit by digit: the marker is a
            // contract between InstantiateRowProgram and BuildRowLayout, so what has to hold is
            // the *round trip*, and a hand-derived expectation of "0015" is how this assertion
            // spent a while disagreeing with a product that was right.
            Check(GapMarkerHundredths(marked) == 1500,
                  "1.5 em at 10 px is 15.00 px, written into the marker");
            Check(wcschr(marked, L'[') == nullptr, "and the tag itself is gone from the text");

            // The row's size doubles, so the *same* tag must double: that is the difference
            // between the two units, and it is decided by the tag's own syntax.
            s.rowFontSize[0] = 20;
            s.rowMaxFontSize[0] = 20;
            MarkRow(L"a[[gap:1.5]]b", Values(), s, marked, 640, rings, &ringCount, kMaxRingsPerRow);
            wprintf(L"    same tag at 20 px -> \"%ls\"\n", marked);
            Check(GapMarkerHundredths(marked) == 3000,
                  "the same em gap is twice the pixels at twice the size");

            // The px unit does not follow the size, which is the whole reason both units exist.
            MarkRow(L"a[[gap:12px]]b", Values(), s, marked, 640, rings, &ringCount,
                    kMaxRingsPerRow);
            Check(GapMarkerHundredths(marked) == 1200,
                  "a px gap is the same pixels whatever the row's size is");

            // The clamp. 100 em at 48 px is 4800 px of empty space, which is well past the cap --
            // a valid tag that the writer can still see was honoured, not a tag that vanished.
            s.rowFontSize[0] = 48;
            s.rowMaxFontSize[0] = 48;
            MarkRow(L"a[[gap:100]]b", Values(), s, marked, 640, rings, &ringCount,
                    kMaxRingsPerRow);
            const int clamped = GapMarkerHundredths(marked);
            printf("    100 em at 48 px -> %d hundredths of a pixel (cap %.0f)\n", clamped,
                   (double)kMaxGapPx);
            Check(clamped == (int)(kMaxGapPx * 100.0f + 0.5f),
                  "an absurd but valid gap is clamped to the cap rather than running off the panel");
        }

        // --- an alignment group in the marked string ---
        {
            WCHAR marked[640];
            RingSpec rings[kMaxRingsPerRow];
            int ringCount = 0;

            // Groups do not nest: the second opener closes the first group at that point, so
            // there is exactly one OPEN marker per group and one CLOSE per group, in order.
            MarkRow(L"a[[left]]b[[center]]c[[/center]]d", Values(), s, marked, 640, rings,
                    &ringCount, kMaxRingsPerRow);
            wprintf(L"    nested openers -> \"%ls\"\n", marked);
            Check(MarkedEquals(marked,
                               L"a" L"\xE008" L"1" L"\xE001" L"b" L"\xE008" L"\xE001" L"\xE008"
                               L"2" L"\xE001" L"c" L"\xE008" L"\xE001" L"d"),
                  "an alignment tag inside a group closes it and opens a new one");

            // An unclosed group ends at the row's end: the close marker is written by the
            // instantiator, not inferred by the layout from "the string ran out".
            MarkRow(L"[[center]]abc", Values(), s, marked, 640, rings, &ringCount,
                    kMaxRingsPerRow);
            wprintf(L"    unclosed     -> \"%ls\"\n", marked);
            Check(MarkedEquals(marked, L"\xE008" L"2" L"\xE001" L"abc" L"\xE008" L"\xE001"),
                  "an unclosed group is closed at the row's end");

            // A close tag with nothing open has nothing to cut, and disappears: a well-formed
            // tag with no job is not a reason to print markup at the user.
            MarkRow(L"abc[[/left]]def", Values(), s, marked, 640, rings, &ringCount,
                    kMaxRingsPerRow);
            Check(MarkedEquals(marked, L"abcdef"), "a stray closing tag cuts nothing and vanishes");

            // Mixed content in a group: text, an icon and a gap all keep their own markers.
            MarkRow(L"[[right]][[icon:CPU]][[gap:1]]x[[/right]]", Values(), s, marked, 640,
                    rings, &ringCount, kMaxRingsPerRow);
            Check(wcschr(marked, kIconMarkStart) != nullptr &&
                      wcschr(marked, kGapMarkStart) != nullptr &&
                      wcschr(marked, kAlignMarkStart) != nullptr,
                  "a group can hold text, an icon and a gap at once");
        }

        // --- [[/]]: the generic close ---------------------------------------------------------
        //
        // The feature is a *compile-time desugaring*: `[[/]]` has to instantiate to exactly the
        // marker sequence the explicit form writes, so nothing downstream -- BuildRowLayout,
        // BuildRowPlan, the paint path -- knows the tag exists. Every assertion below therefore
        // compares two marked strings produced by the product, rather than spelling out a marker
        // sequence: the property is "same as the explicit form", and a hardcoded expectation would
        // only agree with whichever of the two the test happened to be written against.
        {
            LOCAL_SETTINGS(anyS, g_gapLayout);
            anyS.fontSize = 15;
            anyS.rowHeight = 30;
            anyS.rowCount = 1;
            // The font run cases need a registered chain: `[[font:Arial]]` that was never
            // registered is ordinary text, so a comparison against it would compare two literals.
            wcscpy(anyS.tagFonts[0].key, L"Arial");
            wcscpy(anyS.tagFonts[0].chain.names[0], L"Arial");
            anyS.tagFonts[0].chain.count = 1;
            anyS.tagFontCount = 1;

            struct CloseCase {
                const WCHAR *generic;
                const WCHAR *explicitForm;
            };
            const CloseCase cases[] = {
                // Each block kind on its own, and the mixed case the brief calls out: the generic
                // close takes the *innermost* block, so the group goes before the size run.
                {L"[[size:24]]hi[[/]]", L"[[size:24]]hi[[/size]]"},
                {L"[[font:Arial]]hi[[/]]", L"[[font:Arial]]hi[[/font]]"},
                {L"[[center]]hi[[/]]", L"[[center]]hi[[/center]]"},
                {L"[[size:24]][[center]]X[[/]][[/]]",
                 L"[[size:24]][[center]]X[[/center]][[/size]]"},
                {L"[[font:Arial]][[size:24]]ab[[/]][[/]]",
                 L"[[font:Arial]][[size:24]]ab[[/size]][[/font]]"},
                // A generic close and an explicit one in the same row, in both orders.
                {L"[[size:24]]a[[/]][[center]]b[[/]]",
                 L"[[size:24]]a[[/size]][[center]]b[[/center]]"},
                {L"[[size:24]]a[[/size]][[center]]b[[/]]",
                 L"[[size:24]]a[[/size]][[center]]b[[/center]]"},
                // The flat-group rule: a second opener *replaces* the group, so the generic close
                // after it closes the newer group, not the older one.
                {L"[[left]]a[[center]]b[[/]]", L"[[left]]a[[center]]b[[/center]]"},
                {L"[[left]]a[[center]]b[[/]][[/]]",
                 L"[[left]]a[[center]]b[[/center]][[/left]]"},
                // A close followed by an element. The second `[[/]]` here is a *stray* (the first
                // one closed the size run), so the two sides are written with the same text and
                // what is asserted is the row's own shape, not a generic-vs-explicit pair.
                {L"[[size:24]]a[[/]] [[icon:CPU]][[/]]",
                 L"[[size:24]]a[[/]] [[icon:CPU]][[/]]"},
                // Runs do not nest, so a size tag written inside a size run replaces the size in
                // force; the generic close ends the run either way.
                {L"[[size:24]]a[[size:20]]b[[/]]",
                 L"[[size:24]]a[[size:20]]b[[/size]]"},
                // Text that only looks like the tag is text, on both sides of the comparison.
                {L"a[[/]]]", L"a[[/]]]"},
                // A close in front of a same-kind run: `[[/]]` takes the *innermost* frame off, so
                // the size run the second `[[/]]` closes is the one underneath the font run, not the
                // one the first close already ended. A close that looked the innermost block up by
                // *kind* instead of taking the top frame off gets this pair wrong -- it finds the
                // font frame, drops the size frame with it, and then has nothing left for the second
                // `[[/]]` (which the explicit form writes a marker for).
                {L"[[size:9]][[font:Arial]][[/]][[/]]",
                 L"[[size:9]][[font:Arial]][[/font]][[/size]]"},
                {L"[[size:9]][[font:Arial]][[/]]x[[/]]",
                 L"[[size:9]][[font:Arial]][[/font]]x[[/size]]"},
            };
            for (const CloseCase &c : cases) {
                WCHAR generic[640], explicitForm[640];
                RingSpec genericRings[kMaxRingsPerRow], explicitRings[kMaxRingsPerRow];
                int genericCount = 0, explicitCount = 0;
                MarkRow(c.generic, Values(), anyS, generic, 640, genericRings, &genericCount,
                        kMaxRingsPerRow);
                MarkRow(c.explicitForm, Values(), anyS, explicitForm, 640, explicitRings,
                        &explicitCount, kMaxRingsPerRow);
                wprintf(L"    %ls\n      -> ", c.generic);
                for (const WCHAR *p = generic; *p; p++) {
                    if (*p >= 0x20 && *p < 0x7F) wprintf(L"%lc", (wint_t)*p);
                    else wprintf(L"{%04X}", (unsigned)*p);
                }
                wprintf(L"\n");
                Check(wcscmp(generic, explicitForm) == 0,
                      "[[/]] closes what the explicit close would have closed");
            }

            // The stray forms. A `[[/]]` with nothing open has no block to close, and the rule is
            // the one the explicit closes already follow: the tag is consumed and writes no marker
            // (it does *not* become literal text -- a well-formed tag with no job has never been
            // printed at the user). Each row is paired with the same text written without the stray
            // tag, which is what "no marker and no text" means. The pairs that also contain an
            // *honoured* close keep it on both sides: only the stray one is being taken out.
            struct StrayCase {
                const WCHAR *withStray;
                const WCHAR *without;
            };
            const StrayCase strays[] = {
                {L"[[/]]abc", L"abc"},
                {L"abc[[/]]def", L"abcdef"},
                {L"[[/]][[/]]abc", L"abc"},
                {L"[[size:24]]a[[/size]]b[[/]]", L"[[size:24]]a[[/size]]b"},
                {L"[[size:24]]a[[/]]b[[/]]c", L"[[size:24]]a[[/]]bc"},
                {L"[[font:Arial]]a[[/font]]b[[/]]", L"[[font:Arial]]a[[/font]]b"},
            };
            for (const StrayCase &c : strays) {
                WCHAR generic[640], asText[640];
                RingSpec ring1[kMaxRingsPerRow], ring2[kMaxRingsPerRow];
                int count1 = 0, count2 = 0;
                MarkRow(c.withStray, Values(), anyS, generic, 640, ring1, &count1,
                        kMaxRingsPerRow);
                MarkRow(c.without, Values(), anyS, asText, 640, ring2, &count2, kMaxRingsPerRow);
                wprintf(L"    stray %ls -> ", c.withStray);
                for (const WCHAR *p = generic; *p; p++) {
                    if (*p >= 0x20 && *p < 0x7F) wprintf(L"%lc", (wint_t)*p);
                    else wprintf(L"{%04X}", (unsigned)*p);
                }
                wprintf(L"\n      without it -> ");
                for (const WCHAR *p = asText; *p; p++) {
                    if (*p >= 0x20 && *p < 0x7F) wprintf(L"%lc", (wint_t)*p);
                    else wprintf(L"{%04X}", (unsigned)*p);
                }
                wprintf(L"\n");
                Check(wcscmp(generic, asText) == 0,
                      "a stray [[/]] is a no-op: no marker, and no text either");
            }

            // The markup that must *not* be recognised. `[[/ ]]` is not the tag (the body is
            // exactly "/", the way `[[left ]]` is not the left tag), and neither is a close with a
            // word in it: strict and documented, no whitespace trimming.
            const WCHAR *notTags[] = {L"[[/ ]]", L"[[ /]]", L"[[/x]]", L"[[//]]", L"[[/size ]]",
                                      L"[[/size", L"[[/"};
            for (const WCHAR *text : notTags) {
                WCHAR marked[640];
                RingSpec ringBuf[kMaxRingsPerRow];
                int ringCount2 = 0;
                MarkRow(text, Values(), anyS, marked, 640, ringBuf, &ringCount2, kMaxRingsPerRow);
                wprintf(L"    not a tag %ls -> ", text);
                for (const WCHAR *p = marked; *p; p++) {
                    if (*p >= 0x20 && *p < 0x7F) wprintf(L"%lc", (wint_t)*p);
                    else wprintf(L"{%04X}", (unsigned)*p);
                }
                wprintf(L"\n");
                Check(MarkedEquals(marked, text),
                      "a tag this code cannot read stays exactly as it was written");
            }

            // A *chain* of closes: each round opens a size run and a font run and then closes both,
            // so every close has a block to take off (six opens in a row would be two blocks and ten
            // strays). This is where a memoiser that resolved a `[[/]]` at the wrong position, or a
            // close that looked the innermost block up by kind instead of taking it off the top,
            // shows up as a missing marker -- both mistakes were made here and both made this case
            // shorter than the explicit one.
            const WCHAR *chainGeneric =
                L"[[size:9]][[font:Arial]][[/]][[/]]"
                L"[[size:9]][[font:Arial]][[/]][[/]]"
                L"[[size:9]][[font:Arial]][[/]][[/]]";
            const WCHAR *chainExplicit =
                L"[[size:9]][[font:Arial]][[/font]][[/size]]"
                L"[[size:9]][[font:Arial]][[/font]][[/size]]"
                L"[[size:9]][[font:Arial]][[/font]][[/size]]";
            {
                WCHAR generic[640], explicitForm[640];
                RingSpec genericRings[kMaxRingsPerRow], explicitRings[kMaxRingsPerRow];
                int genericCount = 0, explicitCount = 0;
                MarkRow(chainGeneric, Values(), anyS, generic, 640, genericRings, &genericCount,
                        kMaxRingsPerRow);
                MarkRow(chainExplicit, Values(), anyS, explicitForm, 640, explicitRings,
                        &explicitCount, kMaxRingsPerRow);
                printf("    3 x (size+font) opens and closes: generic %d char(s), explicit %d\n",
                       (int)wcslen(generic), (int)wcslen(explicitForm));
                if (wcscmp(generic, explicitForm) != 0) {
                    DumpWide("      generic ", generic);
                    DumpWide("      explicit", explicitForm);
                }
                Check(wcscmp(generic, explicitForm) == 0,
                      "a chain of generic closes matches the explicit chain");
                Check(wcschr(generic, L'[') == nullptr,
                      "and no tag text is left on screen in that chain");
            }
        }

        // --- [[/]] across the whole font-size range -------------------------------------------
        //
        // The desugaring runs at compile time and writes *digits* into the size marker, so a
        // one-size sweep would not see a size-dependent mistake. This walks the real range and
        // compares the generic form against the explicit one at every size, which is the property
        // a regression would break.
        {
            bool allSame = true;
            int checkedSizes = 0;
            ModSettings &sizeS = g_gapLayout;
            sizeS = kDefaultSettings;
            sizeS.rowCount = 1;
            for (int sizePx = kMinRowFontSize; sizePx <= kMaxRowFontSize; sizePx++) {
                sizeS.rowFontSize[0] = sizePx;
                sizeS.rowHeightPx[0] = sizePx + 15;
                sizeS.rowMaxFontSize[0] = sizePx;
                WCHAR generic[640], explicitForm[640];
                RingSpec genericRings[kMaxRingsPerRow], explicitRings[kMaxRingsPerRow];
                int genericCount = 0, explicitCount = 0;
                MarkRow(L"a[[size:20]]b[[/]]c", Values(), sizeS, generic, 640, genericRings,
                        &genericCount, kMaxRingsPerRow);
                MarkRow(L"a[[size:20]]b[[/size]]c", Values(), sizeS, explicitForm, 640,
                        explicitRings, &explicitCount, kMaxRingsPerRow);
                if (wcscmp(generic, explicitForm) != 0) allSame = false;
                checkedSizes++;
            }
            printf("    %d font size(s) from %d to %d\n", checkedSizes, kMinRowFontSize,
                   kMaxRowFontSize);
            Check(checkedSizes == kMaxRowFontSize - kMinRowFontSize + 1 && checkedSizes > 1,
                  "the sweep covers the real font-size range, not one size");
            Check(allSame, "the generic close is the explicit one at every font size");
        }

        // --- the zero-cost property: no tag, no new elements, no new markers ---
        {
            const WCHAR plainRow[] = L"CPU 12%  RAM 73%";
            RowProgram plain;
            CompileRowText(s, plainRow, plain);
            Check(plain.partCount == 1 && plain.parts[0].kind == kPartText,
                  "a plain row is still one text element (the new tags cost nothing unconfigured)");
            Check(!ProgramHasKind(plain, kPartGap) && !ProgramHasKind(plain, kPartAlignOpen) &&
                      !ProgramHasKind(plain, kPartAlignClose),
                  "and holds none of the new element kinds");

            WCHAR marked[640];
            RingSpec rings[kMaxRingsPerRow];
            int ringCount = 0;
            MarkRow(plainRow, Values(), s, marked, 640, rings, &ringCount, kMaxRingsPerRow);
            Check(MarkedEquals(marked, plainRow),
                  "which instantiate to exactly the row's own text, with no marker in it");
        }
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
        // The "still fading" sample is placed *inside* the fade by deriving it from the
        // fade's own constants, so it stays inside the window when the span or the speed
        // changes. A hardcoded cue (this used to be 0.62) falls past the end as soon as
        // the fade gets faster, and then the check below quietly becomes trivial.
        const double midCue = kCueContentIn + (kCueContentFull - kCueContentIn) * 0.6;
        g_intro.active = true;
        g_intro.startedAt = GetTickCount() - (DWORD)(midCue * g_intro.durationMs);
        IntroFrame mid = EvaluateIntro();
        g_intro.active = true;
        g_intro.startedAt = GetTickCount() - (DWORD)((kCueContentFull + 0.01) * g_intro.durationMs);
        IntroFrame late = EvaluateIntro();
        printf("  content at cue 0.20 = %.3f, %.3f = %.3f, %.2f = %.3f\n",
               early.contentAlpha, midCue, mid.contentAlpha, kCueContentFull + 0.01, late.contentAlpha);
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

            // (b) The fade's span is the span constant divided by the speed constant, so
            // a further speed change stays a one-constant edit; a hardcoded (rounded) end
            // cue would show up here as a deviation. The tolerance is only there because
            // the span is *measured* as (end - start) and that subtraction does not
            // round-trip bit-exactly; it is 1e-9, i.e. far below any hand-written cue.
            const double window = kCueContentFull - kCueContentIn;
            const double windowError = window - kCueContentSpan / kCueTextFadeSpeed;
            printf("  fade window = %.6f of the intro (%d ms at the default), error %.2e\n",
                   window, (int)(window * 1200 + 0.5), windowError);
            Check(windowError < 1e-9 && windowError > -1e-9,
                  "the fade span is the span constant divided by the speed constant");
            // ... and that span constant is still the *un-scaled* window, i.e. the
            // distance to the shared kCueContentReady the style-1 Fade branch uses.
            // Moving that shared cue now reddens here instead of silently rescaling this
            // fade, which is why the stepped fade got its own end cue (kCueContentFull).
            Check(kCueContentSpan == kCueContentReady - kCueContentIn,
                  "the span constant is still the un-scaled content-fade window");
            // The compression is real: at speed 1.0 the two would coincide.
            Check(kCueContentFull < kCueContentReady,
                  "the compressed fade ends before the un-scaled window does");
            // The release contract, which no relationship can pin: a relational check
            // stays green when the speed itself moves, and "the numbers surface 1.5x
            // sooner" is the user-facing promise of this change (AGENTS 7.6).
            Check(kCueTextFadeSpeed >= 1.5,
                  "the text fade is at least 1.5x faster than the un-scaled window");
            // Every stage sits inside the compressed span and in order: scaling the end
            // cue without moving the stages is the mistake this catches.
            Check(kCueTextFade1 > kCueContentIn && kCueTextFade2 > kCueTextFade1 &&
                  kCueTextFade3 > kCueTextFade2 && kCueTextFade4 > kCueTextFade3 &&
                  kCueContentFull > kCueTextFade4,
                  "the four fade stages stay inside the compressed span, in order");
        }

        // (a) The fade must reach exactly full opacity at its own end cue and must not be
        // full just before it. Both samples are taken on a much longer timeline so
        // GetTickCount's ~16 ms granularity cannot move the cue: on the 1200 ms default
        // that granularity is worth up to 0.013 of the intro, which would land the
        // "just before" sample past the end. The end cue is *derived*
        // (kCueContentFull = kCueContentIn + kCueContentSpan / kCueTextFadeSpeed), so a
        // keyframe table left at its old cues reddens the first of these two.
        {
            const double savedDurationMs = g_intro.durationMs;
            g_intro.durationMs = 100000.0;
            g_intro.active = true;
            g_intro.startedAt =
                GetTickCount() - (DWORD)((kCueContentFull + 0.001) * g_intro.durationMs);
            const IntroFrame atEnd = EvaluateIntro();
            g_intro.active = true;
            g_intro.startedAt =
                GetTickCount() - (DWORD)((kCueContentFull - 0.01) * g_intro.durationMs);
            const IntroFrame beforeEnd = EvaluateIntro();
            printf("  fade alpha at %.4f = %.4f, at %.4f = %.4f\n",
                   kCueContentFull + 0.001, atEnd.contentAlpha,
                   kCueContentFull - 0.01, beforeEnd.contentAlpha);
            Check(atEnd.contentAlpha == 1.0,
                  "the text fade reaches exactly full opacity at its own end cue");
            Check(beforeEnd.contentAlpha < 1.0,
                  "and it is not yet full just before that");
            g_intro.durationMs = savedDurationMs;
        }

        // Collapsing must shrink across the WHOLE duration, not snap to the tab
        // early and then sit there fading. That was the reported bug: the shrink
        // used the grow cue, so it finished at 16% and the rest was a fade.
        //
        // Note the argument order: StartIntro is given (current, target), so a
        // collapse is (expanded, tab) — fromHeight stays the larger value.
        //
        // The collapse timeline is *not* recomputed here from the ratio: StartIntro
        // itself is driven for both directions and the two durations are compared
        // below, so the assertion is about the product's wiring rather than a copy of
        // its expression (AGENTS 7.6/7.13 — the test used to multiply the ratio itself).
        double expandMs = 0.0, collapseMs = 0.0;
        const int drivenIntroMs = 1200;
        {
            const int savedIntroMs = g_settings.introMs;
            const int savedIntroStyle = g_settings.introStyle;
            g_settings.introMs = drivenIntroMs;
            g_settings.introStyle = 0;
            StartIntro(nullptr, from, to);   // to > from: arriving
            expandMs = g_intro.durationMs;
            StartIntro(nullptr, to, from);   // leaving
            collapseMs = g_intro.durationMs;
            g_settings.introMs = savedIntroMs;
            g_settings.introStyle = savedIntroStyle;
            printf("\n  StartIntro: arriving %.0f ms -> leaving %.0f ms (ratio %.2f)\n",
                   expandMs, collapseMs, kCollapseDurationRatio);
        }
        g_intro.active = true;
        g_intro.fromHeight = to;
        g_intro.toHeight = from;
        g_intro.style = 0;
        // The collapse timeline the product just computed; cue 1.0 is then the end.
        g_intro.durationMs = collapseMs;
        printf("\n  collapsing trace (shrink runs the whole collapse duration)\n");
        printf("   cue   height  scale  content\n");
        double lastShrink = to * 2;
        bool shrinkMonotonic = true;
        double atQuarter = 0.0, atMid = 0.0, atLate = 0.0;
        for (int step = 0; step <= 10; step++) {
            double cue = step / 10.0;
            g_intro.startedAt = GetTickCount() - (DWORD)(cue * g_intro.durationMs);
            g_intro.active = true;
            IntroFrame f = EvaluateIntro();
            printf("  %.2f  %7.2f  %.3f  %.3f\n", f.cue, f.height, f.scale, f.contentAlpha);
            if (f.height > lastShrink + 0.01) shrinkMonotonic = false;
            lastShrink = f.height;
            if (step == 2) atQuarter = f.height;
            if (step == 5) atMid = f.height;
            if (step == 9) atLate = f.height;
        }
        printf("  height at cue 0.25 = %.1f, 0.50 = %.1f, 0.90 = %.1f "
               "(tab is %.0f, expanded is %.0f)\n", atQuarter, atMid, atLate, from, to);
        Check(shrinkMonotonic, "collapse height only ever shrinks");
        // At the quarter point it must be genuinely between the two, proving the
        // shrink is spread over the duration rather than completed up front.
        Check(atQuarter < to && atQuarter > from, "collapse is still shrinking at the quarter point");
        // ... and it must still have work left at the midpoint. It used to be finished
        // there, because kCollapseDurationRatio (a *duration* ratio) was used as a *cue*
        // on a timeline that had already been shortened: the panel then sat at tab height
        // for the rest while the frame clock kept repainting it, so the visible collapse
        // was IntroMs/4 back when the ratio was still 1/2 (a sixteenth of the intro if
        // the same mistake were made now).
        Check(atMid < to - 1.0 && atMid > from,
              "the shrink still has work left at the midpoint");
        Check(atLate < atMid && atLate > from - 0.01,
              "and it keeps shrinking past the midpoint without passing the tab");

        g_intro.active = true;
        g_intro.fromHeight = to;
        g_intro.toHeight = from;
        g_intro.durationMs = collapseMs;
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
        g_intro.durationMs = collapseMs;
        g_intro.startedAt = GetTickCount();
        IntroFrame justStarted = EvaluateIntro();
        printf("  collapse first frame height = %.2f (expect ~%.0f)\n",
               justStarted.height, to);
        Check(justStarted.height > to * 0.9, "collapse starts from the expanded height");

        // A collapse must take the configured fraction of the arriving duration, asserted
        // as a relationship between the two timelines the product's own StartIntro
        // produced (so it keeps holding if the default intro duration changes, and it
        // catches the ratio being applied to the wrong direction or dropped entirely).
        Check(expandMs == (double)drivenIntroMs,
              "an expansion runs the whole configured duration");
        Check(collapseMs == expandMs * kCollapseDurationRatio,
              "the collapse timeline is the configured ratio of the intro");
        // Plus the release contract itself, which no relationship can pin: a relational
        // check stays green when the ratio moves, and "closing takes a quarter of the
        // intro" (twice as fast as the half it used to be) is the user-facing promise.
        Check(kCollapseDurationRatio == 0.25,
              "the collapse timeline is a quarter of the intro");
        printf("  arriving %d ms -> leaving %d ms\n", (int)expandMs, (int)collapseMs);

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

    printf("\n=== inline icon images: one cache entry per file, bounded ===\n");
    {
        // The image cache used to be one slot per settings row (Icon1..Icon4), which cannot
        // express an inline icon: one row can show a file twice, and two rows can show it too,
        // and both must share one decode. It is a small fixed table keyed by the resolved path
        // now, with a stated eviction rule -- an unused entry first, else the least recently
        // used one, whose images are disposed before it is reused.
        //
        // Only the bookkeeping is exercised here: IconCachePick allocates nothing and
        // ReleaseIconEntry disposes nothing while the entry is empty, so this runs without
        // GDI+ -- which is also why the eviction rule can be asserted at all.
        DisposeIconImage();
        Check(kIconCacheSlots >= 2, "the icon image cache holds more than one file");

        WCHAR names[kIconCacheSlots + 1][64];
        for (int i = 0; i <= kIconCacheSlots; i++) {
            swprintf(names[i], 64, L"C:\\pics\\icon%d.png", i);
        }

        const int first = IconCachePick(names[0]);
        printf("    %d slot(s); \"%ls\" -> %d\n", kIconCacheSlots, names[0], first);
        Check(first >= 0 && first < kIconCacheSlots, "a new file takes a slot inside the table");
        Check(IconCacheFind(names[0]) == first, "and the entry records the path it belongs to");
        Check(IconCachePick(names[0]) == first, "asking for the same path again is the same slot");

        // Windows paths are case-insensitive, so two spellings of one file must not decode it
        // twice: the lookup folds case.
        WCHAR shouted[64];
        wcscpy(shouted, names[0]);
        for (WCHAR *c = shouted; *c; c++) {
            if (*c >= L'a' && *c <= L'z') *c = (WCHAR)(*c - L'a' + L'A');
        }
        Check(IconCacheFind(shouted) == first, "the path lookup folds case, like the filesystem");

        // Fill the table. Every distinct path gets its own entry until the table is full.
        int used = 1;
        bool distinct = true;
        for (int i = 1; i < kIconCacheSlots; i++) {
            const int slot = IconCachePick(names[i]);
            if (IconCacheFind(names[i]) != slot) distinct = false;
            if (slot == first) distinct = false;
            used++;
        }
        Check(distinct, "every distinct file gets its own entry while there is room");
        Check(used == kIconCacheSlots, "and the table is now full");
        Check(IconCacheFind(names[0]) == first && IconCacheFind(names[1]) == 1,
              "every file is still findable once the table is full");
        Check(IconCacheFind(names[kIconCacheSlots]) < 0, "a file that was never asked for is not");

        // The eviction rule, isolated: touching entry 0 makes entry 1 the least recently used
        // one, so the next new file has to take entry 1's slot and not entry 0's.
        printf("    touching \"%ls\", then asking for \"%ls\"\n", names[0],
               names[kIconCacheSlots]);
        IconCachePick(names[0]);
        const int evicted = IconCachePick(names[kIconCacheSlots]);
        printf("    evicted slot %d (entry 1 was the least recently used)\n", evicted);
        Check(evicted != first, "the most recently used entry is the one that survives");
        Check(IconCacheFind(names[1]) < 0, "the least recently used entry is the one evicted");
        Check(IconCacheFind(names[0]) == first, "and the entry that was touched is still there");
        Check(IconCacheFind(names[kIconCacheSlots]) == evicted,
              "the new file took the evicted entry's slot");

        // The bound is the point of the table: however many files a panel names, the number of
        // live entries never grows past the table.
        int live = 0;
        for (int i = 0; i < kIconCacheSlots; i++) {
            if (g_iconCache[i].used) live++;
        }
        Check(live <= kIconCacheSlots, "the cache never holds more files than it has slots");

        // Release everything, so a later section cannot see this state.
        DisposeIconImage();
        int afterRelease = 0;
        for (int i = 0; i < kIconCacheSlots; i++) {
            if (g_iconCache[i].used) afterRelease++;
        }
        Check(afterRelease == 0, "disposing the icon images empties every entry");
        Check(IconCacheFind(names[0]) < 0, "so nothing is remembered after a settings change");
    }

    printf("\n=== custom icon resampling plan ===\n");
    {
        // A loaded file has to be brought down to the icon's own pixel size. Two things must
        // hold:
        //
        //   * a large source is reduced in several steps, not one. A single big
        //     reduction makes GDI+ sample only a few source pixels (aliasing).
        //   * nothing is ever scaled up past its natural size.
        struct Case { int w, h, target; };
        const Case cases[] = {
            {4000, 4000, 23},    // extreme: a photo used as an icon
            {512,  512,  23},
            {256,  128,  23},    // non-square
            {200,  20,   23},    // a banner: the short axis is already under the plate
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
                // The short side must be what the fit gives, rounded by at most a pixel.
                // A percentage drift cannot say that for a 200:1 banner, whose short side
                // is 1 or 2 px either way (the previous 12%-drift check failed on it).
                const int srcLong = c.w > c.h ? c.w : c.h;
                const int srcShort = c.w > c.h ? c.h : c.w;
                const int lastShort = lastW < lastH ? lastW : lastH;
                const double exactShort = (double)srcShort * (double)c.target / (double)srcLong;
                printf("    short side %d, exact fit %.2f\n", lastShort, exactShort);
                Check(lastShort >= 1 && (double)lastShort <= exactShort + 1.0,
                      "the short side is the exact fit, rounded up by at most a pixel");
                if (exactShort >= 4.0) {
                    // Where the short side is several pixels, the ratio has to hold to
                    // within rounding too.
                    double srcAspect = (double)c.w / c.h;
                    double dstAspect = (double)lastW / lastH;
                    double drift = srcAspect > dstAspect ? srcAspect / dstAspect
                                                         : dstAspect / srcAspect;
                    Check(drift < 1.12, "aspect ratio is preserved where rounding cannot dominate");
                }
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

        // The plan's whole point is that no single stage is a large reduction: with one
        // big step GDI+ samples only a few source pixels, which is the aliasing this
        // exists to prevent. The banner is where that used to fail -- the proportional
        // clamp had a fixed point, so 200x20 spent twelve stages resampling itself 1:1
        // and then one 8.7x squash (4000x20 ended in a single 173x step).
        for (const Case &c : cases) {
            if (c.w <= c.target && c.h <= c.target) continue;
            DownscalePlan plan = PlanDownscale(c.w, c.h, c.target);
            const bool landscape = c.w >= c.h;
            double worstLong = 1.0, worstShort = 1.0;
            int pw = c.w, ph = c.h;
            for (int i = 0; i < plan.count; i++) {
                double rw = (double)pw / plan.sizes[i].w;
                double rh = (double)ph / plan.sizes[i].h;
                double rl = landscape ? rw : rh;
                double rs = landscape ? rh : rw;
                if (rl > worstLong) worstLong = rl;
                if (rs > worstShort) worstShort = rs;
                pw = plan.sizes[i].w;
                ph = plan.sizes[i].h;
            }
            printf("  %4dx%-4d worst step: long %.2fx, short %.2fx\n", c.w, c.h, worstLong,
                   worstShort);
            Check(worstLong <= 2.05, "no stage reduces the long edge by more than 2x");
            // Integer halving truncates: an odd 125 px edge goes to 62, which is a
            // 2.016x step rather than 2.0x, so the bar is 2.05x and not 2.0x.
            // The short edge is floored at 1 px on a very wide source, so it may round
            // its way down a little faster; it must still never be squashed in one go.
            Check(worstShort <= 2.51, "and the short edge is floored, never squashed");
        }

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

        // The box handed to DrawString and the trim budget share one floor, so the two
        // cannot disagree about how little room is too little. Asked through the product
        // function rather than by copying the expression (the old assertion here only
        // checked the constant, and the GDI+ section copied the expression itself).
        Check(LayoutWidthFloor(0.0f) == kMinLayoutWidth, "a zero-width box is raised to the floor");
        Check(LayoutWidthFloor(-5.0f) == kMinLayoutWidth, "so is a negative one");
        Check(LayoutWidthFloor(kMinLayoutWidth / 2.0f) == kMinLayoutWidth,
              "and anything under the floor");
        Check(LayoutWidthFloor(120.0f) == 120.0f, "a usable width is left alone");
        Check(LayoutWidthFloor(kMinLayoutWidth) == kMinLayoutWidth,
              "the floor itself passes through unchanged");

        // The PDH rule the CPU path and the GPU array readers now share. Accepting only
        // ERROR_SUCCESS (as the GPU readers did) drops a counter that has just been
        // created: its first sample is NEW_DATA, not VALID_DATA.
        Check(PdhValueUsable(PDH_CSTATUS_VALID_DATA), "a PDH value with valid data is used");
        Check(PdhValueUsable(PDH_CSTATUS_NEW_DATA),
              "and so is a counter's first sample (new data)");
        Check(!PdhValueUsable((PDH_STATUS)0x800007D2), "more-data is not a value");
        Check(!PdhValueUsable((PDH_STATUS)0xC0000BBA), "an invalid-data status is rejected");
        Check(!PdhValueUsable((PDH_STATUS)0xC0000BC6), "and so is a missing instance");
    }

    printf("\n=== icon image size follows the intro scale ===\n");
    {
        // A cached image is already exactly plate-sized, so the old branch (which only
        // applied the intro scale to images *larger* than the plate) never scaled it: a
        // custom icon faded in while its plate grew.
        float w = 0.0f, h = 0.0f;
        const float plateRadius = 11.5f;     // a 23 px plate, the default font's size
        ComputeIconImageDrawSize(23, 23, plateRadius, 1.0f, &w, &h);
        Check(w == 23.0f && h == 23.0f, "a plate-sized image draws at plate size");
        ComputeIconImageDrawSize(23, 23, plateRadius, 0.5f, &w, &h);
        Check(w == 11.5f && h == 11.5f, "and follows the intro as it scales the plate in");
        ComputeIconImageDrawSize(23, 23, plateRadius, 0.0f, &w, &h);
        Check(w == 0.0f && h == 0.0f, "at zero scale it draws at zero size");

        // A source larger than the plate is capped by the plate, keeping its aspect, and
        // the animation multiplies that cap.
        ComputeIconImageDrawSize(400, 200, plateRadius, 1.0f, &w, &h);
        Check(w == 23.0f && h == 11.5f, "a large source is capped at the plate, aspect kept");
        ComputeIconImageDrawSize(400, 200, plateRadius, 0.25f, &w, &h);
        Check(w == 5.75f && h == 2.875f, "and the intro scales the capped size too");

        // Smaller than the plate: never upsampled to fill it, but the animation still
        // shrinks it -- the cap is a ceiling, not a fixed size.
        ComputeIconImageDrawSize(10, 10, plateRadius, 1.0f, &w, &h);
        Check(w == 10.0f && h == 10.0f, "a source smaller than the plate is not blown up");
        ComputeIconImageDrawSize(10, 10, plateRadius, 0.5f, &w, &h);
        Check(w == 5.0f && h == 5.0f, "while the intro still scales it down");

        // The sampler join decides whether the values lock may be deleted. Only a real
        // join counts: deleting a critical section a live thread can still be inside is
        // undefined behaviour, which is exactly what the ignored timeout did.
        Check(ValuesLockCanBeReleased(WAIT_OBJECT_0),
              "the values lock is released once the sampler is joined");
        Check(!ValuesLockCanBeReleased(WAIT_TIMEOUT),
              "a sampler that did not stop leaves its lock alone");
        Check(!ValuesLockCanBeReleased(WAIT_FAILED), "and so does a failed wait");
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

    // The budget an inline run is trimmed against is a different question: the run
    // starts where it starts, so its limit has to count the gauges that still follow
    // it. It used to be handed the run's own measured width as its budget, and
    // FitTextToWidth can never exceed that (its first test is "does it already fit?"),
    // so a row using [[ring:...]] was never truncated at all: the text ran to the bitmap
    // edge with no ellipsis and pushed its own gauge off the panel.
    {
        const float right = inl.textRight;      // the row's right inset
        const float cursor = 12.0f;             // where a run at the left edge starts
        Check(FlowTextLimit(right, cursor, 0, 26) == right - cursor,
              "an inline run with no gauge after it may use the whole row");
        Check(FlowTextLimit(right, cursor, 1, 26) == right - cursor - (26.0f + 4.0f),
              "one gauge after the run is reserved out of its budget");
        Check(FlowTextLimit(right, cursor, 2, 26) <
                  FlowTextLimit(right, cursor, 1, 26),
              "each further gauge costs the run more");
        // The relation, not the arithmetic: run + the gauges that follow it = the row.
        Check(FlowTextLimit(right, cursor, 2, 26) + cursor + 2.0f * (26.0f + 4.0f) ==
                  right,
              "the run plus the gauges after it exactly fill the row");
        Check(FlowTextLimit(right, 500.0f, 1, 26) == 0.0f,
              "a run already past the right inset gets nothing, never a negative budget");
        // The parked path asks the same arithmetic with the column's right edge. Passing the
        // column's *width* where an edge belongs used to be the shape of this call, and it
        // collapsed every parked run's budget to the floor -- which draws nothing at all.
        RowTextMetrics parkedColumn = ComputeRowTextMetrics(420.0f, 0, 26, 12, false);
        Check(ComputeParkedRunLimit(parkedColumn.textWidth, cursor, 0, 26) ==
                  parkedColumn.textRight - cursor,
              "a parked run gets the same column, measured from its own cursor");
    }

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

        // Iterate the product's own registry rather than a hand-written copy of it. A
        // variable added to kVariables[] used to be invisible here -- the list below was
        // a second copy that nothing compared against the first, so registering a
        // variable without implementing it left every assertion green, which is the
        // opposite of what AGENTS 9 claims.
        for (int i = 0; i < kVariableCount; i++) {
            const WCHAR *token = kVariables[i].name;
            WCHAR row[128];
            swprintf(row, 128, L"[{%ls}]", token);
            WCHAR marked[640];
            RingSpec rings[kMaxRingsPerRow];
            int ringCount = 0;
            MarkRow(row, t, g_settings, marked, 640, rings, &ringCount, kMaxRingsPerRow);
            // The expansion must not still contain a brace.
            bool resolved = wcschr(marked, L'{') == nullptr;
            if (!resolved) printf("    {%ls} -> \"%ls\"\n", token, marked);
            Check(resolved, "every registered variable resolves (no leftover braces)");
            // A registration with no group would never be collected, so the token would
            // render a value that is always zero.
            Check(kVariables[i].group != G_NONE, "every registered variable names a collector");
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
            // The same rule for the fields the PDH fallback cannot read at all. They had
            // no validity flag, so the panel invented a reading: "0 %" of fan, "0.0" GB
            // of VRAM. The whole point of gpuTempValid is that "cannot read it" and
            // "reads zero" are different things.
            {L"{gpu.fan}", L"--"},
            {L"{gpu.power}", L"--"},
            {L"{gpu.vram.used}", L"--"},
            {L"{gpu.vram.total}", L"--"},
            {L"F={gpu.fan}rpm", L"F=--rpm"},
        };
        for (const Expect &e : expects) {
            WCHAR marked[640];
            RingSpec rings[kMaxRingsPerRow];
            int ringCount = 0;
            MarkRow(e.row, g, g_settings, marked, 640, rings, &ringCount, kMaxRingsPerRow);
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
        // A gauge that cannot be built used to be consumed and dropped, so the text simply
        // vanished -- and the vanished text was the only clue about what the row asked for.
        // ParseRing's contract has always been "the caller falls back to the raw text"; this
        // is that fallback, and the malformed-ring row is what pins it.
        {L"T=[[ring:nonsense]]C", 0, false, L"T=[[ring:nonsense]]C"},
    };

    for (const Case &c : cases) {
        WCHAR marked[640];
        RingSpec rings[kMaxRingsPerRow];
        int ringCount = 0;
        MarkRow(c.row, v, g_settings, marked, 640, rings, &ringCount, kMaxRingsPerRow);

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

    printf("\n=== row programs: the markup is compiled at settings time ===\n");
    {
        // The row pipeline is two halves now. CompileRowText reads the markup once, while the
        // settings are read; InstantiateRowProgram only fills the {tokens} in with the values of
        // one publish. Everything below is about the first half, because that is the half that
        // did not exist: what a program records, that it records the same thing twice, and that
        // instantiating it produces the marked string the old expander produced -- written out
        // literally, so the expectation is not the old code asking itself.
        LOCAL_SETTINGS(s, g_programCompile);
        s.fontSize = 15;
        s.rowHeight = 30;
        s.ringSize = 26;
        s.ringThickness = 4;
        s.rowCount = 1;
        // One registered chain, so [[font:Arial]] resolves without consulting the installed
        // fonts: CompileRowText only *looks up* a chain, it never re-validates a family name.
        wcscpy(s.tagFonts[0].key, L"Arial");
        wcscpy(s.tagFonts[0].chain.names[0], L"Arial");
        s.tagFonts[0].chain.count = 1;
        s.tagFontCount = 1;

        printf("    ModSettings=%d B, row program=%d B x %d rows\n", (int)sizeof(ModSettings),
               (int)sizeof(RowProgram), kMaxRows);
        Check(sizeof(ModSettings) < 64 * 1024,
              "a settings snapshot stays small enough to copy once per sample");

        // --- the zero-cost shape: an untagged row is one template, nothing else ---
        const WCHAR plainRow[] = L"CPU 12%  RAM 73%";
        RowProgram plain;
        CompileRowText(s, plainRow, plain);
        printf("    \"%ls\" -> %d element(s)\n", plainRow, plain.partCount);
        Check(plain.partCount == 1, "an untagged row compiles to exactly one element");
        Check(plain.first == 0 && plain.parts[0].kind == kPartText, "and that element is TEXT");
        Check(plain.parts[0].offset == 0 && plain.parts[0].length == (int)wcslen(plainRow),
              "carrying the whole row as its template");
        Check(plain.parts[0].next == -1, "with nothing after it");
        Check(plain.parts[0].tagChain == 0 && plain.parts[0].runFontSize == 0,
              "and neither a font chain nor a run size in force");

        // --- compiling is not instantiating: {tokens} are values, so they are not compiled ---
        const WCHAR tokenRow[] = L"{cpu}%  T={gpu.temp}  {ext.music.title}";
        RowProgram tokenProgram;
        CompileRowText(s, tokenRow, tokenProgram);
        WCHAR templateText[256];
        LinearTemplate(tokenProgram, tokenRow, templateText, 256);
        wprintf(L"    \"%ls\" -> template \"%ls\"\n", tokenRow, templateText);
        Check(wcscmp(templateText, tokenRow) == 0,
              "compiling leaves every {token} in the template: it expands nothing");

        // --- the same row compiles the same way, and the comparison is not blind ---
        RowProgram again;
        CompileRowText(s, tokenRow, again);
        Check(RowProgramEqual(tokenProgram, again),
              "compiling the same row twice produces the same program");
        RowProgram mutated = again;
        mutated.parts[0].tagChain = 1;
        Check(!RowProgramEqual(tokenProgram, mutated),
              "and that comparison notices one changed field (it can fail)");

        // --- what a [[font:...]] / [[size:NN]] / [[/size]] run records ---
        const WCHAR fontRow[] = L"A[[font:Arial]]B[[/font]]C";
        RowProgram fontProgram;
        CompileRowText(s, fontRow, fontProgram);
        const RowPart *f0 = ProgramElement(fontProgram, 0);
        const RowPart *f1 = ProgramElement(fontProgram, 1);
        const RowPart *f2 = ProgramElement(fontProgram, 2);
        const RowPart *f3 = ProgramElement(fontProgram, 3);
        const RowPart *f4 = ProgramElement(fontProgram, 4);
        Check(f0 && f0->kind == kPartText && f0->tagChain == 0,
              "text before [[font:...]] is in the main chain");
        Check(f1 && f1->kind == kPartFontOpen && f1->tagChain == 1,
              "[[font:Arial]] records its registered chain");
        Check(f2 && f2->kind == kPartText && f2->tagChain == 1,
              "and the run it opens carries that chain");
        Check(f3 && f3->kind == kPartFontClose, "[[/font]] closes the run");
        Check(f4 && f4->kind == kPartText && f4->tagChain == 0,
              "so the text after it is back in the main chain");

        const WCHAR sizeRow[] = L"A[[size:24]]B[[/size]]C";
        RowProgram sizeProgram;
        CompileRowText(s, sizeRow, sizeProgram);
        const RowPart *z0 = ProgramElement(sizeProgram, 0);
        const RowPart *z1 = ProgramElement(sizeProgram, 1);
        const RowPart *z2 = ProgramElement(sizeProgram, 2);
        const RowPart *z3 = ProgramElement(sizeProgram, 3);
        const RowPart *z4 = ProgramElement(sizeProgram, 4);
        Check(z0 && z0->kind == kPartText && z0->runFontSize == 0,
              "text before [[size:NN]] is drawn at the row's own size");
        Check(z1 && z1->kind == kPartSizeOpen && z1->runFontSize == 24,
              "[[size:24]] records the run's size");
        Check(z2 && z2->kind == kPartText && z2->runFontSize == 24,
              "and the run it opens carries it");
        Check(z3 && z3->kind == kPartSizeClose, "[[/size]] closes the run");
        Check(z4 && z4->kind == kPartText && z4->runFontSize == 0,
              "so the text after it is back at the row's size");

        // --- a tag that cannot be honoured is ordinary text, whole ---
        const WCHAR badFontRow[] = L"[[font:NoSuchFamilyQZX]]x";
        RowProgram badFont;
        CompileRowText(s, badFontRow, badFont);
        WCHAR badFontTemplate[256];
        LinearTemplate(badFont, badFontRow, badFontTemplate, 256);
        Check(badFont.partCount == 1 && badFont.parts[0].kind == kPartText,
              "an unregistered [[font:...]] is not markup at all");
        Check(wcscmp(badFontTemplate, badFontRow) == 0,
              "so it stays in the template character for character");

        const WCHAR badSizeRow[] = L"x[[size:abc]]y";
        RowProgram badSize;
        CompileRowText(s, badSizeRow, badSize);
        WCHAR badSizeTemplate[256];
        LinearTemplate(badSize, badSizeRow, badSizeTemplate, 256);
        Check(badSize.partCount == 1, "a malformed [[size:NN]] is not markup either");
        Check(wcscmp(badSizeTemplate, badSizeRow) == 0, "and it stays literal too");

        // --- the recorded attributes are the ones rendering derives from the markers ---
        // Walking a program's TEXT elements next to BuildRowLayout's segments: if the two ever
        // disagreed, a run would be marked as one thing and recorded as another, and the next
        // consumer of the program (icons, alignment) would inherit the wrong answer.
        {
            const WCHAR mixed[] = L"A[[font:Arial]]B[[size:24]]C[[/size]]D[[/font]]E";
            RowProgram mixedProgram;
            CompileRowText(s, mixed, mixedProgram);
            WCHAR marked[640];
            RingSpec rings[kMaxRingsPerRow];
            int ringCount = 0;
            MarkRow(mixed, Values(), s, marked, 640, rings, &ringCount, kMaxRingsPerRow);
            RowState state = {};
            BuildRowLayout(marked, ringCount, state, 0);

            int segment = 0;
            bool attributesMatch = true;
            bool slicesMatch = true;
            for (int i = 0;; i++) {
                const RowPart *part = ProgramElement(mixedProgram, i);
                if (!part) break;
                if (part->kind != kPartText) continue;
                if (segment >= state.segmentCount[0]) {
                    attributesMatch = false;
                    break;
                }
                const TextSeg &seg = state.segments[0][segment];
                if (seg.tagChain != part->tagChain || seg.runFontSize != part->runFontSize) {
                    attributesMatch = false;
                }
                if (seg.length != part->length ||
                    wcsncmp(state.text[0] + seg.offset, mixed + part->offset, part->length) != 0) {
                    slicesMatch = false;
                }
                segment++;
            }
            printf("    %d TEXT element(s) vs %d segment(s)\n", segment, state.segmentCount[0]);
            Check(attributesMatch && segment == state.segmentCount[0],
                  "every TEXT element carries the chain and size BuildRowLayout derived");
            Check(slicesMatch, "and its template slice is exactly that segment's text");
        }

        // --- end to end: the marked string and the ring count, spelled out ---
        //
        // The markers are the contract between instantiation and BuildRowLayout, so the
        // expectations are literal: E000 <digit> E001 wraps a gauge (the digit is its index),
        // E002 <digit> E001 opens a font run and E003 closes it, E004 <two digits> E001 opens a
        // size run and E005 closes it. The malformed rows expect their own text back, which is
        // the tolerance rule seen from this side.
        {
            Values v;
            v.cpu = 12;
            v.memPercent = 73;
            struct InstantiateCase {
                const WCHAR *raw;
                const WCHAR *expect;
                int expectRings;
            };
            const InstantiateCase cases[] = {
                {L"CPU 12%  RAM 73%", L"CPU 12%  RAM 73%", 0},
                {L"cpu {cpu}%  mem {mem}%", L"cpu 12%  mem 73%", 0},
                {L"x [[ring:50]] y", L"x \xE000" L"0" L"\xE001" L" y", 1},
                {L"[[ring:10]][[ring:20]]",
                 L"\xE000" L"0" L"\xE001" L"\xE000" L"1" L"\xE001", 2},
                {L"T=[[ring:nonsense]]C", L"T=[[ring:nonsense]]C", 0},
                {L"A[[font:Arial]]B[[/font]]C",
                 L"A\xE002" L"1" L"\xE001" L"B\xE003" L"C", 0},
                {L"A[[size:24]]B[[/size]]C",
                 L"A\xE004" L"24" L"\xE001" L"B\xE005" L"C", 0},
                {L"x[[size:abc]]y", L"x[[size:abc]]y", 0},
                {L"[[font:Arial]]c {cpu} [[ring:50]]",
                 L"\xE002" L"1" L"\xE001" L"c 12 \xE000" L"0" L"\xE001", 1},
            };
            for (const InstantiateCase &c : cases) {
                WCHAR marked[640];
                RingSpec rings[kMaxRingsPerRow];
                int ringCount = 0;
                MarkRow(c.raw, v, s, marked, 640, rings, &ringCount, kMaxRingsPerRow);
                wprintf(L"    \"%ls\"\n", c.raw);
                printf("      rings=%d (expect %d)\n", ringCount, c.expectRings);
                Check(ringCount == c.expectRings, "instantiated ring count");
                Check(wcscmp(marked, c.expect) == 0, "instantiated marked string, markers and all");
            }
        }

        // --- the compile step is ReadSettings', and it agrees with the cost gate ---
        {
            const StubSetting rows[] = {
                {L"Row1", L"CPU {cpu}%"},
                {L"Row2", L"[[font:Arial]]A[[/font]][[size:20]]B[[/size]][[ring:{cpu}]]"},
            };
            g_stubStrings = rows;
            g_stubStringCount = 2;
            g_stubInts = nullptr;
            g_stubIntCount = 0;
            EnumerateFonts();     // the tag chain has to survive the installed-name filter
            LOCAL_SETTINGS(rs, g_programRead);
            ReadSettings(rs);
            Check(rs.rowCount == 2, "both rows were read");
            Check(rs.rowProgram[0].partCount == 1 &&
                      rs.rowProgram[0].parts[0].kind == kPartText,
                  "ReadSettings compiles the plain row into a single template");
            Check(!rs.rowFontTag[0] && !ProgramHasKind(rs.rowProgram[0], kPartFontOpen),
                  "no font tag: the row is off the multi-font path and has no font element");
            Check((rs.rowFontTag[1] != 0) == ProgramHasKind(rs.rowProgram[1], kPartFontOpen),
                  "a font tag puts it on that path, and its program agrees");
            Check(ProgramHasKind(rs.rowProgram[1], kPartRing) &&
                      ProgramHasKind(rs.rowProgram[1], kPartSizeOpen),
                  "and the same program carries the gauge and the size run");
            g_stubStrings = nullptr;
            g_stubStringCount = 0;
        }
    }

    printf("\n=== font chains and the multi-font cost gate ===\n");
    {
        // The feature is two settings-shaped pieces of data (a global candidate list and one
        // chain per [[font:...]] tag) plus a rule about when neither is consulted. The rule is
        // the part worth the most: an unconfigured panel must do exactly what it did before
        // the feature existed, and that is asserted here rather than assumed.
        g_stubStrings = nullptr;
        g_stubStringCount = 0;
        g_stubInts = nullptr;
        g_stubIntCount = 0;
        // Mirrors WhTool_ModInit: the installed list has to exist before any name is
        // validated, or every family would look uninstalled and the filter would hide bugs.
        EnumerateFonts();

        // --- the gate itself ---
        printf("  tag/no-tag x global-list/no-global-list\n");
        Check(!RowNeedsFontPath(false, false),
              "a row with no tag and no global list needs no multi-font path");
        Check(RowNeedsFontPath(true, false), "a tagged row does need it");
        Check(RowNeedsFontPath(false, true), "and so does any row once a global list exists");
        Check(RowNeedsFontPath(true, true), "both together do too");

        // --- the list parser ---
        struct ChainCase {
            const WCHAR *text;
            int expectCount;
            int expectRejected;
            const WCHAR *expectKey;
        };
        const ChainCase chains[] = {
            {L"Consolas; Arial", 2, 0, L"Consolas;Arial"},
            {L"  Consolas  ;\tArial\t;", 2, 0, L"Consolas;Arial"},
            {L"Consolas;;   ;consolas", 1, 0, L"Consolas"},
            {L"", 0, 0, L""},
            {L"   ;  ;\t", 0, 0, L""},
            {L"A;B;C;D", 4, 0, L"A;B;C;D"},
            {L"A;B;C;D;E;F", 4, 2, L"A;B;C;D"},
            {L"this family name is far too long to be a real one at all", 0, 1, L""},
        };
        for (const ChainCase &c : chains) {
            FontChain chain;
            int rejected = 0;
            ParseFontChainList(c.text, c.text + wcslen(c.text), chain, &rejected);
            WCHAR key[kMaxFontKeyChars];
            FontChainKey(chain, key, kMaxFontKeyChars);
            wprintf(L"    \"%ls\" -> %d name(s), %d rejected, key=\"%ls\"\n", c.text, chain.count,
                    rejected, key);
            Check(chain.count == c.expectCount, "chain length");
            Check(rejected == c.expectRejected, "surplus/unusable entries are counted, not hidden");
            Check(wcscmp(key, c.expectKey) == 0, "canonical key");
        }

        // --- installed-name filtering ---
        {
            FontChain mixed;
            const WCHAR *text = L"Consolas; NoSuchFamilyQZX";
            ParseFontChainList(text, text + wcslen(text), mixed, nullptr);
            int dropped = 0;
            FilterChainToInstalled(mixed, &dropped);
            wprintf(L"    \"%ls\" -> %d kept, %d dropped\n", text, mixed.count, dropped);
            Check(mixed.count == 1 && _wcsicmp(mixed.names[0], L"Consolas") == 0,
                  "an uninstalled family is dropped, the rest survives");
            Check(dropped == 1, "and it is reported as dropped");

            FontChain bogus;
            const WCHAR *bogusText = L"NoSuchFamilyQZX;AlsoMissingQZX";
            ParseFontChainList(bogusText, bogusText + wcslen(bogusText), bogus, nullptr);
            FilterChainToInstalled(bogus, nullptr);
            Check(bogus.count == 0, "a chain of nothing but typos is empty (the tag stays visible)");
        }

        // --- tag collection from a raw row ---
        struct TagCase {
            const WCHAR *row;
            int expectHonoured;
            int expectChains;
            const WCHAR *expectKey;
        };
        const TagCase tags[] = {
            {L"[[font:Consolas]]CPU[[/font]]", 1, 1, L"Consolas"},
            {L"[[font:Consolas]]A[[/font]] [[font:Consolas]]B[[/font]]", 2, 1, L"Consolas"},
            {L"[[font:Consolas; Arial]]x[[/font]]", 1, 1, L"Consolas;Arial"},
            {L"[[font:Consolas]]unclosed run to the end", 1, 1, L"Consolas"},
            {L"[[font:]]x[[/font]]", 0, 0, L""},
            {L"[[font:NoSuchFamilyQZX]]x[[/font]]", 0, 0, L""},
            {L"[[fonts:Consolas]]x[[/fonts]]", 0, 0, L""},   // a typo is not a tag
            {L"[[font:Consolas]x[[/font]]", 0, 0, L""},      // no closing brackets
            {L"[[font:]]", 0, 0, L""},
        };
        for (const TagCase &c : tags) {
            FontTag table[kMaxFontChains];
            int count = 0;
            int rejected = 0;
            const int honoured = CollectRowFontTags(c.row, table, &count, &rejected);
            wprintf(L"    \"%ls\" -> honoured=%d chains=%d rejected=%d\n", c.row, honoured, count,
                    rejected);
            Check(honoured == c.expectHonoured, "tags honoured");
            Check(count == c.expectChains, "distinct chains registered");
            if (c.expectChains > 0) {
                Check(wcscmp(table[0].key, c.expectKey) == 0, "registered key");
            }
        }

        // More distinct chains than the table holds: the surplus tags stay visible as text.
        {
            FontTag table[kMaxFontChains];
            int count = 0;
            int rejected = 0;
            const WCHAR *many = L"[[font:Consolas]]a[[/font]][[font:Arial]]b[[/font]]"
                                L"[[font:Consolas;Arial]]c[[/font]][[font:Arial;Consolas]]d[[/font]]"
                                L"[[font:Courier New]]e[[/font]][[font:Tahoma]]f[[/font]]"
                                L"[[font:Verdana]]g[[/font]]";
            const int honoured = CollectRowFontTags(many, table, &count, &rejected);
            printf("    seven distinct tags -> honoured=%d chains=%d rejected=%d\n", honoured,
                   count, rejected);
            Check(count == kMaxFontChains, "the chain table stops at its documented limit");
            Check(rejected == 1, "and the tag that did not fit is counted");
        }

        // --- which family a run is drawn in ---
        //
        // The coverage question is injected, so every branch is reachable without a font, and
        // the counter is what makes the performance promise assertable: the *number* of
        // coverage questions is part of the contract.
        g_fakeQueries = 0;

        const FontChain latin = MakeChain(L"LatinOnly", nullptr);
        const FontChain everything = MakeChain(L"Everything", nullptr);
        const FontChain two = MakeChain(L"LatinOnly", L"Everything");
        const FontChain neither = MakeChain(L"LatinOnly", L"NothingHere");
        const FontChain noChain;

        Check(!PickFontFamily(nullptr, 0, L"LatinOnly", noChain, noChain, FakeCovers) &&
                  g_fakeQueries == 0,
              "an empty run is the main font and asks nothing");

        g_fakeQueries = 0;
        Check(FamilyIs(PickFontFamily(L"CPU", 3, L"LatinOnly", latin, noChain, FakeCovers),
                       L"LatinOnly") &&
                  g_fakeQueries == 0,
              "an ASCII run in a tagged run uses the tag outright, with no query");

        g_fakeQueries = 0;
        Check(!PickFontFamily(L"\u4e2d", 1, L"LatinOnly", latin, noChain, FakeCovers) &&
                  g_fakeQueries == 1,
              "a tag whose family has no glyph for the run is not forced on it");

        g_fakeQueries = 0;
        Check(FamilyIs(PickFontFamily(L"\u4e2d", 1, L"LatinOnly", two, noChain, FakeCovers),
                       L"Everything") &&
                  g_fakeQueries == 2,
              "a tag naming several families asks about each until one covers the run");

        g_fakeQueries = 0;
        Check(FamilyIs(PickFontFamily(L"\u4e2d", 1, L"LatinOnly", latin, everything, FakeCovers),
                       L"Everything"),
              "a tag that covers nothing falls through to the global list");

        g_fakeQueries = 0;
        Check(!PickFontFamily(L"\u4e2d", 1, L"LatinOnly", neither, noChain, FakeCovers) &&
                  g_fakeQueries == 2,
              "and with nothing anywhere covering it, it is the main font");

        // The gate, at the level of a single run.
        g_fakeQueries = 0;
        Check(!PickFontFamily(L"CPU 12%  RAM 73%", 15, L"LatinOnly", noChain, latin, FakeCovers) &&
                  g_fakeQueries == 0,
              "an ASCII run never consults the global list -- this is the cost gate");

        g_fakeQueries = 0;
        const WCHAR *picked =
            PickFontFamily(L"\u4e2d", 1, L"Everything", noChain, latin, FakeCovers);
        printf("    CJK run, main=Everything, list=LatinOnly -> %ls (%d queries)\n",
               picked ? picked : L"(main font)", g_fakeQueries);
        Check(!picked, "a run the main family covers stays in the main family");
        Check(g_fakeQueries == 1, "and only the main family was asked -- the list was not probed");

        g_fakeQueries = 0;
        picked = PickFontFamily(L"\u4e2d", 1, L"LatinOnly", noChain, everything, FakeCovers);
        printf("    CJK run, main=LatinOnly, list=Everything -> %ls (%d queries)\n",
               picked ? picked : L"(main font)", g_fakeQueries);
        Check(FamilyIs(picked, L"Everything"),
              "a run the main family cannot draw goes to the list");

        g_fakeQueries = 0;
        Check(!PickFontFamily(L"\u4e2d", 1, L"LatinOnly", noChain, noChain, FakeCovers) &&
                  g_fakeQueries == 0,
              "with no chain configured at all there is nothing to ask");

        // --- the real coverage query, and its cache ---
        //
        // The injected callback above proves the policy; this proves the implementation the
        // policy is handed on a real machine: a real GDI glyph lookup, and a cache that makes
        // the second identical question free.
        {
            const DWORD before = g_coverQueryCount;
            const bool consolasA = FontFamilyCovers(L"Consolas", L'A');
            const DWORD afterFirst = g_coverQueryCount;
            const bool consolasAAgain = FontFamilyCovers(L"Consolas", L'A');
            const DWORD afterSecond = g_coverQueryCount;
            printf("    Consolas 'A': %d then %d; real queries %lu -> %lu -> %lu\n", consolasA,
                   consolasAAgain, (unsigned long)before, (unsigned long)afterFirst,
                   (unsigned long)afterSecond);
            Check(consolasA, "the real query finds a Latin glyph in a Latin font");
            Check(afterFirst == before + 1, "and it ran the real lookup once");
            Check(afterSecond == afterFirst, "a repeated question is answered from the cache");

            const DWORD beforeCjk = g_coverQueryCount;
            const bool consolasCjk = FontFamilyCovers(L"Consolas", 0x4E2D);
            printf("    Consolas '\u4e2d': %d (queries %lu -> %lu)\n", consolasCjk,
                   (unsigned long)beforeCjk, (unsigned long)g_coverQueryCount);
            Check(!consolasCjk,
                  "a Latin-only family reports no coverage for CJK (that is what drives fallback)");
        }

        // --- ReadSettings wiring ---
        //
        // The parser is asserted above; this is the part that only the real reader can show:
        // that "Fallback fonts" reaches fallbackFonts, that a row's tag reaches rowFontTag, and
        // that an empty setting leaves both switched off.
        {
            const StubSetting withFonts[] = {
                {L"FallbackFonts", L"Consolas; NoSuchFamilyQZX"},
                {L"Row1", L"[[font:Arial]]CPU[[/font]] 12%"},
                {L"Row2", L"RAM 73%"},
            };
            g_stubStrings = withFonts;
            g_stubStringCount = (int)(sizeof(withFonts) / sizeof(withFonts[0]));
            LOCAL_SETTINGS(s, g_fontChain);
            ReadSettings(s);
            printf("    \"Fallback fonts\"=Consolas;NoSuch -> %d kept; row1 tag=%d chains=%d\n",
                   s.fallbackFonts.count, s.rowFontTag[0] ? 1 : 0, s.tagFontCount);
            Check(s.fallbackFonts.count == 1, "the installed name is kept");
            Check(s.rowFontTag[0], "row 1 carries a tag");
            Check(!s.rowFontTag[1], "row 2 does not");
            Check(s.tagFontCount == 1 && wcscmp(s.tagFonts[0].key, L"Arial") == 0,
                  "the tag's chain is registered and keyed by its text");

            const StubSetting noFonts[] = {
                {L"Row1", L"CPU 12%"},
                {L"Row2", L"RAM 73%"},
            };
            g_stubStrings = noFonts;
            g_stubStringCount = (int)(sizeof(noFonts) / sizeof(noFonts[0]));
            ReadSettings(s);
            Check(s.fallbackFonts.count == 0, "an empty setting is an empty chain");
            Check(s.tagFontCount == 0, "and no tags were registered");
            Check(!s.rowFontTag[0] && !s.rowFontTag[1], "so nothing needs the multi-font path");
        }

        // --- a tagged row, end to end: expansion, markers, segments ---
        {
            const StubSetting tagged[] = {
                {L"FallbackFonts", L"Consolas"},
                {L"Row1", L"[[font:Arial]]CPU[[/font]] 12%"},
                {L"Row2", L"A[[font:Arial]]B[[/font]]C"},
                {L"Row3", L"[[font:NoSuchFamilyQZX]]typo[[/font]]"},
                {L"Row4", L"[[font:Arial]no closing brackets"},
            };
            g_stubStrings = tagged;
            g_stubStringCount = (int)(sizeof(tagged) / sizeof(tagged[0]));
            LOCAL_SETTINGS(s, g_fontTagged);
            ReadSettings(s);

            struct RowCase {
                const WCHAR *row;
                const WCHAR *expectText;
                int expectSegments;
                int expectFirstChain;
            };
            const RowCase rows[] = {
                {s.rows[0], L"CPU 12%", 2, 1},          // "CPU" tagged, " 12%" not
                {s.rows[1], L"ABC", 3, 0},              // untagged, tagged, untagged
                {s.rows[2], L"[[font:NoSuchFamilyQZX]]typo", 1, 0},   // tag stays visible
                {s.rows[3], L"[[font:Arial]no closing brackets", 1, 0},
            };
            for (const RowCase &c : rows) {
                WCHAR marked[640];
                RingSpec rings[kMaxRingsPerRow];
                int ringCount = 0;
                MarkRow(c.row, Values(), s, marked, 640, rings, &ringCount, kMaxRingsPerRow);
                RowState state;
                BuildRowLayout(marked, ringCount, state, 0);
                wprintf(L"    \"%ls\" -> text=\"%ls\" segs=%d\n", c.row, state.text[0],
                        state.segmentCount[0]);
                Check(wcscmp(state.text[0], c.expectText) == 0, "tag is stripped, text survives");
                Check(state.segmentCount[0] == c.expectSegments, "segment count");
                Check(state.segments[0][0].tagChain == c.expectFirstChain,
                      "first segment's chain");
                // The tolerance rule in its strongest form: no form of a broken tag may eat
                // text. The stripped text must still account for every visible character.
                int covered = 0;
                for (int i = 0; i < state.segmentCount[0]; i++) {
                    if (state.segments[0][i].offset >= 0) covered += state.segments[0][i].length;
                }
                Check(covered == (int)wcslen(state.text[0]),
                      "every visible character belongs to a segment");
            }
        }
    }

    printf("\n=== per-row font size and height ===\n");
    {
        // Rows stopped being interchangeable: each one can ask for its own font size and box
        // height, from a setting or from a tag at the start of its text. The two things worth
        // guarding are the inheritance rule (0 means "not set", and a tag beats a setting) and
        // the fact that a row can never end up shorter than the font inside it.
        g_stubStrings = nullptr;
        g_stubStringCount = 0;
        g_stubInts = nullptr;
        g_stubIntCount = 0;

        // --- the prefix parser ---
        struct TagCase {
            const WCHAR *row;
            int expectFontSize;
            int expectHeight;
            const WCHAR *expectText;
        };
        const TagCase tagCases[] = {
            {L"[[h:44]]CPU", 0, 44, L"CPU"},
            {L"[[size:18]]CPU", 18, 0, L"CPU"},
            {L"[[h:44]][[size:18]]CPU", 18, 44, L"CPU"},
            {L"[[size:18]][[h:44]]CPU", 18, 44, L"CPU"},      // order cannot matter
            {L"[[SIZE:18]][[H:44]]CPU", 18, 44, L"CPU"},      // case-insensitive
            {L"[[size:18]][[size:22]]CPU", 22, 0, L"CPU"},    // a repeat overwrites
            {L"[[h:0]]CPU", 0, 0, L"CPU"},                    // 0 means "not set", so it is
                                                              // consumed but overrides nothing
            {L"", 0, 0, L""},
            // Anything that is not a well-formed prefix tag has to stay *text*: a row that
            // loses characters to a typo is the failure this whole area keeps producing.
            {L"CPU [[size:20]] RAM", 0, 0, L"CPU [[size:20]] RAM"},
            {L"[[size:abc]]CPU", 0, 0, L"[[size:abc]]CPU"},
            {L"[[size:]]CPU", 0, 0, L"[[size:]]CPU"},
            {L"[[size:18]CPU", 0, 0, L"[[size:18]CPU"},
            {L"[[ size:18 ]]CPU", 0, 0, L"[[ size:18 ]]CPU"},
            {L"[[height:44]]CPU", 0, 0, L"[[height:44]]CPU"},
            {L"[[size:1000]]CPU", 0, 0, L"[[size:1000]]CPU"},
            // A font tag at the start is a different mechanism and must survive this one.
            {L"[[font:Consolas]]CPU", 0, 0, L"[[font:Consolas]]CPU"},
        };
        for (const TagCase &c : tagCases) {
            int fontSize = -1, height = -1;
            const WCHAR *text = ParseRowMetricTags(c.row, &fontSize, &height);
            wprintf(L"    \"%ls\" -> size=%d h=%d text=\"%ls\"\n", c.row, fontSize, height, text);
            Check(fontSize == c.expectFontSize, "prefix tag: font size");
            Check(height == c.expectHeight, "prefix tag: height");
            Check(wcscmp(text, c.expectText) == 0, "prefix tag: remaining text");
        }

        // --- inheritance and the floor under a grown font ---
        {
            LOCAL_SETTINGS(d, g_metricDefault);
            d.fontSize = 15;
            d.rowHeight = 30;
            printf("    defaults: floor=%d height=%d top1=%d\n",
                   MinimumRowHeightForFont(d.fontSize), RowHeightPx(d, 0), RowTopPx(d, 1));
            Check(MinimumRowHeightForFont(15) == 30,
                  "the default row height is exactly what a 15 px font needs");
            Check(RowFontSizePx(d, 0) == 15 && RowHeightPx(d, 0) == 30,
                  "an unset row inherits both global values");
            Check(RowFontSizePx(d, kMaxRows + 3) == 15, "an out-of-range row cannot read past the table");

            d.rowFontSize[1] = 22;
            Check(RowFontSizePx(d, 1) == 22, "a row with its own size uses it");
            Check(RowHeightPx(d, 1) == 37,
                  "and its box grows to fit that font (22 + the same 15 px of slack)");

            d.rowHeightPx[2] = 20;
            Check(RowHeightPx(d, 2) == 30,
                  "an explicitly short row is still lifted to what its font needs");
            d.rowFontSize[2] = 24;
            Check(RowHeightPx(d, 2) == 39,
                  "and the floor follows that row's font, not the global one");

            d.rowHeightPx[3] = 52;
            Check(RowHeightPx(d, 3) == 52,
                  "a row taller than its font keeps its configured height");

            LOCAL_SETTINGS(huge, g_metricHuge);
            huge.fontSize = 15;
            huge.rowHeight = 30;
            huge.rowFontSize[0] = 999;
            Check(RowFontSizePx(huge, 0) == kMaxRowFontSize, "a font size is clamped");
        }

        // --- the panel is the sum of the rows, and the rows do not overlap ---
        {
            LOCAL_SETTINGS(s, g_metricRow);
            s.fontSize = 15;
            s.rowHeight = 30;

            // The regression guard, first: with nothing configured the arithmetic has to give
            // exactly what the old `rowHeight * count` gave, and the tops exactly `r * rowHeight`.
            bool sameAsBefore = true;
            bool topsAsBefore = true;
            for (int count = 1; count <= kMaxRows; count++) {
                if (PanelRowsHeight(s, count) != count * s.rowHeight) sameAsBefore = false;
                for (int r = 0; r < count; r++) {
                    if (RowTopPx(s, r) != r * s.rowHeight) topsAsBefore = false;
                }
            }
            Check(sameAsBefore, "with no per-row metrics the panel height is rowHeight * rows");
            Check(topsAsBefore, "and every row starts exactly where it used to");

            s.rowHeightPx[1] = 44;
            s.rowFontSize[1] = 20;
            printf("    rows: heights %d %d %d %d, tops %d %d %d %d, panel %d\n",
                   RowHeightPx(s, 0), RowHeightPx(s, 1), RowHeightPx(s, 2), RowHeightPx(s, 3),
                   RowTopPx(s, 0), RowTopPx(s, 1), RowTopPx(s, 2), RowTopPx(s, 3),
                   PanelRowsHeight(s, 4));
            Check(RowHeightPx(s, 1) == 44, "the taller row keeps its height (44 > 20 + 15)");
            Check(PanelRowsHeight(s, 4) == 30 + 44 + 30 + 30, "the panel is the sum of the rows");
            Check(RowTopPx(s, 2) == 74, "and the row below starts after the taller one");
            bool noOverlap = true;
            for (int r = 0; r + 1 < kMaxRows; r++) {
                if (RowTopPx(s, r) + RowHeightPx(s, r) > RowTopPx(s, r + 1)) noOverlap = false;
            }
            Check(noOverlap, "adjacent rows never overlap");
            Check(PanelRowsHeight(s, 4) == RowTopPx(s, 4),
                  "the panel height is the top of the row just past the last one");

            // The icon diameter follows the size *in effect where the icon is written*, which
            // is the rule the user reported twice (once per direction): a plate that follows a
            // global size redraws every icon when one row's size changes, and a plate that
            // follows the row ignores "make just these characters bigger". The two sizes below
            // are the two elements of one row, and they must differ.
            LOCAL_SETTINGS(icons, g_iconSize);
            icons.fontSize = 15;
            icons.rowHeight = 30;
            icons.rowFontSize[1] = 24;
            icons.rowMaxFontSize[1] = 24;
            const float rowIcon = IconDiameterForFontSize(ElementFontSizePx(icons, 0, 0));
            const float rowIcon32 = IconDiameterForFontSize(ElementFontSizePx(icons, 0, 32));
            printf("    icon diameters: %.2f at the row's size, %.2f inside [[size:32]]\n",
                   (double)rowIcon, (double)rowIcon32);
            Check(rowIcon == (float)15 * (float)kIconPlateRatio,
                  "an icon at the row's size uses the row's font size");
            Check(rowIcon32 == (float)32 * (float)kIconPlateRatio,
                  "an icon inside a run uses the run's size");
            Check(rowIcon32 != rowIcon,
                  "so two icons in one row at two sizes are two diameters");
            Check(RowHeightPx(icons, 1) >= MinimumRowHeightForFont(24),
                  "a row with its own larger size keeps a box that holds it");
        }

        // --- RowMetrics: one owner for every derived pixel size ---
        //
        // The sizes used to be worked out at each use site and drifted apart. These assertions
        // pin the single derivation the drawing code now reads: given the two numbers a row
        // has, what size is its text and how tall is its box. An icon's diameter is
        // deliberately *not* here -- it belongs to the element, not to the row (see the
        // [[size:NN]] run assertions below).
        {
            LOCAL_SETTINGS(s, g_sizeRun);
            s.fontSize = 15;
            s.rowHeight = 30;
            s.rowFontSize[1] = 24;
            s.rowHeightPx[1] = 40;
            s.rowFontSize[2] = 32;
            s.rowMaxFontSize[2] = 32;

            const RowMetrics r0 = MetricsForRow(s, 0);
            const RowMetrics r1 = MetricsForRow(s, 1);
            const RowMetrics r2 = MetricsForRow(s, 2);
            printf("    RowMetrics: r0 f=%g h=%g | r1 f=%g h=%g | r2 f=%g h=%g\n", r0.fontSize,
                   r0.lineHeight, r1.fontSize, r1.lineHeight, r2.fontSize, r2.lineHeight);

            Check(r0.fontSize == 15.0f && r0.lineHeight == 30.0f,
                  "a row that inherits the global metrics reports them");
            Check(r1.fontSize == 24.0f && r1.lineHeight == 40.0f,
                  "a row with its own font size and height reports those");
            Check(r2.fontSize == 32.0f &&
                      r2.lineHeight == (float)MinimumRowHeightForFont(32),
                  "and a row whose font raises its floor reports the floored height");
            // The relationship, not the number: whatever the table says, a row's box has to
            // hold the text that row draws at its largest.
            Check(r2.lineHeight >= (float)MinimumRowHeightForFont((int)r2.fontSize),
                  "every row's box holds at least its own font");
            Check(sizeof(RowMetrics) == 2 * sizeof(float),
                  "RowMetrics is two numbers: an icon size that pretended to be per-row is gone");
        }

        // --- ReadSettings wiring ---
        {
            const StubSetting tagged[] = {
                {L"Row1", L"[[size:22]][[h:44]]CPU"},
                {L"Row2", L"GPU"},
            };
            g_stubStrings = tagged;
            g_stubStringCount = (int)(sizeof(tagged) / sizeof(tagged[0]));
            LOCAL_SETTINGS(s, g_sizeTagged);
            ReadSettings(s);
            printf("    tagged row: text=\"%ls\" size=%d height=%d; row2 size=%d height=%d\n",
                   s.rows[0], s.rowFontSize[0], s.rowHeightPx[0], s.rowFontSize[1],
                   s.rowHeightPx[1]);
            Check(wcscmp(s.rows[0], L"CPU") == 0, "the metric tags are stripped from the row text");
            Check(s.rowFontSize[0] == 22 && s.rowHeightPx[0] == 44, "and reach the row's metrics");
            Check(s.rowFontSize[1] == 0 && s.rowHeightPx[1] == 0, "a row without them stays unset");

            const StubSetting settingsOnly[] = {
                {L"Row1", L"CPU"},
                {L"Row2", L"GPU"},
            };
            // Numeric settings come from the stub's int table, not its string table -- the two
            // are separate on purpose, exactly as Wh_GetIntSetting and Wh_GetStringSetting are.
            const StubSetting settingsOnlyInts[] = {
                {L"Row1FontSize", L"20"},
                {L"Row1Height", L"40"},
                {L"Row2Height", L"500"},     // past the maximum: clamped, not honoured
            };
            g_stubStrings = settingsOnly;
            g_stubStringCount = (int)(sizeof(settingsOnly) / sizeof(settingsOnly[0]));
            g_stubInts = settingsOnlyInts;
            g_stubIntCount = (int)(sizeof(settingsOnlyInts) / sizeof(settingsOnlyInts[0]));
            ReadSettings(s);
            printf("    settings only: row1 size=%d height=%d; row2 height=%d\n", s.rowFontSize[0],
                   s.rowHeightPx[0], s.rowHeightPx[1]);
            Check(s.rowFontSize[0] == 20 && s.rowHeightPx[0] == 40,
                  "Row N font size / height reach that row");
            Check(s.rowHeightPx[1] == kMaxRowHeight, "an out-of-range height is clamped");

            const StubSetting both[] = {
                {L"Row1", L"[[size:22]][[h:44]]CPU"},
            };
            const StubSetting bothInts[] = {
                {L"Row1FontSize", L"20"},
                {L"Row1Height", L"40"},
            };
            g_stubStrings = both;
            g_stubStringCount = (int)(sizeof(both) / sizeof(both[0]));
            g_stubInts = bothInts;
            g_stubIntCount = (int)(sizeof(bothInts) / sizeof(bothInts[0]));
            ReadSettings(s);
            Check(s.rowFontSize[0] == 22 && s.rowHeightPx[0] == 44,
                  "a tag wins over the setting for the same row");

            // A gap in the rows: the settings are numbered from 1 while the rows are indexed in
            // the order they are drawn, so `Row3`'s metrics belong to the *second* drawn row.
            const StubSetting gap[] = {
                {L"Row1", L"CPU"},
                {L"Row3", L"[[size:26]]GPU"},
            };
            const StubSetting gapInts[] = {
                {L"Row3Height", L"50"},
            };
            g_stubStrings = gap;
            g_stubStringCount = (int)(sizeof(gap) / sizeof(gap[0]));
            g_stubInts = gapInts;
            g_stubIntCount = (int)(sizeof(gapInts) / sizeof(gapInts[0]));
            ReadSettings(s);
            printf("    Row2 empty: rows=%d, row2 metrics size=%d height=%d, panel=%d\n",
                   s.rowCount, s.rowFontSize[1], s.rowHeightPx[1], PanelRowsHeight(s, s.rowCount));
            Check(s.rowCount == 2, "the empty row is skipped");
            Check(s.rowFontSize[1] == 26 && s.rowHeightPx[1] == 50,
                  "Row3's metrics land on the second drawn row");
            Check(s.rowFontSize[0] == 0, "and they do not leak onto the first");
            Check(RowFontSizePx(s, 1) == 26 && RowHeightPx(s, 1) == 50,
                  "the drawn row uses them");
        }

        // --- a [[size:NN]] run inside the text ---
        //
        // The row-prefix form makes the whole line that size; the same tag written anywhere
        // else decorates just the run it wraps, exactly like [[font:...]] does. Both are
        // asserted here because the difference is *where* the tag is, not what it says.
        {
            printf("    ParseInlineSize: ");
            int size = 0;
            const WCHAR digits18[] = L"18";
            const WCHAR digit5[] = L"5";
            const WCHAR digits100[] = L"100";
            const WCHAR word[] = L"abc";
            const WCHAR digits1234[] = L"1234";
            Check(ParseInlineSize(digits18, digits18 + 2, &size) && size == 18,
                  "a plain size parses");
            Check(ParseInlineSize(digit5, digit5 + 1, &size) && size == kMinRowFontSize,
                  "below the minimum it clamps up");
            Check(ParseInlineSize(digits100, digits100 + 3, &size) && size == kMaxRowFontSize,
                  "above the maximum it clamps down");
            Check(!ParseInlineSize(word, word + 3, &size), "a word is not a size");
            Check(!ParseInlineSize(nullptr, nullptr, &size), "a null body is not a size");
            Check(!ParseInlineSize(digits1234, digits1234 + 4, &size), "four digits is not a size");
            printf("\n");

            struct RunCase {
                const WCHAR *row;
                const WCHAR *expectText;
                int expectSegments;
                int expectSizes[kMaxSegments];
            };
            // Only inline runs here. A [[size:NN]] at the very start of a row is a *row*
            // property, consumed by ReadSettings before the expander ever sees the text, so it
            // is asserted through ReadSettings below instead (calling the expander directly
            // with an unstripped prefix would test a path the product never takes).
            const RunCase runCases[] = {
                // "BIG" is the run; the row's own size stays inherited (0).
                {L"A[[size:24]]BIG[[/size]]C", L"ABIGC", 3, {0, 24, 0, 0}},
                // No closing tag: the run goes to the end of the line.
                {L"A[[size:30]]to the end", L"Ato the end", 2, {0, 30, 0, 0}},
                // A malformed tag stays visible; a stray close still ends a run, so it splits
                // the text into two runs that happen to share the row's size.
                {L"x[[size:abc]]y[[/size]]z", L"x[[size:abc]]yz", 2, {0, 0, 0, 0}},
                // Two runs in one row.
                {L"[[size:12]]a[[/size]]b[[size:28]]c[[/size]]", L"abc", 3, {12, 0, 28, 0}},
            };
            for (const RunCase &c : runCases) {
                WCHAR marked[640];
                RingSpec rings[kMaxRingsPerRow];
                int ringCount = 0;
                LOCAL_SETTINGS(s, g_runCase);
                s.fontSize = 15;
                s.rowHeight = 30;
                MarkRow(c.row, Values(), s, marked, 640, rings, &ringCount, kMaxRingsPerRow);
                RowState state;
                BuildRowLayout(marked, ringCount, state, 0);
                wprintf(L"    \"%ls\" -> text=\"%ls\" segs=%d\n", c.row, state.text[0],
                        state.segmentCount[0]);
                Check(wcscmp(state.text[0], c.expectText) == 0, "run: tag stripped, text kept");
                Check(state.segmentCount[0] == c.expectSegments, "run: segment count");
                bool sizesOk = state.segmentCount[0] == c.expectSegments;
                for (int i = 0; i < state.segmentCount[0] && sizesOk; i++) {
                    if (state.segments[0][i].runFontSize != c.expectSizes[i]) sizesOk = false;
                }
                Check(sizesOk, "run: each segment's own size");
            }

            // The row-prefix path: the tag is consumed as the row's size, and the text that
            // reaches the expander is the rest of the line, so it carries no run marker.
            const StubSetting runRows[] = {
                {L"Row1", L"A[[size:24]]BIG[[/size]]C"},
                {L"Row2", L"[[size:20]]all at 20"},
                {L"Row3", L"small [[size:32]]BIG"},
            };
            g_stubStrings = runRows;
            g_stubStringCount = (int)(sizeof(runRows) / sizeof(runRows[0]));
            g_stubInts = nullptr;         // the previous block's Row3Height must not leak in
            g_stubIntCount = 0;
            LOCAL_SETTINGS(s, g_prefixRows);
            ReadSettings(s);
            printf("    rows: base sizes %d %d %d, largest %d %d %d\n", s.rowFontSize[0],
                   s.rowFontSize[1], s.rowFontSize[2], s.rowMaxFontSize[0], s.rowMaxFontSize[1],
                   s.rowMaxFontSize[2]);
            Check(s.rowFontSize[0] == 0 && s.rowMaxFontSize[0] == 24,
                  "a run sets the row's *largest* text, not its base size");
            Check(s.rowFontSize[1] == 20 && s.rowMaxFontSize[1] == 20,
                  "a prefix tag sets the base size and leaves no run behind");
            Check(s.rowFontSize[2] == 0 && s.rowMaxFontSize[2] == 32,
                  "and a run later in the line still counts towards the largest");
            {
                WCHAR marked[640];
                RingSpec rings[kMaxRingsPerRow];
                int ringCount = 0;
                MarkRow(s.rows[1], Values(), s, marked, 640, rings, &ringCount, kMaxRingsPerRow);
                RowState state;
                BuildRowLayout(marked, ringCount, state, 0);
                printf("    the prefix row expands to text=\"%ls\" segs=%d size=%d\n",
                       state.text[0], state.segmentCount[0], state.segments[0][0].runFontSize);
                Check(wcscmp(state.text[0], L"all at 20") == 0 &&
                          state.segmentCount[0] == 1 &&
                          state.segments[0][0].runFontSize == 0,
                      "and the stored text has no run marker left in it");
            }
            // The height floor follows the largest text in the row, so a big run cannot spill
            // into the row below. Row 2 is plain 20 px text, which already raises its floor.
            Check(RowHeightPx(s, 0) == 24 + kRowHeightFontSlack,
                  "the row grows to fit its biggest run");
            Check(RowHeightPx(s, 1) == 20 + kRowHeightFontSlack,
                  "a row of 20 px text gets the floor for 20 px");
            Check(RowHeightPx(s, 2) == 32 + kRowHeightFontSlack,
                  "and so does a row whose run is bigger than its base size");

            // A change of run size with identical text still has to be a change, or the row
            // would keep drawing the old size.
            {
                RowState a = {};
                RowState b = {};
                a.segmentCount[0] = 1;
                b.segmentCount[0] = 1;
                a.segments[0][0].length = 3;
                b.segments[0][0].length = 3;
                wcscpy(a.text[0], L"abc");
                wcscpy(b.text[0], L"abc");
                a.segments[0][0].runFontSize = 12;
                b.segments[0][0].runFontSize = 24;
                Check(!RowStateEqual(a, b, 1),
                      "a run size change is a layout change, so the row is repainted");
                b.segments[0][0].runFontSize = 12;
                Check(RowStateEqual(a, b, 1), "and the same sizes compare equal");
            }
        }

        // ComputeGeometry is where the window height, the hit-test height and the intro's
        // target all come from, so the link between "the panel is the sum of its rows" and the
        // height actually used has to be asserted. Without this the sums above could be perfect
        // while the panel still sized itself with rowHeight * rows.
        {
            LOCAL_SETTINGS(saved, g_geometrySaved);
            saved = g_settings;
            LOCAL_SETTINGS(s, g_geometry);
            s.fontSize = 15;
            s.rowHeight = 30;
            s.rowCount = 3;
            wcscpy(s.rows[0], L"CPU");
            wcscpy(s.rows[1], L"GPU");
            wcscpy(s.rows[2], L"DISK");
            s.rowHeightPx[1] = 48;
            g_settings = s;
            ComputeGeometry();
            printf("    ComputeGeometry: rows=%d panelH=%d displayH=%d\n", g_visibleRows,
                   g_panelHeight, g_displayHeight);
            Check(g_visibleRows == 3, "every configured row is counted");
            Check(g_panelHeight == 30 + 48 + 30,
                  "the panel is as tall as its rows actually are, not rows x Row height");
            Check(g_panelHeight == PanelRowsHeight(s, 3),
                  "and that is the same sum the painter lays the rows out with");
            g_settings = saved;
        }

        g_stubStrings = nullptr;
        g_stubStringCount = 0;
        g_stubInts = nullptr;
        g_stubIntCount = 0;
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
            Skip("GDI+ starts on this machine (real-text checks)");
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
                // Product code: a null here is a failure, not a reason to skip. The
                // NoWrap/fitting checks below are the only automatic defence for the
                // "text wraps and the first line rides up" regression, and they used to
                // disappear from the output without a trace.
                Check(format != nullptr, "CreateRowStringFormat builds the row format");

                if (font && format) {
                    // Font objects are cached by (family, size), so a row that keeps asking for
                    // the same fallback family gets the same GpFont back instead of a new one
                    // per frame. The second half of the check is the lifecycle: a settings
                    // change has to drop them, or a size change would keep drawing the old
                    // metrics (the same trap the ellipsis-width cache fell into).
                    {
                        GpFont *first = AcquireRowFont(graphics, L"Consolas", 15);
                        GpFont *second = AcquireRowFont(graphics, L"Consolas", 15);
                        GpFont *other = AcquireRowFont(graphics, L"Consolas", 24);
                        Check(first != nullptr, "a fallback font can be created");
                        Check(first == second, "asking again for the same one returns the same object");
                        Check(other != nullptr && other != first,
                              "a different size is a different, live object");
                        ReleaseFontResources();
                        // Asserting on the slot table, not on the next pointer GDI+ hands back:
                        // a freed object's address can legitimately be reused, so "the new
                        // pointer differs" would be a check that passes for the wrong reason and
                        // fails on a different allocator.
                        bool anySlotUsed = false;
                        for (int i = 0; i < kFontCacheSlots; i++) {
                            if (g_fontCache[i].used) anySlotUsed = true;
                        }
                        Check(!anySlotUsed, "releasing the caches really empties them");
                        GpFont *afterRelease = AcquireRowFont(graphics, L"Consolas", 15);
                        Check(afterRelease != nullptr, "and they can be built again afterwards");
                        ReleaseFontResources();
                    }

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

                        float budget = LayoutWidthFloor(seg.budget);
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

                    // --- the per-row layout plan: what a frame does *not* have to redo ---
                    //
                    // Placing and fitting a row's runs used to happen on every frame: each run
                    // re-chose its family and re-fitted itself with a binary search over
                    // MeasureString, 25 times a second, for text that had not changed since the
                    // last sample. The plan is built once per publish and read verbatim after
                    // that, so the assertion that matters is "the second call measures nothing".
                    // It is measured through the product's own counter (g_measureCalls, which
                    // every text measurement goes through) rather than by timing anything.
                    {
                        LOCAL_SETTINGS(savedSettings, g_planSaved);
                        savedSettings = g_settings;
                        const RowState savedState = g_rowState;
                        const int savedRows = g_publishedRows;
                        const unsigned savedEpoch = g_layoutEpoch;

                        LOCAL_SETTINGS(s, g_plan);
                        s.fontSize = 15;
                        s.rowHeight = 30;
                        s.panelWidth = 380;
                        s.ringSize = 22;
                        s.ringRightOffset = 6;
                        wcscpy(s.fontFamily, L"Segoe UI");
                        s.rowCount = 1;
                        // One inline gauge, so the plan also exercises the inline-flow branch
                        // (per-run budget against the gauges that still follow it).
                        wcscpy(s.rows[0], L"CPU 12%  RAM 73%  [[ring:50]]  done");
                        g_settings = s;

                        WCHAR marked[640];
                        RingSpec rings[kMaxRingsPerRow];
                        int ringCount = 0;
                        MarkRow(s.rows[0], Values(), s, marked, 640, rings, &ringCount,
                                kMaxRingsPerRow);
                        RowState staged = {};
                        BuildRowLayout(marked, ringCount, staged, 0);
                        for (int i = 0; i < ringCount; i++) staged.rings[0][i] = rings[i];
                        g_rowState = staged;
                        g_publishedRows = 1;
                        g_layoutEpoch++;   // a publish happened, so any plan is stale

                        const float planWidth = 380.0f;
                        const int planFontSize = 15;
                        const float planRowHeight = 30.0f;

                        const unsigned before = g_measureCalls;
                        const RowLayoutPlan *built = EnsureRowPlan(
                            graphics, font, format, 0, 1, planWidth, planFontSize,
                            planRowHeight, false);
                        const unsigned afterBuild = g_measureCalls;
                        const RowLayoutPlan *reused = EnsureRowPlan(
                            graphics, font, format, 0, 1, planWidth, planFontSize,
                            planRowHeight, false);
                        const unsigned afterReuse = g_measureCalls;
                        printf("    layout plan: %d segments, measure build=%lu reuse=%lu\n",
                               built->segmentCount, (unsigned long)(afterBuild - before),
                               (unsigned long)(afterReuse - afterBuild));

                        Check(built == reused, "a plan that still matches is handed back, not rebuilt");
                        Check(afterBuild > before, "building a row's layout measures its runs");
                        Check(afterReuse == afterBuild,
                              "reusing it measures nothing: that is the whole point of the cache");

                        // Every field of the key has to be able to invalidate the plan, or a
                        // changed row would keep drawing the run it was measured with. Each of
                        // these is a change that arrives *without* the text changing.
                        struct PlanChange {
                            const char *what;
                            float width, rowHeight;
                            int fontSize;
                        };
                        // The row's left inset used to be a key field of its own (the shared
                        // icon plate column sized it). It is a constant now -- an inline icon
                        // moves the text it is written next to, and nothing else -- so there is
                        // nothing left for a test to vary.
                        const PlanChange changes[] = {
                            {"width", planWidth + 20.0f, planRowHeight, planFontSize},
                            {"row height", planWidth, planRowHeight + 8.0f, planFontSize},
                            {"font size", planWidth, planRowHeight, planFontSize + 3},
                        };
                        for (const PlanChange &c : changes) {
                            const unsigned was = g_measureCalls;
                            const RowLayoutPlan *again = EnsureRowPlan(
                                graphics, font, format, 0, 1, c.width, c.fontSize,
                                c.rowHeight, false);
                            printf("      %-11s -> remeasured %lu\n", c.what,
                                   (unsigned long)(g_measureCalls - was));
                            Check(g_measureCalls > was, "a changed key remeasures the row");
                            // And the rebuilt plan records the key it was built with. Comparing
                            // pointers would prove nothing here: a plan is a fixed slot per row,
                            // so the address is the same whether it was rebuilt or not.
                            Check(again->width == c.width &&
                                      again->rowHeight == c.rowHeight &&
                                      again->rowFontSize == c.fontSize,
                                  "and remembers the geometry it was built from");
                            // Put the original key back for the next change in the table.
                            EnsureRowPlan(graphics, font, format, 0, 1, planWidth,
                                          planFontSize, planRowHeight, false);
                        }

                        // The bump that a new sample performs: same geometry, same fonts, same
                        // number of segments -- *only* the text differs. That isolation is the
                        // point: with the segment count changed as well, the plan would be
                        // rebuilt for that reason and this would pass even if the epoch were
                        // ignored (it did, until this was fixed).
                        {
                            // The old plan is a fixed slot, so its content is captured before the
                            // rebuild overwrites it -- otherwise "carries the new text" would be
                            // comparing the new plan with itself.
                            WCHAR firstSliceBefore[640];
                            wcsncpy(firstSliceBefore, built->segments[0].slice, 639);
                            firstSliceBefore[639] = L'\0';

                            WCHAR marked2[640];
                            RingSpec rings2[kMaxRingsPerRow];
                            int ringCount2 = 0;
                            MarkRow(L"CPU 99%  RAM 12%  [[ring:80]]  ok", Values(), s,
                                    marked2, 640, rings2, &ringCount2, kMaxRingsPerRow);
                            RowState staged2 = {};
                            BuildRowLayout(marked2, ringCount2, staged2, 0);
                            for (int i = 0; i < ringCount2; i++) staged2.rings[0][i] = rings2[i];
                            Check(staged2.segmentCount[0] == built->segmentCount,
                                  "the two samples really do have the same shape");
                            g_rowState = staged2;
                            g_layoutEpoch++;
                            const unsigned was = g_measureCalls;
                            const RowLayoutPlan *afterPublish = EnsureRowPlan(
                                graphics, font, format, 0, 1, planWidth, planFontSize,
                                planRowHeight, false);
                            printf("      new sample  -> remeasured %lu, first run \"%ls\" -> \"%ls\"\n",
                                   (unsigned long)(g_measureCalls - was), firstSliceBefore,
                                   afterPublish->segments[0].slice);
                            Check(g_measureCalls > was, "a new sample rebuilds the row's layout");
                            Check(wcscmp(afterPublish->segments[0].slice, firstSliceBefore) != 0,
                                  "and the plan carries the new text, not the old one");
                            Check(wcsncmp(afterPublish->segments[0].slice, g_rowState.text[0],
                                          wcslen(afterPublish->segments[0].slice)) == 0,
                                  "which is the text that was just published");
                        }

                        // Two more fields of the key. Neither can differ without the epoch in
                        // the product (a structure change always comes from new text, and new
                        // text bumps the epoch), so these force the situation directly: they are
                        // defence in depth, and defence in depth that nothing exercises is
                        // exactly the kind of check that silently stops working.
                        {
                            const int savedCount = g_rowState.segmentCount[0];
                            const unsigned wasSegments = g_measureCalls;
                            g_rowState.segmentCount[0] = savedCount > 1 ? savedCount - 1 : 1;
                            EnsureRowPlan(graphics, font, format, 0, 1, planWidth,
                                          planFontSize, planRowHeight, false);
                            printf("      segment count -> remeasured %lu\n",
                                   (unsigned long)(g_measureCalls - wasSegments));
                            Check(g_measureCalls > wasSegments,
                                  "a row that changed shape is remeasured even without a new sample");
                            g_rowState.segmentCount[0] = savedCount;

                            // Rebuild under the original key (the plan left behind is the
                            // shrunken one, so this call is *supposed* to measure), then prove
                            // the rebuilt plan is fresh. Without that proof the row-count case
                            // below would rebuild because of leftover staleness and pass for the
                            // wrong reason -- which it did, until this was added.
                            EnsureRowPlan(graphics, font, format, 0, 1, planWidth,
                                          planFontSize, planRowHeight, false);
                            const unsigned wasRestore = g_measureCalls;
                            EnsureRowPlan(graphics, font, format, 0, 1, planWidth,
                                          planFontSize, planRowHeight, false);
                            Check(g_measureCalls == wasRestore,
                                  "the plan is fresh again under the original key");

                            const unsigned wasRows = g_measureCalls;
                            EnsureRowPlan(graphics, font, format, 0, 2, planWidth,
                                          planFontSize, planRowHeight, false);
                            printf("      row count     -> remeasured %lu\n",
                                   (unsigned long)(g_measureCalls - wasRows));
                            Check(g_measureCalls > wasRows,
                                  "and so is a row when the panel gains or loses one");
                            EnsureRowPlan(graphics, font, format, 0, 1, planWidth,
                                          planFontSize, planRowHeight, false);
                        }

                        g_settings = savedSettings;
                        g_rowState = savedState;
                        g_publishedRows = savedRows;
                        g_layoutEpoch = savedEpoch + 1;   // leave nothing stale behind
                    }

                    // --- inline icons in a plan: their own size, their own box, their own
                    // ripples ---
                    //
                    // This is where "does the icon follow the size where it appears" is decided
                    // in the product: BuildRowPlan turns each icon element into a box of its
                    // own, and the frame just reads it. The assertions below are on the plan the
                    // painter consumes, not on a second derivation the test could keep in sync
                    // with the bug.
                    {
                        LOCAL_SETTINGS(savedSettings, g_planSaved);
                        savedSettings = g_settings;
                        const RowState savedState = g_rowState;
                        const int savedRows = g_publishedRows;
                        const unsigned savedEpoch = g_layoutEpoch;

                        const float planWidth2 = 420.0f;
                        const int planFont = 15;

                        // Builds one row's plan and hands it back. The plan is a fixed slot per
                        // row, so the caller reads it before the next call.
                        auto ensureIconPlan = [&](const WCHAR *rowText,
                                                  int rowMaxFont) -> const RowLayoutPlan * {
                            LOCAL_SETTINGS(s, g_iconPlan);
                            s.fontSize = planFont;
                            s.rowHeight = 30;
                            s.panelWidth = 420;
                            s.ringSize = 26;
                            s.ringRightOffset = 12;
                            s.rowCount = 1;
                            s.rowMaxFontSize[0] = rowMaxFont;
                            wcsncpy(s.rows[0], rowText, kRowTextCap - 1);
                            s.rows[0][kRowTextCap - 1] = L'\0';
                            // One registered chain, so a row below can use [[font:...]] and be
                            // split into two text runs without consulting the installed fonts
                            // (CompileRowText only looks a chain up, it never re-validates a
                            // family name). It changes nothing for rows that have no font tag:
                            // the cost gate (RowNeedsFontPath) is still closed here.
                            wcscpy(s.tagFonts[0].key, L"Segoe UI");
                            wcscpy(s.tagFonts[0].chain.names[0], L"Segoe UI");
                            s.tagFonts[0].chain.count = 1;
                            s.tagFontCount = 1;
                            // Compiled into the *same* settings object that is published, which
                            // is what ReadSettings does and what the icon marker's payload
                            // (an element index) depends on.
                            CompileRowText(s, s.rows[0], s.rowProgram[0]);
                            g_settings = s;

                            WCHAR marked[640];
                            RingSpec rings[kMaxRingsPerRow];
                            int ringCount = 0;
                            InstantiateRowProgram(s, 0, Values(), marked, 640, rings, &ringCount,
                                                  kMaxRingsPerRow);
                            RowState staged = {};
                            BuildRowLayout(marked, ringCount, staged, 0);
                            for (int i = 0; i < ringCount; i++) staged.rings[0][i] = rings[i];
                            g_rowState = staged;
                            g_publishedRows = 1;
                            g_layoutEpoch++;
                            return EnsureRowPlan(graphics, font, format, 0, 1, planWidth2,
                                                 planFont, (float)RowHeightPx(s, 0), false);
                        };

                        // Two icons, two sizes, one row. This is the reported bug in both
                        // directions: an icon that ignores its own size, and an icon that
                        // follows a global size instead of the one where it is written.
                        {
                            const RowLayoutPlan *plan = ensureIconPlan(
                                L"[[icon:CPU]][[size:24]][[icon:Memory]][[/size]]tail", 24);
                            printf("    two icons: segs=%d  d0=%.2f d1=%.2f  x0=%.1f x1=%.1f "
                                   "x2=%.1f\n",
                                   plan->segmentCount, (double)plan->segments[0].diameter,
                                   (double)plan->segments[1].diameter, (double)plan->segments[0].x,
                                   (double)plan->segments[1].x, (double)plan->segments[2].x);
                            Check(plan->segmentCount == 3, "the row is two icons and one text run");
                            Check(plan->segments[0].isIcon && plan->segments[1].isIcon,
                                  "both icon tags became icon elements");
                            Check(!plan->segments[2].isIcon, "and the text after them did not");
                            Check(plan->segments[0].glyph == 1 && plan->segments[1].glyph == 2,
                                  "each element keeps its own glyph");
                            Check(plan->segments[0].fontSize == 15 &&
                                      plan->segments[1].fontSize == 24,
                                  "the element's size is the run it sits in");
                            Check(plan->segments[0].diameter == IconDiameterForFontSize(15),
                                  "so the first icon is the row's size");
                            Check(plan->segments[1].diameter == IconDiameterForFontSize(24),
                                  "and the second is the run's size");
                            Check(plan->segments[0].diameter != plan->segments[1].diameter,
                                  "two icons in one row at two sizes have two diameters");
                            Check(plan->segments[1].diameter > plan->segments[0].diameter,
                                  "and the bigger run is the bigger icon");
                            // The flow: an element occupies its box, and the next element starts
                            // after it (plus the one content gap).
                            Check(plan->segments[1].x ==
                                      plan->segments[0].x + plan->segments[0].diameter +
                                          (float)kIconContentGap,
                                  "the second icon starts after the first one's box");
                            Check(plan->segments[2].x ==
                                      plan->segments[1].x + plan->segments[1].diameter +
                                          (float)kIconContentGap,
                                  "and the text starts after the last icon");
                            Check(plan->segments[0].x == kRowTextLeftInset,
                                  "the first element starts at the row's own padding");
                            Check(plan->segmentCount == g_rowState.segmentCount[0],
                                  "the plan has one entry per segment the row produced");
                            // The ripple default, read from the plan the painter reads.
                            Check(plan->segments[0].ripple && !plan->segments[1].ripple,
                                  "by default only the row's first icon ripples");
                        }

                        // The same size twice must be the same diameter -- the other half of
                        // "the icon follows its own size": a per-element rule that ignored the
                        // run would also pass the two-size check above.
                        {
                            const RowLayoutPlan *plan =
                                ensureIconPlan(L"[[icon:CPU]][[icon:Memory]]tail", 0);
                            printf("    same size twice: d0=%.2f d1=%.2f\n",
                                   (double)plan->segments[0].diameter,
                                   (double)plan->segments[1].diameter);
                            Check(plan->segments[0].diameter == plan->segments[1].diameter,
                                  "the same size twice is the same diameter");
                            Check(plan->segments[1].fontSize == plan->segments[0].fontSize,
                                  "and the same font size twice");
                        }

                        // The overrides, again through the plan.
                        {
                            const RowLayoutPlan *plan = ensureIconPlan(
                                L"[[icon:CPU:noripple]][[icon:Memory:ripple]]tail", 0);
                            Check(!plan->segments[0].ripple,
                                  ":noripple keeps even the row's first icon quiet");
                            Check(plan->segments[1].ripple,
                                  "and :ripple makes a later icon ripple");
                        }

                        // A file icon: the path is a slice of the row, trimmed by CopyUserPath
                        // (quotes and padding are the user's, not the loader's), and the disc
                        // falls back to a real glyph if the file cannot be decoded.
                        {
                            const RowLayoutPlan *plan = ensureIconPlan(
                                L"[[icon:file=\"C:\\my pics\\ssd.png\":ripple]]x", 0);
                            wprintf(L"    file icon path=\"%ls\" glyph=%d ripple=%d\n",
                                    plan->segments[0].path, plan->segments[0].glyph,
                                    plan->segments[0].ripple ? 1 : 0);
                            Check(wcscmp(plan->segments[0].path, L"C:\\my pics\\ssd.png") == 0,
                                  "the image path keeps its spaces and loses its quotes");
                            Check(plan->segments[0].path[0] != L'"',
                                  "no quote reaches the image loader");
                            Check(plan->segments[0].ripple,
                                  "a :ripple after the path is the ripple switch, not the path");
                            Check(plan->segments[0].glyph == kIconImageFallbackGlyph,
                                  "an image icon still has a glyph to fall back on");
                            Check(wcsstr(plan->segments[0].path, L":ripple") == nullptr,
                                  "and the switch is not part of the path");
                        }

                        // An icon is an element of the flow, so a gauge written after one still
                        // lands after it -- the property that used to hold because every ring
                        // was placed relative to the text column.
                        {
                            const RowLayoutPlan *plan =
                                ensureIconPlan(L"[[icon:CPU]]CPU [[ring:50]] done", 0);
                            const RowSegmentPlan &icon = plan->segments[0];
                            const RowSegmentPlan &text = plan->segments[1];
                            const RowSegmentPlan &ring = plan->segments[2];
                            printf("    icon x=%.1f w=%.1f  text x=%.1f  ring centre=%.1f\n",
                                   (double)icon.x, (double)icon.diameter, (double)text.x,
                                   (double)ring.x);
                            Check(icon.isIcon && !text.isIcon && ring.isRing,
                                  "icon, then text, then the gauge it was written after");
                            Check(text.x >= icon.x + icon.diameter,
                                  "the text follows the icon's box");
                            const float ringLeftEdge = ring.x - (float)g_settings.ringSize / 2.0f;
                            Check(ringLeftEdge >= icon.x + icon.diameter,
                                  "and the inline gauge still lands after the icon");
                            Check(ringLeftEdge >= text.x,
                                  "which is after the text it was written after");
                        }

                        // A row with no icon tag: the plan is text and gauges only, and the
                        // text starts at the row's own padding -- there is no plate column left
                        // to inset it by.
                        {
                            const RowLayoutPlan *plan = ensureIconPlan(L"CPU 12%  RAM 73%", 0);
                            bool anyIcon = false;
                            for (int i = 0; i < plan->segmentCount; i++) {
                                if (plan->segments[i].isIcon) anyIcon = true;
                            }
                            printf("    no tag: segs=%d first x=%.1f (inset %.1f)\n",
                                   plan->segmentCount, (double)plan->segments[0].x,
                                   (double)kRowTextLeftInset);
                            Check(!anyIcon, "a row with no tag has no icon element in its plan");
                            Check(plan->segments[0].x == kRowTextLeftInset,
                                  "and its text starts at the row's padding, not after a plate");
                        }

                        // A **parked** row is still a flow. The gauges are parked at the right
                        // edge (the tag was written before any text), but the runs of text in it
                        // are placed one after another, and each run's advance is its own
                        // measured width -- not the whole text budget. Two ways of getting that
                        // wrong have both been seen: advancing the cursor only for inline rows
                        // (`[[ring:x]] A[[font:…]]B` drew "B" back at the row's inset, on top of
                        // "A"), and advancing a parked run by its whole budget (which pushed "B"
                        // under the parked gauge, past the right edge of the text column). The
                        // parked text column is [inset, textRight], and the gauges live to the
                        // right of textRight, so all three relations are asserted.
                        {
                            const RowLayoutPlan *plan = ensureIconPlan(
                                L"[[ring:40]] A[[font:Segoe UI]]B", 0);
                            const RowSegmentPlan &ring = plan->segments[0];
                            const RowSegmentPlan &first = plan->segments[1];
                            const RowSegmentPlan &second = plan->segments[2];
                            // From the product, not re-derived here: the right edge the parked
                            // path reserved for text on this row.
                            const float textRight =
                                ComputeRowTextMetrics(planWidth2, 1, g_settings.ringSize,
                                                      g_settings.ringRightOffset, false)
                                    .textRight;
                            printf("    parked: segs=%d  x0=%.1f adv0=%.1f box0=%.1f  x1=%.1f "
                                   "box1=%.1f  (inset %.1f, textRight %.1f)  ring=%.1f\n",
                                   plan->segmentCount, (double)first.x, (double)first.advance,
                                   (double)first.boxWidth, (double)second.x,
                                   (double)second.boxWidth, (double)kRowTextLeftInset,
                                   (double)textRight, (double)ring.x);
                            Check(plan->segmentCount == 3,
                                  "a parked row with a font tag is one gauge and two text runs");
                            Check(ring.isRing && !first.isIcon && !second.isIcon,
                                  "and only the gauge is an element");
                            Check(!g_rowState.inlineRing[0],
                                  "the row really is parked (the gauge precedes the text)");
                            Check(first.x == kRowTextLeftInset,
                                  "the first run starts at the row's padding");
                            Check(first.advance > 0.0f, "and it has a real width");
                            Check(first.advance < first.boxWidth,
                                  "a parked run's advance is its own width, not its whole budget");
                            Check(second.x >= first.x + first.advance,
                                  "the second run starts after the first one, not back at the "
                                  "row's inset");
                            Check(second.x != kRowTextLeftInset,
                                  "so a parked row's runs do not pile up on the left edge");
                            Check(second.x > first.x,
                                  "and the row's runs advance left to right");
                            Check(second.x + second.boxWidth <= textRight + 0.5f,
                                  "and the runs' boxes stay inside the column reserved for the "
                                  "text, so nothing lands under the parked gauge");
                            // The parked gauge is placed from the right edge, so it has to stay
                            // clear of the text column.
                            const float ringLeftEdge = ring.x - (float)g_settings.ringSize / 2.0f;
                            Check(ringLeftEdge >= textRight - 0.5f,
                                  "the parked gauge sits at or past the text column's right edge");
                        }

                        g_settings = savedSettings;
                        g_rowState = savedState;
                        g_publishedRows = savedRows;
                        g_layoutEpoch = savedEpoch + 1;
                    }

                    // --- gaps and alignment groups in a real layout plan ---
    //
    // Everything above settles what the tags compile to. This is where they are *placed*: the
    // assertions below are read off the same `RowLayoutPlan` the painter consumes, with real
    // GDI+ fonts and real measurements, because "the gap scales with the row", "the group is
    // centred" and "a run stays inside the panel" are all statements about pixels.
    {
        LOCAL_SETTINGS(savedSettings, g_gapSaved);
        savedSettings = g_settings;
        const RowState savedState = g_rowState;
        const int savedRows = g_publishedRows;
        const unsigned savedEpoch = g_layoutEpoch;

        const float rowW = 420.0f;
        const int rowFont = 15;
        const float rowH = 30.0f;

        // Builds one row's plan and hands the plan back. The plan is a fixed slot per row, so the
        // caller reads what it needs before the next call.
        auto ensureRow = [&](const WCHAR *text, int fontPx) -> const RowLayoutPlan * {
            LOCAL_SETTINGS(s2, g_gapPlan);
            s2.fontSize = fontPx;
            s2.rowHeight = (int)rowH;
            s2.panelWidth = (int)rowW;
            s2.ringSize = 26;
            s2.ringRightOffset = 12;
            s2.rowCount = 1;
            s2.rowFontSize[0] = fontPx;
            s2.rowMaxFontSize[0] = fontPx;
            wcsncpy(s2.rows[0], text, kRowTextCap - 1);
            s2.rows[0][kRowTextCap - 1] = L'\0';
            CompileRowText(s2, s2.rows[0], s2.rowProgram[0]);
            g_settings = s2;

            WCHAR marked2[640];
            RingSpec rings2[kMaxRingsPerRow];
            int ringCount2 = 0;
            InstantiateRowProgram(s2, 0, Values(), marked2, 640, rings2, &ringCount2,
                                  kMaxRingsPerRow);
            RowState staged2 = {};
            BuildRowLayout(marked2, ringCount2, staged2, 0);
            for (int i = 0; i < ringCount2; i++) staged2.rings[0][i] = rings2[i];
            g_rowState = staged2;
            g_publishedRows = 1;
            g_layoutEpoch++;
            // The row's main font has to be the one for *this* row size: `BuildRowPlan` is told
            // `rowFontSizePx` and uses the passed font for every run that does not carry its own
            // `[[size:NN]]`. Handing it the fixture's 15 px font while claiming 16 px makes the
            // plan measure and draw at 15 while its own metadata says 16, which shows up as a
            // "the plan disagrees with a fresh measurement" failure that is about the fixture, not
            // the product. The product never has this problem: `PaintPanel` builds the row's font
            // from the row's own size.
            GpFont *rowFont = fontPx == 15 ? font : AcquireRowFont(graphics, L"Segoe UI", fontPx);
            if (!rowFont) rowFont = font;
            return EnsureRowPlan(graphics, rowFont, format, 0, 1, rowW, fontPx, rowH, false);
        };
        // The row's text column, from the product: the left edge every group is placed against
        // and the right edge no run may cross. Re-deriving these here would be testing a copy.
        const RowTextMetrics column = ComputeRowTextMetrics(rowW, 0, g_settings.ringSize,
                                                            g_settings.ringRightOffset, false);

        // The text of one group, concatenated from the segments the plan will draw. Elements that
        // draw nothing of their own (a gauge, an icon, a gap) are skipped: the statements under
        // test are about the *characters* between the tags.
        auto groupText = [&](const RowLayoutPlan *plan, int g, WCHAR *out, int cap) {
            int used = 0;
            const AlignGroupPlan &group = plan->groups[g];
            for (int s = group.firstSegment; s <= group.lastSegment && used + 1 < cap; s++) {
                const RowSegmentPlan &entry = plan->segments[s];
                if (entry.skip || entry.isRing || entry.isIcon || entry.isGap) continue;
                for (const WCHAR *ch = entry.slice; *ch && used + 1 < cap; ch++) out[used++] = *ch;
            }
            out[used] = L'\0';
        };
        // The plan index of the group with this alignment, or -1: reading it by *kind* rather
        // than by position keeps the lookup from quietly passing when the group the tag asked for
        // is not the one that was built.
        auto groupIndexOf = [&](const RowLayoutPlan *plan, int kind) {
            for (int g = 0; g < plan->alignCount; g++) {
                if (plan->groups[g].kind == kind) return g;
            }
            return -1;
        };

        // (1) The gap follows the font size in effect, the same way the icon diameter does. This
        // one assertion is what separates `[[gap:N]]` from `[[gap:Npx]]`: an implementation that
        // treated every gap as absolute pixels would place the same gap in a 12 px row and a
        // 24 px row, and this goes red.
        {
            const RowLayoutPlan *small = ensureRow(L"A[[gap:1.5]]B", 12);
            const float smallGap = small->segments[1].advance;
            const RowLayoutPlan *big = ensureRow(L"A[[gap:1.5]]B", 24);
            const float bigGap = big->segments[1].advance;
            printf("    1.5 em: %.2f px at 12, %.2f px at 24 (icon ratio %.2f)\n", (double)smallGap,
                   (double)bigGap, kIconPlateRatio);
            Check(small->segmentCount == 3 && small->segments[1].isGap,
                  "a gap between two runs is a gap element in the plan");
            Check(bigGap > smallGap + 10.0f, "a gap written in ems scales with the font size");
            Check(smallGap == 12.0f * 1.5f && bigGap == 24.0f * 1.5f,
                  "and it is the font-size multiple the tag asked for");
            // Both derived sizes come from the same font size, each through its own single
            // multiplication: the gap is `n` em and the icon is kIconPlateRatio em, so their ratio
            // is the ratio of those two constants and changes with neither row nor run size.
            Check(smallGap / IconDiameterForFontSize(12) ==
                      bigGap / IconDiameterForFontSize(24),
                  "the gap's em unit and the icon's diameter scale from the same font size");
        }

        // (2) The px unit does not follow the size, which is the whole reason both units exist.
        {
            const RowLayoutPlan *small = ensureRow(L"A[[gap:12px]]B", 12);
            const RowLayoutPlan *big = ensureRow(L"A[[gap:12px]]B", 24);
            printf("    12 px: %.2f px at 12, %.2f px at 24\n", (double)small->segments[1].advance,
                   (double)big->segments[1].advance);
            Check(small->segments[1].advance == 12.0f && big->segments[1].advance == 12.0f,
                  "a gap written in px is the same pixels whatever the row's size is");
        }

        // (3) A zero gap is allowed, and advances nothing rather than becoming negative or NaN.
        // The plan is a fixed slot per row, so each side is captured as a value.
        {
            const RowLayoutPlan *zero = ensureRow(L"A[[gap:0]]B", 15);
            const float zeroAdvance = zero->segments[1].advance;
            const float zeroTextX = zero->segments[2].x;
            const float firstRunRight = zero->segments[0].x + zero->segments[0].advance;
            Check(zeroAdvance == 0.0f, "a zero gap is a zero advance");
            printf("    zero gap: advance=%.2f  first run ends at %.2f, next starts at %.2f\n",
                   (double)zeroAdvance, (double)firstRunRight, (double)zeroTextX);
            Check(zeroTextX == firstRunRight,
                  "so the run after it starts exactly where the run before it ended");
        }

        // (4) A clamped gap stays a gap: finite, positive, and inside the panel.
        {
            const RowLayoutPlan *huge = ensureRow(L"A[[gap:100]]B", 48);
            const float gap = huge->segments[1].advance;
            printf("    [[gap:100]] at 48 px -> %.2f px (cap %.0f)\n", (double)gap,
                   (double)kMaxGapPx);
            Check(huge->segments[1].isGap, "an absurd gap is still a gap element");
            Check(gap == kMaxGapPx, "and its width is the cap, not the number the writer typed");
            Check(gap > 0.0f && gap < rowW, "which is a real, on-panel advance");
        }

        // (5) The icon's own diameter counts towards the group's width, and the group's width is
        // the sum of its parts -- measured off the parts the plan draws.
        {
            const RowLayoutPlan *plan = ensureRow(L"[[left]][[icon:CPU]][[gap:1.5]]abc[[/left]]", 15);
            const float diameter = IconDiameterForFontSize(15);
            const float gap = 15.0f * 1.5f;
            const RowSegmentPlan &icon = plan->segments[0];
            const RowSegmentPlan &gapSeg = plan->segments[1];
            const RowSegmentPlan &text = plan->segments[2];
            printf("    icon+gap+text: group x=%.2f w=%.2f (icon %.2f at %.2f, gap %.2f, "
                   "text %.2f at %.2f) textSlice=\"%ls\" skip=%d segs=%d\n",
                   (double)plan->groups[0].x, (double)plan->groups[0].width, (double)diameter,
                   (double)icon.x, (double)gapSeg.advance, (double)text.advance, (double)text.x,
                   text.slice, (int)text.skip, plan->segmentCount);
            // Measured from the parts the plan draws, not from the group's own field: the two
            // must agree, and the field is what a stale derivation would have got wrong. A text
            // run counts as the width of its *ink* (what GDI+ will draw), an element as its own
            // advance -- which is the same rule the plan builds the group's width from.
            const float textDrawn =
                MeasureDrawnWidth(graphics, font, format, text.slice, rowH, (float)text.fontSize);
            const float drawnWidth = icon.advance + gapSeg.advance + textDrawn;
            Check(plan->alignCount == 1 && plan->groups[0].kind == kAlignSegLeft,
                  "the row has one left-aligned group");
            Check(diameter > 5.0f, "the icon has a real diameter to account for");
            Check(icon.isIcon && gapSeg.isGap && !text.isGap && !text.isIcon,
                  "the group holds an icon, a gap and a text run");
            Check(gapSeg.advance == gap, "the gap in the group is still the em multiple");
            Check(icon.x + diameter + (float)kIconContentGap == gapSeg.x,
                  "the gap starts after the icon's own box");
            Check(plan->groups[0].x == text.x - (icon.advance + gapSeg.advance),
                  "the text starts a whole icon and gap after the group's left edge");
            Check(plan->groups[0].width == drawnWidth,
                  "the group's width is the sum of its parts, icon diameter included");
            Check(plan->groups[0].width > diameter + gap,
                  "which is more than the icon and the gap alone");
        }

        // (6) Centred means the group's midpoint is the column's midpoint -- not the column's
        // left edge, which is what an implementation that ignored the group would place it at.
        {
            const RowLayoutPlan *plan = ensureRow(L"[[center]]CPU[[/center]]", 15);
            const AlignGroupPlan &group = plan->groups[0];
            const float mid = group.x + group.width / 2.0f;
            const float columnMid = kRowTextLeftInset + column.textWidth / 2.0f;
            printf("    centred: x=%.2f w=%.2f mid=%.2f (column mid %.2f)\n", (double)group.x,
                   (double)group.width, (double)mid, (double)columnMid);
            Check(plan->alignCount == 1 && group.kind == kAlignSegCenter,
                  "the row has one centred group");
            Check(group.width > 0.0f, "the group has a width to centre");
            Check(mid >= columnMid - 0.5f && mid <= columnMid + 0.5f,
                  "a centred group's midpoint is the text column's midpoint");
            Check(group.x > kRowTextLeftInset + 1.0f,
                  "so a centred group does not start at the left edge");
        }

        // (7) Right means the group's right edge is the column's right edge.
        {
            const RowLayoutPlan *plan = ensureRow(L"[[right]]CPU[[/right]]", 15);
            const AlignGroupPlan &group = plan->groups[0];
            printf("    right-aligned: x=%.2f w=%.2f right=%.2f (column right %.2f)\n",
                   (double)group.x, (double)group.width, (double)(group.x + group.width),
                   (double)column.textRight);
            Check(group.kind == kAlignSegRight, "the row has one right-aligned group");
            Check(group.x + group.width == column.textRight,
                  "a right-aligned group ends at the text column's right edge");
        }

        // (8) Every fitted run stays inside the panel. A grouped run's budget may not be its own
        // measured width (the mistake `ComputeInlineTextLimit`'s comment records: FitTextToWidth's
        // first test is "does it already fit", so a run handed its own width is never trimmed),
        // and it may not be the group's left edge plus a fixed width either -- the group's offset
        // is added after the fit. The fixture is a left-aligned group holding a capped gap and a
        // run long enough to need trimming.
        {
            const WCHAR *longRun = L"12345678901234567890123456789012345678901234567890";
            WCHAR row[192];
            swprintf(row, 192, L"[[left]][[gap:99px]]%ls[[/left]]", longRun);
            const RowLayoutPlan *plan = ensureRow(row, 15);
            const AlignGroupPlan &group = plan->groups[0];
            printf("    grouped run: group x=%.2f w=%.2f | text x=%.2f adv=%.2f right=%.2f "
                   "(column right %.2f)\n",
                   (double)group.x, (double)group.width, (double)plan->segments[1].x,
                   (double)plan->segments[1].advance,
                   (double)(plan->segments[1].x + plan->segments[1].advance),
                   (double)column.textRight);
            Check(plan->segmentCount == 2 && plan->segments[0].isGap &&
                      !plan->segments[1].isGap,
                  "the group is a gap and a run");
            Check(plan->segments[1].advance > 0.0f, "the grouped run has a real width");
            Check(plan->segments[1].x + plan->segments[1].advance <= column.textRight + 0.5f,
                  "every fitted run stays inside the panel (its right edge is bounded by the "
                  "column, or by its group)");
            Check(group.x >= kRowTextLeftInset, "and the group never starts off the panel's edge");
        }

        // (9) A group wider than the column degrades to left-aligned: it does not start off the
        // panel on the left, and its runs are still fitted inside the column. A text run is
        // trimmed to the column, so a group only comes out wider than the column when a
        // *fixed-size* part makes it so -- an icon, or a gap wider than a whole column. Both are
        // in the row below, and both are content, so this is the shape a writer can reach.
        {
            WCHAR row[160];
            swprintf(row, 160, L"[[center]][[icon:CPU]][[gap:99px]][[icon:Memory]][[gap:99px]]"
                               L"[[icon:Disk]][[/center]]");
            const RowLayoutPlan *plan = ensureRow(row, 48);
            const AlignGroupPlan &group = plan->groups[0];
            printf("    over-wide group: x=%.2f w=%.2f (column width %.2f, %d segments)\n",
                   (double)group.x, (double)group.width, (double)column.textWidth,
                   plan->segmentCount);
            Check(group.width > column.textWidth, "the group really is wider than the column");
            Check(group.x == kRowTextLeftInset, "an over-wide group is clamped to the left edge");
            Check(plan->segments[0].x == kRowTextLeftInset,
                  "so its first element starts at the column's left edge, on the panel");
            for (int s = 0; s < plan->segmentCount; s++) {
                if (plan->segments[s].isIcon || plan->segments[s].isGap) continue;
                Check(plan->segments[s].x + plan->segments[s].advance <= column.textRight + 0.5f,
                      "and its text runs are still fitted inside the column");
            }
        }

        // (10) A gap inside a group is ordinary content: the group's placement moves it exactly
        // as far as it moves the runs beside it.
        {
            const float gapPx = 15.0f * 1.5f;
            const RowLayoutPlan *grouped =
                ensureRow(L"[[right]][[gap:1.5]]CPU[[/right]]", 15);
            const float groupedAdv =
                grouped->segmentCount > 0 ? grouped->segments[0].advance : -1.0f;
            const int groupedIsGap = grouped->segmentCount > 0 && grouped->segments[0].isGap;
            const float groupedX = grouped->segmentCount > 0 ? grouped->segments[0].x : -1.0f;
            const float nextX = grouped->segmentCount > 1 ? grouped->segments[1].x : -1.0f;
            const int groupedCount = grouped->segmentCount;
            const int groupedAlign = grouped->alignCount;
            const float groupedGroupX = groupedAlign > 0 ? grouped->groups[0].x : -1.0f;
            const RowLayoutPlan *loose = ensureRow(L"CPU[[gap:1.5]]x", 15);
            const float looseAdv = loose->segments[1].advance;
            printf("    gap in a right group: segs=%d align=%d x=%.2f adv=%.2f isGap=%d next=%.2f "
                   "(loose gap %.2f, group x %.2f)\n",
                   groupedCount, groupedAlign, (double)groupedX, (double)groupedAdv, groupedIsGap,
                   (double)nextX, (double)looseAdv, (double)groupedGroupX);
            Check(groupedIsGap, "a gap inside a group is a gap element");
            Check(groupedAdv == gapPx, "the alignment group does not change the gap's width");
            Check(nextX == groupedX + gapPx, "the run after the gap starts one gap further right");
            Check(groupedX == groupedGroupX, "and the gap is placed at the group's own left edge");
            // A gap is ordinary content: the group's placement moves it too. In a left-aligned
            // group it would sit at the column's inset, so a right-aligned one landing somewhere
            // else is the group's own offset reaching the gap.
            Check(groupedX != kRowTextLeftInset,
                  "a right-aligned group carries the gap away from the column's inset");
        }

        // (11) Groups do not nest, and an unclosed group ends at the row's end. Both are read off
        // the plan's group list, which is built from the markers in order.
        {
            const RowLayoutPlan *nested =
                ensureRow(L"a[[left]]b[[center]]c[[/center]]d", 15);
            printf("    nested openers -> %d group(s): kind %d (segs %d..%d), kind %d (segs %d..%d)"
                   "\n",
                   nested->alignCount, nested->groups[0].kind, nested->groups[0].firstSegment,
                   nested->groups[0].lastSegment, nested->groups[1].kind,
                   nested->groups[1].firstSegment, nested->groups[1].lastSegment);
            Check(nested->alignCount == 2,
                  "two openers are two groups, not one nested inside the other");
            Check(nested->groups[0].kind == kAlignSegLeft &&
                      nested->groups[1].kind == kAlignSegCenter,
                  "and each keeps its own alignment");
            Check(nested->groups[0].lastSegment < nested->groups[1].firstSegment,
                  "the groups are consecutive, never overlapping");

            const RowLayoutPlan *unclosed = ensureRow(L"[[center]]abc", 15);
            printf("    unclosed -> %d group(s), covers segs %d..%d of %d\n",
                   unclosed->alignCount, unclosed->groups[0].firstSegment,
                   unclosed->groups[0].lastSegment, unclosed->segmentCount);
            Check(unclosed->alignCount == 1, "an unclosed group is still one group");
            Check(unclosed->groups[0].lastSegment == unclosed->segmentCount - 1,
                  "and it ends at the row's own end, not past it");

            const RowLayoutPlan *stray = ensureRow(L"abc[[/left]]def", 15);
            Check(stray->alignCount == 0, "a closing tag that closes nothing makes no group");
            Check(stray->segmentCount > 0 && !stray->segments[0].isGap,
                  "and the text around it is untouched");
        }

        // (12) The reported bug: the character that was written just before the closing tag of an
        // alignment group disappeared from the panel -- `[[center]]ABC[[/center]]` showed `AB`.
        // The enclosed text, read off the plan the painter consumes, has to be byte-identical to
        // the text between the tags, for every alignment, for one / two / many characters, and
        // wherever the group sits in the row. The width the group reports has to be the width of
        // that text, so the previously swallowed character also has to come back in the pixels.
        {
            const WCHAR *names[3] = {L"left", L"center", L"right"};
            const WCHAR *contents[4] = {L"A", L"AB", L"ABC", L"ABCDEFGHIJ"};
            const int aligns[3] = {kAlignSegLeft, kAlignSegCenter, kAlignSegRight};

            for (int a = 0; a < 3; a++) {
                for (int c = 0; c < 4; c++) {
                    // The group at the start of the row, in the middle (text on both sides), and
                    // at the end of the row: the three positions its close marker can sit in.
                    WCHAR rows[3][192];
                    swprintf(rows[0], 192, L"[[%ls]]%ls[[/%ls]]", names[a], contents[c], names[a]);
                    swprintf(rows[1], 192, L"x[[%ls]]%ls[[/%ls]]y", names[a], contents[c], names[a]);
                    swprintf(rows[2], 192, L"xy[[%ls]]%ls[[/%ls]]", names[a], contents[c], names[a]);
                    const WCHAR *where[3] = {L"alone", L"middle", L"at end"};
                    for (int w = 0; w < 3; w++) {
                        const RowLayoutPlan *plan = ensureRow(rows[w], 15);
                        const int groupIndex = groupIndexOf(plan, aligns[a]);
                        WCHAR seen[128];
                        seen[0] = L'\0';
                        if (groupIndex >= 0) groupText(plan, groupIndex, seen, 128);
                        printf("    [[%ls]]%ls[[/%ls]] %-6ls -> \"%ls\" (%d seg, align %d)\n",
                               names[a], contents[c], names[a], where[w], seen, plan->segmentCount,
                               plan->alignCount);
                        Check(groupIndex >= 0, "the row's alignment group is in the plan at all");
                        if (groupIndex < 0) continue;
                        Check(wcscmp(seen, contents[c]) == 0,
                              "the group's enclosed text is byte-identical to the text between "
                              "the tags");
                        const float expected =
                            MeasureAdvance(graphics, font, format, contents[c], rowH);
                        printf("      group width %.2f vs the text's own advance %.2f\n",
                               (double)plan->groups[groupIndex].width, (double)expected);
                        Check(plan->groups[groupIndex].width >= expected - 0.5f,
                              "and the group's width counts the last character too");
                    }
                }
            }

            // The two shapes that are not a matched pair: a group left open, and a close tag with
            // nothing open. Both must show every character that was typed.
            {
                const RowLayoutPlan *unclosed = ensureRow(L"[[center]]ABC", 15);
                WCHAR seen[64];
                groupText(unclosed, 0, seen, 64);
                printf("    unclosed [[center]]ABC -> \"%ls\"\n", seen);
                Check(wcscmp(seen, L"ABC") == 0, "an unclosed group keeps all of its text");

                const RowLayoutPlan *stray = ensureRow(L"A[[center]]BC[[/left]]D", 15);
                groupText(stray, 0, seen, 64);
                printf("    stray [[/left]] inside a group -> group text \"%ls\"\n", seen);
                Check(wcscmp(seen, L"BC") == 0,
                      "a stray closing tag cuts nothing out of the group");
            }
        }

        // (12b) The user's second report: the alignment group is still not fixed on the real
        // machine.
        //
        //   * one character in the group -> it is drawn off its position (偏离原位);
        //   * more than one             -> the last character is not drawn at all, CJK and ASCII
        //                                  alike;
        //   * appending a space to the text fixes it, in both languages;
        //   * exact example `[[h:100]] [[center]]你好[[/center]]` draws only `你`;
        //   * static and dynamic rows alike, and tag parsing is not involved.
        //
        // The width of an alignment group was the sum of its parts' pen advances, and the layout box
        // the painter hands GDI+ was that same advance. The two are not the same number: GDI+
        // reports a string's width as the extent of its *inked* glyphs, and the last glyph's ink
        // reaches past where the pen stops (its right side bearing). Two facts, both measured below
        // with the real font at 15 px:
        //
        //   * the pen advance of "W" is 14.43 px while GDI+ reports 19.43 px for the same string,
        //     a 5.00 px difference that stays 5.00 for "WW" and "WWW" because only the trailing
        //     glyph contributes;
        //   * drawing "WWW" in a box of exactly its pen advance (43.29) puts only two of the three
        //     glyphs on the bitmap, while a box of the drawn width (48.29) keeps all three.
        //
        // GDI+ does not overflow the box, it *drops* what does not fit -- so a layout box of the
        // advance width loses the last glyph of the run. The budget and the box therefore have to
        // be at least the **drawn width**, which is the number `MeasureDrawnWidth` owns: the pen
        // advance, or the extent GDI+ reports, whichever is larger. A group whose last run is
        // clipped loses its last character (the user's "more than one" case); with a single
        // character the clipped box is narrower than the ink while the group is still placed by the
        // pen advance, which is the "off its position" case. A trailing space hides both, because
        // it adds pen advance *after* the last inked glyph and so moves the clip edge past the ink.
        //
        // `MeasureAdvance` is not wrong and does not change: it is the pen advance, and the flow
        // cursor, the group's placement and the natural width all want exactly that. What was wrong
        // is using it as the layout box. `MeasureDrawnWidth` is the second and only other number,
        // and the two call sites that need it (the box, and the group's width) both ask it.
        {
            // The independently obtained quantity: what GDI+ itself reports for the string, asked
            // in a box far wider than anything the panel can produce, so no clipping can be
            // involved in obtaining it.
            auto reportedWidth = [&](GpFont *f, const WCHAR *text) -> float {
                RectF box(0.0f, 0.0f, 4000.0f, rowH), bounds = {};
                INT cp = 0, lines = 0;
                g_gdiplus.MeasureString(graphics, text, -1, f, &box, format, &bounds, &cp, &lines);
                return bounds.Width;
            };

            // (a) The measurement the whole thing rests on: the width a run is given for its layout
            // box is never less than the width GDI+ reports for the same text. Red on the committed
            // tree, where the box was the pen advance alone -- the gap is the trailing glyph's
            // right side bearing (5.00 px at 15 px for `W`, and it grows with the size).
            {
                int worstSize = -1;
                float worstGap = 0.0f;
                const WCHAR *probe[5] = {L"W", L"WWW", L"\x4F60", L"\x4F60\x597D",
                                         L"\x4F60\x597D\x4E16"};
                for (int fs = 10; fs <= 30; fs++) {
                    GpFont *f = AcquireRowFont(graphics, L"Segoe UI", fs);
                    if (!f) continue;
                    for (int i = 0; i < 5; i++) {
                        const float gap =
                            reportedWidth(f, probe[i]) -
                            MeasureDrawnWidth(graphics, f, format, probe[i], rowH, (float)fs);
                        if (gap > worstGap) {
                            worstGap = gap;
                            worstSize = fs;
                        }
                    }
                }
                printf("    the drawn width falls short of what GDI+ reports by up to %.4f px "
                       "(at %d px)\n",
                       (double)worstGap, worstSize);
                Check(worstGap <= 0.01f,
                      "a run's layout box is at least as wide as the text GDI+ draws in it");
            }

            // (b) The same thing in pixels, because pixels are the user's evidence. `penAdvance`
            // is computed here with the sentinel trick alone -- the number the old measurement
            // returned -- so the two boxes below are "the box the old code built" and "the box the
            // code builds now", for the same string. Two of the three glyphs against three.
            {
                const int W = 300, H = 40;
                BITMAPINFO bmi = {};
                bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
                bmi.bmiHeader.biWidth = W;
                bmi.bmiHeader.biHeight = -H;
                bmi.bmiHeader.biPlanes = 1;
                bmi.bmiHeader.biBitCount = 32;
                bmi.bmiHeader.biCompression = BI_RGB;
                void *bits = nullptr;
                HBITMAP dib = CreateDIBSection(nullptr, &bmi, DIB_RGB_COLORS, &bits, nullptr, 0);
                GpBitmap *bmp = nullptr;
                GpGraphics *g2 = nullptr;
                GpSolidFill *solid = nullptr;
                g_gdiplus.CreateSolidFill(0xFFFFFFFF, &solid);
                bool ready = false;
                if (dib && bits && solid &&
                    g_gdiplus.CreateBitmapFromScan0(W, H, W * 4, kPixelFormat32bppPARGB,
                                                    (BYTE *)bits, &bmp) == 0 &&
                    g_gdiplus.GetImageGraphicsContext((GpImage *)bmp, &g2) == 0 && g2) {
                    g_gdiplus.SetTextRenderingHint(g2, kTextHintAntiAliasGridFit);
                    ready = true;
                }
                if (ready) {
                    GpFont *f = AcquireRowFont(graphics, L"Segoe UI", 15);
                    auto inkColumns = [&](const WCHAR *text, float boxW) {
                        memset(bits, 0, (size_t)W * H * 4);
                        RectF layout(0.0f, 0.0f, boxW, rowH);
                        g_gdiplus.DrawString(g2, text, -1, f, &layout, format, (GpBrush *)solid);
                        int last = -1;
                        const DWORD *px = (const DWORD *)bits;
                        for (int x = 0; x < W; x++) {
                            for (int y = 0; y < H; y++) {
                                if ((px[y * W + x] & 0x00FFFFFFu) != 0) {
                                    last = x;
                                    break;
                                }
                            }
                        }
                        return last;
                    };
                    // The pen advance on its own, for a given font: the string plus the sentinel,
                    // minus the sentinel. This is exactly what the product measured before the fix,
                    // so the two boxes below are "the box the old code built" against "the box the
                    // code builds now", for the same string and the same font.
                    auto penAdvanceFor = [&](GpFont *pf, const WCHAR *text) {
                        WCHAR withSentinel[64];
                        swprintf(withSentinel, 64, L"%ls\x2502", text);
                        return reportedWidth(pf, withSentinel) - reportedWidth(pf, L"\x2502");
                    };
                    // `WWW` is three glyphs of one advance each, so "the ink reaches about two
                    // advances in" is exactly "the third glyph is still there".
                    const WCHAR *three = L"WWW";
                    const float pen = penAdvanceFor(f, three);
                    const float onePen = penAdvanceFor(f, L"W");
                    const float measured = MeasureDrawnWidth(graphics, f, format, three, rowH, 15.0f);
                    const int tight = inkColumns(three, pen);
                    const int wide = inkColumns(three, measured);
                    printf("    fs=15 \"W\": pen=%.4f reported=%.4f | \"WWW\": pen=%.4f measured="
                           "%.4f -> ink columns %d (box=pen) / %d (box=measured)\n",
                           (double)onePen, (double)reportedWidth(f, L"W"), (double)pen,
                           (double)measured, tight, wide);
                    Check(tight < (int)(pen * 2.0f),
                          "a box of the pen advance alone really does drop the last glyph (this is "
                          "the reported symptom)");
                    // "The last glyph is back" is about *ink*, not about a column count: the box
                    // that holds the drawn width has to put materially more of the run on the
                    // bitmap than the box that holds only the advance. (An exact column count
                    // would be a font-metric assertion, not a layout one.)
                    Check(wide >= tight + 2 && wide >= (int)(measured * 0.9f),
                          "while a box of the drawn width keeps every glyph of the run");
                    Check(measured + 0.5f >= reportedWidth(f, three),
                          "and the drawn width is at least what GDI+ reports for the string");
                } else {
                    Skip("the ink probe bitmap could be created (real-text checks)");
                }
                if (solid) g_gdiplus.DeleteBrush((GpBrush *)solid);
                if (g2) g_gdiplus.DeleteGraphics(g2);
                if (bmp) g_gdiplus.DisposeImage((GpImage *)bmp);
                if (dib) DeleteObject(dib);
            }

            // (c) The layout, over the whole matrix the report describes: CJK and ASCII, one / two
            // / many characters, all three alignments, the group at the start / middle / end of
            // the row, with and without the trailing space. The plan has to draw exactly the
            // characters between the tags, the group has to be as wide as what it draws, no run
            // may have a box narrower than the text GDI+ will put in it, and a centred group has
            // to be centred on its own text.
            {
                const WCHAR *names[3] = {L"left", L"center", L"right"};
                const WCHAR *content[6] = {
                    L"W", L"WWW", L"ABCDEFGHIJ", L"\x4F60", L"\x4F60\x597D",
                    L"\x4F60\x597D\x4E16\x754C\x4E5F\x597D",
                };
                const WCHAR *where[3] = {L"at row start", L"in the middle", L"at row end"};
                int lost = 0, shallow = 0, clipped = 0, offCentre = 0;
                int lastBadSize = 0;
                for (int fs = 10; fs <= 30; fs += 2) {
                    for (int a = 0; a < 3; a++) {
                        for (int c = 0; c < 6; c++) {
                            for (int w = 0; w < 3; w++) {
                                for (int sp = 0; sp < 2; sp++) {
                                    WCHAR row[256];
                                    WCHAR body[160];
                                    swprintf(body, 160, L"[[%ls]]%ls%ls[[/%ls]]", names[a],
                                             content[c], sp ? L" " : L"", names[a]);
                                    if (w == 0) swprintf(row, 256, L"%ls", body);
                                    else if (w == 1) swprintf(row, 256, L"x%lsy", body);
                                    else swprintf(row, 256, L"xy%ls", body);
                                    const RowLayoutPlan *plan = ensureRow(row, fs);
                                    const int gi = groupIndexOf(plan, kAlignSegLeft + a);
                                    WCHAR seen[256];
                                    seen[0] = L'\0';
                                    if (gi >= 0) groupText(plan, gi, seen, 256);
                                    WCHAR want[160];
                                    swprintf(want, 160, L"%ls%ls", content[c], sp ? L" " : L"");
                                    if (wcscmp(seen, want) != 0) {
                                        lost++;
                                        lastBadSize = fs;
                                        if (lost <= 3) {
                                            printf("      LOST at %d px: [[%ls]] %ls \"%ls\" -> "
                                                   "\"%ls\" (segs=%d align=%d)\n",
                                                   fs, names[a], where[w], content[c], seen,
                                                   plan->segmentCount, plan->alignCount);
                                        }
                                        if (lost <= 3) {
                                            for (int s = 0; s < plan->segmentCount; s++) {
                                                const RowSegmentPlan &e = plan->segments[s];
                                                printf("        seg%d grp=%d skip=%d ring=%d icon=%d "
                                                       "gap=%d fs=%d x=%.2f adv=%.2f box=%.2f "
                                                       "fitted=%d sliceLen=%d\n",
                                                       s, e.group, (int)e.skip, (int)e.isRing,
                                                       (int)e.isIcon, (int)e.isGap, e.fontSize,
                                                       (double)e.x, (double)e.advance,
                                                       (double)e.boxWidth, (int)e.fitted,
                                                       (int)wcslen(e.slice));
                                            }
                                        }
                                    }
                                    if (gi < 0) continue;
                                    const AlignGroupPlan &gr = plan->groups[gi];
                                    float drawn = 0.0f;
                                    for (int s = gr.firstSegment; s <= gr.lastSegment; s++) {
                                        const RowSegmentPlan &e = plan->segments[s];
                                        if (e.skip || e.isRing || e.isIcon || e.isGap) continue;
                                        GpFont *rf = AcquireRowFont(
                                            graphics, e.family[0] ? e.family : L"Segoe UI",
                                            e.fontSize);
                                        if (!rf) rf = font;
                                        const float need = MeasureDrawnWidth(
                                            graphics, rf, format, e.slice, rowH, (float)e.fontSize);
                                        drawn += need;
                                        if (e.boxWidth + 0.01f < need) {
                                            clipped++;
                                            lastBadSize = fs;
                                            if (clipped <= 3) {
                                                printf("      CLIPPED at %d px: [[%ls]] \"%ls\" "
                                                       "box=%.3f drawn=%.3f (seg fs=%d adv=%.3f, "
                                                       "remeasured=%.3f, planRowFs=%d)\n",
                                                       fs, names[a], e.slice, (double)e.boxWidth,
                                                       (double)need, e.fontSize,
                                                       (double)e.advance,
                                                       (double)MeasureAdvance(graphics, rf, format,
                                                                              e.slice, rowH),
                                                       plan->rowFontSize);
                                            }
                                        }
                                    }
                                    if (gr.width + 0.01f < drawn) {
                                        shallow++;
                                        lastBadSize = fs;
                                        if (shallow <= 3) {
                                            printf("      SHALLOW at %d px: [[%ls]] %ls w=%.3f "
                                                   "drawn=%.3f\n",
                                                   fs, names[a], where[w], (double)gr.width,
                                                   (double)drawn);
                                        }
                                    }
                                    if (a == 1 && drawn > 0.0f) {
                                        // A group's own bounds are what it is centred within; the
                                        // acceptance here is a couple of pixels, because the ink
                                        // box and the advance differ by the trailing glyph's side
                                        // bearing and a sub-pixel box difference is not the
                                        // "drawn off its position" the report is about.
                                        const float textCentre =
                                            kRowTextLeftInset + column.textWidth / 2.0f;
                                        const float drawnCentre = gr.x + drawn / 2.0f;
                                        if (drawnCentre < textCentre - 2.5f ||
                                            drawnCentre > textCentre + 2.5f) {
                                            offCentre++;
                                            lastBadSize = fs;
                                            if (offCentre <= 3) {
                                                printf("      OFF-CENTRE at %d px: \"%ls\" centre="
                                                       "%.3f want=%.3f\n",
                                                       fs, content[c], (double)drawnCentre,
                                                       (double)textCentre);
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
                printf("    layout matrix 10..30 px: lost=%d shallow=%d clipped=%d offCentre=%d "
                       "(last bad at %d px)\n",
                       lost, shallow, clipped, offCentre, lastBadSize);
                Check(lost == 0,
                      "the group draws every character between its tags, at every font size and "
                      "both scripts");
                Check(shallow == 0,
                      "and the group is as wide as the text it draws (so it is centred where that "
                      "text is)");
                Check(clipped == 0,
                      "and no run is given a box narrower than the text GDI+ draws in it");
                Check(offCentre == 0, "and a centred group is centred on its own text");
            }

            // (d) The exact strings from the report, with the raw numbers printed so a failure
            // names the shape, plus the variants the report calls out (the trailing space, a group
            // at the row's start with text after it, and a [[size:24]] run in the same row).
            {
                struct UserCase {
                    const WCHAR *row;
                    const WCHAR *want;
                    const char *label;
                };
                const UserCase cases[] = {
                    {L"[[h:100]] [[center]]\x4F60\x597D[[/center]]", L"\x4F60\x597D",
                     "the reported row (h:100 + leading space + centre CJK)"},
                    {L"[[h:100]] [[center]]hello[[/center]]", L"hello", "the same row in ASCII"},
                    {L"[[h:100]] [[center]]\x4F60[[/center]]", L"\x4F60", "one CJK character"},
                    {L"[[h:100]] [[center]]W[[/center]]", L"W", "one ASCII character"},
                    {L"[[h:100]] [[center]]\x4F60\x597D [[/center]]", L"\x4F60\x597D ",
                     "the reported row with the trailing space that fixes it"},
                    {L"[[h:100]] [[center]]\x4F60\x597D[[/center]] [[after]]",
                     L"\x4F60\x597D", "a group at the row's start with text after it"},
                    {L"[[h:100]] [[size:24]]\x4F60\x597D[[/size]][[center]]x[[/center]]",
                     L"x", "a [[size:24]] run in the same row as the group"},
                };
                for (const UserCase &uc : cases) {
                    const RowLayoutPlan *plan = ensureRow(uc.row, 15);
                    WCHAR seen[128];
                    seen[0] = L'\0';
                    int gi = -1;
                    for (int g = 0; g < plan->alignCount; g++) {
                        if (plan->groups[g].kind == kAlignSegCenter) gi = g;
                    }
                    if (gi < 0 && plan->alignCount > 0) gi = 0;
                    Check(gi >= 0, "the reported row's group is in the plan at all");
                    if (gi < 0) continue;
                    groupText(plan, gi, seen, 128);
                    const AlignGroupPlan &gr = plan->groups[gi];
                    GpFont *f = AcquireRowFont(graphics, L"Segoe UI", 15);
                    float pen = 0.0f;
                    for (int s = gr.firstSegment; s <= gr.lastSegment; s++) {
                        const RowSegmentPlan &e = plan->segments[s];
                        if (e.skip || e.isRing || e.isIcon || e.isGap) continue;
                        GpFont *rf = AcquireRowFont(graphics,
                                                    e.family[0] ? e.family : L"Segoe UI",
                                                    e.fontSize);
                        if (!rf) rf = f;
                        pen += MeasureAdvance(graphics, rf, format, e.slice, rowH);
                    }
                    printf("    %s\n      drawn \"%ls\" want \"%ls\" | group x=%.2f w=%.2f pen=%.2f "
                           "| column %.2f..%.2f\n",
                           uc.label, seen, uc.want, (double)gr.x, (double)gr.width, (double)pen,
                           (double)kRowTextLeftInset, (double)column.textRight);
                    Check(wcscmp(seen, uc.want) == 0,
                          "the reported row's group draws its whole text, and nothing else");
                    Check(gr.width + 0.01f >= pen, "and its width is at least what it draws");
                    Check(gr.x + gr.width <= column.textRight + 0.5f,
                          "and the group still ends inside the panel's text column");
                    for (int s = gr.firstSegment; s <= gr.lastSegment; s++) {
                        const RowSegmentPlan &e = plan->segments[s];
                        if (e.skip || e.isRing || e.isIcon || e.isGap) continue;
                        GpFont *rf = AcquireRowFont(graphics,
                                                    e.family[0] ? e.family : L"Segoe UI",
                                                    e.fontSize);
                        if (!rf) rf = f;
                        Check(e.boxWidth + 0.01f >=
                                  MeasureDrawnWidth(graphics, rf, format, e.slice, rowH,
                                                    (float)e.fontSize),
                              "and every run of it has a box wide enough for its own glyphs");
                    }
                }
            }
        }

        // (13) The reported bug: a group is placed by its own tags, so a run *inside* one must
        // never be fitted against wherever the row's flow happened to leave the cursor.
        //
        // With a flow run long enough to reach near the column's right edge,
        // `[[center]]ABC[[/center]]` drew `AB` -- the last character of the enclosed content was
        // gone -- and one 'W' later the whole group was gone, while the group itself sat in the
        // middle of a panel with room to spare. Phase A fitted the grouped run against the flow
        // cursor ("the widest prefix that fits *there*"), and phases B and C can only ever
        // *shorten* a run, so that trim was permanent. The rule the code's own phase A comment
        // states is the opposite -- "phase A fits against the column, and phase C re-fits once the
        // group sits where it sits" -- and a group's position is not known until phase B.
        //
        // The prefix is a flow run of 'W', the widest letter in this font, so the flow cursor
        // walks from the row's inset out past the column's right edge and each alignment meets
        // the situation somewhere in the sweep.
        {
            const WCHAR *names[3] = {L"left", L"center", L"right"};
            for (int a = 0; a < 3; a++) {
                bool keptEveryCharacter = true;
                bool widthCountsThem = true;
                bool insideTheColumn = true;
                int firstBadPrefix = -1;
                for (int n = 0; n <= 40; n++) {
                    WCHAR row[192];
                    int used = 0;
                    for (int k = 0; k < n; k++) row[used++] = L'W';
                    swprintf(row + used, 192 - used, L"[[%ls]]ABC[[/%ls]]", names[a], names[a]);
                    const RowLayoutPlan *plan = ensureRow(row, 15);
                    if (plan->alignCount != 1) {
                        keptEveryCharacter = false;
                        continue;
                    }
                    WCHAR seen[128];
                    groupText(plan, 0, seen, 128);
                    const float wanted = MeasureAdvance(graphics, font, format, L"ABC", rowH);
                    if (wcscmp(seen, L"ABC") != 0) {
                        keptEveryCharacter = false;
                        if (firstBadPrefix < 0) firstBadPrefix = n;
                    }
                    if (plan->groups[0].width < wanted - 0.5f) widthCountsThem = false;
                    if (plan->groups[0].x + plan->groups[0].width > column.textRight + 0.5f) {
                        insideTheColumn = false;
                    }
                    // Two samples of the sweep, so a failure names the shape that shows it.
                    if (n == 26 || n == 40) {
                        printf("    %2d W + [[%ls]]ABC[[/%ls]] -> group \"%ls\" w=%.2f (text "
                               "%.2f) x=%.2f\n",
                               n, names[a], names[a], seen, (double)plan->groups[0].width,
                               (double)wanted, (double)plan->groups[0].x);
                    }
                }
                printf("    [[%ls]]: shortest prefix that loses a character = %d W (of 40)\n",
                       names[a], firstBadPrefix);
                Check(keptEveryCharacter,
                      "a group's content survives a long flow run before it (all three "
                      "alignments)");
                Check(widthCountsThem,
                      "and the group's width counts the characters the flow run was stealing");
                Check(insideTheColumn, "while the group itself stays inside the text column");
            }
        }
        {
            // A randomized net under the two blocks above: whatever the markers do to *grouping*,
            // the characters the plan draws must be exactly the row's own text with the six
            // alignment tags removed. A stray `[[/left]]` vanishes (that is the documented rule)
            // and a well-formed tag that is never honoured would draw as text -- the grammar is
            // only the six tags, so "removed" is what every one of them must be.
            //
            // The pieces include CJK: the defect this net sits under is a measurement one, and a
            // net made only of ASCII would not exercise the glyphs whose ink and advance differ
            // most. `[[size:24]]` is in there for the same reason: a run's own size changes which
            // font measures and draws it.
            const WCHAR *pieces[] = {
                L"A", L"BC", L"xy", L" ", L"[[left]]", L"[[/left]]", L"[[center]]",
                L"[[/center]]", L"[[right]]", L"[[/right]]", L"Q", L"[[left]]", L"[[/center]]",
                L"\x4F60", L"\x597D", L"\x4F60\x597D", L"\x4E16\x754C", L"[[size:24]]",
                L"[[/size]]",
            };
            const int pieceCount = (int)(sizeof(pieces) / sizeof(pieces[0]));
            unsigned seed = 12345u;
            int mismatches = 0;
            for (int iter = 0; iter < 4000 && mismatches < 6; iter++) {
                WCHAR row[256];
                int used = 0;
                row[0] = L'\0';
                int count = 1 + (int)(seed % 7);
                for (int i = 0; i < count; i++) {
                    seed = seed * 1103515245u + 12345u;
                    const WCHAR *piece = pieces[(seed >> 16) % pieceCount];
                    const int len = (int)wcslen(piece);
                    if (used + len + 1 >= 256) break;
                    for (int k = 0; k < len; k++) row[used++] = piece[k];
                    row[used] = L'\0';
                }
                if (used == 0) continue;
                // What the row says once its alignment tags are gone: every one of the six is
                // well formed, and a stray closer vanishes too (that is the documented rule), so
                // removing all six substrings is the expected drawn text.
                WCHAR expected[256];
                int eused = 0;
                for (const WCHAR *q = row; *q && eused + 1 < 256;) {
                    int matched = 0;
                    if (*q == L'[' && q[1] == L'[') {
                        // Every **inline** marker the pieces can produce is removed from the
                        // expected text, because that is what the layout does with them: they are
                        // markup, not characters. The six alignment tags are the ones this net is
                        // about; the two `[[size:…]]` markers are here because a run's own size is
                        // what makes it measure and draw in a different font, which is the code
                        // path the net above exists to exercise.
                        static const WCHAR *const tags[] = {
                            L"[[left]]", L"[[/left]]", L"[[center]]", L"[[/center]]",
                            L"[[right]]", L"[[/right]]", L"[[size:24]]", L"[[/size]]",
                        };
                        for (int t = 0; t < 8; t++) {
                            const int len = (int)wcslen(tags[t]);
                            if (wcsncmp(q, tags[t], len) == 0) {
                                q += len;
                                matched = 1;
                                break;
                            }
                        }
                    }
                    if (!matched) expected[eused++] = *q++;
                }
                expected[eused] = L'\0';
                const RowLayoutPlan *plan = ensureRow(row, 15);
                WCHAR drawn[1024];
                int dused = 0;
                for (int s = 0; s < plan->segmentCount && dused + 1 < 1024; s++) {
                    const RowSegmentPlan &entry = plan->segments[s];
                    if (entry.skip || entry.isRing || entry.isIcon || entry.isGap) continue;
                    for (const WCHAR *ch = entry.slice; *ch && dused + 1 < 1024; ch++)
                        drawn[dused++] = *ch;
                }
                drawn[dused] = L'\0';
                if (wcscmp(drawn, expected) != 0) {
                    mismatches++;
                    DumpWide("row     ", row);
                    DumpWide("drawn   ", drawn);
                    DumpWide("expected", expected);
                    printf("      segs=%d align=%d\n", plan->segmentCount, plan->alignCount);
                }
            }
            printf("    fuzz mismatches: %d\n", mismatches);
            Check(mismatches == 0,
                  "over 4000 random rows: the alignment markers never change the text");
        }

        g_settings = savedSettings;
        g_rowState = savedState;
        g_publishedRows = savedRows;
        g_layoutEpoch = savedEpoch + 1;
    }

    // The trailing-space sentinel: GDI+ MeasureString deliberately ignores
                    // trailing spaces, so "9%  " would advance no further than "9%" and an
                    // inline gauge would land on top of the '%'. Measured here with the
                    // real font; remove the sentinel from MeasureAdvance and this goes red.
                    {
                        const float bare =
                            MeasureAdvance(graphics, font, format, L"CPU 12%", boxHeight);
                        const float padded =
                            MeasureAdvance(graphics, font, format, L"CPU 12%   ", boxHeight);
                        printf("  trailing spaces: \"CPU 12%%\" = %.1f px, with 3 spaces = %.1f px\n",
                               bare, padded);
                        Check(padded > bare + 1.0f,
                              "trailing spaces advance the run (that is what the sentinel buys)");
                    }

                    // The ellipsis width used to be cached "once per process" on the claim
                    // that the font never changes. The painter rebuilds the font from the
                    // settings on every frame, so after a font-size change the cache held
                    // the old width and a trimmed run could overshoot its budget by the
                    // difference -- but only for the budgets where the kept prefix happens
                    // to fill the (underestimated) reservation, which is why this sweeps
                    // budgets instead of testing one. The small-font primer is what fills
                    // the old cache; it is the point of the check, not decoration.
                    {
                        GpFont *bigFont = nullptr;
                        if (family) g_gdiplus.CreateFont(family, 24.0f, 0, 0, &bigFont);
                        if (bigFont) {
                            WCHAR primer[640];
                            wcsncpy(primer, L"Memory utilization is climbing steadily", 639);
                            primer[639] = L'\0';
                            const bool primerTrimmed =
                                FitTextToWidth(graphics, font, format, primer,
                                               sizeof(primer) / sizeof(primer[0]), 90.0f,
                                               boxHeight);

                            float worstOver = 0.0f, worstBudget = 0.0f;
                            int trimmedCount = 0;
                            for (int budget = 40; budget <= 200; budget++) {
                                WCHAR run[640];
                                wcsncpy(run, L"Memory utilization is climbing steadily", 639);
                                run[639] = L'\0';
                                if (FitTextToWidth(graphics, bigFont, format, run,
                                                   sizeof(run) / sizeof(run[0]),
                                                   (float)budget, boxHeight)) {
                                    trimmedCount++;
                                }
                                const float drawn =
                                    MeasureAdvance(graphics, bigFont, format, run, boxHeight);
                                const float over = drawn - ((float)budget + 0.6f);
                                if (over > worstOver) {
                                    worstOver = over;
                                    worstBudget = (float)budget;
                                }
                            }
                            printf("  primer(15 px) trimmed=%d; 24 px font over %d budgets "
                                   "(40..200): %d trimmed, worst overshoot %.1f px at %.0f\n",
                                   primerTrimmed ? 1 : 0, 161, trimmedCount, worstOver,
                                   worstBudget);
                            Check(primerTrimmed,
                                  "the small font trims too (this is what primes the cache)");
                            Check(worstOver <= 0.0f,
                                  "a trimmed run fits its budget under any second font size");
                            Check(trimmedCount > 100,
                                  "and it really is being trimmed at most of those budgets");
                            g_gdiplus.DeleteFont(bigFont);
                        } else {
                            Skip("a second font size can be created (ellipsis cache check)");
                        }
                    }
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
                    Skip("Segoe UI is installed (real-text checks)");
                }
                if (format) g_gdiplus.DeleteStringFormat(format);
                if (font) g_gdiplus.DeleteFont(font);
                if (family) g_gdiplus.DeleteFontFamily(family);
                g_gdiplus.DeleteGraphics(graphics);
            } else {
                Skip("the probe surface could be built (real-text checks)");
            }
            if (bitmap) g_gdiplus.DisposeImage((GpImage *)bitmap);
            if (dib) DeleteObject(dib);
            // GDI+ is deliberately *not* shut down here any more. `GdiplusShutdown` invalidates
            // every object created before it, and this host measures through a product-owned
            // cached surface (MeasureGraphics) in the width section below -- restarting the
            // runtime in between left that section measuring through a freed Graphics, which is
            // an access violation with nothing to do with the width logic. The shutdown happens
            // once, at the end of the last section that needs it.
        }
    }

    // -----------------------------------------------------------------------
    // The panel's width: what it is derived from, and when it is recomputed.
    //
    // `Panel width` stopped being *the* width: in `Fit content` mode the panel is as wide as its
    // widest row needs, so the width comes out of the same publish-side measurement that fills
    // the row layout plans. Everything asserted below is a property of that resolution:
    //
    //   * the clamp is total -- every degenerate configuration has a defined answer;
    //   * the width the rows need is measured from the *plan's parts*, so a longer row really
    //     does produce a wider panel, up to the cap;
    //   * the resolution is constant per state (the flicker this project has removed four times
    //     was a width that varied from frame to frame);
    //   * Fixed mode measures nothing at all, which is the "Fixed mode is free" promise.
    //
    // Negative controls that must turn these red are listed in docs/panel-width-design.md.
    // -----------------------------------------------------------------------
    printf("\n=== the panel's width: resolved from the rows, or fixed ===\n");
    {
        // (1) The clamp, as a pure function. These need no GDI+: they are about the rule, not
        // about pixels.
        {
            printf("  clamp: content 300 min 200 max 800 screen 1920 -> %d (%s)\n",
                   ResolvePanelWidth(300, 200, 800, 1920).width,
                   ResolvePanelWidth(300, 200, 800, 1920).clamp == PWC_NONE ? "content" : "clamped");
            Check(ResolvePanelWidth(300, 200, 800, 1920).width == 300,
                  "inside the range the content is the width");
            Check(ResolvePanelWidth(300, 200, 800, 1920).clamp == PWC_NONE,
                  "and nothing clamped it");

            Check(ResolvePanelWidth(150, 200, 800, 1920).width == 200,
                  "below the minimum the floor is the width");
            Check(ResolvePanelWidth(150, 200, 800, 1920).clamp == PWC_MIN,
                  "and the floor is what clamped it");
            Check(ResolvePanelWidth(200, 200, 800, 1920).clamp == PWC_NONE,
                  "exactly the minimum is not a clamp (the boundary is inclusive)");

            Check(ResolvePanelWidth(900, 200, 800, 1920).width == 800,
                  "above the maximum the cap is the width");
            Check(ResolvePanelWidth(900, 200, 800, 1920).clamp == PWC_MAX,
                  "and the configured maximum is what clamped it");
            Check(ResolvePanelWidth(800, 200, 800, 1920).clamp == PWC_NONE,
                  "exactly the maximum is not a clamp either");

            // The screen clamp, the same rule the island card uses. The maximum is not allowed
            // to outgrow the monitor it is drawn on. Note what is compared: the width the
            // *content* resolves to, which is the row's content plus its chrome -- so the
            // numbers here are chosen to be above the clamp before it is applied.
            const int contentFor900 = PanelWidthForContent(900);
            const int contentFor2000 = PanelWidthForContent(2000);
            printf("  clamp: %d px of content on a 1280 px screen -> %d (%s)\n", contentFor2000,
                   ResolvePanelWidth(contentFor2000, 200, 1600, 1280).width,
                   ResolvePanelWidth(contentFor2000, 200, 1600, 1280).clamp == PWC_SCREEN
                       ? "screen"
                       : "other");
            Check(PanelWidthCap(1600, 1280) == 1280 - 24,
                  "the cap is lowered to the screen minus 24 px");
            Check(ResolvePanelWidth(contentFor2000, 200, 1600, 1280).width == 1280 - 24,
                  "so a row that wants 2000 px on a 1280 px screen is capped by the screen");
            Check(ResolvePanelWidth(contentFor2000, 200, 1600, 1280).clamp == PWC_SCREEN,
                  "and the clamp is reported as the screen's, not the setting's");
            Check(PanelWidthCap(400, 1280) == 400,
                  "a maximum already inside the screen is left alone");
            Check(ResolvePanelWidth(contentFor2000, 200, 1600, 0).clamp == PWC_MAX,
                  "an unknown screen leaves the configured maximum in charge");
            Check(ResolvePanelWidth(contentFor900, 200, 1600, 0).clamp == PWC_NONE,
                  "and content inside the maximum is in charge of itself");
            Check(PanelWidthCap(1600, 0) == 1600,
                  "an unknown screen is not treated as a zero-width one");
            // The chrome floor is the *last* word: a cap the screen pushed below it is raised to
            // it, and a configured minimum below it is raised to it too. Both are asserted with
            // values that actually reach the floor (24), which needs a minimum of 0 -- the
            // reading of the setting is clamped to >= 200, so 24 is only reachable through the
            // screen clamp and through a direct call. The screen has to be narrow enough that
            // `screen - 24` itself is below the floor, i.e. under 48 px.
            printf("  chrome floor: cap(1600,40)=%d cap(1600,100)=%d minabs=%d\n",
                   PanelWidthCap(1600, 40), PanelWidthCap(1600, 100), kPanelMinAbsoluteWidth);
            Check(PanelWidthCap(1600, 40) == kPanelMinAbsoluteWidth,
                  "a screen narrower than the chrome raises the cap to the chrome floor");
            Check(PanelWidthCap(1600, 100) == 100 - 24,
                  "a screen still wider than the chrome is the screen minus 24");

            // Inverted min/max: defined, not undefined. The cap wins, so the configuration
            // degrades to a constant (fixed) width.
            {
                const PanelWidth inverted = ResolvePanelWidth(500, 800, 400, 1920);
                printf("  inverted min 800 / max 400 with 500 px of content -> %d (%s)\n",
                       inverted.width, inverted.clamp == PWC_MAX ? "max" : "other");
                Check(inverted.width == 400,
                      "an inverted minimum/maximum resolves to the maximum, not to the minimum");
                Check(inverted.clamp == PWC_MAX, "and it is reported as the cap winning");
                Check(ResolvePanelWidth(100, 800, 400, 1920).width == 400,
                      "an inverted pair does not move with the content (it is a fixed width)");
                Check(ResolvePanelWidth(5000, 800, 400, 1920).width == 400,
                      "nor does a row wider than either end");
            }

            // Degenerate values. Nothing here may produce a zero or negative width: a zero-width
            // bitmap or window is not a panel, it is a bug that reads as "the panel vanished".
            Check(ResolvePanelWidth(-500, 200, 800, 1920).width == 200,
                  "a negative content width is treated as no content");
            Check(ResolvePanelWidth(0, 200, 800, 1920).width == 200,
                  "no content at all falls back to the minimum, not to zero");
            Check(ResolvePanelWidth(-1, 0, 0, 0).width == kPanelMinAbsoluteWidth,
                  "even with every setting at zero the panel is at least its own chrome");
            // Below the chrome floor *and* with a minimum below it too: the floor is what
            // answers, and `clamp` has to say so rather than report "the content".
            Check(ResolvePanelWidth(10, 0, 800, 1920).width == kPanelMinAbsoluteWidth,
                  "a minimum below the chrome floor is raised to it");
            Check(ResolvePanelWidth(10, 0, 800, 1920).clamp == PWC_MIN,
                  "and the floor still reports itself as the floor");
            Check(PanelWidthFloor(-100) == kPanelMinAbsoluteWidth,
                  "the floor is never below the chrome");
            Check(PanelWidthFloor(kPanelMinAbsoluteWidth - 1) == kPanelMinAbsoluteWidth,
                  "not even one pixel below it");
            Check(PanelWidthFloor(kPanelMinAbsoluteWidth) == kPanelMinAbsoluteWidth,
                  "and exactly the chrome is the chrome");
            Check(PanelWidthFloor(kPanelMinAbsoluteWidth + 1) == kPanelMinAbsoluteWidth + 1,
                  "one pixel above it is the caller's own minimum");
            // The band, over a sweep of inputs including every degenerate end. Nothing may come
            // out of `ResolvePanelWidth` narrower than the chrome or wider than the setting's own
            // ceiling; the sweep is what makes "defined for every input" a checked statement.
            bool inBand = true;
            int worstWidth = 0;
            for (int w = -200; w <= 4000; w += 37) {
                const PanelWidth d = ResolvePanelWidth(w, 0, 0, 0);
                if (d.width < kPanelMinAbsoluteWidth || d.width > kMaxPanelWidth) {
                    inBand = false;
                    worstWidth = w;
                }
            }
            printf("  band sweep: every input in -200..4000 stays within %d..%d: %s\n",
                   kPanelMinAbsoluteWidth, kMaxPanelWidth, inBand ? "yes" : "NO");
            Check(inBand, "no input produces a width outside the chrome..ceiling band");
            Check(worstWidth == 0 || !inBand, "and the sweep would have named the bad input");
        }

        // (2) The content width is the chrome plus the rows. This half needs no GDI+ either.
        {
            Check(PanelWidthForContent(300) == 300 + 2 * kPanelSidePadding,
                  "the width the content needs is the content plus the chrome on both sides");
            Check(PanelWidthForContent(-10) == 2 * kPanelSidePadding,
                  "a negative content width still leaves the chrome");
            // The two halves have to meet: the panel a content width resolves to must leave that
            // content exactly the column it was measured in, or a fitted panel would trim text
            // it had just decided fits.
            const int resolved = ResolvePanelWidth(PanelWidthForContent(300), 200, 1600, 1920).width;
            const RowTextMetrics column =
                ComputeRowTextMetrics((float)resolved, 0, 26, 12, false);
            printf("  300 px of content -> panel %d px -> text column %.1f px\n", resolved,
                   (double)column.textWidth);
            Check(column.textWidth == 300.0f,
                  "and the resulting row column is exactly the content width it was sized for");
        }
    }

    // -----------------------------------------------------------------------
    // The measurement itself, with real GDI+ fonts. Skipped as a counted block when GDI+ (or the
    // cached scratch surface) is unavailable, never silently.
    // -----------------------------------------------------------------------
    printf("\n=== the natural width a row needs (real text, skipped without GDI+) ===\n");
    {
        // The product's own scratch measuring surface, from the product's own factory, rather
        // than a copy of it. GDI+ is still up here (the earlier section deliberately does not
        // shut it down any more), which matters: the factory caches its surface for the process,
        // and a surface created before a GdiplusShutdown is a dangling pointer afterwards -- the
        // first version of this section restarted GDI+ and measured through a freed Graphics,
        // which is an access violation with no relation to the width logic at all.
        GpGraphics *measureSurface = MeasureGraphics();
        GpStringFormat *measureFormat = measureSurface ? CreateRowStringFormat() : nullptr;
        if (!measureSurface || !measureFormat) {
            Skip("the measure surface can be created (panel width, real-text checks)");
        } else {
            // Stages one row exactly the way the publish path does -- compile, instantiate,
            // segment -- and hands back what the row would need with nothing trimmed. Each call
            // re-stages, so the previous row's state cannot leak into the next measurement.
            auto natural = [&](const WCHAR *text, int fontPx) -> int {
                LOCAL_SETTINGS(s2, g_gapPlan);
                s2.fontSize = fontPx;
                s2.rowHeight = 30;
                s2.rowFontSize[0] = 0;
                s2.rowMaxFontSize[0] = 0;
                s2.ringSize = 26;
                s2.ringRightOffset = 12;
                s2.rowCount = 1;
                wcsncpy(s2.rows[0], text, kRowTextCap - 1);
                s2.rows[0][kRowTextCap - 1] = L'\0';
                s2.rowMaxFontSize[0] = MaxInt(s2.rowFontSize[0], MaxInlineRunSize(s2.rows[0]));
                CompileRowText(s2, s2.rows[0], s2.rowProgram[0]);
                g_settings = s2;

                WCHAR marked[640];
                RingSpec rings[kMaxRingsPerRow];
                int ringCount = 0;
                InstantiateRowProgram(s2, 0, Values(), marked, 640, rings, &ringCount,
                                      kMaxRingsPerRow);
                RowState staged = {};
                BuildRowLayout(marked, ringCount, staged, 0);
                for (int i = 0; i < ringCount; i++) staged.rings[0][i] = rings[i];
                g_rowState = staged;
                g_publishedRows = 1;
                g_layoutEpoch++;
                RowTextMeasure measure = {measureSurface, measureFormat};
                return MeasureRowNaturalWidth(s2, 0, measure);
            };

            // (a) The measurement comes from the plan's parts: a longer run of text, and a run
            // followed by a fixed-size element, both land on the widths those parts add up to.
            const int shortRow = natural(L"CPU", 15);
            const int longRow = natural(L"CPU 12%  RAM 73%  GPU 33%", 15);
            const float cpuWidth =
                MeasureAdvance(measureSurface, AcquireRowFont(measureSurface, L"Segoe UI", 15),
                               measureFormat, L"CPU", 30.0f);
            const float longWidth = MeasureAdvance(measureSurface,
                                                   AcquireRowFont(measureSurface, L"Segoe UI", 15),
                                                   measureFormat,
                                                   L"CPU 12%  RAM 73%  GPU 33%", 30.0f);
            printf("  \"CPU\" -> %d px natural (text alone %.1f px)\n", shortRow, (double)cpuWidth);
            printf("  \"CPU 12%%  RAM 73%%  GPU 33%%\" -> %d px natural (text alone %.1f px)\n",
                   longRow, (double)longWidth);
            Check(cpuWidth >= 1.0f && longWidth > cpuWidth,
                  "the real font really does measure those two runs differently");
            Check(shortRow == (int)(cpuWidth + 0.5f),
                  "a text-only row needs exactly its own text, as the plan measures it");
            Check(longRow == (int)(longWidth + 0.5f),
                  "and a longer row needs more, by the text's own width");
            Check(longRow > shortRow + 20,
                  "which is a difference big enough to move the panel");

            // (b) Elements count too, and by the same amounts the plan places them with: an icon
            // is its own diameter plus the content gap, a gap is its own pixels.
            {
                const float diameter = IconDiameterForFontSize(15);
                const int iconRow = natural(L"[[icon:CPU]]CPU", 15);
                printf("  icon + \"CPU\" -> %d px natural (diameter %.1f + gap %.1f + text %.1f)\n",
                       iconRow, (double)diameter, (double)kIconContentGap, (double)cpuWidth);
                Check(iconRow == (int)(diameter + kIconContentGap + cpuWidth + 0.5f),
                      "an icon-bearing row needs its icon's own box, as the plan places it");
                const int withGap = natural(L"CPU[[gap:20px]]X", 15);
                const int withoutGap = natural(L"CPUX", 15);
                Check(withGap > withoutGap + 19,
                      "a fixed 20 px gap widens the row the measurement reports");
            }

            // (c) Real text at two sizes: a larger font needs a wider panel.
            Check(natural(L"CPU 12%", 24) > natural(L"CPU 12%", 12),
                  "the same text at a larger size needs a wider panel");

            // (d) A longer row yields a wider panel, up to the cap -- and past the cap the
            // *panel* stops growing while the row still needs more, which is exactly the
            // situation the per-run trimming is there for.
            {
                // The short row is measured first, so the comparison is not against whatever the
                // previous assertion happened to leave staged.
                const int contentShort = natural(L"CPU 12%  RAM 73%", 15);

                // Stages a long row and hands back what the panel would need for it.
                auto longRowContent = [&](const WCHAR *text) -> int {
                    LOCAL_SETTINGS(s3, g_gapSaved);
                    s3 = g_settings;
                    wcsncpy(s3.rows[0], text, kRowTextCap - 1);
                    s3.rows[0][kRowTextCap - 1] = L'\0';
                    s3.rowMaxFontSize[0] = MaxInt(s3.rowFontSize[0],
                                                  MaxInlineRunSize(s3.rows[0]));
                    CompileRowText(s3, s3.rows[0], s3.rowProgram[0]);
                    g_settings = s3;
                    WCHAR marked3[640];
                    RingSpec rings3[kMaxRingsPerRow];
                    int ringCount3 = 0;
                    InstantiateRowProgram(s3, 0, Values(), marked3, 640, rings3, &ringCount3,
                                          kMaxRingsPerRow);
                    RowState staged3 = {};
                    BuildRowLayout(marked3, ringCount3, staged3, 0);
                    for (int i = 0; i < ringCount3; i++) staged3.rings[0][i] = rings3[i];
                    g_rowState = staged3;
                    return MeasurePanelContentWidth(s3, 1, {measureSurface, measureFormat});
                };

                const int contentLong =
                    longRowContent(L"CPU 12%  RAM 73%  GPU 33%  NET 1.2 MB/s  DISK 68%  FAN 1200");
                // A row past any sane cap, so the cap is what answers rather than the row.
                const int contentHuge = longRowContent(
                    L"CPU 12%  RAM 73%  GPU 33%  NET 1.2 MB/s  DISK 68%  FAN 1200  CPU 12%  "
                    L"RAM 73%  GPU 33%  NET 1.2 MB/s  DISK 68%  FAN 1200");

                // With a 2000 px ceiling both rows get the width they asked for, and the longer
                // one asks for more: that is "a longer row yields a wider panel".
                const int panelShort =
                    ResolvePanelWidth(PanelWidthForContent(contentShort), 200, 2000, 1920).width;
                const int panelLong =
                    ResolvePanelWidth(PanelWidthForContent(contentLong), 200, 2000, 1920).width;
                // With a 600 px ceiling the panel stops moving while the content keeps growing:
                // past the cap the panel is a bound, not a promise (the runs are trimmed instead).
                const PanelWidth capped =
                    ResolvePanelWidth(PanelWidthForContent(contentHuge), 200, 600, 1920);
                printf("  short row -> content %d -> panel %d; long row -> content %d -> panel %d "
                       "(max 2000), huge row -> content %d -> panel %d (max 600, %s)\n",
                       contentShort, panelShort, contentLong, panelLong, contentHuge, capped.width,
                       capped.clamp == PWC_MAX ? "capped" : "not capped");
                Check(contentLong > contentShort + 20,
                      "the longer row reports a larger natural content width");
                Check(panelShort >= 200 && panelShort < 300,
                      "a short row's panel is the floor, not a degenerate width");
                Check(panelLong > panelShort,
                      "and a longer row yields a wider panel, up to the cap");
                Check(capped.width == 600,
                      "the panel stops at the configured maximum, not at what the row wants");
                Check(capped.clamp == PWC_MAX, "and the cap is recorded as the reason");
                Check(PanelWidthForContent(contentHuge) > capped.width,
                      "past the cap the row still needs more than the panel can give it");
            }

            // (e) Frame stability: the same state resolves to the same width, and the plan that
            // was built at that width is genuinely a function of it. A width recomputed from
            // scratch every frame (or re-derived inside the paint loop) is the property this
            // breaks -- and a width that differs between the window being resized and the bitmap
            // being submitted is the flicker this project has removed four times.
            {
                LOCAL_SETTINGS(s4, g_gapSaved);
                s4 = g_settings;
                s4.panelSizing = kPanelSizingFit;
                s4.panelMinWidth = 200;
                s4.panelMaxWidth = 1600;
                g_settings = s4;
                g_publishedRows = 1;
                g_layoutEpoch++;

                const int first =
                    MeasurePanelContentWidth(g_settings, 1, {measureSurface, measureFormat});
                const int panel = ResolvePanelWidth(PanelWidthForContent(first), 200, 1600, 1920)
                                      .width;
                const int second =
                    MeasurePanelContentWidth(g_settings, 1, {measureSurface, measureFormat});
                const int again = ResolvePanelWidth(PanelWidthForContent(second), 200, 1600, 1920)
                                      .width;
                printf("  one state, twice: content %d / %d px, panel %d / %d px\n", first, second,
                       panel, again);
                Check(first == second,
                      "the same state measures the same content width every time");
                Check(panel == again && panel == PanelWidthForContent(first),
                      "so it resolves to the same panel width every time (constant per state)");

                // A plan built at this width is fresh at this width and stale at another. The
                // freshness key already carries the width, so there is exactly one cache and a
                // second one would be the thing that goes stale.
                const float rowHeight = 30.0f;
                const int rowFont = RowFontSizePx(g_settings, 0);
                const int segmentCount = g_rowState.segmentCount[0];
                // The row's own font, acquired the way the painter acquires it: a row that draws
                // at the global size uses the one font, and there is no separate object for it.
                GpFont *rowMainFont = AcquireRowFont(measureSurface, g_settings.fontFamily, rowFont);
                const RowLayoutPlan *plan =
                    EnsureRowPlan(measureSurface, rowMainFont, measureFormat, 0, g_publishedRows,
                                  (float)panel, rowFont, rowHeight, false);
                const float drawnAdvance = plan->segments[0].advance;
                Check(RowPlanIsFresh(*plan, g_layoutEpoch, g_publishedRows, (float)panel, rowHeight,
                                     rowFont, segmentCount),
                      "the plan built at the resolved width is fresh at that width");
                Check(!RowPlanIsFresh(*plan, g_layoutEpoch, g_publishedRows, (float)panel + 1.0f,
                                      rowHeight, rowFont, segmentCount),
                      "and a single pixel of width makes it stale, so the key really carries it");
                Check(!RowPlanIsFresh(*plan, g_layoutEpoch + 1, g_publishedRows, (float)panel,
                                      rowHeight, rowFont, segmentCount),
                      "so does a new publish generation");
                // The round trip that ties the two halves together: the plan was fitted at the
                // width the resolution produced, so what it drew fits inside that panel.
                printf("  resolved %d px, first run drawn %.1f px from its inset\n", panel,
                       (double)drawnAdvance);
                Check(drawnAdvance > 0.0f &&
                          kRowTextLeftInset + drawnAdvance <= (float)panel + 0.5f,
                      "and the runs the plan fitted at that width stay inside the panel");
            }

            // (e2) The resolver itself, driven across frames. This is the half a pure function
            // cannot show: the cache in EnsurePanelWidth is what makes the width constant for a
            // state, and `g_panelWidthResolves` is the product's own record of how many times the
            // width was actually worked out. Three frames of one state must resolve it once.
            {
                LOCAL_SETTINGS(s7, g_gapChecked);
                s7 = g_settings;
                s7.panelSizing = kPanelSizingFit;
                s7.panelMinWidth = 200;
                s7.panelMaxWidth = 1600;
                s7.panelWidth = 900;      // deliberately *not* what the rows need
                g_settings = s7;

                g_panelWidthResolves = 0;
                g_layoutEpoch++;
                const PanelWidth a = EnsurePanelWidth(nullptr, measureSurface, measureFormat, false);
                const DWORD afterFirst = g_panelWidthResolves;
                const PanelWidth b = EnsurePanelWidth(nullptr, measureSurface, measureFormat, false);
                const PanelWidth c = EnsurePanelWidth(nullptr, measureSurface, measureFormat, false);
                const DWORD afterThree = g_panelWidthResolves;
                printf("  three frames, one state: widths %d / %d / %d, resolves %lu -> %lu "
                       "(configured %d)\n",
                       a.width, b.width, c.width, (unsigned long)afterFirst,
                       (unsigned long)afterThree, g_settings.panelWidth);
                Check(afterFirst == 1,
                      "the first frame of a state resolves the panel width once");
                Check(afterThree == afterFirst,
                      "and the next two frames resolve it zero more times (constant per state)");
                Check(a.width == b.width && b.width == c.width,
                      "so all three frames of that state have the same width");
                Check(a.width != g_settings.panelWidth,
                      "and that width is the resolved one, not the configured Panel width");
                // What the frame draws with. The painter hands one number to the bitmap, to the
                // window rectangle (which the hit test reads back) and to the card's own rule --
                // so "the window and the bitmap agree, and it is the resolved width" is this
                // call. A frame that took `Panel width` instead fails here.
                {
                    // The product's frame sequence, in the product's order, with nothing
                    // re-assigned in between: the painter stores the resolved width, and
                    // `ComputeGeometry` runs *after* it -- on expand/collapse and on every
                    // settings change -- owning the height. It must not hand the width back to
                    // the configured value while doing so. (An earlier version of this block
                    // re-assigned the resolved width after `ComputeGeometry` to "restore" it;
                    // that made the next assertion tautological -- it passed even with the paint
                    // path reading `Panel width` -- which is the one thing a negative control is
                    // for. See docs/panel-width-design.md.)
                    g_panelWidth = a.width;
                    ComputeGeometry();
                    printf("  after ComputeGeometry the width is %d (resolved %d, configured %d)\n",
                           g_panelWidth, a.width, g_settings.panelWidth);
                    Check(g_panelWidth == a.width,
                          "ComputeGeometry leaves a resolved width alone (it only seeds an unset "
                          "one)");
                    const int frameWidth = FrameWidthFor(g_panelWidth, /*cardUp=*/false, g_settings,
                                                         IslandCardMetrics(), 1920);
                    // The painter's own record of the bitmap it is about to submit. Assigned the
                    // way the paint path assigns it, so the window rectangle below is compared
                    // with what the frame will actually blit rather than with the resolution.
                    g_lastBitmapWidth = frameWidth;
                    const int windowWidth = g_restWidth > 0 ? g_restWidth : g_panelWidth;
                    const IslandMetrics cardMetrics = IslandCardMetrics();
                    const PanelRect monitorRect = {0, 0, 1920, 0};
                    printf("  frame width %d; width in effect %d; bitmap %d; window %d; resolved "
                           "%d (configured %d); card would be %d\n",
                           frameWidth, g_panelWidth, g_lastBitmapWidth, windowWidth, a.width,
                           g_settings.panelWidth,
                           ComputeIslandRect(cardMetrics, g_settings, monitorRect, 0, 0).w);
                    Check(a.width != g_settings.panelWidth,
                          "the resolved width is not the configured one, so the next check can "
                          "tell them apart");
                    Check(frameWidth == a.width && frameWidth == g_panelWidth,
                          "the window and the bitmap agree, and the width is the resolved one");
                    Check(frameWidth != g_settings.panelWidth,
                          "which is not the configured Panel width (900)");
                    Check(g_lastBitmapWidth == windowWidth,
                          "and the window rectangle is the width of the bitmap being submitted");
                    // The card keeps its own rule while it is up: same function, card branch.
                    const int cardWidth =
                        FrameWidthFor(g_panelWidth, /*cardUp=*/true, g_settings, cardMetrics, 1920);
                    Check(cardWidth == ComputeIslandCardWidth(cardMetrics, g_settings, 1920),
                          "and while a card is up the card's own width rule still wins");
                }

                // A new publish generation is the one thing that must resolve it again.
                g_layoutEpoch++;
                EnsurePanelWidth(nullptr, measureSurface, measureFormat, false);
                Check(g_panelWidthResolves == afterThree + 1,
                      "a new publish generation resolves it once more, and only once");
                Check(EnsurePanelWidth(nullptr, measureSurface, measureFormat, false).width ==
                          a.width,
                      "and the same state still produces the same width");
            }

            // (f) Fixed mode is free: no natural measurement runs at all. The counter is the
            // product's own `g_measureCalls` -- the one the `[perf]` line prints -- so this is
            // the same number a real machine reports, and the promise is per frame.
            {
                LOCAL_SETTINGS(s5, g_gapSaved);
                s5 = g_settings;
                s5.panelSizing = kPanelSizingFixed;
                s5.panelWidth = 420;
                s5.rowCount = 1;
                g_settings = s5;
                const bool contentDriven = PanelWidthIsContentDriven(g_settings);
                const DWORD before = g_measureCalls;
                int fixedWidth = 0;
                for (int frame = 0; frame < 3; frame++) {
                    // Exactly what the painter does for a Fixed width: take the setting. Three
                    // frames of it must cost three times nothing.
                    if (!contentDriven) fixedWidth = g_settings.panelWidth;
                }
                const DWORD after = g_measureCalls;
                printf("  Fixed mode: contentDriven=%d width=%d, measure calls %lu -> %lu over 3 "
                       "frames\n",
                       contentDriven ? 1 : 0, fixedWidth, (unsigned long)before,
                       (unsigned long)after);
                Check(!contentDriven, "Fixed mode does not ask for a content width at all");
                Check(after == before,
                      "and three Fixed frames measure nothing (the measurement counter does not "
                      "move)");
                Check(fixedWidth == 420, "the Fixed width is the configured width, unchanged");
                // The second half of "Fixed is free": the width does not follow the content at
                // all. A Fixed panel whose rows are re-published (a new generation, new text)
                // keeps the configured width -- there is nothing to re-resolve.
                g_publishedRows = 1;
                g_layoutEpoch++;
                const DWORD beforePublish = g_measureCalls;
                LOCAL_SETTINGS(s5b, g_gapChecked);
                s5b = s5;
                wcsncpy(s5b.rows[0], L"a much, much longer row of text than before", 63);
                s5b.rows[0][63] = L'\0';
                CompileRowText(s5b, s5b.rows[0], s5b.rowProgram[0]);
                g_settings = s5b;
                Check(!PanelWidthIsContentDriven(g_settings) && g_measureCalls == beforePublish,
                      "and a new publish does not make a Fixed panel measure either");
                // The gate itself, not just its use: the same settings with Fit content answers
                // the other way, so the assertion above cannot hold for the wrong reason.
                LOCAL_SETTINGS(s6, g_gapSaved);
                s6 = g_settings;
                s6.panelSizing = kPanelSizingFit;
                g_settings = s6;
                Check(PanelWidthIsContentDriven(g_settings),
                      "while Fit content does (so the gate is really what was tested)");
                g_settings = s5;
            }
        }
        if (measureFormat) g_gdiplus.DeleteStringFormat(measureFormat);
        if (g_gdiplusToken) g_gdiplus.Shutdown(g_gdiplusToken);
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

        // A value the settings allow has to survive the trip into a row. It used to be
        // cut at 127 characters by a 128-char scratch buffer on the way, so
        // "External value length = 512" bought 127 visible characters and never said so.
        {
            WCHAR stored[400];
            for (int i = 0; i < 399; i++) stored[i] = (WCHAR)(L'a' + (i % 26));
            stored[399] = L'\0';
            ExtStoreValue(slot, L"long", stored, 60000, kExtMaxValueChars);
            Values none = {};
            WCHAR marked[640];
            RingSpec rings[kMaxRingsPerRow];
            int ringCount = 0;
            MarkRow(L"{ext.music.long}", none, g_settings, marked, 640, rings, &ringCount,
                    kMaxRingsPerRow);
            printf("  399-char value -> %d chars in the row\n", (int)wcslen(marked));
            Check(wcscmp(marked, stored) == 0,
                  "a long pushed value reaches the row whole, not cut at 127");
            // Do not leave a live value behind: the deadline assertions below check that
            // nothing live means no deadline.
            ExtInvalidate(slot, L"long");
        }
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

        LOCAL_SETTINGS(allow, g_extAllow);
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

        // Replies to a client that never reads them. Each cancelled reply costs the pipe
        // thread the whole I/O timeout (500 ms), so a publisher that ignores them used to
        // slow the server down to about two frames a second -- and the two values it wrote
        // for one key then landed one timeout apart, which is what made the panel flicker.
        {
            bool abandon = false;
            Check(ExtReplyTimeoutsAfter(1, 0, &abandon) == 0 && !abandon,
                  "a reply that goes out means the client is reading");
            Check(ExtReplyTimeoutsAfter(-2, 0, &abandon) == 1 && !abandon,
                  "one timeout is not yet proof that nobody is reading");
            Check(ExtReplyTimeoutsAfter(-2, 1, &abandon) == 2 && abandon,
                  "two in a row is: stop replying to this connection");
            Check(ExtReplyTimeoutsAfter(-2, 9, &abandon) == 10 && abandon,
                  "and the count keeps rising once it has been decided");
            Check(ExtReplyTimeoutsAfter(-1, 1, &abandon) == 1 && !abandon,
                  "a hard write failure says nothing about whether the client reads");
            Check(ExtReplyTimeoutsAfter(0, 1, &abandon) == 1 && !abandon,
                  "and neither does any other outcome");
            Check(ExtReplyTimeoutsAfter(1, 1, &abandon) == 0,
                  "the count is consecutive, not cumulative: a success resets it");
        }

        {
            LOCAL_SETTINGS(rows, g_extRows);
            wcsncpy(rows.rows[0], L"CPU {cpu}%  {ext.music.title}", 255);
            rows.rowCount = 1;
            unsigned groups = RequiredGroups(rows);
            Check((groups & G_EXT) != 0, "{ext.*} selects the external group");
            Check((groups & G_CPU) != 0, "and built-in groups are still found");
            LOCAL_SETTINGS(bad, g_extBad);
            wcsncpy(bad.rows[0], L"{ext.}", 255);
            bad.rowCount = 1;
            Check((RequiredGroups(bad) & G_EXT) == 0, "a malformed {ext.} selects nothing");
        }

        ExtStoreValue(slot, L"progress", L"0.42", 60000, 128);
        {
            Values v;
            LOCAL_SETTINGS(cfg, g_extCfg);
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
            LOCAL_SETTINGS(s, g_extSettings);
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
            // The event's text field carries two fields: the title, then -- after a run
            // of two or more spaces -- the artist. A title *contains* spaces (`Blue
            // Hour`), so a token-shaped title is what used to chop it: the old parse
            // gave titleLen 4 ("Blue") and dropped `Hour  Mili` onto the second row.
            Check(ExtParseLine(L"EVENT music info 8000 Blue Hour  Mili", ev) == 0 &&
                      ev.cmd == EC_EVENT && ev.severity == ES_INFO && ev.ttlLen == 4 &&
                      ev.titleLen == 9 && wcsncmp(ev.title, L"Blue Hour", 9) == 0 &&
                      ev.artistLen == 4 && wcscmp(ev.artist, L"Mili") == 0,
                  "EVENT keeps a title with spaces and splits the artist off at two spaces");
            // Both halves may contain single spaces. This is the frame the bridge emits.
            Check(ExtParseLine(L"EVENT netease info 6000 \x94C1\x82B1\x98DE  Mili / \x585E\x58EC\x5531\x7247-MSR",
                               ev) == 0 &&
                      ev.titleLen == 3 && wcsncmp(ev.title, L"\x94C1\x82B1\x98DE", 3) == 0 &&
                      wcscmp(ev.artist, L"Mili / \x585E\x58EC\x5531\x7247-MSR") == 0,
                  "the bridge's real frame splits into song name and artist");
            // One space is text, not a separator -- that is the whole reason the rule is
            // a *run* of two.
            Check(ExtParseLine(L"EVENT music info 8000 Blue Hour", ev) == 0 &&
                      ev.titleLen == 9 && ev.artistLen == 0,
                  "a single space does not split; the event then has no artist at all");
            // An old-format frame (one text field, never a separator) is not an error
            // and not a guess: the whole text is the title and the card draws one row.
            Check(ExtParseLine(L"EVENT music info 8000 Title body text here", ev) == 0 &&
                      ev.titleLen == 20 && ev.artistLen == 0,
                  "a frame without the separator is a title-only event (old format)");
            Check(ExtParseLine(L"EVENT music critical - T b", ev) == 0 &&
                      ev.severity == ES_CRITICAL && ev.ttlLen == 1 && ev.titleLen == 3,
                  "critical severity and the default-ttl marker parse");
            Check(ExtParseLine(L"EVENT music nope 8000 T b", ev) == 400,
                  "an unknown severity is a parse error");
            Check(ExtParseLine(L"EVENT music info 8000", ev) == 400,
                  "EVENT without any text is a parse error");
            Check(ExtParseLine(L"EVENT music info 8000 ", ev) == 400,
                  "an event whose text is only whitespace is a parse error");
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

            // The event's identity is *both* fields. `repeat` is the badge the user reads
            // as "the same thing again", so an event that only matches on the title would
            // count two different tracks (or the same track before and after its artist
            // arrived) as one.
            {
                IslandEvent one = {};
                wcsncpy(one.provider, L"music", kExtProviderChars);
                wcsncpy(one.title, L"Blue Hour", kIslandTitleChars - 1);
                wcsncpy(one.artist, L"Mili", kIslandArtistChars - 1);
                one.severity = ES_INFO; one.ttlMs = 6000; one.repeat = 1; one.queuedAt = 1000;
                IslandEvent mq[kIslandQueueMax] = {};
                unsigned mdrops = 0;
                int mn = IslandEnqueue(mq, 0, 4, one, &mdrops);
                Check(mn == 1 && wcscmp(mq[0].title, L"Blue Hour") == 0 &&
                          wcscmp(mq[0].artist, L"Mili") == 0,
                      "an event is queued with both of its fields");
                IslandEvent same = one; same.queuedAt = 1500;
                mn = IslandEnqueue(mq, mn, 4, same, &mdrops);
                Check(mn == 1 && mq[0].repeat == 2,
                      "the same title *and* artist inside the window is a repeat");
                IslandEvent reArtist = one;
                wcsncpy(reArtist.artist, L"Other", kIslandArtistChars - 1);
                reArtist.queuedAt = 2000;
                mn = IslandEnqueue(mq, mn, 4, reArtist, &mdrops);
                Check(mn == 1 && mq[0].repeat == 1 && wcscmp(mq[0].artist, L"Other") == 0,
                      "the same title with another artist is a new event, not a repeat");
            }

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

            // A repeat that arrives at a *higher* level is an escalation, not a repeat:
            // the merge window used to keep the first sighting's severity, so "same
            // title, now critical" never reached the screen (the card stayed at warn).
            {
                IslandEvent esc[kIslandQueueMax] = {};
                unsigned escDrops = 0;
                IslandEvent warn = {};
                wcsncpy(warn.provider, L"music", kExtProviderChars);
                wcsncpy(warn.title, L"Now playing", kIslandTitleChars - 1);
                wcsncpy(warn.artist, L"battery 15%", kIslandArtistChars - 1);
                warn.severity = ES_WARN; warn.ttlMs = 6000; warn.repeat = 1;
                warn.queuedAt = 1000;

                // The same event (both identity fields) re-reported as critical.
                IslandEvent hot = warn;
                hot.severity = ES_CRITICAL; hot.queuedAt = 1500;   // inside the window

                int m = IslandEnqueue(esc, 0, 4, warn, &escDrops);
                m = IslandEnqueue(esc, m, 4, hot, &escDrops);
                Check(m == 1 && esc[0].severity == ES_CRITICAL,
                      "an escalation inside the merge window raises the queued level");
                Check(esc[0].repeat == 2 && wcscmp(esc[0].artist, hot.artist) == 0,
                      "and it still counts as the same thing -- both fields match");

                // The symptom, driven through the real state machine: a warn card on
                // screen must give way when the queue now holds a critical.
                IslandView escView = {};
                escView.state = IS_CARD;
                escView.showing = ES_WARN;
                escView.cardDeadline = 9999;
                Check(StepIsland(&escView, esc, m, 2000, false, 6000, nullptr) == IS_COLLAPSING,
                      "so a card already on screen stands down for it");

                // A repeat at a lower level must not lower the queued one either.
                IslandEvent cool = hot;
                cool.severity = ES_INFO; cool.queuedAt = 2000;
                m = IslandEnqueue(esc, m, 4, cool, &escDrops);
                Check(m == 1 && esc[0].severity == ES_CRITICAL,
                      "a repeat never lowers the level the queue already holds");

                // Outside the window it is a new card rather than a repeat. It carries
                // the same provider, so it *replaces* that provider's pending entry
                // instead of queueing beside it (which is the documented rule), so what
                // identifies it as new is the timestamp and a repeat count of 1.
                // The window runs from the *last* sighting -- every repeat refreshes it.
                IslandEvent later = warn;
                later.queuedAt = esc[0].queuedAt + kIslandMergeWindowMs + 1;
                m = IslandEnqueue(esc, m, 4, later, &escDrops);
                Check(m == 1 && esc[0].queuedAt == later.queuedAt && esc[0].repeat == 1,
                      "the same title after the merge window is a new card, not a repeat");
                IslandEvent again = later;
                again.queuedAt += 10;
                m = IslandEnqueue(esc, m, 4, again, &escDrops);
                Check(m == 1 && esc[0].repeat == 2,
                      "and a repeat of that new card merges with it, not with the old one");
            }
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
            Check(StepIsland(&v, q, 1, 1000, false, 6000, &pop) == IS_POPPING && pop == 0,
                  "a queued event starts the pop");
            Check(v.cardDeadline == 1500, "the card inherits the event's ttl");
            Check(StepIsland(&v, q, 1, 1100, false, 6000, nullptr) == IS_POPPING,
                  "the pop keeps playing until the animation reports it finished");
            Check(StepIsland(&v, q, 1, 1200, true, 6000, nullptr) == IS_CARD,
                  "the pop settles into a card");
            Check(StepIsland(&v, q, 1, 1499, false, 6000, nullptr) == IS_CARD,
                  "the card lives until its ttl");
            Check(StepIsland(&v, q, 1, 1500, false, 6000, nullptr) == IS_COLLAPSING,
                  "then it collapses -- nothing stays on screen forever");
            Check(StepIsland(&v, q, 0, 1600, true, 6000, nullptr) == IS_IDLE,
                  "with nothing queued, collapsing settles back to idle");
            v.state = IS_CARD; v.showing = ES_INFO; v.cardDeadline = 2000;
            Check(StepIsland(&v, q, 1, 2000, false, 6000, nullptr) == IS_COLLAPSING,
                  "an expired card starts collapsing");
            Check(StepIsland(&v, q, 1, 2100, true, 6000, &pop) == IS_POPPING && pop == 0,
                  "a queue that is not empty goes straight into the next pop");

            v.state = IS_CARD; v.showing = ES_INFO; v.cardDeadline = 99000;
            Check(StepIsland(&v, q, 2, 3000, false, 6000, nullptr) == IS_COLLAPSING,
                  "a waiting critical preempts an info card");
            Check(StepIsland(&v, q, 2, 3100, true, 6000, &pop) == IS_POPPING && pop == 1,
                  "and the critical is what pops next");
            Check(v.showing == ES_CRITICAL, "the view now shows the critical");
        }

        // A publisher's *value* ttl is not a display duration. The bridge sends its
        // 600000 ms value ttl along with every event, and the mod used to take it
        // verbatim: one card held the island for ten minutes, a queue of eight held
        // hours, and the card on screen was minutes behind the music -- which is what
        // "the island looks broken with dense lyrics" actually was.
        printf("  card duration: value ttl 600000, no ttl, a deliberate 500ms flash, exactly 60000\n");
        Check(IslandCardDuration(600000, 6000) == 60000,
              "a ten-minute value ttl is capped to the longest card");
        Check(IslandCardDuration(0, 6000) == 6000,
              "an event that sends no ttl uses the configured default");
        Check(IslandCardDuration(500, 6000) == 500,
              "a publisher that deliberately asks for a short flash keeps it");
        Check(IslandCardDuration(60000, 6000) == 60000, "the cap itself is not shortened");
        {
            IslandView cv = {};
            IslandEvent cq[1] = {};
            cq[0].severity = ES_INFO;
            cq[0].ttlMs = 600000;
            cq[0].queuedAt = 1000;
            int cpop = -1;
            Check(StepIsland(&cv, cq, 1, 1000, false, 6000, &cpop) == IS_POPPING && cpop == 0,
                  "a card carrying a ten-minute ttl still starts like any other");
            Check(StepIsland(&cv, cq, 1, 1200, true, 6000, nullptr) == IS_CARD,
                  "and settles into a card");
            Check(StepIsland(&cv, cq, 1, 60000, false, 6000, nullptr) == IS_CARD,
                  "it is still up at the cap's boundary");
            Check(StepIsland(&cv, cq, 1, 61001, false, 6000, nullptr) == IS_COLLAPSING,
                  "and it goes away just past the cap, not ten minutes later");
        }

        // Staleness bounds the lag. Depth alone was not enough: with a lyric line per
        // second the queue held eight *stale* lines, so the island narrated the song
        // from a minute ago. A critical event gets a longer window -- an alarm is worth
        // more than a lyric line, and dropping one silently is the worst of both.
        printf("  stale: 9s info kept, 11s info dropped, 11s critical kept, 41s critical dropped\n");
        {
            IslandEvent sq[4] = {};
            sq[0].queuedAt = 3000;  sq[0].severity = ES_INFO;      // 9 s old
            sq[1].queuedAt = 5000;  sq[1].severity = ES_WARN;      // 7 s old
            sq[2].queuedAt = 1000;  sq[2].severity = ES_INFO;      // 11 s old -> stale
            sq[3].queuedAt = 1000;  sq[3].severity = ES_CRITICAL;  // 11 s old -> kept
            unsigned sdrops = 0;
            const int kept = IslandDropStale(sq, 4, 12000, &sdrops);
            Check(kept == 3, "info and warn events past the staleness bound are dropped");
            Check(sdrops == 1, "and every drop is counted, so the log can report it");
            Check(sq[0].queuedAt == 3000 && sq[1].queuedAt == 5000 && sq[3].queuedAt == 1000,
                  "the survivors keep their order");
            Check(IslandStaleBoundMs(ES_CRITICAL) > IslandStaleBoundMs(ES_INFO),
                  "a critical event is allowed to wait longer than an info one");
            IslandEvent cq2[1] = {};
            cq2[0].queuedAt = 1000;
            cq2[0].severity = ES_CRITICAL;
            unsigned cdrops = 0;
            Check(IslandDropStale(cq2, 1, 1000 + IslandStaleBoundMs(ES_CRITICAL) + 1, &cdrops) == 0 &&
                      cdrops == 1,
                  "but a critical event is not immortal either");
        }

        // What the card shows, and how severity maps to colour (design 7.4 / 14 Q4).
        {
            IslandEvent ev = {};
            wcsncpy(ev.title, L"T", kIslandTitleChars - 1);
            ev.repeat = 1;
            IslandCardLayout p1 = PlanIslandCard(ev);
            Check(p1.lines == 1 && !p1.hasBadge,
                  "a card without an artist is one line and carries no merge badge");
            wcsncpy(ev.artist, L"B", kIslandArtistChars - 1);
            ev.repeat = 7;
            IslandCardLayout p2 = PlanIslandCard(ev);
            Check(p2.lines == 2 && p2.hasBadge && wcscmp(p2.badge, L"\x00D7" L"7") == 0,
                  "an artist adds the second line and a repeat adds the merged count");

            // Frame -> parse -> the product's own "what the card holds" step -> the plan
            // the painter asks for. The conversion is `ExtEventFromCommand`, not a copy of
            // it in the test: the first version of this check sanitized the two slices
            // here, and reverting the product to "sanitize to the end of the frame buffer"
            // left the host green -- it was asserting on itself (AGENTS 7.8). The third
            // frame is the interesting one: it *has* the separator, so a shape-based rule
            // would draw an empty row two.
            const WCHAR *rowFrames[3] = {
                L"EVENT music info 8000 Blue Hour  Mili",
                L"EVENT music info 8000 Blue Hour",
                L"EVENT music info 8000 Blue Hour  ",
            };
            const int rowWant[3] = {2, 1, 1};
            const char *rowLabel[3] = {
                "a frame with an artist draws two rows, the title intact",
                "a frame without a separator draws one row (no blank second row)",
                "a separator with nothing after it draws one row, not an empty row two",
            };
            for (int i = 0; i < 3; i++) {
                ExtCommand pc;
                const bool parsed = ExtParseLine(rowFrames[i], pc) == 0;
                const IslandEvent fe = ExtEventFromCommand(pc, L"music", 6000, 1000);
                const IslandCardLayout lp = PlanIslandCard(fe);
                Check(parsed && lp.lines == rowWant[i] && wcscmp(fe.title, L"Blue Hour") == 0 &&
                          (i == 0 ? wcscmp(fe.artist, L"Mili") == 0 : fe.artist[0] == L'\0'),
                      rowLabel[i]);
            }
            Check(ResolveSeverityColor(ES_CRITICAL, 0xFF112233) == 0xFFE4573D &&
                      ResolveSeverityColor(ES_WARN, 0xFF112233) == 0xFFF2C14E &&
                      ResolveSeverityColor(ES_INFO, 0xFF112233) == 0xFF112233,
                  "severity picks the accent: fixed for warn/critical, the theme's text for info");
        }

        // The card's target width: the configured minimum is a floor, text widens it,
        // and it is capped by the screen rather than growing without bound (design 12).
        {
            LOCAL_SETTINGS(cfg, g_islandCfgA);
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
        }

        // M3: geometry, regions, hit test and dismissal. The property the milestone is
        // judged on is the last one: every visible pixel must belong to some region.
        {
            LOCAL_SETTINGS(cfg, g_islandCfgB);
            cfg.rowHeight = 30;
            cfg.islandMinWidth = 260;
            IslandMetrics m = {};
            m.titleChars = 20;
            PanelRect monitor = {0, 0, 1920, 1080};
            PanelRect card = ComputeIslandRect(m, cfg, monitor, 0, 168);
            Check(card.w == ComputeIslandCardWidth(m, cfg, monitor.w),
                  "the card's rectangle and its width come out of one function");
            Check(card.x == (1920 - card.w) / 2 && card.y == 0 && card.h == 168,
                  "the card is centred in the monitor and keeps the panel's top edge");

            PanelRect narrowMonitor = {0, 0, 400, 1080};
            PanelRect narrow = ComputeIslandRect(m, cfg, narrowMonitor, 0, 168);
            Check(narrow.w <= 400 && narrow.x >= 0 && narrow.x + narrow.w <= 400,
                  "on a narrow monitor the card stays inside the screen");

            IslandHitRegion regions[4];
            const int count = BuildIslandRegions(card, cfg.rowHeight, regions, 4);
            Check(count == 2 && regions[0].id == IR_CARD && regions[1].id == IR_CLOSE,
                  "the card is one region and the close button sits on top of it");

            const PanelRect &close = regions[1].rect;
            Check(close.x >= card.x && close.x + close.w <= card.x + card.w &&
                      close.y >= card.y && close.y + close.h <= card.y + card.h,
                  "the close button never pokes outside the card");

            // A card can be shorter than the button (a single 18 px row against the 22 px
            // square). No fixed-size square fits there: the old code clamped the top and
            // then the bottom clamp pushed it back out, top edge first (y = card.y - 4).
            printf("  card h  close rect (w,h)  inside\n");
            const int cardHeights[] = {1, 8, 18, 21, 22, 30, 90, 168};
            for (int h : cardHeights) {
                PanelRect shortCard = {10, 20, 360, h};
                PanelRect btn = ComputeIslandCloseRect(shortCard, cfg.rowHeight);
                const bool inside = btn.x >= shortCard.x && btn.y >= shortCard.y &&
                                    btn.x + btn.w <= shortCard.x + shortCard.w &&
                                    btn.y + btn.h <= shortCard.y + shortCard.h &&
                                    btn.w >= 1 && btn.h >= 1;
                printf("  %6d  %9dx%-4d  %s\n", h, btn.w, btn.h, inside ? "yes" : "NO");
                Check(inside, "the close button fits inside a card of any height");
                if (h < kIslandCloseSize) {
                    Check(btn.h == h, "and it shrinks to the card when the card is smaller");
                }
            }
            Check(ComputeIslandCloseRect(card, cfg.rowHeight).w == kIslandCloseSize,
                  "on a card with room to spare the button keeps its full size");

            Check(IslandHitTest(regions, count, close.x + 2, close.y + 2) == IR_CLOSE,
                  "a click on the button is the button");
            Check(IslandHitTest(regions, count, card.x + 5, card.y + 5) == IR_CARD,
                  "a click on the card is the card");
            Check(IslandHitTest(regions, count, card.x - 1, card.y - 1) == IR_NONE,
                  "a click outside the card is nothing");
            Check(IslandHitTest(regions, count, card.x + 5, card.y + 5) == IR_CARD &&
                      IslandHitTest(regions, count, close.x + 2, close.y + 2) == IR_CLOSE,
                  "the button wins over the card inside itself");

            // What a click does with that hit. The rule used to live in WindowProc and
            // read "if the button was hit *and* the dismiss took effect, else fall
            // through": a second click on the × during the fade toggled the panel, and so
            // did a click anywhere on the card.
            Check(IslandClickActionFor(IR_NONE, true) == ICA_PANEL,
                  "a click outside the card belongs to the panel");
            Check(IslandClickActionFor(IR_CARD, true) == ICA_CONSUMED &&
                      IslandClickActionFor(IR_CARD, false) == ICA_CONSUMED,
                  "a click on the card is swallowed, never the panel's");
            Check(IslandClickActionFor(IR_CLOSE, true) == ICA_DISMISS,
                  "the button dismisses while there is a card to dismiss");
            Check(IslandClickActionFor(IR_CLOSE, false) == ICA_CONSUMED,
                  "and during the fade it is still swallowed, not handed to the panel");
            {
                IslandView live = {};
                live.state = IS_CARD;
                IslandView fading = {};
                fading.state = IS_COLLAPSING;
                Check(IslandDismissable(live) && !IslandDismissable(fading),
                      "a live card can be dismissed and a fading one cannot");
                Check(IslandClickActionFor(IR_CLOSE, IslandDismissable(live)) == ICA_DISMISS &&
                          IslandClickActionFor(IR_CLOSE, IslandDismissable(fading)) ==
                              ICA_CONSUMED,
                      "so the button's two questions agree on both states");
            }

            int unclickable = 0;
            for (int x = card.x; x < card.x + card.w; x++) {
                for (int y = card.y; y < card.y + card.h; y++) {
                    if (IslandHitTest(regions, count, x, y) == IR_NONE) unclickable++;
                }
            }
            Check(unclickable == 0,
                  "every visible pixel of the card belongs to a clickable region");

            IslandView view = {};
            view.state = IS_CARD;
            view.phaseStarted = 1000;
            Check(IslandDismiss(&view, 2000) && view.state == IS_COLLAPSING &&
                      view.phaseStarted == 2000,
                  "dismissing a card starts the same collapse the ttl expiry would");
            Check(!IslandDismiss(&view, 2500) && view.phaseStarted == 2000,
                  "and a second click does not restart the fade");

            // The frame clock is a decision: 16 ms only while something moves. A resting
            // card must not hold the tick up -- that is the difference between a hover
            // highlight and 25 wake-ups a second for a window doing nothing.
            IslandView anim = {};
            anim.state = IS_POPPING;
            Check(IslandPhaseAnimating(anim), "a popping phase wants the frame clock");
            anim.state = IS_COLLAPSING;
            Check(IslandPhaseAnimating(anim), "a collapsing phase wants it too");
            anim.state = IS_CARD;
            Check(!IslandPhaseAnimating(anim),
                  "but a card that is just sitting there does not");
            anim.state = IS_IDLE;
            Check(!IslandPhaseAnimating(anim), "and neither does an idle island");

            const bool savedIntroActive = g_intro.active;
            const IslandView savedView = g_islandView;
            g_intro.active = false;
            g_islandView.state = IS_CARD;
            Check(DesiredTickMs() == kCursorPollMs,
                  "with nothing animating the tick falls back to the cheap cursor poll");
            g_islandView.state = IS_POPPING;
            Check(DesiredTickMs() == 16, "and while a phase runs it is the frame rate");
            g_islandView.state = IS_CARD;
            g_intro.active = true;
            Check(DesiredTickMs() == 16, "the panel's own intro asks for it as well");
            g_intro.active = savedIntroActive;
            g_islandView = savedView;

            // Turning the island off has to drop a phase that is in flight. Nothing
            // drives the machine while the feature is off, so the phase stayed POPPING
            // forever, IslandPhaseAnimating kept the tick pinned at 16 ms for the rest of
            // the session, and the stale card could not even be dismissed (that path is
            // gated on the same switch). UpdateIsland is the only thing that advances the
            // state machine, so it is where the phase has to be dropped.
            {
                const bool savedIsland = g_settings.islandEnabled;
                const bool savedCardValid = g_islandCardValid;
                g_settings.islandEnabled = false;
                g_islandView.state = IS_POPPING;
                g_islandView.phaseStarted = GetTickCount() - 10000;
                UpdateIsland();
                Check(g_islandView.state == IS_IDLE,
                      "turning the island off drops a phase that was in flight");
                Check(DesiredTickMs() == kCursorPollMs,
                      "and the tick falls back off the frame rate with it");
                g_settings.islandEnabled = savedIsland;
                g_islandCardValid = savedCardValid;
            }
        }

        // Effective CPU clock: Windows exposes a *ratio* (PDH "% Processor
        // Performance"), so the conversion is the part that can be checked here.
        {
            Check(ComputeEffectiveGhz(4.0, 100.0) == 4.0,
                  "100% of nominal is the nominal clock");
            Check(ComputeEffectiveGhz(4.0, 125.0) == 5.0, "turbo scales it up");
            Check(ComputeEffectiveGhz(4.0, 50.0) == 2.0, "and idling scales it down");
            Check(ComputeEffectiveGhz(4.0, 0.0) == 0.0,
                  "a counter with nothing to report says so instead of inventing a value");
            Check(ComputeEffectiveGhz(4.0, -5.0) == 0.0, "and so does a negative reading");
            Check(ComputeEffectiveGhz(0.0, 100.0) == 0.0,
                  "without a nominal clock there is nothing to scale");
            Check(ComputeEffectiveGhz(4.0, 100000.0) == 0.0,
                  "an absurd ratio is treated as unusable rather than displayed");

            // Only {cpu.ghz} may open the PDH query -- the coarser tokens must not pay
            // for it, and {cpu.ghz.max} must not be mistaken for it.
            Check(!RowUsesEffectiveClock(L"{cpu} {cpu.name} {cpu.ghz.max}"),
                  "{cpu}, {cpu.name} and {cpu.ghz.max} must not open the PDH query");
            Check(RowUsesEffectiveClock(L"now {cpu.ghz} GHz"),
                  "{cpu.ghz} on its own does");
            Check(RowUsesEffectiveClock(L"{cpu.ghz:0.2} GHz {cpu.ghz.max}"),
                  "so does {cpu.ghz} with a format");
            Check(!RowUsesEffectiveClock(L"nothing here"),
                  "and text without the token does not");
        }

        // The publisher command line is split the way CreateProcessW does when
        // lpApplicationName is null: a quoted first token may contain spaces, an unquoted
        // one stops at the first space, and everything after it is the argument string.
        {
            WCHAR exe[512];
            WCHAR args[512];
            bool ok = ExtParsePublisherCommand(L"pwsh -NoProfile -File C:\\a\\b.ps1",
                                               exe, 512, args, 512);
            Check(ok && wcscmp(exe, L"pwsh") == 0 &&
                      wcscmp(args, L"-NoProfile -File C:\\a\\b.ps1") == 0,
                  "an unquoted publisher command splits at the first space");
            ok = ExtParsePublisherCommand(
                L"  \"C:\\Program Files\\PowerShell\\pwsh.exe\"  -File \"x y.ps1\"",
                exe, 512, args, 512);
            Check(ok && wcscmp(exe, L"C:\\Program Files\\PowerShell\\pwsh.exe") == 0 &&
                      wcscmp(args, L"-File \"x y.ps1\"") == 0,
                  "a quoted publisher executable may contain spaces; arguments keep their quotes");
            Check(!ExtParsePublisherCommand(L"   ", exe, 512, args, 512),
                  "a whitespace-only publisher command has no executable");
            Check(!ExtParsePublisherCommand(L"", exe, 512, args, 512),
                  "an empty publisher command has no executable");

            // 看门狗的名字匹配：大小写不敏感 ✓、带不带 .exe 都认 ✓、空名字不匹配 ✓
            Check(ExtProcessNameMatches(L"cloudmusic.exe", L"cloudmusic"),
                  "a watched name without .exe matches the image name");
            Check(ExtProcessNameMatches(L"cloudmusic", L"cloudmusic.exe"),
                  "a watched name with .exe also matches");
            Check(ExtProcessNameMatches(L"CloudMusic.EXE", L"cloudmusic"),
                  "the watched name match is case-insensitive");
            Check(!ExtProcessNameMatches(L"cloudmusic_reporter.exe", L"cloudmusic"),
                  "a longer image name does not match the watched name");
            Check(!ExtProcessNameMatches(L"", L"cloudmusic") &&
                      !ExtProcessNameMatches(L"cloudmusic.exe", L""),
                  "an empty name on either side never matches");
        }

        // The nominal clock has two sources: the registry's "~MHz" (what ACPI/BIOS
        // reports) and the power API's per-processor maximum. They disagree by ~10% on
        // the machine this was reported from, and the API is the one Windows treats as
        // nominal.
        {
            ProcPowerInfo one[1] = {};
            one[0].maxMhz = 2200;
            Check(CpuNominalFromInfo(one, 1, 2.42) == 2.2,
                  "the power API's nominal beats the registry's value");
            ProcPowerInfo hybrid[4] = {};
            hybrid[0].maxMhz = 2200;
            hybrid[1].maxMhz = 1600;
            hybrid[2].maxMhz = 2200;
            hybrid[3].maxMhz = 1600;
            Check(CpuNominalFromInfo(hybrid, 4, 2.42) == 2.2,
                  "a hybrid CPU's slower cores must not become the nominal");
            ProcPowerInfo nothing[1] = {};
            Check(CpuNominalFromInfo(nothing, 1, 2.42) == 2.42,
                  "and when the API reports nothing, the registry value stands");
        }

        // Which source wins has to survive the *second* registry read. ReadCpuStatic()
        // runs again on every settings-version change, and an unconditional write to
        // g_cpuGhzMax there reverted a 2.20 GHz nominal to the registry's 2.42 for the
        // rest of the session -- the power API is only ever tried once, so nothing put
        // it back. The latch is what keeps the two apart, so this drives the real
        // ReadCpuStatic() rather than re-deriving the rule here.
        {
            const double savedNominal = g_cpuGhzMax;
            const double savedFromApi = g_cpuGhzFromApi;

            // Ask the product itself what the registry alone would hand out, rather
            // than reading the registry a second way here: the fallback's *source* is
            // what is under test, so the test must not compute it independently.
            g_cpuGhzFromApi = 0.0;
            g_cpuGhzMax = 0.0;
            ReadCpuStatic();
            const double registryFallback = g_cpuGhzMax;

            // A nominal no registry value can equal, so "the registry overwrote it" is
            // never mistaken for "the latch held".
            g_cpuGhzFromApi = 3.3;   // the power API has answered
            g_cpuGhzMax = 3.3;
            ReadCpuStatic();
            Check(g_cpuGhzMax == 3.3,
                  "a later registry read does not undo the power API's nominal");

            if (registryFallback > 0.0) {
                printf("  registry fallback = %.2f GHz, API answer = 3.30 GHz (both paths live)\n",
                       registryFallback);
            } else {
                printf("  no ~MHz here: the second registry read had nothing to write, so "
                       "the check above cannot fail on this machine\n");
            }

            g_cpuGhzMax = savedNominal;
            g_cpuGhzFromApi = savedFromApi;
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
                Skip("the pipe DACL admits the user running the mod (no pipe clients here)");
                printf("    control pipe err=%lu (a confined sandbox refuses every descriptor)\n",
                       controlErr);
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

        // A pushed value has to republish the rows, not re-run the collectors. Waking
        // the sampler for g_extDirty used to run a whole SamplerIteration, so an active
        // publisher set the panel's sampling rate: with a bridge pushing a title, a
        // lyric and a position a few times a second, CPU, memory, network (and NVML,
        // whenever a row asks for {gpu...}) were re-read at the publisher's rate, and
        // every one of those samples repainted. RepublishRows is the light path; these
        // checks are what keep it light, and what keep it from being repainted when
        // nothing changed.
        printf("\n  --- a pushed value republishes, it does not re-sample ---\n");
        InitializeCriticalSection(&g_settingsLock);
        InitializeCriticalSection(&g_valuesLock);   // the host never ran WhTool_ModInit
        {
            LOCAL_SETTINGS(s, g_publish);
            s.rowCount = 1;
            wcscpy(s.rows[0], L"{ext.music.title}  [[ring:{ext.music.progress}]]");
            // The settings were built by hand, so the compile step ReadSettings performs has
            // to be performed here: a row that was never compiled instantiates to nothing.
            CompileRowText(s, s.rows[0], s.rowProgram[0]);
            const Values v;
            EnterCriticalSection(&g_settingsLock);
            g_settings = s;
            LeaveCriticalSection(&g_settingsLock);
            InterlockedIncrement(&g_settingsVersion);   // as the loader thread does
            g_values = v;
            // The provider name is what the row's provider is matched against; the
            // earlier part of this section connected as "music", and spelling it out
            // here keeps this block readable on its own.
            wcscpy(g_ext[0].name, L"music");

            ExtStoreValue(0, L"title", L"Blue Hour", 60000, 512);
            Check(PublishRows(v, s), "the first publish is a change");
            Check(!PublishRows(v, s),
                  "publishing the same values again is not (so it is not repainted)");

            ExtStoreValue(0, L"title", L"Blue Hour II", 60000, 512);
            Check(PublishRows(v, s), "a changed pushed value is a change");

            // A row that is nothing but a gauge: its text never moves while the value
            // does, so a text-only comparison would miss the move entirely.
            ExtStoreValue(0, L"progress", L"42", 60000, 512);
            Check(PublishRows(v, s), "a gauge that moved is a change even when no text did");
            Check(!PublishRows(v, s), "and it settles again once the gauge stops moving");

            const unsigned long long samplesBefore = g_sampleCount;
            ExtStoreValue(0, L"title", L"Blue Hour III", 60000, 512);
            Check(RepublishRows(), "the light path finds the pushed change by itself");
            Check(g_sampleCount == samplesBefore, "a pushed value costs no system sample");
            Check(!RepublishRows(), "and a second pass finds nothing new to draw");
            Check(g_sampleCount == samplesBefore, "so it still costs none");
            SamplerIteration();
            Check(g_sampleCount == samplesBefore + 1,
                  "while a scheduled sample does re-read the counters");
            printf("  light republishes=%llu, samples=%llu\n", g_republishCount, g_sampleCount);
        }
        DeleteCriticalSection(&g_valuesLock);
        DeleteCriticalSection(&g_settingsLock);

        DeleteCriticalSection(&g_extLock);
        g_extLockReady = false;
    }

    printf("\n=== sampler wake policy ===\n");
    {
        // A pushed value is cheap; the refresh interval is not optional. If a chatty
        // publisher could make every wake "light", the system rows would never be
        // re-read at all -- the opposite failure of the one this rule fixes.
        printf("  push just after a sample, push mid-interval, push past the interval, "
               "interval alone\n");
        Check(SamplerWakeIsLight(true, 1000, 1000, 1000),
              "a push arriving right after a sample is the cheap path");
        Check(SamplerWakeIsLight(true, 1500, 1000, 1000),
              "and so is a push that lands mid-interval");
        Check(!SamplerWakeIsLight(true, 2000, 1000, 1000),
              "but a push that lands once the interval is due re-reads the counters");
        Check(!SamplerWakeIsLight(true, 9000, 1000, 1000),
              "and a badly overdue interval cannot be talked out of it by a push");
        Check(!SamplerWakeIsLight(false, 1500, 1000, 1000),
              "an interval wake re-reads the counters, push or no push");
    }

    printf("\n=== publisher restart budget ===\n");
    {
        // The budget is for crash loops, not a lifetime tally. Five starts *ever* --
        // including the harmless ones a user causes by restarting the music client, or
        // by killing the publisher by hand -- used to disable it for the rest of the
        // session, and the only cure was reloading the mod: the song title and lyric
        // simply stopped updating while the log said "gave up after 5 restarts".
        printf("  up 61s / exactly 60s / 59s / dead-but-long-lived / never started\n");
        Check(PublisherEarnedBackRestarts(true, 62000, 1000),
              "a publisher that has been up for over a minute earns its budget back");
        Check(PublisherEarnedBackRestarts(true, 61000, 1000),
              "exactly a minute counts too (the boundary is inclusive)");
        Check(!PublisherEarnedBackRestarts(true, 60000, 1000),
              "just under a minute of uptime is not enough to prove anything");
        Check(!PublisherEarnedBackRestarts(false, 900000, 1000),
              "a process that has died has earned nothing, however long it lived");
        Check(!PublisherEarnedBackRestarts(true, 900000, 0),
              "nor has a process the mod itself never started");
    }

    // The settings strings are the one place the product has to pair an allocation with
    // a release, and the stub now models that ownership. Anything still outstanding here
    // leaked; a free the ledger did not recognise already printed its own line.
    printf("\n=== settings string ownership ===\n");
    printf("  handed out %d string(s) during the run, %d still outstanding\n",
           g_stringsHandedOut, g_handedOutCount);
    Check(g_handedOutCount == 0, "every settings string the run asked for was released");
    Check(!g_stringLedgerBroken, "no string was released twice or never handed out");

    // Skips are printed in the last line too: a run that quietly stopped checking half
    // its invariants must not read like a clean pass.
    if (g_skips) printf("\nSKIPPED: %d check(s) could not run in this environment\n", g_skips);
    if (g_skips) {
        printf("%s (%d failure(s), %d skipped)\n", g_failures ? "FAILED" : "PASSED", g_failures,
               g_skips);
    } else {
        printf("%s (%d failure(s))\n", g_failures ? "FAILED" : "PASSED", g_failures);
    }
    return g_failures ? 1 : 0;
}
