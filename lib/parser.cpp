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

/*
 * Tarmac parser that is as general as I can make it.
 */

#include "libtarmac/parser.hh"
#include "libtarmac/misc.hh"
#include "libtarmac/registers.hh"

#include <algorithm>
#include <cassert>
#include <cstring>
#include <iostream>
#include <set>
#include <sstream>
#include <string>
#include <vector>

using std::back_inserter;
using std::begin;
using std::copy_if;
using std::end;
using std::endl;
using std::max;
using std::ostringstream;
using std::set;
using std::string;
using std::vector;

// This can be replaced with std::string::starts_with once C++20 is old enough.
static bool starts_with(const std::string &str, const char *prefix)
{
    size_t prefix_len = strlen(prefix);
    return str.size() >= prefix_len && str.compare(0, prefix_len, prefix) == 0;
}

// This can be replaced with std::string::ends_with once C++20 is old enough.
static bool ends_with(const std::string &str, const char *suffix)
{
    size_t suffix_len = strlen(suffix);
    return str.size() >= suffix_len &&
           str.compare(str.size() - suffix_len, suffix_len, suffix) == 0;
}

static bool contains_only(const std::string &str, const char *permitted_chars)
{
    return str.find_first_not_of(permitted_chars) == string::npos;
}

struct Token {
    static constexpr const char *decimal_digits = "0123456789";
    static constexpr const char *hex_digits = "0123456789ABCDEFabcdef";
    static constexpr const char *hex_digits_us = "0123456789ABCDEFabcdef_";

    size_t startpos, endpos;
    char c;   // '\0' if this is a word/EOL, otherwise a single punct character
    string s; // if c == '\0', the text of the word, or "" for EOL

    Token() : c('\0'), s("") {}
    Token(char c) : c(c), s("") {}
    Token(const string &s) : c('\0'), s(s) {}

    inline Token &setpos(size_t start, size_t end)
    {
        startpos = start;
        endpos = end;
        return *this;
    }

    inline bool iseol() const { return c == '\0' && s.size() == 0; }
    inline bool isword() const { return c == '\0' && s.size() > 0; }
    inline bool isword(const char *permitted_chars) const
    {
        return isword() && contains_only(s, permitted_chars);
    }
    inline bool isdecimal() const { return isword(decimal_digits); }
    inline uint64_t decimalvalue() const
    {
        assert(isdecimal());
        return stoull(s, NULL, 10);
    }
    inline bool ishex() const { return isword(hex_digits); }
    inline bool ishex_us() const { return isword(hex_digits_us); }
    inline bool ishexwithoptionalnamespace() const
    {
        // Match a hex value, optionally followed by a namespace specifier,
        // which must be one of "_S" or "_NS".
        if (!isword())
            return false;
        for (const char *suffix : {"_S", "_NS", ""}) {
            if (ends_with(s, suffix))
                return contains_only(s.substr(0, s.size() - strlen(suffix)),
                                     hex_digits);
        }
        return false;
    }
    inline uint64_t hexvalue() const
    {
        assert(ishex());
        return stoull(s, NULL, 16);
    }
    inline int length() const { return isword() ? s.size() : 1; }

    inline bool starts_with(const char *prefix)
    {
        return ::starts_with(s, prefix);
    }

    void remove_chars(const char *chars)
    {
        size_t i = 0, j = 0;
        while ((j = s.find_first_not_of(chars, j)) != string::npos)
            s[i++] = s[j++];
        s.resize(i);
    }

    inline bool operator==(const Token &rhs) const
    {
        return (c == rhs.c && (!isword() || s == rhs.s));
    }
    inline bool operator==(const char c_) const
    {
        assert(c_ != '\0');
        return c == c_;
    }
    inline bool operator==(const string &s_) const
    {
        return isword() && s == s_;
    }
    template <class T> inline bool operator!=(const T &rhs) const
    {
        return !(*this == rhs);
    }
};

