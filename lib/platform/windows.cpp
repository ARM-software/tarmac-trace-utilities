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
#include "libtarmac/intl.hh"

#include "cmake.h"

#include <windows.h>
#include <shlobj.h>
#include <stdlib.h>

#include <sstream>

using std::ostringstream;
using std::string;

bool get_file_timestamp(const string &filename, uint64_t *out_timestamp)
{
    WIN32_FILE_ATTRIBUTE_DATA data;
    if (!GetFileAttributesEx(filename.c_str(), GetFileExInfoStandard, &data))
        return false;
    *out_timestamp = ((uint64_t)data.ftLastWriteTime.dwHighDateTime << 16) |
                     data.ftLastWriteTime.dwLowDateTime;
    return true;
}

bool is_interactive()
{
    DWORD ignored_output;
    return GetConsoleMode(GetStdHandle(STD_OUTPUT_HANDLE), &ignored_output);
}

string get_error_message()
{
    DWORD err = GetLastError();

    std::ostringstream oss;
    oss << "error " << err;

    constexpr size_t MAX_MESSAGE_SIZE = 0x10000;
    char msgtext[MAX_MESSAGE_SIZE];
    if (!FormatMessage(
            (FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS), NULL,
            err, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), msgtext,
            MAX_MESSAGE_SIZE - 1, nullptr)) {
        oss << " (unable to format: FormatMessage returned " << GetLastError()
            << ")";
    } else {
        oss << ": " << msgtext;
    }

    string msg = oss.str();
    while (!msg.empty() && msg[msg.size() - 1] == '\n')
        msg.resize(msg.size() - 1);

    return msg;
}

struct MMapFile::PlatformData {
    HANDLE fh;
    HANDLE mh;
};

MMapFile::MMapFile(const string &filename, bool writable)
    : filename(filename), writable(writable)
{
    pdata = new PlatformData;

    pdata->fh = CreateFile(filename.c_str(),
                           GENERIC_READ | (writable ? GENERIC_WRITE : 0),
                           FILE_SHARE_READ, NULL,
                           (writable ? CREATE_ALWAYS : OPEN_EXISTING), 0, NULL);
    if (pdata->fh == INVALID_HANDLE_VALUE)
        reporter->err(1, "%s: CreateFile", filename.c_str());

    LARGE_INTEGER size;
    if (!GetFileSizeEx(pdata->fh, &size))
        reporter->err(1, "%s: GetFileSizeEx", filename.c_str());
    next_offset = curr_size = size.QuadPart;

    mapping = nullptr;
    pdata->mh = nullptr;
    map();
}

MMapFile::~MMapFile()
{
    unmap();
    if (writable) {
        LARGE_INTEGER pos;
        pos.QuadPart = next_offset;
        if (!SetFilePointerEx(pdata->fh, pos, NULL, FILE_BEGIN))
            reporter->err(1, "%s: SetFilePointerEx (truncating file)",
                          filename.c_str());
        if (!SetEndOfFile(pdata->fh))
            reporter->err(1, "%s: SetEndOfFile (truncating file)",
                          filename.c_str());
    }
    CloseHandle(pdata->fh);
    delete pdata;
}

void MMapFile::map()
{
    if (!curr_size)
        return;

    assert(!pdata->mh);
    assert(!mapping);

    size_t mapping_size = curr_size;
    if (writable) {
        // Round the file mapping size to a multiple of a page. I observe
        // that CreateFileMapping tends to give ERROR_NO_SYSTEM_RESOURCES
        // if I don't do this, though there's nothing in the documentation
        // to suggest why.
        mapping_size = (mapping_size + 0xFFFF) & ~0xFFFF;
    }

    pdata->mh = CreateFileMapping(
        pdata->fh, NULL, (writable ? PAGE_READWRITE : PAGE_READONLY),
        (mapping_size >> 16) >> 16, mapping_size & 0xFFFFFFFF, NULL);
    if (!pdata->mh)
        reporter->err(1, "%s: CreateFileMapping", filename.c_str());

    mapping = MapViewOfFile(pdata->mh,
                            (writable ? FILE_MAP_ALL_ACCESS : FILE_MAP_READ), 0,
                            0, curr_size);
    if (!mapping)
        reporter->err(1, "%s: MapViewOfFile", filename.c_str());
}

void MMapFile::unmap()
{
    if (!curr_size)
        return;

    assert(mapping);
    UnmapViewOfFile(mapping);
    mapping = nullptr;

    assert(pdata->mh);
    CloseHandle(pdata->mh);
    pdata->mh = NULL;
}

void MMapFile::resize(size_t newsize)
{
    unmap();

    LARGE_INTEGER pos;
    pos.QuadPart = newsize;
    if (!SetFilePointerEx(pdata->fh, pos, NULL, FILE_BEGIN))
        reporter->err(1, "%s: SetFilePointerEx (extending file)",
                      filename.c_str());
    if (!SetEndOfFile(pdata->fh))
        reporter->err(1, "%s: SetEndOfFile (extending file)",
                      filename.c_str());
    curr_size = newsize;

    map();
}

#if !HAVE_APPDATAPROGRAMDATA
// Compensate for this not being defined by earlier toolchain versions
static const GUID FOLDERID_AppDataProgramData = {
    0x559d40a3, 0xa036, 0x40fa,
    {0xaf, 0x61, 0x84, 0xcb, 0x43, 0x0a, 0x4d, 0x34},
};
#endif

bool get_conf_path(const string &filename, string &out)
{
    ostringstream oss;

    char path[MAX_PATH];
    DWORD len = GetEnvironmentVariable("TARMAC_TRACE_UTILITIES_CONFIG",
                                       path, sizeof(path));
    if (len > 0 && len < sizeof(path) && path[0]) {
        oss << path;
        goto found_dir;
    }

    PWSTR appdata;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_AppDataProgramData,
                                       0, nullptr, &appdata))) {
        BOOL used_default_char = false;
        int status = WideCharToMultiByte(CP_ACP, 0, appdata, -1,
                                         path, sizeof(path),
                                         nullptr, &used_default_char);
        CoTaskMemFree(appdata);
        if (status > 0 && !used_default_char) {
            oss << path << "\\tarmac-trace-utilities";
            goto found_dir;
        }
    }

    return false;

  found_dir:
    oss << "\\" << filename;
    out = oss.str();
    return true;
}

FILE *fopen_wrapper(const char *filename, const char *mode)
{
    FILE *ret;
    if (fopen_s(&ret, filename, mode) != 0)
        return NULL;
    return ret;
}

struct tm localtime_wrapper(time_t t)
{
    struct tm ret;
    localtime_s(&ret, &t);
    return ret;
}

string asctime_wrapper(struct tm tm)
{
    char buffer[256];
    asctime_s(buffer, sizeof(buffer), &tm);
    return buffer;
}

bool get_environment_variable(const string &varname, string &out)
{
    if (varname.find('\0') != string::npos)
        return false;      // NUL is legal in a std::string but not in env var

    char *val;
    size_t len;
    errno_t err = _dupenv_s(&val, &len, varname.c_str());
    if (err)
        return false;

    out = std::string(val, len);
    if (!out.empty() && out.back() == '\0')
        out.pop_back();
    free(val);
    return true;
}

void gettext_setup()
{
#if HAVE_LIBINTL
#error "We didn't expect gettext to be available on Windows, so no setup code"
#endif
}
