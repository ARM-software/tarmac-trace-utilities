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

#include "semihosting.h"

#if defined __aarch64__
#define SEMIHOSTING_INSTRUCTION "HLT #0xF000"
#elif __ARM_ARCH_PROFILE == 'M'
#define SEMIHOSTING_INSTRUCTION "BKPT #0xAB"
#elif defined __thumb__ && defined USE_SEMIHOSTING_HLT
#define SEMIHOSTING_INSTRUCTION "HLT #0x3C"
#elif defined __thumb__
#define SEMIHOSTING_INSTRUCTION "SVC #0xAB"
#elif defined USE_SEMIHOSTING_HLT
#define SEMIHOSTING_INSTRUCTION "HLT #0xF000"
#else
#define SEMIHOSTING_INSTRUCTION "SVC #0x123456"
#endif

#if defined __aarch64__
#define SEMIHOSTING_OPCODE_REG "x0"
#define SEMIHOSTING_PARAM_REG "x1"
#else
#define SEMIHOSTING_OPCODE_REG "r0"
#define SEMIHOSTING_PARAM_REG "r1"
#endif

static inline uintptr_t semihosting_call(uintptr_t opcode, uintptr_t param)
{
    register uintptr_t opcode_reg __asm__(SEMIHOSTING_OPCODE_REG) = opcode;
    register uintptr_t param_reg __asm__(SEMIHOSTING_PARAM_REG) = param;
    __asm__ volatile(SEMIHOSTING_INSTRUCTION
                     : "+r"(opcode_reg)
                     : "r"(param_reg));
    return opcode_reg;
}

static inline uintptr_t semihosting_call_noparam(uintptr_t opcode)
{
    register uintptr_t opcode_reg __asm__(SEMIHOSTING_OPCODE_REG) = opcode;
    __asm__ volatile(SEMIHOSTING_INSTRUCTION : "+r"(opcode_reg));
    return opcode_reg;
}

uintptr_t sys_clock(void) { return semihosting_call_noparam(0x10); }

uintptr_t sys_close(uintptr_t filehandle)
{
    uintptr_t block[1] = {filehandle};
    return semihosting_call(0x02, (uintptr_t)block);
}

bool sys_elapsed(uint64_t *out)
{
    uintptr_t block[8 / sizeof(uintptr_t)];
    if (semihosting_call(0x30, (uintptr_t)block) != 0)
        return false;
#if defined __aarch64__
    *out = block[0];
#else
    *out = ((uint64_t)block[1] << 32) | block[0];
#endif
    return true;
}

int sys_errno(void) { return semihosting_call_noparam(0x13); }

#define ADP_Stopped_ApplicationExit 0x20026

void sys_exit(int exitcode)
{
#if defined __aarch64__
    uintptr_t block[2] = {ADP_Stopped_ApplicationExit, exitcode};
    semihosting_call(0x18, (uintptr_t)block);
#else
    /* 32-bit SYS_EXIT does not support returning an exit status */
    semihosting_call(0x18, ADP_Stopped_ApplicationExit);
#endif
}

void sys_exit_extended(int exitcode)
{
    uintptr_t block[2] = {ADP_Stopped_ApplicationExit, exitcode};
    semihosting_call(0x20, (uintptr_t)block);
}

intptr_t sys_flen(uintptr_t filehandle)
{
    uintptr_t block[1] = {filehandle};
    return semihosting_call(0x0C, (uintptr_t)block);
}

char *sys_get_cmdline(char *buffer, size_t buflen)
{
    uintptr_t block[2] = {(uintptr_t)buffer, buflen};
    if (semihosting_call(0x15, (uintptr_t)block) == 0) {
        /* block[1] is also the length of the returned string, but
         * since it's NUL-terminated, that isn't 100% necessary */
        return (char *)block[0];
    } else {
        return NULL;
    }
}

void sys_heapinfo(struct heapinfo *out)
{
    uintptr_t block[1] = {(uintptr_t)out};
    semihosting_call(0x16, (uintptr_t)block);
}

bool sys_iserror(uintptr_t return_code)
{
    uintptr_t block[1] = {return_code};
    return semihosting_call(0x08, (uintptr_t)block) != 0;
}

int sys_istty(uintptr_t filehandle)
{
    uintptr_t block[1] = {filehandle};
    uintptr_t status = semihosting_call(0x09, (uintptr_t)block);
    if (status >= 2)
        return -1; /* indicate error */
    return status; /* 0 or 1: successful answers */
}

static inline size_t local_strlen(const char *p)
{
    /* Work totally freestanding, in the absence of strlen */
    size_t length = 0;
    while (*p++)
        length++;
    return length;
}

uintptr_t sys_open(const char *filename, unsigned mode)
{
    uintptr_t block[3] = {(uintptr_t)filename, mode, local_strlen(filename)};
    return semihosting_call(0x01, (uintptr_t)block);
}

uintptr_t sys_read(uintptr_t filehandle, void *buf, size_t buflen)
{
    uintptr_t block[3] = {filehandle, (uintptr_t)buf, buflen};
    return semihosting_call(0x06, (uintptr_t)block);
}

char sys_readc(void) { return semihosting_call_noparam(0x07); }

uintptr_t sys_remove(const char *filename)
{
    uintptr_t block[2] = {(uintptr_t)filename, local_strlen(filename)};
    return semihosting_call(0x0E, (uintptr_t)block);
}

uintptr_t sys_rename(const char *from, const char *to)
{
    uintptr_t block[4] = {
        (uintptr_t)from,
        local_strlen(from),
        (uintptr_t)to,
        local_strlen(to),
    };
    return semihosting_call(0x0F, (uintptr_t)block);
}

bool sys_seek(uintptr_t filehandle, uintptr_t pos)
{
    uintptr_t block[2] = {filehandle, pos};
    return semihosting_call(0x0A, (uintptr_t)block) == 0;
}

uintptr_t sys_system(const char *command)
{
    uintptr_t block[2] = {(uintptr_t)command, local_strlen(command)};
    return semihosting_call(0x12, (uintptr_t)block);
}

uintptr_t sys_tickfreq(void) { return semihosting_call(0x31, 0); }

uintptr_t sys_time(void) { return semihosting_call_noparam(0x11); }

char *sys_tmpnam(unsigned char id, char *buffer, size_t buflen)
{
    uintptr_t block[3] = {(uintptr_t)buffer, id, buflen};
    if (semihosting_call(0x0D, (uintptr_t)block) == 0) {
        return buffer;
    } else {
        return NULL;
    }
}

uintptr_t sys_write(uintptr_t filehandle, void *buf, size_t buflen)
{
    uintptr_t block[3] = {filehandle, (uintptr_t)buf, buflen};
    return semihosting_call(0x05, (uintptr_t)block);
}

void sys_writec(char c) { semihosting_call(0x03, (uintptr_t)&c); }

void sys_write0(const char *string)
{
    semihosting_call(0x04, (uintptr_t)string);
}