struct TarmacLineParserImpl {
    string line;
    size_t pos, size;
    Time last_timestamp;
    bool bigend;
    set<string> unrecognised_registers_already_reported;
    set<string> unrecognised_system_operations_reported;
    set<string> unrecognised_tarmac_events_reported;
    ParseReceiver *receiver;

    inline bool iswordchr(char c)
    {
        return isalnum((unsigned char)c) || c == '_' || c == '-' || c == '.' ||
               c == '#';
    }

    void highlight(size_t start, size_t end, HighlightClass cl)
    {
        receiver->highlight(start, end, cl);
    }
    void highlight(const Token &tok, HighlightClass cl)
    {
        highlight(tok.startpos, tok.endpos, cl);
    }

    [[noreturn]] void lex_error(size_t pos) {
        highlight(pos, line.size(), HL_ERROR);
        ostringstream os;
        os << "Unrecognised token" << endl;
        os << line << endl;
        os << string(pos, ' ') << "^" << endl;
        throw TarmacParseError(os.str());
    }

    void parse_error(const Token &tok, string msg)
    {
        highlight(tok, HL_ERROR);
        ostringstream os;
        os << msg << endl;
        os << line << endl;
        os << string(tok.startpos, ' ')
           << string(max((size_t)1, tok.endpos - tok.startpos), '^') << endl;
        throw TarmacParseError(os.str());
    };

    void warning(const string &msg)
    {
        if (receiver->parse_warning(msg))
            throw TarmacParseError(msg);
    }

    Token lex()
    {
        // Eat whitespace.
        while (pos < size && isspace((unsigned char)line[pos]))
            pos++;

        if (pos == size) {
            Token ret;
            ret.setpos(pos, pos);
            return ret;
        }

        // Punctuation characters that we return as single tokens
        if (strchr(":()[],", line[pos])) {
            Token ret(line[pos]);
            ret.setpos(pos, pos + 1);
            highlight(ret, HL_PUNCT);
            pos++;
            return ret;
        }

        // Otherwise, accumulate a 'word' of alphanumerics,
        // underscore, minus signs, dots and hashes.
        size_t start = pos;
        while (pos < size && iswordchr(line[pos]))
            pos++;
        if (pos > start) {
            Token ret(line.substr(start, pos - start));
            ret.setpos(start, pos);
            return ret;
        }

        // Failing that too, we have an error.
        lex_error(pos);
    }

    static bool parse_iset_state(const Token &tok, ISet *output)
    {
        ISet iset;
        if (tok == "A")
            iset = ARM;
        else if (tok == "T")
            iset = THUMB;
        else if (tok == "O")
            iset = A64;
        else
            return false;

        if (output)
            *output = iset;
        return true;
    }

