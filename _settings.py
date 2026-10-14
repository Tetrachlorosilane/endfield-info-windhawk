"""Normalises and validates the Windhawk settings block in the mod source.

Windhawk parses the ==WindhawkModSettings== block with js-yaml. Plain YAML
scalars have sharp edges, and a bare `-` as a `$description` value is the exact
thing that made Windhawk reject this mod's first settings schema, so every
$name / $description value is written as a single-quoted string.

Single quotes are used because they have no escape sequences in YAML: only a
literal apostrophe needs doubling. Values, ids and numeric settings are left
exactly as written, because Windhawk has to read numbers back as numbers.

Run:  python _settings.py            # check only
      python _settings.py --write    # normalise the display strings
"""

import os
import re
import sys

# Resolve the source next to this script so the tool works from any directory.
SOURCE = os.path.join(
    os.path.dirname(os.path.abspath(__file__)), "endfield-info-windhawk.wh.cpp"
)
BEGIN = "// ==WindhawkModSettings=="
END = "// ==/WindhawkModSettings=="


def quote(value: str) -> str:
    value = value.strip()
    if len(value) >= 2 and value[0] == value[-1] and value[0] in "\"'":
        return value
    return "'" + value.replace("'", "''") + "'"


def structural_problems(block, first_line_no):
    """Checks that js-yaml rejects, or that would silently mis-parse.

    These two were added after Windhawk refused the block twice while this script
    reported "no structural problems found" both times:

      * a sequence entry indented by five spaces instead of four, which js-yaml
        reports as "bad indentation of a sequence entry";
      * an entry inserted between another entry's key and its own $name, which
        leaves two $name keys inside one entry ("duplicated mapping key").

    The duplicate-key and duplicate-option checks are the same class of thing:
    the block is a mapping, and YAML silently keeps the last of two equal keys.
    """
    problems = []
    entries = []
    current = None

    for offset, raw in enumerate(block):
        line_no = first_line_no + offset
        stripped = raw.strip()
        if not stripped or stripped in ("/*", "*/"):
            continue
        indent = len(raw) - len(raw.lstrip(" "))

        if stripped.startswith("- "):
            if indent == 0:
                current = {
                    "line": line_no,
                    "key": stripped[2:].partition(":")[0].strip(),
                    "names": 0,
                    "descriptions": 0,
                    "options": False,
                    "items": [],
                }
                entries.append(current)
                continue
            if indent != 4:
                problems.append(
                    f"{line_no}: sequence entry indented by {indent} spaces "
                    f"(option items need exactly 4): {stripped}"
                )
            if current is None:
                problems.append(f"{line_no}: option item outside any entry: {stripped}")
            elif not current["options"]:
                problems.append(
                    f"{line_no}: option item before the entry's $options block: {stripped}"
                )
            else:
                current["items"].append(stripped[2:].partition(":")[0].strip())
            continue

        if indent != 2:
            continue
        if stripped.startswith("$name:"):
            if current is not None:
                current["names"] += 1
        elif stripped.startswith("$description:"):
            if current is not None:
                current["descriptions"] += 1
        elif stripped.startswith("$options:"):
            if current is not None:
                current["options"] = True

    seen_keys = {}
    for entry in entries:
        key = entry["key"]
        if key in seen_keys:
            problems.append(
                f"{entry['line']}: duplicated key {key!r} "
                f"(already used on line {seen_keys[key]})"
            )
        seen_keys[key] = entry["line"]
        if entry["names"] != 1:
            problems.append(
                f"{entry['line']}: entry {key!r} has {entry['names']} $name line(s), "
                f"exactly one is required"
            )
        if entry["descriptions"] != 1:
            problems.append(
                f"{entry['line']}: entry {key!r} has {entry['descriptions']} "
                f"$description line(s), exactly one is required"
            )
        for item in sorted(set(entry["items"])):
            if entry["items"].count(item) > 1:
                problems.append(f"{entry['line']}: option {item!r} is listed twice")
    return problems


def main() -> int:
    write = "--write" in sys.argv
    with open(SOURCE, encoding="utf-8") as handle:
        lines = handle.read().split("\n")

    try:
        start = lines.index(BEGIN)
        end = lines.index(END)
    except ValueError:
        print("FAIL: settings block markers not found")
        return 1

    block = lines[start + 1 : end]
    out, problems, quoted = [], [], 0
    entries = 0
    groups = 0

    for offset, raw in enumerate(block):
        line_no = start + 2 + offset
        stripped = raw.strip()

        # Every line is preserved verbatim unless it is one of the two display
        # strings. An earlier version of this script dropped everything else and
        # destroyed the block; the pass-through below is the fix for that.
        if not stripped or stripped in ("/*", "*/"):
            out.append(raw)
            continue

        if stripped.startswith("$name:") or stripped.startswith("$description:"):
            meta_key, _, meta_value = stripped.partition(":")
            meta_value = meta_value.strip()
            if not meta_value:
                problems.append(f"{line_no}: empty value for {meta_key}")
            out.append(f"  {meta_key}: {quote(meta_value)}")
            quoted += 1
            continue

        if raw.startswith("- "):
            entries += 1
            key, _, value = raw[2:].partition(":")
            if not value.strip():
                problems.append(f"{line_no}: top-level entry has no value: {raw}")
            elif key.strip() != key or not key.strip():
                problems.append(f"{line_no}: malformed key: {raw}")

        if re.match(r"^\s{2}\$options:\s*$", raw):
            groups += 1

        if re.match(r"^\s{2}\$", raw) or re.match(r"^\s{2}[A-Za-z]", raw):
            out.append(raw)
            continue

        if re.match(r"^\s{4}- ", raw):
            out.append(raw)
            continue

        out.append(raw)

    print(f"settings entries: {entries}")
    print(f"option groups: {groups}")
    print(f"display strings quoted: {quoted}")

    # Structure, not formatting: the checks below are the ones Windhawk's own
    # parser refused the block for while this script said it was fine.
    problems.extend(structural_problems(block, start + 2))

    if problems:
        print("\nPROBLEMS:")
        for problem in problems:
            print("  " + problem)
    else:
        print("no structural problems found")

    if write:
        lines[start + 1 : end] = out
        with open(SOURCE, "w", encoding="utf-8", newline="\n") as handle:
            handle.write("\n".join(lines))
        print(f"\nrewrote {SOURCE}")

    return 1 if problems else 0


if __name__ == "__main__":
    raise SystemExit(main())
