#!/usr/bin/env python3
"""Audit the mod's wide-format call sites.

Why this exists
---------------
`Wh_Log`, `DBG` and `CpuClockLog` all take a wide format string, and **no compiler
in this toolchain checks them**:

  * `__attribute__((format(printf, 1, 2)))` on a function whose format parameter is
    `wchar_t*` is a hard error ("format argument not a string type"), because
    clang's `printf` archetype wants a `char*`.
  * `ms_printf` and `wprintf` are not supported archetypes here (`-Wignored-attributes`).
  * `-fms-compatibility` (which would enable `ms_printf`) breaks the compiler's own
    headers on this target.
  * MinGW's `swprintf` is not checked either: `swprintf(b, 0, L"%s", 1)` produces no
    diagnostic, so the mod's own buffer formatting is unchecked too.

All four were measured with the Windhawk clang 20 on 2026-10-14; the record lives in
`.tool/checks/_logfmt.md` (moved out of AGENTS §3, which now only names this script).
The consequence is that the host's `Wh_Log` stub proves nothing about format
strings, so this script stands in for the check that cannot exist. It is a syntax
level check, not a compiler: it says what it could not decide instead of guessing.

What it checks
--------------
  * specifier count (including `*` width/precision) against the argument count
  * `%s`/`%S` given something that is clearly a number
  * `%d`/`%u`/`%x`/`%X`/`%c` given a string literal
  * `%f`/`%e`/`%g` given something that is clearly an integer
  * (reported separately) a 64-bit specifier given a 32-bit-looking argument

Usage
-----
    python .tool/checks/_logfmt.py [path-to-.wh.cpp]

Exit code 0 = nothing found, 1 = at least one mismatch.
"""

import re
import sys
from pathlib import Path

# The product sits at the repository root, two levels above .tool/checks/, so the default
# target is resolved from this file rather than from the current directory.
DEFAULT_SRC = Path(__file__).resolve().parents[2] / "endfield-info-windhawk.wh.cpp"
CALLS = ("Wh_Log", "DBG", "CpuClockLog")

INT_CASTS = ("int", "unsigned", "DWORD", "UINT", "ULONG", "LONG", "BOOL", "bool",
             "WORD", "BYTE", "SHORT", "size_t", "UINT_PTR", "DWORD_PTR", "LONG_PTR")
FLOAT_CASTS = ("float", "double")


def split_args(text):
    """Split a call's argument text on top-level commas."""
    args, depth, cur, i, n = [], 0, [], 0, len(text)
    while i < n:
        c = text[i]
        if c in "\"'":
            quote = c
            cur.append(c)
            i += 1
            while i < n:
                cur.append(text[i])
                if text[i] == "\\":
                    i += 1
                    if i < n:
                        cur.append(text[i])
                elif text[i] == quote:
                    break
                i += 1
        elif c in "([{":
            depth += 1
            cur.append(c)
        elif c in ")]}":
            depth -= 1
            cur.append(c)
        elif c == "," and depth == 0:
            args.append("".join(cur).strip())
            cur = []
        else:
            cur.append(c)
        i += 1
    if cur:
        args.append("".join(cur).strip())
    return args


def format_literal(raw):
    """Join adjacent wide literals into the format text, or None if not a literal."""
    if not re.fullmatch(r'\s*(?:L"(?:[^"\\]|\\.)*"\s*)+', raw):
        return None
    text = "".join(re.findall(r'L"((?:[^"\\]|\\.)*)"', raw))
    return text.replace("\\\\", "\\").replace('\\"', '"')


def specifiers(fmt):
    """Yield (spec, length_modifier, args_consumed) for every conversion."""
    out = []
    i, n = 0, len(fmt)
    while i < n:
        if fmt[i] != "%":
            i += 1
            continue
        i += 1
        if i < n and fmt[i] == "%":
            i += 1
            continue
        consumed = 1
        while i < n and fmt[i] in "-+ #0":
            i += 1
        if i < n and fmt[i] == "*":
            consumed += 1
            i += 1
        while i < n and fmt[i].isdigit():
            i += 1
        if i < n and fmt[i] == ".":
            i += 1
            if i < n and fmt[i] == "*":
                consumed += 1
                i += 1
            while i < n and fmt[i].isdigit():
                i += 1
        length = ""
        for lm in ("hh", "ll", "I64", "I32", "h", "l", "z", "j", "t", "L"):
            if fmt.startswith(lm, i):
                length = lm
                i += len(lm)
                break
        if i >= n:
            break
        conv = fmt[i]
        i += 1
        out.append(("%" + length + conv, length, consumed))
    return out


