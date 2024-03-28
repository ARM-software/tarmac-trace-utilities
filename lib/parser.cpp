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

#include "libtarmac/intl.hh"
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
#include <utility>
#include <vector>

using std::back_inserter;
using std::begin;
using std::copy_if;
using std::end;
using std::endl;
using std::max;
using std::ostringstream;
using std::pair;
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
    static constexpr const char *regvalue_chars = "0123456789ABCDEFabcdef_-";

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
    inline bool isregvalue() const { return isword(regvalue_chars); }
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
    inline bool ishyphens() const
    {
        return isword() && contains_only(s, "-");
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

    pair<Token, Token> split(size_t pos) const {
        assert(isword());

        pair<Token, Token> p{ Token(s.substr(0, pos)), Token(s.substr(pos)) };

        p.first.setpos(startpos, startpos + pos);
        p.second.setpos(startpos + pos, endpos);
        return p;
    }
};

class TarmacLineParserImpl {
    friend class TarmacLineParser;

    // State remembered between lines of the input. We keep this as
    // small as possible, but a few things have to be remembered.
    struct InterLineState {
        // Timestamp from the previous line, which is often not
        // repeated on the next line.
        //
        // If any un-timestamped lines are seen at the start of the
        // trace before any timestamped line, this is the timestamp
        // they'll receive. 0 seems reasonable, since timestamps are
        // non-negative integers, so this is the only value that
        // guarantees to preserve monotonicity.
        Time timestamp = 0;

        // True if the event type is one of a small set for which the
        // next line might implicitly continue the same event type.
        bool event_type_is_continuable = false;

        // If event_type_is_continuable is true, this stores the
        // event-type token from the previous line. The startpos and
        // endpos values will therefore be indices into a string that
        // has already been thrown away, so don't use them.
        Token event_type_token;

        // We recognise continuations of LD and ST lines by leading
        // whitespace: the address token on the continuation line is
        // aligned under the address token from the previous line. So
        // this variable stores the starting position of the token
        // after the LD or ST, i.e. the address.
        size_t post_event_type_start = 0;
    };

    string line;
    size_t pos, size;
    const ParseParams &params;
    set<string> unrecognised_registers_already_reported;
    set<string> unrecognised_system_operations_reported;
    set<string> unrecognised_tarmac_events_reported;
    ParseReceiver *receiver;
    InterLineState next_line;

    static set<string> known_timestamp_units;

    TarmacLineParserImpl(const ParseParams &params, ParseReceiver *receiver)
        : params(params), receiver(receiver)
    {
    }

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
        if (pos < size && isspace((unsigned char)line[pos])) {
            size_t startpos = pos;
            do {
                pos++;
            } while (pos < size && isspace((unsigned char)line[pos]));
            highlight(startpos, pos, HL_SPACE);
        }

        if (pos == size) {
            Token ret;
            ret.setpos(pos, pos);
            return ret;
        }

