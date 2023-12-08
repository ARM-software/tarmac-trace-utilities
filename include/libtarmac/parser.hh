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

#ifndef LIBTARMAC_PARSER_HH
#define LIBTARMAC_PARSER_HH

#include "libtarmac/misc.hh"
#include "libtarmac/registers.hh"

#include <cstdint>
#include <exception>
#include <vector>

struct TarmacEvent {
    Time time;
    TarmacEvent(Time time) : time(time) {}
    TarmacEvent() = default;
    TarmacEvent(const TarmacEvent &) = default;
    TarmacEvent &operator=(const TarmacEvent &) = default;
};

enum ISet { ARM, THUMB, A64 };

struct ParseParams {
    bool bigend = false;

    // In C++17 we could replace these two fields with a std::optional<ISet>
    bool iset_specified = false;
    ISet iset;  // only meaningful if specified_iset is True
};

enum HighlightClass {
    HL_NONE,
    HL_SPACE,
    HL_TIMESTAMP,
    HL_EVENT,
    HL_PC,
    HL_INSTRUCTION,
    HL_ISET,
    HL_CPUMODE,
    HL_CCFAIL,
    HL_DISASSEMBLY,
    HL_TEXT_EVENT,
    HL_PUNCT,
    HL_ERROR,
};

enum InstructionEffect {
    IE_EXECUTED,  // instruction was executed as normal
    IE_CCFAIL,    // instruction was skipped because it failed its condition
    IE_FETCHFAIL, // instruction fetch failed, so no encoding available
};

struct InstructionEvent : TarmacEvent {
    InstructionEffect effect;
    Addr pc;
    ISet iset;
    int width; // 16 or 32
    unsigned instruction;
    std::string disassembly;
    InstructionEvent(Time time, InstructionEffect effect, Addr pc, ISet iset,
                     int width, unsigned instruction,
                     const std::string &disassembly)
        : TarmacEvent(time), effect(effect), pc(pc), iset(iset), width(width),
          instruction(instruction), disassembly(disassembly)
    {
    }
    InstructionEvent() = default;
    InstructionEvent(const InstructionEvent &) = default;
    InstructionEvent &operator=(const InstructionEvent &) = default;
};

struct RegisterEvent : TarmacEvent {
    RegisterId reg;
    size_t offset;                     // from base 'address' of register
    std::vector<uint8_t> bytes;
    RegisterEvent(Time time, RegisterId reg, size_t offset,
                  const std::vector<uint8_t> &bytes)
        : TarmacEvent(time), reg(reg), offset(offset), bytes(bytes)
    {
    }
};

struct MemoryEvent : TarmacEvent {
    bool read, known;
    size_t size;
    Addr addr;
    unsigned long long contents;
    MemoryEvent(Time time, bool read, size_t size, Addr addr, bool known,
                unsigned long long contents)
        : TarmacEvent(time), read(read), known(known), size(size), addr(addr),
          contents(contents)
    {
    }
};

struct TextOnlyEvent : TarmacEvent {
    std::string type, msg;
    TextOnlyEvent(Time time, const std::string &type, const std::string &msg)
        : TarmacEvent(time), type(type), msg(msg)
    {
    }
    ~TextOnlyEvent() {}
};

struct TarmacParseError : std::exception {
    std::string msg;
    TarmacParseError(const std::string &msg) : msg(msg) {}
};

class ParseReceiver {
  public:
    virtual void got_event(InstructionEvent &) {}
    virtual void got_event(RegisterEvent &) {}
    virtual void got_event(MemoryEvent &) {}
    virtual void got_event(TextOnlyEvent &) {}

    // start and end describe a half-open interval of locations in the
    // input string, i.e. including line[start] and not including
    // line[end]
    virtual void highlight(size_t /*start*/, size_t /*end*/,
                           HighlightClass /*hc*/)
    {
    }

    // parse_warning can return true to automatically upgrade the
    // warning to an error
    virtual bool parse_warning(const std::string & /*msg*/) { return false; }
};
class TarmacLineParserImpl;
class TarmacLineParser {
    TarmacLineParserImpl *pImpl;

  public:
    TarmacLineParser(ParseParams params, ParseReceiver &);
    ~TarmacLineParser();
    void parse(const std::string &s) const;
};

#endif // LIBTARMAC_PARSER_HH