def classify(arg):
    """What the argument's shape says about its type; "unknown" means undecided."""
    raw = arg.strip()
    body = re.sub(r"^\((?:const\s+)?(?:unsigned\s+|signed\s+)?[A-Za-z_][\w ]*\)", "", raw).strip()
    if body.startswith('L"'):
        return "wide-string"
    if body.startswith('"'):
        return "narrow-string"
    if body in ("true", "false"):
        return "int"
    if body.startswith("sizeof(") or body.startswith("sizeof "):
        return "size"
    if re.fullmatch(r"[-+]?(0[xX][0-9a-fA-F]+|\d+)([uUlL]*)", body):
        return "int-literal"
    if re.fullmatch(r"[-+]?\d*\.\d+f?", body):
        return "float-literal"
    if re.search(r"(GetLastError|GetTickCount|GetTickCount64|GetCurrentProcessId|"
                 r"GetCurrentThreadId|InterlockedIncrement|InterlockedExchange)\s*\(", body):
        return "int"
    for cast in INT_CASTS:
        if re.match(r"^\(\s*" + re.escape(cast) + r"\s*\)", raw):
            return "int"
    return "unknown"


def audit(src_path):
    src = Path(src_path).read_text(encoding="utf-8")
    lines = src.split("\n")
    errors, notes, checked, undecided = [], [], 0, []

    for name in CALLS:
        for m in re.finditer(r"\b" + name + r"\s*\(", src):
            line_no = src[:m.start()].count("\n") + 1
            if lines[line_no - 1].lstrip().startswith("#define"):
                continue                      # the macro definition, not a call site
            start, depth, i, n = m.end(), 1, m.end(), len(src)
            while i < n and depth:
                c = src[i]
                if c == '"':
                    i += 1
                    while i < n and src[i] != '"':
                        i += 2 if src[i] == "\\" else 1
                elif c == "(":
                    depth += 1
                elif c == ")":
                    depth -= 1
                i += 1
            args = split_args(src[start:i - 1])
            if not args or any(a.strip() == "..." for a in args):
                continue                      # a definition, e.g. CpuClockLog(PCWSTR format, ...)
            fmt = format_literal(args[0])
            if fmt is None:
                undecided.append((line_no, name, lines[line_no - 1].strip()[:100]))
                continue

            specs = specifiers(fmt)
            need = sum(s[2] for s in specs)
            have = len(args) - 1
            checked += 1
            if need != have:
                errors.append((line_no, name,
                               "format wants %d argument(s), call passes %d" % (need, have),
                               lines[line_no - 1].strip()[:100]))
                continue

            idx = 1
            for spec, length, consumed in specs:
                for _ in range(consumed - 1):
                    idx += 1
                if idx >= len(args):
                    break
                kind = classify(args[idx])
                conv = spec[-1]
                if conv in "sS" and kind in ("int", "int-literal", "size", "float-literal"):
                    errors.append((line_no, name, "%s gets a %s argument: %s"
                                   % (spec, kind, args[idx][:40]),
                                   lines[line_no - 1].strip()[:100]))
                if conv in "diuoxXc" and kind in ("wide-string", "narrow-string"):
                    errors.append((line_no, name, "%s gets a %s argument: %s"
                                   % (spec, kind, args[idx][:40]),
                                   lines[line_no - 1].strip()[:100]))
                if conv in "fega" and kind in ("int", "int-literal", "size"):
                    errors.append((line_no, name, "%s gets a %s argument: %s"
                                   % (spec, kind, args[idx][:40]),
                                   lines[line_no - 1].strip()[:100]))
                if length in ("ll", "I64", "z", "j", "t") and kind in ("int", "int-literal"):
                    notes.append((line_no, name, "%s gets a 32-bit-looking argument: %s"
                                  % (spec, args[idx][:40])))
                idx += 1

    print("format call sites checked : %d" % checked)
    print("not decidable             : %d" % len(undecided))
    for line_no, name, text in undecided:
        print("    line %d  %s: %s" % (line_no, name, text))

    print("\n== mismatches ==")
    if errors:
        for line_no, name, msg, text in errors:
            print("  %s:%d  %s: %s\n      %s" % (Path(src_path).name, line_no, name, msg, text))
    else:
        print("  (none)")

    print("\n== 64-bit specifier given a 32-bit-looking argument ==")
    if notes:
        for line_no, name, msg in notes:
            print("  %s:%d  %s: %s" % (Path(src_path).name, line_no, name, msg))
    else:
        print("  (none)")

    return 1 if errors else 0


if __name__ == "__main__":
    sys.exit(audit(sys.argv[1] if len(sys.argv) > 1 else DEFAULT_SRC))