        // Punctuation characters that we return as single tokens
        if (strchr(":()[],<>", line[pos])) {
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
        else if (tok == "T" || tok == "T16" || tok == "T32")
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
        // Get the inter-line state referring to the previous line,
        // and replace it with a default-constructed InterLineState
        // which we can update if we need to remember anything from
        // _this_ line until the next.
        InterLineState prev_line;
        std::swap(prev_line, next_line);

        // Constants used in 'byte' arrays for register and memory updates, to
        // represent special values that aren't ordinary bytes.
        constexpr uint16_t UNUSED = 0x100, UNKNOWN = 0x101;

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
        // If they don't, we default to the previous timestamp.
        Time time = prev_line.timestamp;

        // Before even checking for a timestamp on this line, see if
        // this looks like a continuation of a previous LD or ST
        // record. (Otherwise we're at risk of confusing a hex address
        // for a decimal timestamp, if the address happens not to have
        // any [a-f] digits.)
        if (prev_line.event_type_is_continuable &&
            tok.startpos == prev_line.post_event_type_start) {
            pos = tok.startpos;        // rewind past the next token
            tok = prev_line.event_type_token;
        } else {
            // With that case ruled out, look for a timestamp.
            if (tok.isdecimal()) {
                time = tok.decimalvalue();
                highlight(tok, HL_TIMESTAMP);
                tok = lex();

                if (tok.isword() && (known_timestamp_units.find(tok.s) !=
                                     known_timestamp_units.end()))
                    tok = lex();
            } else {
                // Another possibility is that the timestamp and its unit
                // are smushed together in a single token, with no
                // intervening space.
                if (tok.isword()) {
                    size_t end_of_digits =
                        tok.s.find_first_not_of(Token::decimal_digits);
                    if (end_of_digits > 0 && end_of_digits != string::npos &&
                        (known_timestamp_units.find(tok.s.substr(
                             end_of_digits)) != known_timestamp_units.end())) {
                        auto pair = tok.split(end_of_digits);
                        time = pair.first.decimalvalue();
                        highlight(pair.first, HL_TIMESTAMP);
                        tok = lex();
                    }
                }
            }
        }
        next_line.timestamp = time;

        // Now we can have a trace source identifier (cpu or other component)
        // If we want multiple streams recongized, this is it.
        // But for now, we just drop the cpu* identifiers.
        if (tok.starts_with("cpu")) {
            tok = lex();
        }

        // Now we definitely expect an event type, and we diverge
        // based on what it is.
        highlight(tok, HL_EVENT);
        if (tok == "IT" || tok == "IS" || tok == "IF" || tok == "ES") {
            // An instruction-execution (or non-execution) event.

            // The "IS" event is Fast-Models-speak for 'instruction
            // failed its condition'. But we may also see "CCFAIL"
            // just before the disassembly, which is an "ES" line's way
            // of signalling the same thing. So we set ccfail now, but
            // may also override it to true later on.
            //
            // "IF" can appear in some RTL-generated Tarmac. We treat
            // it just like IT.
            InstructionEffect effect = IE_EXECUTED;
            if (tok == "IS")
                effect = IE_CCFAIL;

            bool is_ES = (tok == "ES");

            const Token firsttok = tok;

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
            unsigned long bitpattern = 0;
            int width;
            bool expect_cpu_mode = true;
            bool seen_colon_in_brackets = false;
            bool t16_t32_state = false;

            // Diverge based on the event type.
            if (is_ES) {
                // After "ES", expect an address and an instruction bit
                // pattern, in parentheses and separated by a colon.
                if (tok != '(')
                    parse_error(tok, _("expected '(' to introduce "
                                       "instruction address and bit pattern"));
                tok = lex();

                if (!tok.ishex())
                    parse_error(tok, _("expected a hex instruction address"));
                address = tok.hexvalue();
                highlight(tok, HL_PC);
                tok = lex();

                if (tok != ':')
                    parse_error(tok, _("expected ':' between instruction "
                                       "address and bit pattern"));
                tok = lex();

                if (!tok.ishex()) {
                    if (tok.ishyphens()) {
                        // If the instruction bit pattern is given as a row of
                        // hyphens, that's an indication that the instruction
                        // fetch completely failed. At least one case where
                        // this can arise is if ECC memory reported a fault; it
                        // may later correct the error and retry the fetch.
                        effect = IE_FETCHFAIL;
                    } else {
                        parse_error(
                            tok, _("expected a hex instruction bit pattern"));
                    }
                } else {
                    bitpattern = tok.hexvalue();
                }
                highlight(tok, HL_INSTRUCTION);
                width = tok.length() * 4;
                tok = lex();

                if (tok != ')')
                    parse_error(tok, _("expected ')' after instruction "
                                       "address and bit pattern"));
                tok = lex();
            } else {
                // After "IT" or "IS", expect a Fast Models-style line.
                //
                // These have the general form of
                //
                //   IT (xxxx) yyyy zzzz S M : disassembly
                //
                // but not every producer of this flavour agrees on exactly
                // what the fields are.
                //
                // In FM, the bracketed value xxxx is a decimal counter that
                // increments with each traced instruction; yyyy is the
                // instruction address, and zzzz is its encoding. But in at
                // least one other producer, xxxx is the instruction address
                // (so it's in hex, in particular!), and yyyy is omitted!
                //
                // So we have to wait until we see the _next_ token S (which is
                // the instruction set state, e.g. "A", "T", "O") to find out
                // which of those we're looking at.
                //
                // Also, in some RTL-simulator output, there is a spare copy of
                // the address preceding the index, i.e. we have
                //
                //   IT (address:index) address encoding S [...]

                Token bracketed;
                if (tok == '(') {
                    // Bracketed value
                    tok = lex();
                    if (!tok.isdecimal() && !tok.ishex())
                        parse_error(tok, _("expected a hex or decimal number"));
                    bracketed = tok;

                    tok = lex();
                    if (tok == ':') {
                        // This appears to be a flavour in which there's a
                        // copy of the address before the index.
                        address = bracketed.hexvalue();
                        tok = lex();
                        if (!tok.isdecimal() && !tok.ishex())
                            parse_error(tok,
                                        _("expected a hex or decimal number"));
                        bracketed = tok;
                        tok = lex();
                        seen_colon_in_brackets = true;
                    }
                    if (tok != ')')
                        parse_error(tok,
                                    _("expected ')' after bracketed value"));
                    tok = lex();
                }

                if (!tok.ishex())
                    parse_error(tok, _("expected a hex value"));
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
                        parse_error(tok, _("expected a hex address after ':'"));
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
                                        _("expected a hex address after ','"));
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
                        parse_error(
                            tok, _("expected a hex instruction bit pattern"));
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
            if (!parse_iset_state(tok, &iset)) {
                // We can't find an instruction set state at all. Normally this
                // is a parse failure. But some Tarmac producers don't bother
                // to include it because they expect that you know the answer
                // already, because they're for CPUs that only have one state
                // (e.g. Cortex-M0). So if we've been told what iset state we
                // can assume, then we respond to this parse failure by
                // treating the rest of the line as the instruction.
                if (!params.iset_specified)
                    parse_error(tok, _("expected instruction-set state"));
                iset = params.iset;
            } else {
                highlight(tok, HL_ISET);
                if (tok == "T16" || tok == "T32")
                    t16_t32_state = true;
                tok = lex();

                // Heuristically guess whether we expect to see a CPU mode
                // token, and its following colon, in this record.
                //
                // Currently, I've only encountered one Tarmac producer
                // (Cortex-M4 RTL) that will omit it. That producer uses IT
                // rather than ES style instruction lines, and also has two
                // other features unique in my experience so far: it has a pair
                // of colon-separated numbers in the bracketed section, and it
                // uses "T16" and "T32" instead of plain "T" to show the
                // instruction set state. So, for the moment, my heuristic is
                // that if we see both of those features, we expect the CPU
                // mode to be omitted.
                if (!is_ES && seen_colon_in_brackets && t16_t32_state)
                    expect_cpu_mode = false;

                if (expect_cpu_mode) {
                    if (!tok.isword())
                        parse_error(tok, _("expected CPU mode"));
                    // We currently ignore the CPU mode. If we ever needed to
                    // support register bank switching, we would need to track
                    // it carefully.
                    highlight(tok, HL_CPUMODE);
                    tok = lex();

                    if (tok != ':')
                        parse_error(tok, _("expected ':' before instruction"));
                    tok = lex();
                }

                if (is_ES && tok == "CCFAIL") {
                    effect = IE_CCFAIL;
                    highlight(tok, HL_CCFAIL);
                    tok = lex();
                }
            }

            // Now we're done, and tok.startpos points at the
            // beginning of the instruction disassembly.
            size_t disass_end = line.size();
            while (disass_end > tok.startpos &&
                   isspace((unsigned char)line[disass_end - 1]))
                disass_end--;
            highlight(tok.startpos, disass_end, HL_DISASSEMBLY);
            if (disass_end < line.size())
                highlight(disass_end, line.size(), HL_SPACE);
            InstructionEvent ev(time, effect, address, iset, width,
                                bitpattern, line.substr(tok.startpos));
            receiver->got_event(ev);
        } else if (tok == "R") {
            // Register update.
            tok = lex();
            if (!tok.isword())
                parse_error(tok, _("expected register name"));
            Token regnametok = tok; // save for later error reporting
            string regname = tok.s;
            tok = lex();

            if (regname == "DC" || regname == "IC" || regname == "TLBI" ||
                regname == "AT") {
                if (!unrecognised_system_operations_reported.count(regname)) {
                    unrecognised_system_operations_reported.insert(regname);
                    warning(format(_("unsupported system operation '{}'"),
                                   regnametok.s));
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
                    parse_error(tok, _("expected extra register "
                                       "identification details"));
                extrainfo = tok.s;
                tok = lex();

                if (tok != ')')
                    parse_error(tok, _("expected ')' after extra register "
                                       "identification details"));
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
            bool is_cpsr = (got_reg_id && reg.prefix == RegPrefix::psr &&
                            regname == "cpsr");
            bool is_sp = (!strcasecmp(regname.c_str(), "sp") ||
                          !strncasecmp(regname.c_str(), "sp_", 3));
            bool special = is_fpcr || is_sp || is_cpsr;

            bool got_reg_subrange = false;
            unsigned reg_subrange_skip_lo, reg_subrange_skip_hi;
            if (tok == '<') {
                // Sometimes seen in Cycle Models output: a
                // specification of a partial register update, by
                // writing a range of bits being accessed following
                // the register name, in the form <hi:lo>. For
                // example, "R V0<127:64> 0000000000000000".
                //
                // This is an alternative to the -- syntax used to
                // omit bytes from a full-length register description,
                // and we handle this suffix by converting it into --
                // in the contents string.

                if (special) {
                    // If the register name is also one which we can't identify
                    // until we see the size of data, this is too confusing to
                    // work out what's going on. Refuse to handle this case.
                    parse_error(tok, _("cannot handle register bit range for "
                                       "this register"));
                }

                tok = lex();

                if (!tok.isdecimal())
                    parse_error(tok, _("expected bit offset within register"));
                unsigned top_bit = tok.decimalvalue();
                if ((top_bit & 7) != 7)
                    parse_error(tok, _("expected high bit offset within "
                                       "register to be at the top of a byte"));
                unsigned top_byte = top_bit >> 3;
                if (top_byte >= reg_size(reg))
                    parse_error(tok, _("high bit offset is larger than "
                                       "containing register"));
                tok = lex();

                if (tok != ':')
                    parse_error(tok, _("expected ':' separating bit offsets "
                                       "in register bit range"));
                tok = lex();

                if (!tok.isdecimal())
                    parse_error(tok, _("expected bit offset within register"));
                unsigned bot_bit = tok.decimalvalue();
                if ((bot_bit & 7) != 0)
                    parse_error(tok,
                                _("expected low bit offset within register to "
                                  "be at the bottom of a byte"));
                unsigned bot_byte = bot_bit >> 3;
                if (bot_byte > top_byte)
                    parse_error(tok, _("low bit offset is higher than "
                                       "high bit offset"));
                tok = lex();

                if (tok != '>')
                    parse_error(tok,
                                _("expected '>' after register bit range"));
                tok = lex();

                reg_subrange_skip_lo = bot_byte;
                reg_subrange_skip_hi = reg_size(reg) - (top_byte + 1);
                got_reg_subrange = true;
            }

            if (got_reg_id && !special) {
                // Consume tokens of register contents until we've
                // seen as much data as we expect. We tolerate the
                // contents being separated into multiple tokens by
                // spaces or colons, or having underscores in them
                // (which our lexer will include in a single token).
                size_t hex_digits_expected = 2 * reg_size(reg);
                if (got_reg_subrange) {
                    contents.append(2 * reg_subrange_skip_hi, '-');
                    hex_digits_expected -= 2 * reg_subrange_skip_lo;
                }
                size_t data_start_pos = contents.size();
                while (contents.size() < hex_digits_expected) {
                    if (tok.iseol() &&
                        contents.find_first_not_of('0', data_start_pos) ==
                            string::npos) {
                        // Special case: if the line ends with fewer
                        // hex digits than expected, but all the
                        // digits we've seen are zero, then we assume
                        // that the Tarmac producer abbreviated a zero
                        // value on the grounds that it was boring.
                        // This is seen in Neoverse-N1 RTL, for example.
                        contents.append(hex_digits_expected - contents.size(),
                                        '0');
                        break;
                    }
                    if (!tok.isregvalue())
                        parse_error(tok, _("expected register contents"));
                    consume_register_contents(tok);
                    tok = lex();

                    if (tok == ':')
                        tok = lex();
                }
                if (got_reg_subrange)
                    contents.append(2 * reg_subrange_skip_lo, '-');
            } else if (special) {
                // Special cases described above, where we have to wait to see
                // how much data we can get out of the input line.
                //
                // In all cases of this so far encountered, it's enough to read
                // a single contiguous token of register contents, plus a
                // second one if a ':' follows it.
                if (!tok.isregvalue())
                    parse_error(tok, _("expected register contents"));
                consume_register_contents(tok);
                tok = lex();

                if (tok == ':') {
                    tok = lex();
                    if (!tok.isregvalue())
                        parse_error(tok, _("expected additional register "
                                           "contents after ':'"));
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

                if (is_cpsr) {
                    // If the special register was CPSR, we treat it as 32-bit
                    // always. But some Tarmac generators use the register name
                    // "cpsr" to represent a combination of the literal CPSR
                    // and some bits which are really from other status
                    // registers. Also, they don't reliably print the same
                    // number of digits every time. So we tolerate an arbitrary
                    // number of hex digits, and normalise to the low 32 bits.

                    if (contents.size() < 8)
                        contents =
                            std::string(8 - contents.size(), '0') + contents;
                    contents = contents.substr(contents.size() - 8);
                }
            }

            // Fast Models puts nothing further on a register line. Other
            // producers may add trailing annotations, e.g. helpfully
            // interpreting the hex CPSR value to show the individual NZCV
            // flags.

            unsigned bits = contents.size() * 4;

            vector<uint16_t> bytes;
            if (bits % 8 != 0)
                parse_error(tok, _("expected register contents to be an integer"
                                   " number of bytes"));
            for (unsigned pos = 0; pos < bits / 4; pos += 2) {
                string hex = contents.substr(pos, 2);
                if (hex == "--") {
                    // Special value indicating an unknown byte, in flavours of
                    // Tarmac that include partial register updates.
                    bytes.push_back(UNKNOWN);
                } else {
                    bytes.push_back(stoul(hex, NULL, 16));
                }
            }

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
                    warning(format(_("unrecognised {0}-bit register '{1}'"),
                                   bits, regname));
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

            // Now go through 'bytes' and find maximal contiguous subsequences
            // of non-UNKNOWN values, and emit each one as a RegisterEvent.
            for (size_t offset = 0; offset < bytes.size() ;) {
                if (bytes[offset] == UNKNOWN) {
                    offset++;
                } else {
                    size_t start = offset;
                    vector<uint8_t> realbytes;
                    while (offset < bytes.size() && bytes[offset] != UNKNOWN)
                        realbytes.push_back(bytes[offset++]);
                    RegisterEvent ev(time, reg, start, realbytes);
                    receiver->got_event(ev);
                }
            }
        } else if ((tok.isword() && tok.s.substr(0, 1) == "M") ||
                   tok == "R01" || tok == "R02" || tok == "R04" ||
                   tok == "R08" || tok == "W01" || tok == "W02" ||
                   tok == "W04" || tok == "W08") {
            // Contiguous memory access event.

            const Token firsttok = tok;

            bool seen_rw = false, read = false;
            bool seen_size = false;
            bool expect_memory_order = false;
            size_t size = 0;

            for (size_t pos = 0, end = tok.s.size(); pos < end ;) {
                size_t prevpos = pos;
                char c = tok.s[pos++];

                if (!seen_rw && (c == 'R' || c == 'W')) {
                    seen_rw = true;
                    read = (c == 'R');
                } else if (!seen_size && isdigit((unsigned char)c)) {
                    while (pos < end && isdigit((unsigned char)tok.s[pos]))
                        pos++;
                    seen_size = true;
                    size = stoull(tok.s.substr(prevpos, pos));
                } else if (pos == 8 && end == 8 && (c == 'I' || c == 'A')) {
                    // Memory access events in Cortex-M4 RTL end in a flag
                    // indicating whether the access is data (D), instruction
                    // (I) or a peripheral bus (A). We ignore all but D, by
                    // treating them as text-only events, because I observe
                    // that they have confusing endianness.
                    highlight(firsttok.startpos, line.size(), HL_TEXT_EVENT);
                    TextOnlyEvent ev(time, tok.s,
                                     line.substr(firsttok.startpos));
                    receiver->got_event(ev);
                    return;
                } else if (pos == 8 && end == 8 && (c == 'D')) {
                    // This is a data-bus access in the Cortex-M4 RTL style.
                    // These also differ from the more usual style of
                    // contiguous memory access event in that the bytes are
                    // written in memory order, instead of logical order for
                    // the word being transferred.
                    expect_memory_order = true;
                }
            }
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
                parse_error(tok, _("expected memory address"));
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
                    parse_error(tok, _("expected physical memory address "
                                       "after ':'"));
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
                        parse_error(tok2, _("expected closing parenthesis"));
                    highlight(tok.startpos, line.size(), HL_TEXT_EVENT);
                    TextOnlyEvent ev(time, tok.s,
                                     line.substr(firsttok.startpos));
                    receiver->got_event(ev);
                    return;
                } else {
                    parse_error(tok, _("unrecognised parenthesised keyword"));
                }
            }

            // The value transferred to/from the memory is broken up
            // by underscores if it's more than 8 bytes long. We want
            // to retrieve it as a single integer, so we just strip
            // those out.
            tok.remove_chars("_");
            if (!tok.ishex())
                parse_error(tok, _("expected memory contents in hex"));
            uint64_t contents = tok.hexvalue();

            if (expect_memory_order && !params.bigend) {
                // If we're looking at a memory access we believe to be in
                // memory order (i.e. byte at lowest address is written first),
                // and we believe it's a trace of a little-endian system, then
                // we have to byte-reverse our data so that it ends up in
                // logical order.
                uint64_t new_contents = 0;
                for (unsigned i = 0; i < size; i++) {
                    uint64_t byte = 0xFF & (contents >> (i*8));
                    new_contents |= byte << ((size-i-1) * 8);
                }
                contents = new_contents;
            }

            MemoryEvent ev(time, read, size, addr, true, contents);
            receiver->got_event(ev);
        } else if (tok == "LD" || tok == "ST") {
            // Diagrammatic memory access event.

            next_line.event_type_is_continuable = true;
            next_line.event_type_token = tok;

            bool read = (tok == "LD");
            tok = lex();

            next_line.post_event_type_start = tok.startpos;

            // Expect a hex address.
            if (!tok.ishex())
                parse_error(tok, _("expected load/store memory address"));
            uint64_t baseaddr = tok.hexvalue();
            tok = lex();

            // Now expect a collection of words covering 16 bytes of
            // memory starting at the given base address. These words
            // may contain hex digits, dots and sometimes '#' to
            // indicate an actually unknown value.
            uint16_t bytes[16];
            int bytepos = 0;

            while (true) {
                if (!tok.isword("0123456789ABCDEFabcdef.#"))
                    parse_error(tok, _("expected a word of data bytes, "
                                       "'.' and '#'"));
                if (tok.s.size() % 2)
                    parse_error(tok, _("expected data word to cover a "
                                       "whole number of bytes"));
                for (size_t i = 0; i < tok.s.size(); i += 2) {
                    Token bytetok(tok.s.substr(i, 2));
                    bytetok.startpos += i;
                    bytetok.endpos = bytetok.startpos + 2;

                    if (bytepos >= 16)
                        parse_error(bytetok,
                                    _("expected exactly 16 data bytes"));

                    if (bytetok == "..")
                        bytes[bytepos] = UNUSED;
                    else if (bytetok == "##")
                        bytes[bytepos] = UNKNOWN;
                    else if (bytetok.ishex())
                        bytes[bytepos] = bytetok.hexvalue();
                    else
                        parse_error(bytetok, _("expected each byte to be only "
                                               "one of '.', '#' and hex"));

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
                    if (params.bigend)
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
                    warning(format(_("unknown Tarmac event type '{}'"), type));
                }
            }

            tok = lex();
            highlight(tok.startpos, line.size(), HL_TEXT_EVENT);

            TextOnlyEvent ev(time, type, line.substr(tok.startpos));
            receiver->got_event(ev);
        }
    }
};

TarmacLineParser::TarmacLineParser(const ParseParams &params,
                                   ParseReceiver &rec)
    : pImpl(new TarmacLineParserImpl(params, &rec))
{
}

TarmacLineParser::~TarmacLineParser() { delete pImpl; }

void TarmacLineParser::parse(const string &s) const { pImpl->parse(s); }

set<string> TarmacLineParserImpl::known_timestamp_units = {
    "clk", "ns", "cs", "cyc", "tic", "ps",
};
