#!/usr/bin/env python3
"""
PocketNES ROM builder

Copyright 2021 Damian Yerrick
Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.

PocketNES v7a and later require an extra 48 bytes before the
iNES header.  Format per
https://web.archive.org/web/20131102194638/http://pocketnes.org/faq.html

32 bytes: header (NUL-terminated)
 4 bytes: ROM size (iNES + PRG + CHR)
 4 bytes: emulation options
          $01: enable PPU hack; $02: disable CPU hack;
          $04: approximate PAL timing; $08: Y follow source is a
          CPU address instead of a sprite number
 4 bytes: Y follow source
 4 bytes: 0
"""
import sys
import os
import argparse
import struct

# Workaround for half-baked argparse API ############################

_terminal_width = None
def wrap_to_terminal_width(text, width=None):
    """Rewrap a hard-wrapped block of text.

1. Remove leading and trailing whitespace from text.
2. Break text at newlines.
3. Combine consecutive blank lines.  Combine nonblank lines except
   after a line shorter than 60 characters or ending with 2 spaces
   or before a blank line or line beginning with 4 spaces.
4. Wrap each line to width, or if width is None, to the terminal
   width guessed through shutil.

If width is not provided, use terminal width.

Return a list of lines.

Rationale:
Python's argparse module provides two distinct behaviors for --help
description, --help epilog, and --version text.  Either it detects
the terminal width, collapses all whitespace, and rewraps everything
to the terminal width, or it leaves everything alone.  The module
offers no public way to modify the behavior other than choosing
one of the exact preset behaviors, as the docstring for anything
HelpFormatter-related states:

    Only the name of this class is considered a public API. All the methods
    provided by the class are considered an implementation detail.

Thus I must rewrap all text before passing it to argparse.
"""
    global _terminal_width
    import textwrap as _textwrap

    if width is None:
        if _terminal_width is None:
            try:
                import shutil as _shutil
            except ImportError:
                _terminal_width = 70
            else:
                _terminal_width = _shutil.get_terminal_size().columns - 2
        width = _terminal_width

    fills, breakafter = [], True
    for line in text.strip().split("\n"):
        lrs = line.rstrip()
        if lrs == '' == fills[-1]: continue  # squeeze blanks
        breakbefore = breakafter or lrs == '' or line.startswith("    ")
        breakafter = len(lrs) < 60 or line.endswith("  ")
        if breakbefore:
            fills.append(lrs)
        else:
            fills[-1] = " ".join((fills[-1], lrs.lstrip()))
    return [l
            for x in fills
            for l in (_textwrap.wrap(x, width) if x else ("",))]

def fill_to_terminal_width(text, width=None):
    """Rewrap a hard-wrapped block of text. per wrap_to_terminal_width.

Return a str joined with newlines."""
    return "\n".join(wrap_to_terminal_width(text, width))

helpText = """
Create a PocketNES compilation for Game Boy Advance
"""
versionText = """
pnesbuild 2021-01-11

Copyright 2021 Damian Yerrick
This is free software licensed under the Apache License, Version 2.0.
This software is provided AS IS with no warranty.
"""

# End workaround ####################################################

def parse_argv(argv):
    p = argparse.ArgumentParser(description=fill_to_terminal_width(helpText),
                                formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("emulator", help="path to pocketnes.gba")
    p.add_argument("rom", nargs="+",
                   help="one or more iNES format ROM images")
    p.add_argument("-o", "--output", default="-",
                   help="path to write compilation ROM (default: - for stdout)")
    p.add_argument('--version', action='version',
                   version=fill_to_terminal_width(versionText))
    return p.parse_args(argv[1:])

def main(argv=None):
    args = parse_argv(argv or sys.argv)
    if args.output == '-' and sys.stdout.isatty():
        print("pnesbuild.py: cannot write ROM to terminal", file=sys.stderr)
        sys.exit(1)

    with open(args.emulator, "rb") as infp:
        out = bytearray(infp.read())
    for romname in args.rom:
        with open(romname, "rb") as infp:
            romdata = infp.read()
        basename = os.path.basename(romname)
        title = os.path.splitext(basename)[0][:28]
        header = bytearray(title.encode("ascii", errors='ignore')[:31])
        header.extend(bytes(32 - len(header)))
        emuoptions = 0
        followvalue = 0
        header.extend(struct.pack("<4I", len(romdata), emuoptions, followvalue, 0))
        out.extend(header)
        out.extend(romdata)

    if args.output != '-':
        with open(args.output, "wb") as outfp:
            outfp.write(out)
    else:
        sys.stdout.buffer.write(out)

if __name__=='__main__':
    main()
