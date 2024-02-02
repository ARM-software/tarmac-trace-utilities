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

'''
Convert a set of gettext .mo files to a binary Windows message resource.
'''

import argparse
import email
import io
import os
import struct
import sys

# Mapping table that translates Linux-style language locale
# identifiers to Windows's numeric LCID system.
#
# This table does not attempt to be complete. Instead, we'll need to
# extend it to include the LCID for any translation added to this code
# base.
#
# As of 2024-01-26, the specification for the LCID system lives at
# https://learn.microsoft.com/en-us/openspecs/windows_protocols/ms-lcid/

language_tag_to_lcid = {
    'en_GB': 0x0809,
    'en_US': 0x0409,
    'fr': 0x040C,
}

def decode_mo(mo):
    """Given the binary contents of a .mo file, construct a dict mapping
    original strings to translated strings.

    The special header string is not included in the returned dict.

    Return value is a tuple of (language name, dictionary).
    """

    # Determine the endianness of the MO file by decoding the first
    # four bytes in both endiannesses to see which one gives the right
    # magic number.
    for endian in "<", ">":
        (magic,) = struct.unpack_from(endian + "L", mo, 0)
        if magic == 0x950412de:
            break
    else:
        sys.exit("Input file did not have correct MO magic number")

    # Decode the rest of the header, or at least the parts of it we
    # care about.
    #
    # (There are further fields describing a hash table, but the hash
    # function is not documented; the hash table itself is optional;
    # and we don't need it anyway, because we're iterating over all
    # the strings rather than trying to extract one quickly.)
    (
        file_format_revision, number_of_strings, orig_offset, trans_offset
    ) = struct.unpack_from(endian + "LLLL", mo, 4)

    # We only support file format revision 0, because that's what's
    # documented at
    # https://www.gnu.org/software/gettext/manual/html_node/MO-Files.html
    # as of the time of writing this (2023-11-24).
    if file_format_revision != 0:
        sys.exit(f"Input file has file format revision {file_format_revision}, "
                 f"but we only understand revision 0")

    # Reusable function to retrieve a string from either the table of
    # original strings or the table of translated ones.
    def get_binary_string(table_entry_offset):
        (
            length, string_offset
        ) = struct.unpack_from(endian + "LL", mo, table_entry_offset)
        return mo[string_offset:string_offset + length]

    # Look up all the strings and make a dict of them.
    binary_pairs = {
        get_binary_string(orig_offset + 8*i) :
        get_binary_string(trans_offset + 8*i)
        for i in range(number_of_strings)
    }

    # Parse the header string (indexed as the translation of the empty
    # string) to find out what character set everything is encoded in.
    #
    # This is most conveniently done using the 'email' Python module,
    # since the header string is RFC822-shaped and has the same
    # MIME-Version and Content-Type headers in particular.
    header_msg = email.message_from_bytes(binary_pairs[b''])
    charset = header_msg.get_content_charset()

    # Another field of the header says what language this translation
    # is for.
    language = header_msg.get('Language')

    # Decode all the string pairs other than the header.
    decoded_pairs = {
        orig.decode(charset): trans.decode(charset)
        for orig, trans in binary_pairs.items()
        if len(orig) != 0
    }

    return language, decoded_pairs

def contiguous_ranges(numbers):
    """Given an iterable of numbers in sorted order, yield a sequence of
    range objects containing contiguous subranges of the input
    numbers."""

    it = iter(numbers)
    current_range = None
    for n in it:
        if current_range is None:
            current_range = range(n, n+1)
        elif n == current_range.stop:
            current_range = range(current_range.start, n+1)
        else:
            yield current_range
            current_range = range(n, n+1)
    if current_range is not None:
        yield current_range

