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
    references -- no compiler looks at those at all;
  * a name **defined twice in one script file**. The reference rule above cannot
    see this shape at all: the name *is* referenced (the callers bind to the
    later definition), so both copies look alive while the earlier one can never
    run. `_netease-bridge.ps1` carried two byte-identical copies of
    `Test-VinylJumpPlausible` (the first removed in e141514) and this script said
    "no dead code found" on exactly that revision. When the two copies *differ*,
    the earlier one is still unreachable and the two have silently diverged --
    at least as dangerous, so it is reported with a different verdict line.

Deliberate limits, so a clean run means something: reads are counted across all
C++ files given (the regression host includes the mod and reads fields the mod
only writes), comments are stripped before anything is counted, and a short
allowlist covers fields that something *outside* this repository reads.

The duplicate-definition rule is deliberately narrow; what it does *not* see:

  * C++ -- a duplicate function/class definition there does not compile, so the
    compiler is already the judge and this rule stays out of the way;
  * PowerShell definitions that are **not at column 0** (nested `function`s, and
    the JavaScript functions that live inside the here-strings this file embeds
    and sends to the page) -- a nested function is only visible once its parent
    ran, so "the later one shadows the earlier one" is not true of it. Here-strings
    are masked out first, using PowerShell's own rule that the closing marker must
    start at column 0;
  * Python definitions in *different* arms of one conditional (`try:`/`except`,
    `if`/`else`) -- that is the legitimate "import fallback" redefinition, and it
    is left alone. Two definitions in the *same* branch (same chain of enclosing
    statements) are reported even when that branch is an `if`, because same-branch
    is exactly the unreachable shape;
  * two definitions of one name in different files, and names bound by anything
    other than `function`/`filter`/`workflow` (PowerShell) or `def`/`class`
    (Python) -- `Set-Alias`, `$x = { ... }`, `x = lambda`, re-imports.

Where the parse is uncertain the rule errs toward *reporting*, never toward silence:
a multi-line condition head (`if (` alone on its line) reads as "not a head", so such a
pair is reported even when it is a legitimate fallback. That is the deliberate direction
-- a missed duplicate is the bug this rule exists for, and the legitimate shapes above
are what keep it from crying wolf.

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

# --- duplicate definitions within one file -----------------------------------
# PowerShell: `function Name {` / `function Name() {` / `filter` / `workflow`. Only
# column 0 counts (see the module docstring): the bridge keeps JavaScript functions
# inside here-strings, and `^function` would otherwise claim those as PowerShell ones.
PS_DEF_RE = re.compile(r"^(function|filter|workflow)\s+([A-Za-z0-9_-]+)", re.I | re.M)
# Python: any indentation -- same-indentation duplicates are the shape that matters,
# and the enclosing-condition check below is what keeps the legitimate ones quiet.
PY_DEF2_RE = re.compile(r"^([ \t]*)(def|class)\s+([A-Za-z_]\w*)")
# Heads that make two same-named definitions in *different* branches legitimate: a
# try/except import fallback, or an `if`/`else` that picks an implementation. `with`,
# `for` and `while` are deliberately absent -- they are not alternative branches, so a
# redefinition inside one of them is still an unreachable earlier copy.
PY_CONDITIONAL_HEADS = ("if", "elif", "else", "try", "except", "finally")

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


def mask_ps_here_strings(raw):
    """Blank out PowerShell here-string bodies, keeping every newline.

    `$x = @'` ... `'@` (and the `@"` ... `"@` form) usually carries JavaScript, and
    JavaScript has `function` declarations too. Without this, an embedded `function pick`
    looks like a PowerShell definition; with a nested `function` inside a *real*
    PowerShell function, two such hits would even pair up into a bogus duplicate.
    The closing marker must sit at column 0 (PowerShell's rule), so searching for a line
    that *starts* with it is the same test the parser makes.
    """
    lines = raw.split("\n")
    out, opener = [], None
    for line in lines:
        if opener is not None:
            if line.startswith(opener):
                opener = None
            out.append("")
            continue
        out.append(line)
        for m in re.finditer(r'@"|@\'', line):
            if not line[m.end():].strip():
                opener = '"@' if m.group(0) == '@"' else "'@"
                break
    return "\n".join(out)


