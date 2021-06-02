..
  Copyright 2016-2021 Arm Limited. All rights reserved.

  Licensed under the Apache License, Version 2.0 (the "License");
  you may not use this file except in compliance with the License.
  You may obtain a copy of the License at

      http://www.apache.org/licenses/LICENSE-2.0

  Unless required by applicable law or agreed to in writing, software
  distributed under the License is distributed on an "AS IS" BASIS,
  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
  See the License for the specific language governing permissions and
  limitations under the License.

  This file is part of Tarmac Trace Utilities


|CIUbuntu2004gcc| |CIUbuntu2004clang| |CIUbuntu1804gcc| |CIUbuntu1804clang|

.. |CIUbuntu2004gcc| image:: https://github.com/ARM-software/tarmac-trace-utilities/actions/workflows/ubuntu-2004-gcc.yml/badge.svg
   :alt: Last build status on Ubuntu 20.04 with gcc
   :target: https://github.com/ARM-software/tarmac-trace-utilities/actions/workflows/ubuntu-2004-gcc.yml

.. |CIUbuntu2004clang| image:: https://github.com/ARM-software/tarmac-trace-utilities/actions/workflows/ubuntu-2004-clang.yml/badge.svg
   :alt: Last build status on Ubuntu 20.04 with clang
   :target: https://github.com/ARM-software/tarmac-trace-utilities/actions/workflows/ubuntu-2004-clang.yml

.. |CIUbuntu1804gcc| image:: https://github.com/ARM-software/tarmac-trace-utilities/actions/workflows/ubuntu-1804-gcc.yml/badge.svg
   :alt: Last build status on Ubuntu 18.04 with gcc
   :target: https://github.com/ARM-software/tarmac-trace-utilities/actions/workflows/ubuntu-1804-gcc.yml

.. |CIUbuntu1804clang| image:: https://github.com/ARM-software/tarmac-trace-utilities/actions/workflows/ubuntu-1804-clang.yml/badge.svg
   :alt: Last build status on Ubuntu 18.04 with clang
   :target: https://github.com/ARM-software/tarmac-trace-utilities/actions/workflows/ubuntu-1804-clang.yml

Tarmac Trace Utilities
~~~~~~~~~~~~~~~~~~~~~~

Arm Tarmac Trace Utilities is a suite of tools to read, analyze and
browse traces of running programs in the 'Tarmac' textual format.

Utilities in this collection can:

* Generate reports based on the data in the index and trace file, such
  as per-function profiling, or showing the tree of function calls and
  returns observed during the trace.

* Repackage the trace data in a different format, such as an IEEE 1364
  Value Change Dump file (``.vcd``).

* Interactively browse the trace file in a way that understands its
  semantics, tracking the complete state of registers and memory as
  you page back and forth in the trace file, and allowing rapid
  navigation to places of interest, such as the previous location
  where a given data item was updated, or the return corresponding to
  a function call.

Requirements
------------

To build the analysis tools from source, you will need a C++ compiler
compatible with C++14, and `CMake <https://cmake.org/>`_.

To build the interactive browsing tools, you will also need a
``curses`` library (for the terminal-based version) or GTK+ 3 (for the
GUI version) or both.

These are all available on current Linux distributions, for example
Ubuntu 16.04 (``xenial``) and later, Debian 9 (``stretch``), CentOS 8,
or Fedora 31.

On Windows, the analysis tools have been built successfully with
Visual Studio 2017, and the terminal-based interactive browser can be
built using PDCurses.

Building
--------

To build the tools, run the following commands:

::

  cmake .
  cmake --build .

(There are many other ways to invoke CMake, but this is the simplest.)

Usage
-----

For detailed information on all the tools and how to use them, see the
`main documentation <doc/index.rst>`_.

License
-------

This package is distributed under the `Apache v2.0 License
<http://www.apache.org/licenses/LICENSE-2.0>`_.

Feedback, Contributions and Support
-----------------------------------

Please use the GitHub issue tracker associated with this repository
for feedback.

We welcome code contributions via GitHub pull requests. Please try to
stick to the style in the rest of the code for your contributions.
