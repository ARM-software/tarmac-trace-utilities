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
#include "libtarmac/reporter.hh"

#include <errno.h>
#include <string.h>

#include <sstream>

#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

using std::ostringstream;
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
        reporter->err(1, "%s: open", filename.c_str());
    next_offset = lseek(pdata->fd, 0, SEEK_END);
    if (next_offset == (OFF_T)-1)
        reporter->err(1, "%s: lseek", filename.c_str());
    curr_size = next_offset;
    mapping = nullptr;
    map();
}

MMapFile::~MMapFile()
{
    unmap();
    if (writable) {
        if (ftruncate(pdata->fd, next_offset) < 0)
            reporter->err(1, "%s: ftruncate", filename.c_str());
    }
    if (close(pdata->fd) < 0)
        reporter->err(1, "%s: close", filename.c_str());
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
        reporter->err(1, "%s: mmap", filename.c_str());
}

void MMapFile::unmap()
{
    if (!curr_size) {
        assert(!mapping);
        return;
    }
    assert(mapping);
    if (munmap(mapping, curr_size) < 0)
        reporter->err(1, "%s: munmap", filename.c_str());
    mapping = nullptr;
}

void MMapFile::resize(size_t newsize)
{
    if (ftruncate(pdata->fd, newsize) < 0)
        reporter->err(1, "%s: ftruncate (extending)", filename.c_str());
    unmap();
    curr_size = newsize;
    map();
}

static bool try_make_conf_path(const char *env_var, const char *suffix,
                               const string &filename, string &out)
{
    const char *env_val = getenv(env_var);
    if (!env_val) {
        // Starting environment variable is not defined
        return false;
    }
    if (!env_val[0]) {
        // It is defined, but to the empty string, which is often an
        // ad-hoc way to 'undefine' something in practice
        return false;
    }

    ostringstream oss;
    oss << env_val;
    if (suffix)
        oss << "/" << suffix;
    oss << "/" << filename;
    out = oss.str();
    return true;
}

bool get_conf_path(const string &filename, string &out)
{
    if (try_make_conf_path("TARMAC_TRACE_UTILITIES_CONFIG", nullptr,
                           filename, out))
        return true;

    if (try_make_conf_path("XDG_CONFIG_HOME", "tarmac-trace-utilities",
                           filename, out))
        return true;

    if (try_make_conf_path("HOME", ".config/tarmac-trace-utilities",
                           filename, out))
        return true;

    return false;
}

FILE *fopen_wrapper(const char *filename, const char *mode)
{
    return fopen(filename, mode);
}

struct tm localtime_wrapper(time_t t)
{
    return *localtime(&t);
}

string asctime_wrapper(struct tm tm)
{
    return asctime(&tm);
}
