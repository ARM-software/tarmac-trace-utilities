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
#include <vector>

struct TarmacEvent {
    Time time;
    TarmacEvent(Time time) : time(time) {}
    TarmacEvent() = default;
    TarmacEvent(const TarmacEvent &) = default;
    TarmacEvent &operator=(const TarmacEvent &) = default;
};

enum ISet { ARM, THUMB, A64 };

enum HighlightClass {
    HL_NONE,
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

struct InstructionEvent : TarmacEvent {
    bool executed;
    Addr pc;
    ISet iset;
    int width; // 16 or 32
    unsigned instruction;
    std::string disassembly;
    InstructionEvent(Time time, bool executed, Addr pc, ISet iset, int width,
                     unsigned instruction, const std::string &disassembly)
        : TarmacEvent(time), executed(executed), pc(pc), iset(iset),
          width(width), instruction(instruction), disassembly(disassembly)
    {
    }
    InstructionEvent() = default;
    InstructionEvent(const InstructionEvent &) = default;
    InstructionEvent &operator=(const InstructionEvent &) = default;
};

struct RegisterEvent : TarmacEvent {
    RegisterId reg;
    bool got_value;
    unsigned long long value;
    std::vector<uint8_t> bytes;
    RegisterEvent(Time time, RegisterId reg, const std::vector<uint8_t> &bytes)
        : TarmacEvent(time), reg(reg), got_value(false), bytes(bytes)
    {
    }
    void set_value(unsigned long long value_)
    {
        got_value = true;
        value = value_;
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

struct TarmacParseError {
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
    TarmacLineParser(bool bigend, ParseReceiver &);
    ~TarmacLineParser();
    void parse(const std::string &s) const;
};

#endif // LIBTARMAC_PARSER_HH
