#!/usr/bin/env python3
"""Finds dead code the compiler cannot see.

The toolchain already covers *symbols*: `-Wunused-function` / `-Wunused-variable`
/ `-Wunused-macros` are on in build.ps1, and the mod builds with exactly one
known warning (the winsock2.h include order, which is Windhawk's own header).
What is left is the part no C++ compiler warns about:

  * struct fields that are **written but never read** -- a value computed every
    frame and then thrown away, or a field left behind by a refactor. A field
    that is never touched at all is reported too;
  * `#if 0` blocks;
  * commented-out code (the classic way a "temporarily disabled" line survives
    for years). Prose comments are not reported: the comment has to end like a
    statement, with `;`, `{` or `}`;
  * functions defined in the **scripts** (PowerShell / Python) that nothing
    references -- no compiler looks at those at all.

Deliberate limits, so a clean run means something: reads are counted across all
C++ files given (the regression host includes the mod and reads fields the mod
only writes), comments are stripped before anything is counted, and a short
allowlist covers fields that something *outside* this repository reads.

Usage:  python .tool/checks/_deadcode.py [paths...]   (default: this repository)
Exit:   0 = nothing found, 1 = at least one finding.
"""
import re
import sys
from pathlib import Path

# This script lives in .tool/checks/, so the repository root is two levels up. The default
# target set is the whole tree -- the host under .tool/regress/ and the product at the root
# have to land in the *same* read-count pass, or a field the host reads looks write-only.
ROOT = Path(__file__).resolve().parents[2]
SKIP_DIRS = {".git", "__pycache__", ".Trash", "dist", "node_modules"}

# Fields our code only writes (or never touches) but that *another* component reads:
# a Win32 / GDI+ structure we declare ourselves, where the API requires the field to
# be filled in. A heuristic cannot know this, so it is spelled out.
EXTERNAL_READERS = {
    "ExtProcessEntry32W.dwSize",          # Process32FirstW requires it
    "GdiplusStartupInput.GdiplusVersion",
    "GdiplusStartupInput.SuppressBackgroundThread",
    "GdiplusStartupInput.SuppressExternalCodecs",
    "GdiplusStartupInput.DebugEventCallback",
}

# Whole structs that mirror an OS structure: the API fills them in and their layout has
# to match, so fields our code never looks at are still not dead code.
EXTERNAL_STRUCTS = {
    "ProcPowerInfo",          # PROCESSOR_POWER_INFORMATION, filled by CallNtPowerInformation
    "ExtProcessEntry32W",     # PROCESSENTRY32W, filled by Process32FirstW/NextW
    "GdiplusStartupInput",    # filled by us, read by GdiplusStartup
}

# A field declaration inside a struct body: a plain `Type name;` / `Type name = x;`.
FIELD_RE = re.compile(
    r"^\s*(?:const\s+)?(?:unsigned\s+|signed\s+)?"
    r"(?:struct\s+\w+|enum\s+\w+|char|short|int|long|float|double|bool|BOOL|"
    r"BYTE|WORD|DWORD|LONG|ULONG|UINT|WCHAR|wchar_t|size_t|void|[A-Z]\w*)"
    r"(?:\s*\*+)?\s+([A-Za-z_]\w*)\s*(?:\[[^\]]*\])?\s*(?:=[^;]*)?;"
)

PS_FUNC_RE = re.compile(r"^\s*function\s+([A-Za-z0-9_-]+)", re.M)
PY_DEF_RE = re.compile(r"^(?:def|class)\s+([A-Za-z_]\w*)", re.M)

# A commented-out *statement*: the `//` is followed by code-shaped text that ends with
# `;`, `{` or `}`. The terminator is what keeps prose out.
COMMENTED_CODE_RE = re.compile(
    r"^\s*(?://+|#)\s*("
    r"\w+\s*(?:\(|\[|\.\w|->)|"
    r"if\s*\(|for\s*\(|while\s*\(|return\b|switch\s*\(|else\b|case\b|"
    r"[A-Za-z_]\w*\s+[A-Za-z_]\w*\s*[;=]|"
    r"[A-Za-z_]\w*\s*=[^=]"
    r").*[;{}]\s*$"
)


def strip_comments(text):
    """Blank out // and /* */ comments, keeping every newline (line numbers survive)."""
    out = []
    i, n = 0, len(text)
    while i < n:
        if text.startswith("//", i):
            while i < n and text[i] != "\n":
                out.append(" ")
                i += 1
            continue
        if text.startswith("/*", i):
            while i < n and not text.startswith("*/", i):
                out.append("\n" if text[i] == "\n" else " ")
                i += 1
            out.append("  ")
            i += 2
            continue
        if text[i] == '"':
            out.append('"')
            i += 1
            while i < n:
                if text[i] == "\\" and i + 1 < n:
                    out.append(text[i:i + 2])
                    i += 2
                    continue
                out.append(text[i])
                if text[i] == '"':
                    i += 1
                    break
                i += 1
            continue
        out.append(text[i])
        i += 1
    return "".join(out)