def ps_definitions(raw):
    """Yield (line, name, body) for every column-0 PowerShell definition.

    `body` is the text from the signature's opening `{` to its matching `}`, so two
    definitions can be compared byte for byte (modulo per-line indentation) without
    comparing their comments. Brace matching skips quoted strings -- `'}'` and `"}"` are
    common in this bridge's scripts and would otherwise truncate the body.
    """
    code = mask_ps_here_strings(raw)
    for m in PS_DEF_RE.finditer(code):
        line = code[: m.start()].count("\n") + 1
        depth, i, start = 0, m.end(), None
        while i < len(code):
            ch = code[i]
            if ch in "\"'":
                quote, j = ch, i + 1
                while j < len(code):
                    if code[j] == "`" and quote == '"':
                        j += 2
                        continue
                    if code[j] == quote:
                        if j + 1 < len(code) and code[j + 1] == quote:   # '' / "" escape
                            j += 2
                            continue
                        break
                    j += 1
                i = j + 1
            elif ch == "{":
                if depth == 0:
                    start = i
                depth += 1
                i += 1
            elif ch == "}":
                depth -= 1
                if depth == 0 and start is not None:
                    yield line, m.group(2), _dedent(code[start: i + 1])
                    break
                i += 1
            else:
                i += 1


def _dedent(text):
    """Drop the common leading indentation, so per-line indent is not a body difference."""
    lines = [ln for ln in text.splitlines() if ln.strip()]
    if len(lines) < 2:
        return text
    pad = min(len(ln) - len(ln.lstrip()) for ln in lines[1:])
    return "\n".join([lines[0]] + [ln[pad:] if len(ln) > pad else ln.strip() for ln in lines[1:]])


def py_code_part(line):
    """The code on a line with any trailing `#` comment removed.

    A head such as `if _HAS_FAST:   # noqa` still ends the *statement* with a colon; testing
    the raw line for that would call it "not a condition head" and turn a legitimate
    if/else pair into a reported duplicate. Quotes are skipped so a `#` inside a string
    literal is not mistaken for a comment.
    """
    quote = None
    for i, ch in enumerate(line):
        if quote:
            if ch == quote:
                quote = None
            continue
        if ch in "\"'":
            quote = ch
        elif ch == "#":
            return line[:i]
    return line


def py_parent_chain(lines, index, indent):
    """The chain of enclosing statements above a definition, innermost first.

    Each entry is (line, is_a_condition_head). Walking outward is what lets the caller ask
    "same conditional, different arm?" -- the key is the *set* of enclosing conditional
    lines, so two `if`s that sit side by side (both at column 0) are two different
    conditionals, not two arms of one.
    """
    chain, want = [], indent
    for j in range(index - 1, -1, -1):
        stripped = lines[j].strip()
        if not stripped or stripped.startswith("#"):
            continue
        parent_indent = len(lines[j]) - len(lines[j].lstrip())
        if parent_indent < want:
            # The head is the first bare word only: `if _HAS_FAST:` must read as `if`, not
            # as `_HAS_F` (a `\w*` here silently turned every conditional into "not a head"),
            # and any trailing comment is cut first so the colon is still the last character.
            statement = py_code_part(stripped).rstrip()
            head = re.match(r"([A-Za-z_]+)", statement)
            word = head.group(1) if head else ""
            chain.append((j + 1, word in PY_CONDITIONAL_HEADS and statement.endswith(":")))
            want = parent_indent
            if not want:
                break
    return chain


