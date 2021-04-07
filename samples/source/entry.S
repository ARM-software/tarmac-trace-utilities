// Copyright 2016-2021 Arm Limited. All rights reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
// This file is part of Tarmac Trace Utilities

        .text
        .globl _start
        .type _start, %function

_start:

#ifdef __aarch64__
        mov x0, #0x100000
        mov sp, x0
#else
        ldr r0, =0x100000
        mov sp, r0
#endif

        bl c_entry

        .size _start, . - _start