def struct_bodies(code):
    """Yield (name, first_body_line, body) for every struct, brace-matched."""
    for m in re.finditer(r"\bstruct\s+([A-Za-z_]\w*)\s*\{", code):
        depth, i = 1, m.end()
        while i < len(code) and depth:
            if code[i] == "{":
                depth += 1
            elif code[i] == "}":
                depth -= 1
            i += 1
        if depth == 0:
            body_start = code[: m.end()].count("\n") + 1
            yield m.group(1), body_start, code[m.end(): i - 1]


def report(kind, path, line, text):
    try:
        rel = path.relative_to(ROOT)
    except ValueError:
        rel = path
    print(f"  {kind:20} {rel}:{line}: {text.strip()[:96]}")


def check_cpp(path, findings, all_code):
    raw = path.read_text(encoding="utf-8", errors="replace")
    code = strip_comments(raw)

    for name, body_start, body in struct_bodies(code):
        if name in EXTERNAL_STRUCTS:
            continue
        # A body containing `{` has member functions (or nested types). Their fields can
        # be referenced unqualified inside those bodies, which a text scan cannot see,
        # so the whole struct is left alone rather than reported as a false positive.
        if "{" in body:
            continue
        for i, decl in enumerate(body.splitlines()):
            fm = FIELD_RE.match(decl)
            if not fm:
                continue
            field = fm.group(1)
            if field in ("struct", "const", "return", "if", "else"):
                continue
            if f"{name}.{field}" in EXTERNAL_READERS:
                continue
            uses = re.findall(r"(?:\.|->)" + re.escape(field) + r"\b", all_code)
            writes = re.findall(r"(?:\.|->)" + re.escape(field) + r"\s*=[^=]", all_code)
            if len(uses) == len(writes):
                what = (f"is set {len(writes)}x and never read" if writes
                        else "is never used at all")
                report("write-only field", path, body_start + i, f"{name}.{field} {what}")
                findings.append("write-only field")

    for i, line in enumerate(raw.splitlines()):
        if re.match(r"^\s*#if\s+0\b", line):
            report("#if 0", path, i + 1, line)
            findings.append("#if 0")
        elif COMMENTED_CODE_RE.match(line):
            report("commented-out code", path, i + 1, line)
            findings.append("commented-out code")


def check_script(path, findings, func_re, label):
    code = strip_comments(path.read_text(encoding="utf-8", errors="replace"))
    for name in sorted(set(func_re.findall(code))):
        if len(re.findall(r"\b" + re.escape(name) + r"\b", code)) <= 1:
            report(f"unreferenced {label}", path, 1, name)
            findings.append(f"unreferenced {label}")


def discover():
    """Every .cpp / .ps1 / .py in the tree, once, skipping .git and byproduct folders.

    The tools used to sit flat in the root, so a single iterdir() was the whole set. Now
    that they live under .tool/, the walk has to be recursive: the host (which #includes
    the product) and the product both have to be in this list, and nothing may be picked
    up twice -- a duplicate would double every read count and hide a write-only field.
    """
    out = []
    for path in ROOT.rglob("*"):
        if path.suffix not in (".cpp", ".ps1", ".py") or not path.is_file():
            continue
        if any(part in SKIP_DIRS for part in path.relative_to(ROOT).parts[:-1]):
            continue
        if path.name == Path(__file__).name:
            continue
        out.append(path)
    return sorted(set(out))


def main(argv):
    targets = [Path(a) for a in argv[1:]]
    if not targets:
        targets = discover()

    cpp = [p for p in targets if p.suffix == ".cpp"]
    ps = [p for p in targets if p.suffix == ".ps1"]
    py = [p for p in targets if p.suffix == ".py"]

    print(f"dead-code scan: {len(cpp)} C++ file(s), {len(ps)} PowerShell, {len(py)} Python")
    findings = []
    all_code = "\n".join(strip_comments(p.read_text(encoding="utf-8", errors="replace"))
                         for p in cpp)
    for p in cpp:
        check_cpp(p, findings, all_code)
    for p in ps:
        check_script(p, findings, PS_FUNC_RE, "function")
    for p in py:
        check_script(p, findings, PY_DEF_RE, "def")

    if findings:
        print(f"\n{len(findings)} finding(s): " + ", ".join(sorted(set(findings))))
        return 1
    print("\nno dead code found (every field is read, no #if 0, every script function used)")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