def encode_mrd(messages):
    """Given a dict mapping numeric message indices to message strings,
    construct a binary table starting with a MESSAGE_RESOURCE_DATA."""

    # Encode the strings in UTF-16.
    encoded = {
        index: (message + '\0').encode("UTF-16LE")
        for index, message in messages.items()
    }

    # Group the message numbers into a list of contiguous ranges.
    ranges = list(contiguous_ranges(sorted(messages)))

    # The MESSAGE_RESOURCE_DATA header is just a count of blocks.
    mrd = struct.pack("<L", len(ranges))

    # The blocks are 12 bytes each, so now we know where the actual
    # message entries will start.
    mre_offset = len(mrd) + 12 * len(ranges)

    mrbs = []
    mres = []
    for r in ranges:
        # MESSAGE_RESOURCE_BLOCK header: start and end message id
        # (inclusive), followed by offset from the
        # MESSAGE_RESOURCE_DATA header to the first message entry.
        mrbs.append(struct.pack("<LLL", r.start, r.stop-1, mre_offset))

        e = io.BytesIO()
        for index in r:
            # MESSAGE_RESOURCE_ENTRY header: offset from here to the
            # next MESSAGE_RESOURCE_ENTRY, plus a flags word
            # containing the value 1. (Apparently the only flag bit is
            # the one for 'string is encoded in Unicode'.)
            enc = encoded[index]
            enc += b'\0' * (3 & -len(enc)) # pad to 32-bit alignment
            e.write(struct.pack("<HH", 4 + len(enc), 1))
            e.write(enc)

        mre = e.getvalue()
        mres.append(mre)
        mre_offset += len(mre)

    return b''.join([mrd] + mrbs + mres)

def encode_lookup(origstrings):
    # This is as simple as possible: the strings are stored in sorted
    # order and we give the offset to the start of each one. So you
    # need to do a binary search via strcmp to find the right string.
    #
    # It might be nice to use a trie or a hash table or something
    # where the lookup time was linear. But this is really simple.
    origstrings = list(sorted(s.encode("UTF-8") for s in origstrings))

    header_size = 4 * len(origstrings) + 4
    header = io.BytesIO()
    header.write(struct.pack("<L", len(origstrings)))
    strings = io.BytesIO()
    for string in origstrings:
        header.write(struct.pack("<L", header_size + strings.tell()))
        strings.write(string + b'\0')
    assert header.tell() == header_size
    return header.getvalue() + strings.getvalue()

def filename_for_lcid(lcid):
    return f"MSG{lcid:05x}.bin"

def write_rc_file(lcids, fh):
    """Given a list of LCIDs (Windows numeric codes for languages), write
    a resource file snippet."""

    # Include the lookup table for the original messages. We give this
    # a custom resource id and type.
    print(f'2001 2001 "msg_lookup.bin"', file=fh)

    # Now write each mapping table, distinguished by language.
    for lcid in lcids:
        lang = lcid & 0x3FF
        sublang = lcid >> 10
        filename = filename_for_lcid(lcid)
        print(f"LANGUAGE {lang:#x},{sublang:#x}", file=fh)
        print(f'1 11 "{filename}"', file=fh)

def main():
    parser = argparse.ArgumentParser(
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("infile", nargs="+", help="Input .po files.")
    parser.add_argument("-o", "--output", required=True,
                        help="Output .rc file. Message tables will be left in "
                        "files named MSGxxxxx.bin alongside it.")
    args = parser.parse_args()

    # Read the input .mo files.
    mappings = {}
    for infilename in args.infile:
        with open(infilename, "rb") as fh:
            lang, mapping = decode_mo(fh.read())

        try:
            lcid = language_tag_to_lcid[lang]
        except KeyError:
            # We can't use this translation, so ignore it
            continue

        mappings[lcid] = mapping

    # Make a list of all the original strings.
    origstrings = set()
    for mapping in mappings.values():
        origstrings |= set(mapping)

    # LCID that we tag the original messages with
    orig_lcid = language_tag_to_lcid["en_GB"]

    # Make a trivial mapping for the original LCID. (All the strings
    # in it will be filled in by the fallback lookup below.)
    mappings[orig_lcid] = {}

    # Sort the original strings into Unicode order, to assign them
    # sensible ids.
    origstrings = list(sorted(origstrings))

    # Write the rc file
    with open(args.output, "w") as fh:
        write_rc_file(list(sorted(mappings)), fh)

    # Write the table files
    for lcid, mapping in mappings.items():
        with open(filename_for_lcid(lcid), "wb") as fh:
            fh.write(encode_mrd({index: mapping.get(orig, orig)
                                 for index, orig in enumerate(origstrings)}))

    # Write a lookup table to map messages to indices
    with open("msg_lookup.bin", "wb") as fh:
        fh.write(encode_lookup(origstrings))

if __name__ == '__main__':
    main()
