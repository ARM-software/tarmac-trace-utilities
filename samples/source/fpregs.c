/*
 * Copyright 2016-2021 Arm Limited. All rights reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * This file is part of Tarmac Trace Utilities
 */

/* This test program demonstrates some simple vector operations.
 * There's no need to explore the far reaches of the vector ISA in
 * question; the aim is to be able to check that the register contents
 * are parsed out of the Tarmac trace in the right endianness. So we
 * can use simple enough vector operations to work in both NEON and
 * MVE, and save on having to have multiple test programs. */
#if __ARM_ARCH_PROFILE == 'M'
#include <arm_mve.h>
#else
#include <arm_neon.h>
#endif

#include "semihosting.h"

__attribute__((noinline)) double test_fn_d(double a, double b)
{
    return a * a + a * b + b * b;
}

__attribute__((noinline)) float test_fn_f(float a, float b)
{
    return a * a + a * b + b * b;
}

__attribute__((noinline)) void
add_vector_b(unsigned char *s, const unsigned char *a, const unsigned char *b)
{
    uint8x16_t av = vld1q_u8(a);
    uint8x16_t bv = vld1q_u8(b);
    uint8x16_t sv = vaddq_u8(av, bv);
    vst1q_u8(s, sv);
}

__attribute__((noinline)) void add_vector_h(unsigned short *s,
                                            const unsigned short *a,
                                            const unsigned short *b)
{
    uint16x8_t av = vld1q_u16(a);
    uint16x8_t bv = vld1q_u16(b);
    uint16x8_t sv = vaddq_u16(av, bv);
    vst1q_u16(s, sv);
}

__attribute__((noinline)) void
add_vector_w(unsigned int *s, const unsigned int *a, const unsigned int *b)
{
    uint32x4_t av = vld1q_u32(a);
    uint32x4_t bv = vld1q_u32(b);
    uint32x4_t sv = vaddq_u32(av, bv);
    vst1q_u32(s, sv);
}

const double e = 2.71828182845904523536028747135266250;
const double pi = 3.14159265358979323846264338327950288;
const unsigned char b_lo[16] = {
    0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
    0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F,
};
const unsigned char b_hi[16] = {
    0x00, 0x10, 0x20, 0x30, 0x40, 0x50, 0x60, 0x70,
    0x80, 0x90, 0xA0, 0xB0, 0xC0, 0xD0, 0xE0, 0xF0,
};
const unsigned short h_lo[8] = {
    0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
};
const unsigned short h_hi[8] = {
    0x00, 0x10, 0x20, 0x30, 0x40, 0x50, 0x60, 0x70,
};
const unsigned int w_lo[4] = {
    0x00,
    0x01,
    0x02,
    0x03,
};
const unsigned int w_hi[4] = {
    0x00,
    0x10,
    0x20,
    0x30,
};

void c_entry(void)
{
    volatile double d = test_fn_d(e, pi);
    volatile float f = test_fn_f(e, pi);
    unsigned char b_sum[16];
    add_vector_b(b_sum, b_lo, b_hi);
    unsigned short h_sum[8];
    add_vector_h(h_sum, h_lo, h_hi);
    unsigned int w_sum[4];
    add_vector_w(w_sum, w_lo, w_hi);

    sys_exit(0);
}
