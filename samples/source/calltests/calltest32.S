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
        mov r0, #0x100000
        mov sp, r0

        // A call via ordinary BL.
        mov r0, #19
        bl leaf_bx_lr

        // A call by BLX, putting the function address in a register.
        ldr r1, =nonleaf_pop_pc
        blx r1

        // A call by BX, having set up LR manually beforehand.
        ldr r1, =nonleaf_pop_lr
        mov lr, pc
        bx r1

        // A call by BL to a helper that uses BX. (These used to be needed
        // in Thumb on the Arm v4T architecture: you couldn't 'mov lr,pc'
        // _and_ you couldn't BX.)
        ldr r1, =leaf_mov_pc_lr
        bl call_via_r1

        // An unusual use of lr to branch back to the start of a loop.
        mov r0, #0
loopstart: cmp r0, #0x10000
        bhs loopend
        ldr lr, =loopstart
        b leaf_bx_lr
loopend:

        // More calls to test functions. I've run out of unusual types
        // of call, so these ones can be done the standard way.
        bl internal_bl
        bl internal_bx_lr
        bl branch_after_call_wrapper
        // After returning from this function, immediately take a
        // conditional branch, which should not be identified as even a
        // _candidate_ for being a call.
        cmp r0, #122
        bne branch_target_after_return
        mov r0, #121
branch_target_after_return:

        // We're done. Exit.
        mov r0, #0x18
        ldr r1, =0x20026
        svc #0x123456
        .size _start, . - _start

        // A leaf function which doesn't need to stack any registers,
        // and returns via the standard 'bx lr' idiom.
leaf_bx_lr:
        mul r1, r0, r0
        add r0, r0, r1
        add r0, r0, #41
        bx lr

        // A leaf function which returns via the old-style MOV pc,lr,
        // used before the interworking BX was available.
leaf_mov_pc_lr:
        mul r1, r0, r0
        add r0, r0, r1
        add r0, r0, #41
        mov pc, lr

        // A non-leaf function which pushes lr so that it can make
        // subroutine calls, and returns via a pop directly to pc.
nonleaf_pop_pc:
        push {r4, lr}
        bl leaf_bx_lr
        bl leaf_mov_pc_lr
        pop {r4, pc}

        // A non-leaf function which pops the saved lr back into lr,
        // and then returns by branching to it.
nonleaf_pop_lr:
        push {r4, lr}
        bl leaf_mov_pc_lr
        bl leaf_bx_lr
        pop {r4, lr}
        bx lr

        // Helper function for the indirect call above.
call_via_r1: bx r1

        // A function which uses BL internally as an ordinary branch
        // instruction, treating the data it writes to lr as unwanted
        // junk. So the BL shouldn't be identified as a call!
internal_bl:
        push {r4, lr}
        add r0, r0, #1
        add r0, r0, #1
        add r0, r0, #1
        bl internal_bl_label
        udf #0
internal_bl_label:
        add r0, r0, #1
        add r0, r0, #1
        add r0, r0, #1
        pop {r4, pc}

        // A function which uses 'BX lr' as a computed table branch.
        // Before TBB/TBH, table branches in this style were common,
        // and they could use any register that happened to be free -
        // including lr. So this 'BX lr' shouldn't be identified as a
        // return.
internal_bx_lr:
        push {r4, lr}
        // Make an input to the table branch in the range {0,1,2,3}
        and r1, r0, #3
        // Branch cases start at 8-byte offsets from just after the BX
        add lr, pc, r1, lsl #3
        bx lr
        // If the input was 0
        mov r0, #15
        b done
        // If the input was 1
        mov r0, #30
        b done
        // If the input was 2
        mov r0, #40
        b done
        // If the input was 03
        mov r0, #100
        b done
done:   mul r0, r0, r0
        pop {r4, pc}

        // Wrapper function that calls branch_after_call, while being
        // very near it, so that the internal branch might trip the
        // call detector.
branch_after_call_wrapper:
        push {r4, lr}
        bl branch_after_call
        pop {r4, lr}
        bx lr
        // A function that does a conditional branch immediately after
        // being called.
branch_after_call:
        push {r4, lr}
        cmp r0, #123
        bne branch_after_call_internal_label
        mov r0, #122
branch_after_call_internal_label:
        add r0, r0, #10
        pop {r4, lr}
        bx lr
