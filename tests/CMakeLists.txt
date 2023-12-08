# Copyright 2016-2021,2023 Arm Limited. All rights reserved.
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

if(NOT CMAKE_VERSION VERSION_LESS "3.17")
  # From 3.17, we have a way to pass arguments to ctest. It's usually
  # helpful to see the output of these tests when they fail, because
  # they'll say what it was they didn't like about the test output.
  list(APPEND CMAKE_CTEST_ARGUMENTS "--output-on-failure")
endif()

# The tests are based on the Python program test-driver.py, which is
# given a command to run and a set of options to indicate what to
# expect from the command afterwards. (The command comes last, because
# it consumes all argv words from the first positional argument
# onwards.)
#
# When running the tests from ctest, we use --cleanup-always, which
# deletes the intermediate files created by the test. If you need to
# fix a test failure, and the output of 'ctest -VV' doesn't contain
# enough information to solve the problem without serious debugging,
# then ctest will at least print the test-driver.py command line, and
# removing the --cleanup-always option will make it leave the
# intermediate files afterwards. Or you can just paste the command
# under test off the end of the test-driver command line and run that
# on its own, of course.

set(test_driver_cmd ${python_exe} ${CMAKE_CURRENT_SOURCE_DIR}/test-driver.py
                 --cleanup-always)

# Test the expression evaluator in lib/expr.cpp (used for arithmetic
# expressions typed at prompts in the interactive browser, e.g. asking
# to open a memory view at sp+0x100 or find visits to the instruction
# at strcmp+0x40). Input is in exprtest.txt; expected output is in
# exprtest.ref.
add_test(NAME exprtest
  COMMAND ${test_driver_cmd}
      --compare stdout reffile:${CMAKE_CURRENT_SOURCE_DIR}/exprtest.ref
      ${CMAKE_BINARY_DIR}/exprtest --infile ${CMAKE_CURRENT_SOURCE_DIR}/exprtest.txt
  )

# Test the Tarmac parser itself. Input is in parsertest.txt; expected
# output is in parsertest.ref. Also a test with the --implicit-thumb
# mode flag.
add_test(NAME parsertest
  COMMAND ${test_driver_cmd}
      --compare stdout reffile:${CMAKE_CURRENT_SOURCE_DIR}/parsertest.ref
      ${CMAKE_BINARY_DIR}/parsertest ${CMAKE_CURRENT_SOURCE_DIR}/parsertest.txt
  )
add_test(NAME parsertest-implicit-thumb
  COMMAND ${test_driver_cmd}
      --compare stdout reffile:${CMAKE_CURRENT_SOURCE_DIR}/parsertest-implicit-thumb.ref
      ${CMAKE_BINARY_DIR}/parsertest --implicit-thumb ${CMAKE_CURRENT_SOURCE_DIR}/parsertest-implicit-thumb.txt
  )

# Index a small manually written trace file and use tarmac-indextool
# to report in detail what the indexer made of it. We test in both
# endiannesses. Input is in indextest.tarmac; expected output is in
# indextest-li.ref and indextest-bi.ref.
add_test(NAME indextest-li
  COMMAND ${test_driver_cmd}
      --tempfile indextest.tarmac.index
      --compare stdout reffile:${CMAKE_CURRENT_SOURCE_DIR}/indextest-li.ref
      ${CMAKE_BINARY_DIR}/tarmac-indextool --index indextest.tarmac.index --omit-index-offsets --seq-with-mem ${CMAKE_CURRENT_SOURCE_DIR}/indextest.tarmac --li
  )
add_test(NAME indextest-bi
  COMMAND ${test_driver_cmd}
      --tempfile indextest.tarmac.index
      --compare stdout reffile:${CMAKE_CURRENT_SOURCE_DIR}/indextest-bi.ref
      ${CMAKE_BINARY_DIR}/tarmac-indextool --index indextest.tarmac.index --omit-index-offsets --seq-with-mem ${CMAKE_CURRENT_SOURCE_DIR}/indextest.tarmac --bi
  )

# Tests of tarmac-callinfo, using the sample trace file
# quicksort.tarmac, made from quicksort.elf. These three tests all ask
# about the same PC value, named by address or by symbol, and with or
# without the accompanying ELF file.
add_test(NAME callinfo-addr
  COMMAND ${test_driver_cmd}
      --tempfile quicksort.tarmac.index
      --match stdout "time: 2030 \\(line:4290, pos:216439\\)"
      ${CMAKE_BINARY_DIR}/tarmac-callinfo --index quicksort.tarmac.index ${CMAKE_CURRENT_SOURCE_DIR}/quicksort.tarmac 0x80ec
  )
