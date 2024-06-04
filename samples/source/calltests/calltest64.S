// Copyright 2024 Arm Limited. All rights reserved.
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
        // Stack setup
        mov x0, #0x100000
        mov sp, x0

        // A call via ordinary BL.
        mov x0, #19
        bl leaf_ret

        // A call by BLR, putting the function address in a register.
        ldr x1, =nonleaf_pop_lr
        blr x1

        // A call by BL to a helper that uses BR. (This is the AArch64
        // translation of an old AArch32 idiom. AArch64 has never
        // needed this, but it's not _impossible_, so we might as well
        // test it.)
        ldr x1, =leaf_ret
        bl call_via_x1

        // An unusual use of lr to branch back to the start of a loop.
        mov x0, #0
loopstart: mov x1, #0x100000000
        cmp x0, x1
        bhs loopend
        ldr lr, =loopstart
        b leaf_ret
loopend:

        // More calls to test functions. I've run out of unusual types
        // of call, so these ones can be done the standard way.
        bl branch_after_call_wrapper
        // After returning from this function, immediately take a
        // conditional branch, which should not be identified as even a
        // _candidate_ for being a call.
        cmp x0, #122
        bne branch_target_after_return
        mov x0, #121
branch_target_after_return:

        // We're done. Exit.
        mov x0, #0x18
        ldr x1, =0x20026
        hlt #0xf000
        .size _start, . - _start

        // A leaf function which doesn't need to stack any registers.
leaf_ret:
        mul x1, x0, x0
        add x0, x0, x1
        add x0, x0, #41
        ret

        // A non-leaf function which pushes lr so that it can make
        // subroutine calls, and pops it again before returning.
nonleaf_pop_lr:
        stp x19, lr, [sp, #-16]!
        bl leaf_ret
        bl leaf_ret
        ldp x19, lr, [sp], #16
        ret

        // Helper function for the indirect call above.
call_via_x1: br x1

        // Wrapper function that calls branch_after_call, while being
        // very near it, so that the internal branch might trip the
        // call detector.
branch_after_call_wrapper:
        stp x19, lr, [sp, #-16]!
        bl branch_after_call
        ldp x19, lr, [sp], #16
        ret
        // A function that does a conditional branch immediately after
        // being called.
branch_after_call:
        stp x19, lr, [sp, #-16]!
        cmp x0, #123
        bne branch_after_call_internal_label
        mov x0, #122
branch_after_call_internal_label:
        add x0, x0, #10
        ldp x19, lr, [sp], #16
        ret
