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

#include "vcd.hh"

#include <cassert>
#include <cstdio>
#include <ctime>

using namespace VCD;
using std::ofstream;
using std::string;
using std::to_string;

void VCDSignal::write(VCDFile &VCD) const
{
    string Var;
    switch (Ty) {
    case Type::Int:
        Var = "integer";
        break;
    case Type::Text:
        Var = "string";
        break;
    case Type::Float:
        Var = "real";
        break;
    case Type::Bool:
        Var = "bit";
        break;
    }
    Var += ' ';
    if (Ty == Type::Int)
        Var += to_string(BitWidth);
    else
        Var += '1';
    Var += ' ';
    Var += Repr;
    Var += ' ';
    Var += Name;
    VCD.addVCDTextSection("var", Var, /* withCR= */ false);
}

void VCDSignal::writeValueChange(VCDFile &VCD, bool b) const
{
    string V(b ? "1" : "0");
    VCD.writeValueChange(V + Repr);
}

static string VCDEscapeString(const string &str)
{
    string Res;

    for (char c : str) {
        switch (c) {
        case '\a':
            Res += "\\a";
            break;
        case '\b':
            Res += "\\b";
            break;
        case '\f':
            Res += "\\f";
            break;
        case '\n':
            Res += "\\n";
            break;
        case '\r':
            Res += "\\r";
            break;
        case '\t':
            Res += "\\t";
            break;
        case '\v':
            Res += "\\v";
            break;
        case '\'':
            Res += "\\'";
            break;
        case '\"':
            Res += "\\\"";
            break;
        case '\\':
            Res += "\\\\";
            break;
        case '\?':
            Res += "\\\?";
            break;
        default:
            if ((c > ' ') && (c <= '~'))
                Res += c;
            else {
                Res += '\\';
                Res += '0' + (c / 64);
                c &= 63;
                Res += '0' + (c / 8);
                c &= 7;
                Res += '0' + c;
            }
            break;
        }
    }

    return Res;
}

void VCDSignal::writeValueChange(VCDFile &VCD, const char *str) const
{
    string V("s");
    VCD.writeValueChange(V + VCDEscapeString(str) + ' ' + Repr);
}

void VCDSignal::writeValueChange(VCDFile &VCD, const std::string &str) const
{
    string V("s");
    VCD.writeValueChange(V + VCDEscapeString(str) + ' ' + Repr);
}

// The double flavour of writeValueChange makes use of the VCD specification
// specified double -> string conversion using format ".16g".
// Unfortunately, this is not enough to distinguish all double precision
// values. Do *NOT* use this routine if you need bitlevel accurate
// conversions.
void VCDSignal::writeValueChange(VCDFile &VCD, double d) const
{
    string V("r");
    char real[32];
    snprintf(real, sizeof(real), "%.16g", d);
    VCD.writeValueChange(V + real + ' ' + Repr);
}

void VCDSignal::writeValueChange(VCDFile &VCD,
                                 std::function<bool(unsigned)> bit) const
{
    string V("b");
    string bits(BitWidth, '0');
    for (unsigned i = 0; i < BitWidth; i++)
        bits[BitWidth - 1 - i] = '0' + bit(i);
    VCD.writeValueChange(V + bits + ' ' + Repr);
}

void VCDSignal::writeValueChange(VCDFile &VCD, unsigned long long u) const
{
    writeValueChange(VCD,
                     [u](unsigned n) { return n < 64 ? (1 & (u >> n)) : 0; });
}

void VCDSignal::writeValueChange(VCDFile &VCD, unsigned long u) const
{
    writeValueChange(VCD, static_cast<unsigned long long>(u));
}

void VCDSignal::writeValueChange(VCDFile &VCD, ExtraState st) const
{
    switch (Ty) {
    case Type::Text:
        switch (st) {
        case ExtraState::TriState:
            writeValueChange(VCD, "");
            break;
        case ExtraState::Undef:
            assert(
                0 &&
                "Don't know yet how to emit ExtraState::Undef value for text "
                "type VCD signals.");
        }
        break;

    case Type::Int: {
        string V("b");
        switch (st) {
        case ExtraState::TriState:
            V += string(BitWidth, 'z');
            break;
        case ExtraState::Undef:
            V += string(BitWidth, 'u');
            break;
        }
        VCD.writeValueChange(V + ' ' + Repr);
        break;
    }

    case Type::Bool:
        switch (st) {
        case ExtraState::TriState:
            VCD.writeValueChange('z' + Repr);
            break;
        case ExtraState::Undef:
            VCD.writeValueChange('x' + Repr);
            break;
        }
        break;

    case Type::Float:
        assert(0 && "Don't know yet how to emit ExtraState values for float "
                    "type VCD signals.");
        break;
    };
}

void VCDScope::write(VCDFile &VCD) const
{
    string Module("module ");
    Module += ModuleName;
    VCD.addVCDTextSection("scope", Module, /* withCR= */ false);
    for (VCDSignalIndex Idx : Signals)
        VCD.writeSignalDefinition(Idx);
    VCD.addVCDKeywords("upscope", "end");
}

string VCDFile::chomp(string str)
{
    string::size_type pos = str.find_last_of("\n");
    if (pos != string::npos)
        str.erase(str.find_last_of("\n"));
    return str;
}

VCDFile::VCDFile(const string &ModuleName, const string &Filename, bool NoDate)
    : Output(Filename.c_str(), ofstream::out), Date(), Version(), Comment(),
      Timescale(TimeScale::NS), VariableDefinition(ModuleName), Signals()
{
    time_t tt;
    time(&tt);
    struct tm *ti = localtime(&tt);

    if (!NoDate)
        Date = chomp(asctime(ti));
}

VCDFile::~VCDFile() { addVCDKeyword("end"); }

// The identifier is the printable ASCII characters: ! to ~ (decimal 33 to 126).
static string getVCDRepr(unsigned Id)
{
    string Repr;
    const unsigned char R = '~' - '!' + 1;
    do {
        unsigned char c = '!' + (Id % R);
        Repr += c;
        Id /= R;
    } while (Id != 0);
    return Repr;
}

VCDSignalIndex VCDFile::addSignal(const VCDSignal &Sig)
{
    unsigned Idx = Signals.size();
    Signals.push_back(Sig);
    Signals.back().setVCDRepr(getVCDRepr(Idx));
    VariableDefinition.addSignal(Idx);
    return Idx;
}

void VCDFile::writeHeader()
{
    if (Date.size())
        addVCDTextSection("date", Date);

    if (Version.size())
        addVCDTextSection("version", Version);

    if (Comment.size())
        addVCDTextSection("comment", Comment);

    switch (Timescale) {
    case TimeScale::PS:
        addVCDTextSection("timescale", "1ps", /* withCR= */ false);
        break;
    case TimeScale::NS:
        addVCDTextSection("timescale", "1ns", /* withCR= */ false);
        break;
    case TimeScale::US:
        addVCDTextSection("timescale", "1us", /* withCR= */ false);
        break;
    case TimeScale::MS:
        addVCDTextSection("timescale", "1ms", /* withCR= */ false);
        break;
    case TimeScale::S:
        addVCDTextSection("timescale", "1s", /* withCR= */ false);
        break;
    }
}

void VCDFile::writeVariableDefinition()
{
    VariableDefinition.write(*this);
    addVCDKeywords("enddefinitions", "end");
}

void VCDFile::writeVCDStart() { Output << "$dumpvars" << '\n'; }

void VCDFile::writeTime(unsigned long t) { Output << '#' << t << '\n'; }