    void parse(const string &line_)
    {
        // Set up the lexer.
        line = line_;
        pos = 0;
        size = line.find_last_not_of("\r\n");
        if (size != string::npos)
            size++;
        else
            size = line.size();
        line.resize(size);

        // Fetch the first token.
        Token tok = lex();

        // Tarmac lines often, but not always, start with a timestamp.
        Time time;
        if (tok.isdecimal()) {
            time = tok.decimalvalue();
            highlight(tok, HL_TIMESTAMP);
            tok = lex();

            if (tok == "clk" || tok == "ns" || tok == "cs" || tok == "cyc" ||
                tok == "tic") {
                // Any of these is something we recognise as a unit of
                // time, so skip over it.
                tok = lex();
            }

            last_timestamp = time;
        } else {
            // If no timestamp, that means the event is simultaneous
            // with the previous one.
            time = last_timestamp;
        }

        // Now we can have a trace source identifier (cpu or other component)
        // If we want multiple streams recongized, this is it.
        // But for now, we just drop the cpu* identifiers.
        if (tok.starts_with("cpu")) {
            tok = lex();
        }

        // Now we definitely expect an event type, and we diverge
        // based on what it is.
        highlight(tok, HL_EVENT);
        if (tok == "IT" || tok == "IS" || tok == "ES") {
            // An instruction-execution (or non-execution) event.

            // The "IS" event is Fast-Models-speak for 'instruction
            // failed its condition'. But we may also see "CCFAIL"
            // just before the disassembly, which is an "ES" line's way
            // of signalling the same thing. So we set ccfail now, but
            // may also override it to true later on.
            bool executed = (tok != "IS");

            bool is_ES = (tok == "ES");

            tok = lex();
            if (tok == "EXC" || tok == "Reset") {
                // Sometimes used to report an exception event relating to the
                // instruction, e.g. because it was illegal. We abandon parsing
                // this as an instruction event, and decide it's text-only.
                tok = lex(); // now tok.startpos begins unparsed text
                highlight(tok.startpos, line.size(), HL_TEXT_EVENT);
                TextOnlyEvent ev(time, "EXC", line.substr(tok.startpos));
                receiver->got_event(ev);
                return;
            }

            unsigned long long address;
            unsigned long bitpattern;
            int width;

            // Diverge based on the event type.
            if (is_ES) {
                // After "ES", expect an address and an instruction bit
                // pattern, in parentheses and separated by a colon.
                if (tok != '(')
                    parse_error(tok, "expected '(' to introduce "
                                     "instruction address and bit pattern");
                tok = lex();

                if (!tok.ishex())
                    parse_error(tok, "expected a hex instruction address");
                address = tok.hexvalue();
                highlight(tok, HL_PC);
                tok = lex();

                if (tok != ':')
                    parse_error(tok, "expected ':' between instruction "
                                     "address and bit pattern");
                tok = lex();

                if (!tok.ishex())
                    parse_error(tok, "expected a hex instruction bit pattern");
                bitpattern = tok.hexvalue();
                highlight(tok, HL_INSTRUCTION);
                width = tok.length() * 4;
                tok = lex();

                if (tok != ')')
                    parse_error(tok, "expected ')' after instruction "
                                     "address and bit pattern");
                tok = lex();
            } else {
                // After "IT" or "IS", expect a Fast Models-style line.
                //
                // These have the general form of
                //
                //   IT (xxxx) yyyy zzzz S M : disassembly
                //
                // but not every producer of this flavour agrees on exactly
                // what the fields are. In FM, the bracketed value xxxx is a
                // decimal counter that increments with each traced
                // instruction; yyyy is the instruction address, and zzzz is
                // its encoding. But in at least one other producer, xxxx is
                // the instruction address (so it's in hex, in particular!),
                // and yyyy is omitted! So we have to wait until we see the
                // _next_ token S (which is the instruction set state, e.g.
                // "A", "T", "O") to find out which of those we're looking
                // at.
                Token bracketed;
                if (tok == '(') {
                    // Bracketed value
                    tok = lex();
                    if (!tok.isdecimal() && !tok.ishex())
                        parse_error(tok, "expected a hex or decimal number");
                    bracketed = tok;

                    tok = lex();
                    if (tok != ')')
                        parse_error(tok, "expected ')' after bracketed value");
                    tok = lex();
                }

                if (!tok.ishex())
                    parse_error(tok, "expected a hex value");
                Token postbracket = tok;
                address = tok.hexvalue();
                highlight(tok, HL_PC);
                tok = lex();

                if (tok == ':') {
                    // Optionally, a colon suffix with another hex
                    // address after it. I'm guessing this is a
                    // physical vs virtual address distinction, though
                    // all I know as of 2018-04-11 is that I saw this
                    // syntax in the output from a Cortex-A9 Fast
                    // Model.
                    tok = lex();
                    if (!tok.ishexwithoptionalnamespace())
                        parse_error(tok, "expected a hex address after ':'");
                    tok = lex();
                    if (tok == ',') {
                        // Optionally, a comma with _another_ hex
                        // address after it, which I've encountered in
                        // the context of a 4-byte Thumb instruction,
                        // with the two comma-separated addresses
                        // pointing to the first and second halfwords.
                        tok = lex();
                        if (!tok.ishexwithoptionalnamespace())
                            parse_error(tok,
                                        "expected a hex address after ','");
                        tok = lex();
                    }
                }

                Token instruction;
                if (parse_iset_state(tok, nullptr)) {
                    // If we see an instruction set state at this
                    // point, then the bracketed value was the
                    // address, and what we've just parsed as the
                    // address was the bit pattern.
                    bitpattern = address;

                    address = bracketed.hexvalue();
                    highlight(bracketed, HL_PC);

                    instruction = postbracket;
                } else {
                    if (!tok.ishex())
                        parse_error(tok,
                                    "expected a hex instruction bit pattern");
                    instruction = tok;
                    tok = lex();
                }
                bitpattern = instruction.hexvalue();
                highlight(instruction, HL_INSTRUCTION);
                width = instruction.length() * 4;
            }

            // Now we reconverge, because both ES and IT formats
            // look basically the same from here on.
            ISet iset;
            if (!parse_iset_state(tok, &iset))
                parse_error(tok, "expected instruction-set state");
            highlight(tok, HL_ISET);
            tok = lex();

            if (!tok.isword())
                parse_error(tok, "expected CPU mode");
            // We currently ignore the CPU mode. If we ever needed to
            // support register bank switching, we would need to track
            // it carefully.
            highlight(tok, HL_CPUMODE);
            tok = lex();

            if (tok != ':')
                parse_error(tok, "expected ':' before instruction");
            tok = lex();

            if (is_ES && tok == "CCFAIL") {
                executed = false;
                highlight(tok, HL_CCFAIL);
                tok = lex();
            }

            // Now we're done, and tok.startpos points at the
            // beginning of the instruction disassembly.
            highlight(tok.startpos, line.size(), HL_DISASSEMBLY);
            InstructionEvent ev(time, executed, address, iset, width,
                                bitpattern, line.substr(tok.startpos));
            receiver->got_event(ev);
        } else if (tok == "R") {
            // Register update.
            tok = lex();
            if (!tok.isword())
                parse_error(tok, "expected register name");
            Token regnametok = tok; // save for later error reporting
            string regname = tok.s;
            tok = lex();

            if (regname == "DC" || regname == "IC" || regname == "TLBI" ||
                regname == "AT") {
                if (!unrecognised_system_operations_reported.count(regname)) {
                    unrecognised_system_operations_reported.insert(regname);
                    ostringstream os;
                    os << "unsupported system operation '" << regnametok.s
                       << "'";
                    warning(os.str());
                }
                return;
            }

            string extrainfo;
            // Some forms of Tarmac follow the main register name with
            // extra information, usually disambiguating banked
            // versions of a register or similar, e.g. "SCTLR
            // (AARCH32)" or "R1 (USR)".
            if (tok == '(') {
                tok = lex();

                if (!tok.isword())
                    parse_error(tok, "expected extra register "
                                     "identification details");
                extrainfo = tok.s;
                tok = lex();

                if (tok != ')')
                    parse_error(tok, "expected ')' after extra register "
                                     "identification details");
                tok = lex();
            }

            string contents;
            auto consume_register_contents = [&contents](Token &tok) {
                copy_if(begin(tok.s), end(tok.s), back_inserter(contents),
                        [](char c) { return c != '_'; });
            };

            // In most cases, lookup_reg_name will tell us how wide we
            // expect the register to be. However, there are a couple
            // of special cases.
            //
            // Register updates for 'sp' can mean either the AArch32
            // or AArch64 stack pointer, which have different ids in
            // the registers.hh system. So we defer figuring out which
            // register we're looking at until we see whether we've
            // been given 32 or 64 bits of data.
            //
            // And 'fpcr' is sometimes seen in traces with 64 bits of
            // data, even though it's 32-bit; so in that case we have
            // to read all 64, and keep the least-significant part.
            RegisterId reg;
            bool got_reg_id = lookup_reg_name(reg, regname);
            bool is_fpcr = (got_reg_id && reg.prefix == RegPrefix::fpcr);
            bool is_sp = (!strcasecmp(regname.c_str(), "sp") ||
                          !strncasecmp(regname.c_str(), "sp_", 3));
            bool special = is_fpcr || is_sp;

            if (got_reg_id && !special) {
                // Consume tokens of register contents until we've
                // seen as much data as we expect. We tolerate the
                // contents being separated into multiple tokens by
                // spaces or colons, or having underscores in them
                // (which our lexer will include in a single token).
                size_t hex_digits_expected = 2 * reg_size(reg);
                while (contents.size() < hex_digits_expected) {
                    if (!tok.ishex_us())
                        parse_error(tok, "expected register contents");
                    consume_register_contents(tok);
                    tok = lex();

                    if (tok == ':')
                        tok = lex();
                }
            } else if (special) {
                // Special cases described above (SP and FPCR), where we have
                // to wait to see how much data we can get out of the input
                // line.
                //
                // In all cases of this so far encountered, it's enough to read
                // a single contiguous token of register contents, plus a
                // second one if a ':' follows it.
                if (!tok.ishex_us())
                    parse_error(tok, "expected register contents");
                consume_register_contents(tok);
                tok = lex();

                if (tok == ':') {
                    tok = lex();
                    if (!tok.ishex_us())
                        parse_error(tok, "expected additional register "
                                    "contents after ':'");
                    consume_register_contents(tok);
                }

                if (is_sp) {
                    // If the special register was SP, use the size of the data
                    // we've just collected to disambiguate between r13 and
                    // xsp.
                    if (contents.size() == 8) {
                        reg = { RegPrefix::r, 13 };
                        got_reg_id = true;
                    } else if (contents.size() == 16) {
                        reg = { RegPrefix::xsp, 0 };
                        got_reg_id = true;
                    }
                }
            }

            // Fast Models puts nothing further on a register line. Other
            // producers may add trailing annotations, e.g. helpfully
            // interpreting the hex CPSR value to show the individual NZCV
            // flags.

            unsigned bits = contents.size() * 4;

            vector<uint8_t> bytes;
            if (bits % 8 != 0)
                parse_error(tok, "expected register contents to be an integer"
                                 " number of bytes");
            for (unsigned pos = 0; pos < bits / 4; pos += 2)
                bytes.push_back(stoul(contents.substr(pos, 2), NULL, 16));

            if (!got_reg_id) {
                if (!unrecognised_registers_already_reported.count(regname)) {
                    unrecognised_registers_already_reported.insert(regname);
#if 0
                    /*
                     * Early versions of this code printed a warning
                     * about unrecognized register names, which was
                     * useful in finding the obvious ones that weren't
                     * implemented yet. Now it mostly reports system
                     * registers that the indexing system doesn't
                     * track, so I think this warning is now
                     * generating noise rather than signal. It could
                     * easily be re-enabled under some kind of
                     * --verbose flag if necessary, by setting a
                     * verbosity field in the Parser object.
                     */
                    ostringstream os;
                    os << "unrecognised " << bits << "-bit register '"
                       << regname << "'";
                    warning(os.str());
#endif
                }
                return;
            }

            // Reverse the order of 'bytes'. Our internal
            // representation of registers is little-endian, but the
            // trace file will have specified the register value in
            // normal human reading order, i.e. big-endian.
            std::reverse(bytes.begin(), bytes.end());

            // Truncate 'bytes' to 32 bits in the case of a 64-bit FPCR update.
            // (We do this after the reversal, so that we keep the LSW of the
            // 64-bit value, not the MSW.)
            if (is_fpcr)
                bytes.resize(reg_size(reg));

            RegisterEvent ev(time, reg, bytes);
            receiver->got_event(ev);
        } else if (tok == "MR1" || tok == "MR2" || tok == "MR4" ||
                   tok == "MR8" || tok == "MW1" || tok == "MW2" ||
                   tok == "MW4" || tok == "MW8" || tok == "MR1X" ||
                   tok == "MR2X" || tok == "MR4X" || tok == "MR8X" ||
                   tok == "MW1X" || tok == "MW2X" || tok == "MW4X" ||
                   tok == "MW8X" || tok == "R01" || tok == "R02" ||
                   tok == "R04" || tok == "R08" || tok == "W01" ||
                   tok == "W02" || tok == "W04" || tok == "W08") {
            // Contiguous memory access event.

            const Token firsttok = tok;
            size_t pos = 0;
            if (tok.s[pos] == 'M')
                pos++; // FM uses an 'M' prefix, but not everyone agrees
            bool read = (tok.s[pos] == 'R');
            pos++;
            size_t size = stoull(tok.s.substr(pos));
            tok = lex();

            if (tok == "X") {
                /*
                 * Sometimes an exclusive memory operation is indicated by a
                 * separate "X" token. In other cases, it's folded in to the
                 * main event type (e.g. "MR4X" in the above if statement).
                 *
                 * We're not currently identifying exclusive operations, so
                 * we ignore this, as we implicitly ignored the X suffix.
                 */
                tok = lex();
            }

            if (!tok.ishex())
                parse_error(tok, "expected memory address");
            uint64_t addr = tok.hexvalue();
            tok = lex();

            if (tok == ':') {
                /*
                 * If there's a second hex number separated by a colon from
                 * the first one, then the first one is virtual address and
                 * the second is physical. For our purposes, we care about
                 * virtual addresses, so we ignore the latter.
                 */
                tok = lex();
                if (!tok.ishex())
                    parse_error(tok, "expected physical memory address "
                                     "after ':'");
                tok = lex();
            }

            if (tok == '(') {
                /*
                 * One thing that can happen at this point in a line
                 * is that we see some parenthesised special keyword
                 * like "(ABORTED)". In that situation we don't model
                 * this as a memory access event at all.
                 */
                tok = lex();
                if (tok == "ABORTED") {
                    Token tok2 = lex();
                    if (tok2 != ')')
                        parse_error(tok2, "expected closing parenthesis");
                    highlight(tok.startpos, line.size(), HL_TEXT_EVENT);
                    TextOnlyEvent ev(time, tok.s,
                                     line.substr(firsttok.startpos));
                    receiver->got_event(ev);
                    return;
                } else {
                    parse_error(tok, "unrecognised parenthesised keyword");
                }
            }

            // The value transferred to/from the memory is broken up
            // by underscores if it's more than 8 bytes long. We want
            // to retrieve it as a single integer, so we just strip
            // those out.
            tok.remove_chars("_");
            if (!tok.ishex())
                parse_error(tok, "expected memory contents in hex");
            uint64_t contents = tok.hexvalue();

            MemoryEvent ev(time, read, size, addr, true, contents);
            receiver->got_event(ev);
        } else if (tok == "LD" || tok == "ST") {
            // Diagrammatic memory access event.

            bool read = (tok == "LD");
            tok = lex();

            // Expect a hex address.
            if (!tok.ishex())
                parse_error(tok, "expected load/store memory address");
            uint64_t baseaddr = tok.hexvalue();
            tok = lex();

            // Now expect a collection of words covering 16 bytes of
            // memory starting at the given base address. These words
            // may contain hex digits, dots and sometimes '#' to
            // indicate an actually unknown value.
            constexpr uint16_t UNUSED = 0x100, UNKNOWN = 0x101;
            uint16_t bytes[16];
            int bytepos = 0;

            while (true) {
                if (!tok.isword("0123456789ABCDEFabcdef.#"))
                    parse_error(tok, "expected a word of data bytes, "
                                     "'.' and '#'");
                if (tok.s.size() % 2)
                    parse_error(tok, "expected data word to cover a "
                                     "whole number of bytes");
                for (size_t i = 0; i < tok.s.size(); i += 2) {
                    Token bytetok(tok.s.substr(i, 2));
                    bytetok.startpos += i;
                    bytetok.endpos = bytetok.startpos + 2;

                    if (bytepos >= 16)
                        parse_error(bytetok, "expected exactly 16 data bytes");

                    if (bytetok == "..")
                        bytes[bytepos] = UNUSED;
                    else if (bytetok == "##")
                        bytes[bytepos] = UNKNOWN;
                    else if (bytetok.ishex())
                        bytes[bytepos] = bytetok.hexvalue();
                    else
                        parse_error(bytetok, "expected each byte to be "
                                             "only one of '.', '#' and hex");

                    bytepos++;
                }
                if (bytepos == 16)
                    break;
                tok = lex();
            }

            // Now go through that array of bytes and combine adjacent
            // non-UNUSED ones into one or more of our own kind of
            // memory access event.
            for (size_t i = 0; i < 16;) {
                if (bytes[i] == UNUSED) {
                    i++; // skip this byte
                } else if (bytes[i] == UNKNOWN) {
                    size_t j = i;
                    while (j < 16 && bytes[j] == UNKNOWN)
                        j++;

                    MemoryEvent ev(time, read, j - i, baseaddr + 16 - j, false,
                                   0);
                    receiver->got_event(ev);

                    i = j;
                } else {
                    size_t j = i;
                    while (j < 16 && j - i < 8 && bytes[j] < 0x100)
                        j++;

                    // LD/ST memory access lines are always shown
                    // little-endian, irrespective of system
                    // endianness. But our MemoryEvents are
                    // represented in system endianness, so we have to
                    // do the conversion here.
                    uint64_t value = 0;
                    if (bigend)
                        for (size_t k = j; k-- > i;)
                            value = (value << 8) | bytes[k];
                    else
                        for (size_t k = i; k < j; k++)
                            value = (value << 8) | bytes[k];

                    MemoryEvent ev(time, read, j - i, baseaddr + 16 - j, true,
                                   value);
                    receiver->got_event(ev);

                    i = j;
                }
            }
        } else if (tok == "Tarmac") {
            // Header line seen at the start of some trace files. Typically
            // says "Tarmac Text Rev 1" or "Tarmac Text Rev 3t", or similar.
            //
            // We discard this line _completely_, without generating any
            // output from this parser at all. (We can't even return it as a
            // text-only event, because it occurs before the first timestamp
            // is seen; and we don't really want to anyway, because it's
            // conceptually part of the container file format rather than
            // the stream of events inside that container.)
            return;
        } else {
            // Anything else is treated as a TextOnlyEvent, i.e. we
            // still show it in the trace but do not perceive any
            // semantic effect on our running model of the state of
            // the world.
            //
            // We have a collection of types of these that we at least
            // know the names of and are confident that we're right to
            // be treating as null; anything not in that collection
            // provokes a warning, just in case it _did_ have
            // important semantics that we shouldn't have ignored.

            string type(tok.s);
            if (type == "CADI" || type == "E" || type == "P" ||
                type == "CACHE" || type == "TTW" || type == "BR" ||
                type == "INFO_EXCEPTION_REASON" || type == "SIGNAL" ||
                type == "EXC") {
                // no warning
            } else {
                if (!unrecognised_tarmac_events_reported.count(type)) {
                    unrecognised_tarmac_events_reported.insert(type);
                    warning("unknown Tarmac event type '" + type + "'");
                }
            }

            tok = lex();
            highlight(tok.startpos, line.size(), HL_TEXT_EVENT);

            TextOnlyEvent ev(time, type, line.substr(tok.startpos));
            receiver->got_event(ev);
        }
    }
};

TarmacLineParser::TarmacLineParser(bool bigend, ParseReceiver &rec)
    : pImpl(new TarmacLineParserImpl)
{
    pImpl->bigend = bigend;
    pImpl->receiver = &rec;

    /*
     * If any un-timestamped lines are seen at the start of the trace
     * before any timestamped line, this is the timestamp they'll
     * receive. 0 seems reasonable, since timestamps are non-negative
     * integers, so this is the only value that guarantees to preserve
     * monotonicity.
     */
    pImpl->last_timestamp = 0;
}

TarmacLineParser::~TarmacLineParser() { delete pImpl; }

void TarmacLineParser::parse(const string &s) const { pImpl->parse(s); }
