"""Smoke client for the mod's external-data pipe.

Two preconditions, both of them deliberate:

  1. the "External data" setting is On, and
  2. at least one row contains an {ext.provider.key} token.

Without them the mod never creates the pipe, and this script fails with
FileNotFoundError. That is the on-demand design working, not a bug.

Usage:
    python .tool/pipe/_extclient.py "Now Playing: Blue Hour"
    python .tool/pipe/_extclient.py --key progress --ttl 60000 0.42
    python .tool/pipe/_extclient.py --event "Blue Hour"          (one row)
    python .tool/pipe/_extclient.py --event --artist Mili "Blue Hour"   (two rows)
    python .tool/pipe/_extclient.py --raw "SET music title 5000 hello"   (no HELLO first)
    python .tool/pipe/_extclient.py --raw "GARBAGE"             (error code)

Replies are read on a thread so a client that only writes still works: the
server tolerates a client that never reads, which is why this cannot hang.
"""

import argparse
import os
import sys
import threading
import time

PIPE = r"\\.\pipe\endfield-info-windhawk"


def send(lines, timeout=1.0, verbose=True):
    """Writes the lines, returns whatever the server answered within timeout."""
    replies = []
    try:
        pipe = open(PIPE, "r+b", buffering=0)
    except FileNotFoundError:
        print("no pipe at %s" % PIPE)
        print("the mod only creates it when 'External data' is On and a row")
        print("contains an {ext.provider.key} token")
        return None
    except PermissionError:
        # Two very different causes look identical from here:
        #   * a sandbox that forbids opening pipes for writing (the DSH shell is
        #     one: even a pipe with no security descriptor is refused), and
        #   * the mod's pipe DACL not granting this user.
        # The mod's log distinguishes them: it prints
        #   "EXT: pipe DACL grants SYSTEM, Administrators and <sid>"
        # so compare that SID with `whoami /user`.
        print("access denied opening %s" % PIPE)
        print("the pipe exists, so either this shell is sandboxed (run it from a")
        print("normal terminal) or the mod's DACL does not include this user.")
        print("Check the Windhawk log for 'EXT: pipe DACL grants ... <sid>' and")
        print("compare the SID with the one from: whoami /user")
        return None

    with pipe:
        fileno = pipe.fileno()

        def reader():
            while True:
                try:
                    chunk = os.read(fileno, 4096)
                except OSError:
                    break
                if not chunk:
                    break
                replies.append(chunk.decode("utf-8", "replace"))

        thread = threading.Thread(target=reader, daemon=True)
        thread.start()
        for line in lines:
            payload = (line + "\n").encode("utf-8")
            # os.write on the raw descriptor instead of pipe.write on the buffered
            # object, and the byte count is printed. That count is what separates "the
            # client never wrote" from "the transport lost it" when the mod's log
            # reports an empty pipe buffer after a connection.
            try:
                written = os.write(fileno, payload)
            except OSError as exc:
                print("-> %s  FAILED: %s" % (line, exc))
                break
            if verbose:
                print("-> %s  (%d/%d bytes)" % (line, written, len(payload)))
        thread.join(timeout)
    return replies


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("text", nargs="*", help="the value to publish")
    parser.add_argument("--provider", default="music")
    parser.add_argument("--key", default="title")
    parser.add_argument("--ttl", default="-", help="'-' uses the setting, 0 deletes")
    parser.add_argument("--event", action="store_true",
                        help="send an EVENT instead (the card needs the island setting On)")
    parser.add_argument("--artist", default="",
                        help="event mode: the card's second row (empty = one row)")
    parser.add_argument("--raw", action="store_true",
                        help="treat the arguments as ready-made protocol lines")
    parser.add_argument("--quiet", action="store_true")
    args = parser.parse_args()

    if args.raw:
        lines = args.text
    elif args.event:
        # The EVENT frame's two text fields are separated by **two spaces** (a single
        # space is part of the text -- titles have spaces in them). No artist = no
        # separator = the card draws one row. See the mod's ExtSplitEventText and
        # docs/external-data-island-design.md 4.7.
        text = " ".join(args.text)
        card = "%s  %s" % (text, args.artist) if args.artist else text
        lines = ["HELLO 1 %s" % args.provider,
                 "EVENT %s info %s %s" % (args.provider, args.ttl, card)]
    else:
        lines = ["HELLO 1 %s" % args.provider,
                 "SET %s %s %s %s" % (args.provider, args.key, args.ttl, " ".join(args.text))]

    if not args.quiet:
        for line in lines:
            print("writing: %s" % line)
    started = time.monotonic()
    replies = send(lines, verbose=not args.quiet)
    if replies is None:
        return 1
    for reply in replies:
        print("<- %s" % reply.strip())
    if not args.quiet:
        print("(%.0f ms)" % ((time.monotonic() - started) * 1000))
    return 0


if __name__ == "__main__":
    sys.exit(main())