add_test(NAME callinfo-addr-with-image
  COMMAND ${test_driver_cmd}
      --tempfile quicksort.tarmac.index
      --match stdout "time: 2030 \\(line:4290, pos:216439\\)"
      ${CMAKE_BINARY_DIR}/tarmac-callinfo --index quicksort.tarmac.index --image ${CMAKE_CURRENT_SOURCE_DIR}/quicksort.elf ${CMAKE_CURRENT_SOURCE_DIR}/quicksort.tarmac 0x80ec
  )
add_test(NAME callinfo-symbol
  COMMAND ${test_driver_cmd}
      --tempfile quicksort.tarmac.index
      --match stdout "time: 2030 \\(line:4290, pos:216439\\)"
      ${CMAKE_BINARY_DIR}/tarmac-callinfo --index quicksort.tarmac.index --image ${CMAKE_CURRENT_SOURCE_DIR}/quicksort.elf ${CMAKE_CURRENT_SOURCE_DIR}/quicksort.tarmac sys_write0
  )

# Test that an explicit endianness option on the tarmac-callinfo
# command line causes an error if it doesn't match the endianness of a
# provided ELF image.
add_test(NAME callinfo-endianness-mismatch
  COMMAND ${test_driver_cmd}
      --tempfile quicksort.tarmac.index
      --match stderr "Endianness mismatch between image and provided endianness"
      ${CMAKE_BINARY_DIR}/tarmac-callinfo --index quicksort.tarmac.index --image ${CMAKE_CURRENT_SOURCE_DIR}/quicksort.elf ${CMAKE_CURRENT_SOURCE_DIR}/quicksort.tarmac sys_write0 --bi
  )

# Tests of tarmac-calltree on the same quicksort.tarmac trace file.
# Expected output, with and without symbol annotations from the ELF
# file, is in calltree-quicksort-*.ref.
add_test(NAME calltree-no-symbols
  COMMAND ${test_driver_cmd}
      --tempfile quicksort.tarmac.index
      --compare stdout reffile:${CMAKE_CURRENT_SOURCE_DIR}/calltree-quicksort-addr.ref
      ${CMAKE_BINARY_DIR}/tarmac-calltree --index quicksort.tarmac.index ${CMAKE_CURRENT_SOURCE_DIR}/quicksort.tarmac
  )
add_test(NAME calltree-symbols
  COMMAND ${test_driver_cmd}
      --tempfile quicksort.tarmac.index
      --compare stdout reffile:${CMAKE_CURRENT_SOURCE_DIR}/calltree-quicksort-symbols.ref
      ${CMAKE_BINARY_DIR}/tarmac-calltree --index quicksort.tarmac.index --image ${CMAKE_CURRENT_SOURCE_DIR}/quicksort.elf ${CMAKE_CURRENT_SOURCE_DIR}/quicksort.tarmac
  )

# Tests of tarmac-flamegraph on the same quicksort.tarmac trace file.
# Expected output, with and without symbol annotations from the ELF
# file, is in flamegraph-quicksort-*.ref.
#
# We also use the -o option with one of these tests and not the other,
# to check that the data goes to the right place in each case.
add_test(NAME flamegraph-no-symbols
  COMMAND ${test_driver_cmd}
      --tempfile quicksort.tarmac.index
      --compare outfile:flamegraph-quicksort-addr.txt reffile:${CMAKE_CURRENT_SOURCE_DIR}/flamegraph-quicksort-addr.ref
      ${CMAKE_BINARY_DIR}/tarmac-flamegraph --index quicksort.tarmac.index ${CMAKE_CURRENT_SOURCE_DIR}/quicksort.tarmac -o flamegraph-quicksort-addr.txt
  )
add_test(NAME flamegraph-symbols
  COMMAND ${test_driver_cmd}
      --tempfile quicksort.tarmac.index
      --compare stdout reffile:${CMAKE_CURRENT_SOURCE_DIR}/flamegraph-quicksort-symbols.ref
      ${CMAKE_BINARY_DIR}/tarmac-flamegraph --index quicksort.tarmac.index --image ${CMAKE_CURRENT_SOURCE_DIR}/quicksort.elf ${CMAKE_CURRENT_SOURCE_DIR}/quicksort.tarmac
  )

