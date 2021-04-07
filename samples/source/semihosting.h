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

#ifndef SEMIHOSTING_H
#define SEMIHOSTING_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct heapinfo {
    void *heap_base;
    void *heap_limit;
    void *stack_base;
    void *stack_limit;
};

// Possible return value from sys_open
#define INVALID_FILEHANDLE ((uintptr_t)-1)

// Mode flags that can be used as input to sys_open
#define OPEN_MODE_R 0          // "r": read only
#define OPEN_MODE_RW 2         // "r+": read/write of existing file
#define OPEN_MODE_W_CREATE 4   // "w": write only, create or truncate
#define OPEN_MODE_RW_CREATE 6  // "w+": read/write, create or truncate
#define OPEN_MODE_W_APPEND 8   // "a": write only, append at end
#define OPEN_MODE_RW_APPEND 10 // "w+": read/write, append at end

#define OPEN_MODE_BINARY 1 // combine this flag with one of above

#if defined __cplusplus
extern "C" {
#endif

uintptr_t sys_clock(void);
uintptr_t sys_close(uintptr_t filehandle);
bool sys_elapsed(uint64_t *out);
int sys_errno(void);
void sys_exit(int exitcode);
void sys_exit_extended(int exitcode);
intptr_t sys_flen(uintptr_t filehandle);
char *sys_get_cmdline(char *buffer, size_t buflen);
void sys_heapinfo(struct heapinfo *out);
bool sys_iserror(uintptr_t return_code);
int sys_istty(uintptr_t filehandle);
uintptr_t sys_open(const char *filename, unsigned mode);
uintptr_t sys_read(uintptr_t filehandle, void *buf, size_t buflen);
char sys_readc(void);
uintptr_t sys_remove(const char *filename);
uintptr_t sys_rename(const char *from, const char *to);
bool sys_seek(uintptr_t filehandle, uintptr_t pos);
uintptr_t sys_system(const char *command);
uintptr_t sys_tickfreq(void);
uintptr_t sys_time(void);
char *sys_tmpnam(unsigned char id, char *buffer, size_t buflen);
uintptr_t sys_write(uintptr_t filehandle, void *buf, size_t buflen);
void sys_writec(char c);
void sys_write0(const char *string);

#if defined __cplusplus
} // extern "C"
#endif

#endif // SEMIHOSTING_H