def py_definitions(raw):
    """Yield (line, name, body, chain, indent) for every `def`/`class`.

    The body is the signature plus every following line that is either blank or indented
    more than the header; that is the block, by Python's own rule.
    """
    lines = raw.split("\n")
    for i, line in enumerate(lines):
        m = PY_DEF2_RE.match(line)
        if not m:
            continue
        indent, name = len(m.group(1).expandtabs(4)), m.group(3)
        sig, depth = [line], line.count("(") - line.count(")")
        j = i
        while depth > 0 and j + 1 < len(lines):
            j += 1
            sig.append(lines[j])
            depth += lines[j].count("(") - lines[j].count(")")
        body = list(sig)
        while j + 1 < len(lines):
            nxt = lines[j + 1]
            if not nxt.strip() or (len(nxt) - len(nxt.lstrip())) > indent:
                body.append(nxt)
                j += 1
            else:
                break
        yield i + 1, name, _dedent("\n".join(body)), py_parent_chain(lines, i, indent), indent


def collect_definitions(path):
    """Every same-file definition the duplicate rule considers, as (name, line, body, scope, indent).

    `scope` is the key a duplicate has to share to be real: the chain of enclosing
    statements for Python, or 0 for PowerShell (where only column-0 functions count, so
    there is exactly one scope).
    """
    raw = path.read_text(encoding="utf-8", errors="replace")
    if path.suffix == ".ps1":
        return [(name, line, body, 0, 0) for line, name, body in ps_definitions(raw)]
    return [(name, line, body, tuple(chain), indent)
            for line, name, body, chain, indent in py_definitions(raw)]


def check_duplicate_definitions(paths, findings):
    """Report a name defined more than once in one file -- the shape no other rule sees.

    The point is not the count, it is *which* copy can run: PowerShell keeps the last
    definition, so an earlier one is unreachable; when the bodies differ the two also
    diverge silently, which is worse than the identical case, not better.
    """
    dups = 0
    names = 0
    for path in paths:
        groups = {}
        for name, line, body, scope, indent in collect_definitions(path):
            groups.setdefault(name, []).append((line, body, scope, indent))
        names += len(groups)
        for name in sorted(groups):
            defs = groups[name]
            if len(defs) < 2:
                continue
            lines = [line for line, _b, _s, _i in defs]
            # Different indentation = different scope: a nested redefinition is invisible
            # from the outer one, so it is not a shadowing copy of it.
            if len({indent for _l, _b, _s, indent in defs}) != 1:
                continue
            # Two definitions in *different* arms of one conditional is how a fallback is
            # spelled (`try: import x except ImportError: import y as x`). That is the one
            # legitimate shape, so it is left alone; a redefinition in the *same* branch
            # (same chain) is reported even when it sits under an `if`.
            scopes, all_conditional = [], True
            for _l, _b, scope, _i in defs:
                heads = scope if isinstance(scope, tuple) else ()
                scopes.append(frozenset(heads))
                all_conditional = all_conditional and any(cond for _ln, cond in heads)
            if all_conditional and len({s for s in scopes}) == len(defs):
                continue
            dups += 1
            identical = len({body for _l, body, _s, _i in defs}) == 1
            try:
                rel = path.relative_to(ROOT)
            except ValueError:
                rel = path
            verdict = ("byte-identical bodies -- provably dead, only the last definition can run"
                       if identical else
                       "bodies differ -- the earlier definition can never run, and the two "
                       "have silently diverged")
            print(f"  {'duplicate definition':21} {rel}:{lines[0]}: {name} defined "
                  f"{len(defs)}x at line(s) {', '.join(str(x) for x in lines)}")
            print(f"  {'':21} {verdict}")
            findings.append("duplicate definition")
    return dups, names


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
    dups, names = check_duplicate_definitions(ps + py, findings)

    if findings:
        print(f"\n{len(findings)} finding(s): " + ", ".join(sorted(set(findings))))
        return 1
    print("\nno dead code found (every field is read, no #if 0, every script function used, "
          f"0 duplicate definitions among {names} script definition name(s))")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
