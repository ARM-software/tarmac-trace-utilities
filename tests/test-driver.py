#!/usr/bin/env python3

# Copyright 2016-2021 Arm Limited. All rights reserved.
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

import sys
import os
import re
import argparse
import itertools
import subprocess
import shlex
import difflib

class TestFailure(Exception):
    pass

class ComparableStream:
    def cleanup(self):
        pass

def normalize_line_endings(s):
    if os.name == "nt":
        s = s.replace("\r\n", "\n")
    return s

class Stdout(ComparableStream):
    def text(self, out, err):
        return out.decode()
    def name(self):
        return "standard output"

class Stderr(ComparableStream):
    def text(self, out, err):
        return err.decode()
    def name(self):
        return "standard error"

class RefFile(ComparableStream):
    def __init__(self, filename):
        self.filename = filename
    def text(self, out, err):
        try:
            with open(self.filename) as fh:
                return fh.read()
        except FileNotFoundError:
            raise TestFailure(
                "Expected output file '{}' to exist, but it did not"
                .format(self.filename))
    def name(self):
        return "reference file '{}'".format(self.filename)

class OutFile(RefFile):
    def cleanup(self):
        try:
            os.remove(self.filename)
        except FileNotFoundError:
            pass
    def name(self):
        return "output file '{}'".format(self.filename)

class String(ComparableStream):
    def __init__(self, string):
        self.string = string
    def text(self, out, err):
        return self.string + "\n"
    def name(self):
        return "string '{}'".format(self.string)

all_streams = []

def parse_stream(name):
    def parse_inner(name):
        words = name.split(":", 1)
        if words[0] == "stdout":
            return Stdout()
        if words[0] == "stderr":
            return Stderr()
        if words[0] == "outfile":
            return OutFile(words[1])
        if words[0] == "reffile":
            return RefFile(words[1])
        if words[0] == "string":
            return String(words[1])
        raise ValueError("unrecognised prefix in stream name '{}'".format(name))
    stream = parse_inner(name)
    all_streams.append(stream)
    return stream

class HeterogeneousAppendAction(argparse.Action):
    def __init__(self, option_strings, dest, types=None, **kwargs):
        self.types = types
        self.nargs = len(self.types)
        super().__init__(option_strings, dest, nargs=self.nargs, **kwargs)
    def __call__(self, parser, namespace, values, option_string=None):
        values = [typefn(val) for typefn, val in zip(self.types, values)]
        getattr(namespace, self.dest).append(values)

def main():
    parser = argparse.ArgumentParser(
        description='Test driver for Tarmac Trace Utilities')
    parser.add_argument("command", nargs=argparse.REMAINDER,
                        help="Command to run")
    parser.add_argument("--exit-status", type=int, default=0,
                        help="Expected exit status of the command")
    parser.add_argument("--compare", nargs=2, metavar=["STREAM1", "STREAM2"],
                        type=parse_stream, action="append",
                        help="Expect two files to match")
    parser.add_argument("--match", metavar=["STREAM", "PATTERN"],
                        types=[parse_stream, str],
                        action=HeterogeneousAppendAction,
                        help="Expect a file to match a pattern")
    parser.add_argument("--tempfile", action="append",
                        help="Name a file expected to be generated as a side "
                        "effect of running this test")
    parser.add_argument("--cleanup-always", action="store_true",
                        help="Clean up output files after the test runs")
    parser.add_argument("--cleanup-on-pass", action="store_true",
                        help="Clean up output files if the test passes")
    parser.set_defaults(tempfile=[], compare=[], match=[])
    args = parser.parse_args()

    # Add any temp files to the list of things we'll clean up before
    # and after the test
    for name in args.tempfile:
        all_streams.append(OutFile(name))

    def cleanup():
        for stream in all_streams:
            stream.cleanup()

    # Remove previous versions of any output files, so that we don't
    # mistake a pre-existing file for the output of this test if this
    # one failed to write anything at all.
    cleanup()

    # Run the command.
    p = subprocess.Popen(args.command, stdin=subprocess.PIPE,
                         stdout=subprocess.PIPE,
                         stderr=subprocess.PIPE)
    out, err = p.communicate(b'')
    p.wait()
    status = p.returncode

    try:
        if status != args.exit_status:
            raise TestFailure("Command exit status was {} (expected {})"
                              .format(status, args.exit_status))

        for objpair in args.compare:
            textpair = [normalize_line_endings(obj.text(out, err))
                        for obj in objpair]
            if textpair[0] != textpair[1]:
                raise TestFailure(
                    "{} differs from {}:\n".format(objpair[0].name(),
                                                   objpair[1].name()) +
                    "\n".join(difflib.unified_diff(
                        textpair[0].splitlines(), textpair[1].splitlines(),
                        fromfile=objpair[0].name(), tofile=objpair[1].name(),
                        lineterm="")))
        for obj, pattern in args.match:
            text = obj.text(out, err)
            if not re.search(pattern, text):
                raise TestFailure(
                    "{} does not match regex '{}':\n".format(obj.name(),
                                                             pattern)
                    + text)
    except TestFailure as ex:
        # Remove previous versions of any output files.
        sys.exit("Test command: {}\nTEST FAILED: {}".format(
            " ".join(map(shlex.quote, args.command)), str(ex)))

    finally:
        if args.cleanup_always:
            cleanup()

    if args.cleanup_on_pass:
        cleanup()

    print("Test passed")

if __name__ == '__main__':
    main()
