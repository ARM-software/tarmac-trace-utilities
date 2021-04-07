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

#include "libtarmac/disktree.hh"
#include "libtarmac/misc.hh"

#include <errno.h>
#include <string.h>

#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

using std::string;

bool get_file_timestamp(const string &filename, uint64_t *out_timestamp)
{
    struct stat st;
    if (stat(filename.c_str(), &st) != 0)
        return false;
    *out_timestamp = static_cast<uint64_t>(st.st_mtime);
    return true;
}

bool is_interactive() { return isatty(1); }

string get_error_message() { return strerror(errno); }

struct MMapFile::PlatformData {
    int fd;
};

MMapFile::MMapFile(const string &filename, bool writable)
    : filename(filename), writable(writable)
{
    pdata = new PlatformData;

    pdata->fd =
        open(filename.c_str(), writable ? O_RDWR | O_CREAT : O_RDONLY, 0666);
    if (pdata->fd < 0)
        err(1, "%s: open", filename.c_str());
    next_offset = lseek(pdata->fd, 0, SEEK_END);
    if (next_offset == (off_t)-1)
        err(1, "%s: lseek", filename.c_str());
    curr_size = next_offset;
    mapping = nullptr;
    map();
}

MMapFile::~MMapFile()
{
    unmap();
    if (writable) {
        if (ftruncate(pdata->fd, next_offset) < 0)
            err(1, "%s: ftruncate", filename.c_str());
    }
    if (close(pdata->fd) < 0)
        err(1, "%s: close", filename.c_str());
    delete pdata;
}

void MMapFile::map()
{
    assert(!mapping);
    if (!curr_size)
        return;
    mapping = mmap(NULL, curr_size, PROT_READ | (writable ? PROT_WRITE : 0),
                   MAP_SHARED, pdata->fd, 0);
    if (mapping == MAP_FAILED)
        err(1, "%s: mmap", filename.c_str());
}

void MMapFile::unmap()
{
    if (!curr_size) {
        assert(!mapping);
        return;
    }
    assert(mapping);
    if (munmap(mapping, curr_size) < 0)
        err(1, "%s: munmap", filename.c_str());
    mapping = nullptr;
}

off_t MMapFile::alloc(size_t size)
{
    assert(writable);
    if ((size_t)(curr_size - next_offset) < size) {
        off_t new_curr_size = (next_offset + size) * 5 / 4 + 65536;
        assert(new_curr_size >= next_offset);
        if (ftruncate(pdata->fd, new_curr_size) < 0)
            err(1, "%s: ftruncate (extending)", filename.c_str());
        unmap();
        curr_size = new_curr_size;
        map();
    }
    off_t ret = next_offset;
    next_offset += size;
    return ret;
}