# Tests of tarmac-profile, with and without ELF symbol annotations.
# Same input trace file as the above tests; expected output is in
# profile-quicksort-*.ref.
add_test(NAME profile-no-symbols
  COMMAND ${test_driver_cmd}
      --tempfile quicksort.tarmac.index
      --compare stdout reffile:${CMAKE_CURRENT_SOURCE_DIR}/profile-quicksort-addr.ref
      ${CMAKE_BINARY_DIR}/tarmac-profile --index quicksort.tarmac.index ${CMAKE_CURRENT_SOURCE_DIR}/quicksort.tarmac
  )
add_test(NAME profile-symbols
  COMMAND ${test_driver_cmd}
      --tempfile quicksort.tarmac.index
      --compare stdout reffile:${CMAKE_CURRENT_SOURCE_DIR}/profile-quicksort-symbols.ref
      ${CMAKE_BINARY_DIR}/tarmac-profile --index quicksort.tarmac.index --image ${CMAKE_CURRENT_SOURCE_DIR}/quicksort.elf ${CMAKE_CURRENT_SOURCE_DIR}/quicksort.tarmac
  )

# Test of tarmac-vcd. Expected output is in vcd-quicksort-nodate.ref.
# Here we use --no-date to avoid putting the file's creation date
# inside the file (which would make the output different every time
# and harder to test).
add_test(NAME vcd-no-date
  COMMAND ${test_driver_cmd}
      --tempfile quicksort.tarmac.index
      --compare outfile:quicksort-nodate.vcd reffile:${CMAKE_CURRENT_SOURCE_DIR}/vcd-quicksort-nodate.ref
      ${CMAKE_BINARY_DIR}/tarmac-vcd --index quicksort.tarmac.index ${CMAKE_CURRENT_SOURCE_DIR}/quicksort.tarmac --no-date -o quicksort-nodate.vcd
  )

# Test the missing piece: if tarmac-vcd is not given the --no-date
# option, it should emit a $date line into the output.
add_test(NAME vcd-date
  COMMAND ${test_driver_cmd}
      --tempfile quicksort.tarmac.index
      --match outfile:quicksort-date.vcd "\\$date"
      ${CMAKE_BINARY_DIR}/tarmac-vcd --index quicksort.tarmac.index ${CMAKE_CURRENT_SOURCE_DIR}/quicksort.tarmac -o quicksort-date.vcd
  )

# Test class Argparse.
add_test(NAME Argparse-help
  COMMAND ${test_driver_cmd}
      --compare stdout reffile:${CMAKE_CURRENT_SOURCE_DIR}/argparse-help.ref
      ${CMAKE_BINARY_DIR}/argparsetest --help
  )
add_test(NAME Argparse-short
  COMMAND ${test_driver_cmd}
      --compare stdout reffile:${CMAKE_CURRENT_SOURCE_DIR}/argparse.ref
      ${CMAKE_BINARY_DIR}/argparsetest -s -v 2 arg1 multi1 multi2
  )
add_test(NAME Argparse-long
  COMMAND ${test_driver_cmd}
      --compare stdout reffile:${CMAKE_CURRENT_SOURCE_DIR}/argparse.ref
      ${CMAKE_BINARY_DIR}/argparsetest --switch --value 2 arg1 multi1 multi2
  )
add_test(NAME Argparse-via
  COMMAND ${test_driver_cmd}
      --compare stdout reffile:${CMAKE_CURRENT_SOURCE_DIR}/argparse.ref
      ${CMAKE_BINARY_DIR}/argparsetest --via-file ${CMAKE_CURRENT_SOURCE_DIR}/argparse-via.txt
  )
add_test(NAME Argparse-via-override
  COMMAND ${test_driver_cmd}
      --compare stdout reffile:${CMAKE_CURRENT_SOURCE_DIR}/argparse-via-override.ref
      ${CMAKE_BINARY_DIR}/argparsetest --via-file ${CMAKE_CURRENT_SOURCE_DIR}/argparse-via.txt --value 10
  )

# Test btod.
add_test(NAME btod
  COMMAND ${test_driver_cmd}
      --compare stdout reffile:${CMAKE_CURRENT_SOURCE_DIR}/btod.ref
      ${CMAKE_BINARY_DIR}/btodtest
  )

# Test the reference counting in the AVL tree system.
add_test(NAME avl
  COMMAND ${test_driver_cmd}
      ${CMAKE_BINARY_DIR}/avltest
  )
