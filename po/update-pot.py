#!/usr/bin/env python3
#
# Copyright 2024 Arm Limited. All rights reserved.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#
# This file is part of Tarmac Trace Utilities

'''Scan all the source files in this project for translatable
messages, and update tarmac-trace-utilities.pot.
'''

import argparse
import os
import shlex
import subprocess

def main():
    parser = argparse.ArgumentParser(
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("-v", "--verbose", action="store_true",
                        help="Print the xgettext command.")
    args = parser.parse_args()

    po_dir = os.path.dirname(os.path.abspath(__file__))
    src_root_dir = os.path.dirname(po_dir)
    files = []
    for pattern in ["*.cpp", "*.hh"]:
        filenames = subprocess.check_output(
            ["git", "ls-files", "-z", pattern], cwd=src_root_dir)
        for filename in map(os.fsdecode, filenames.rstrip(b'\0').split(b'\0')):
            if filename.startswith("samples/"):
                # The 'samples' directory is logically a separate code
                # base, and even if it were to contain things that looked
                # like gettext string lookups, they wouldn't relate to
                # this message database.
                continue
            files.append(filename)

    command = [
        "xgettext",

        # Indicate that we're using the _(...) convention for wrapping
        # strings, instead of calling the function under its verbose
        # official name of gettext()
        "-k_",

        # Character encoding of the source files
        "--from-code=UTF-8",

        # Metadata
        "--package-name=Tarmac Trace Utilities",
        "--copyright-holder=Arm Limited",

        # Output file
        "-o", os.path.join(po_dir, "tarmac-trace-utilities.pot"),
    ] + files

    if args.verbose:
        print(*map(shlex.quote, command))

    subprocess.check_call(command, cwd=src_root_dir)

if __name__ == '__main__':
    main()
