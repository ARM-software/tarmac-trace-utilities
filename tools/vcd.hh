/*
 * Copyright 2016-2021,2025 Arm Limited. All rights reserved.
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

#ifndef TARMAC_VCD_HH
#define TARMAC_VCD_HH

#include <fstream>
#include <functional>
#include <string>
#include <vector>

namespace VCD {

typedef unsigned long VCDSignalIndex;

class VCDFile;

class VCDSignal {
    friend VCDFile;

  public:
    enum class Type { Text, Float, Int, Bool };
    enum class ExtraState { TriState, Undef };

    VCDSignal() : Name(), Repr(), Ty(Type::Int), BitWidth(1) {}
    VCDSignal(const VCDSignal &) = default;
    VCDSignal(const std::string &Name, Type Ty, unsigned BitWidth = 1)
        : Name(Name), Repr(), Ty(Ty), BitWidth(BitWidth)
    {
    }

    VCDSignal &operator=(const VCDSignal &) = default;

    void setVCDRepr(const std::string &str) { Repr = str; }

  private:
    std::string Name;
    std::string Repr; // The VCD string representation for this signal.
    Type Ty;
    unsigned BitWidth;

    void write(VCDFile &VCD) const;
    void writeValueChange(VCDFile &VCD, bool b) const;
    void writeValueChange(VCDFile &VCD, const std::string &str) const;
    void writeValueChange(VCDFile &VCD, const char *str) const;
    // The double flavour of writeValueChange makes use of the VCD specification
    // specified double -> string conversion using format ".16g".
    // Unfortunately, this is not enough to distinguish all double precision
    // values. Do *NOT* use this routine if you need bitlevel accurate
    // conversions.
    void writeValueChange(VCDFile &VCD, double d) const;
    void writeValueChange(VCDFile &VCD, std::function<bool(unsigned)>) const;
    void writeValueChange(VCDFile &VCD, unsigned long long u) const;
    void writeValueChange(VCDFile &VCD, unsigned long u) const;
    void writeValueChange(VCDFile &VCD, ExtraState st) const;
};

class VCDScope {
    friend VCDFile;

  public:
    VCDScope() = default;
    VCDScope(const VCDScope &) = default;
    VCDScope &operator=(const VCDScope &) = default;
    VCDScope(const std::string &ModuleName)
        : ModuleName(ModuleName), Signals(), SubScopes()
    {
    }

    VCDScope setName(const std::string &Name)
    {
        ModuleName = Name;
        return *this;
    }

    void addSignal(VCDSignalIndex SignalIdx) { Signals.push_back(SignalIdx); }

    VCDScope &addVCDScope(const std::string &ModuleName)
    {
        SubScopes.push_back(VCDScope(ModuleName));
        return SubScopes.back();
    }

  private:
    std::string ModuleName;
    std::vector<VCDSignalIndex> Signals;
    std::vector<VCDScope> SubScopes;

    void write(VCDFile &VCD) const;
};

class VCDFile {
    friend VCDSignal;
    friend VCDScope;

  public:
    enum class TimeScale { FS, PS, NS, US, MS, S };

    VCDFile() = delete;
    VCDFile(const VCDFile &) = delete;
    VCDFile(const std::string &ModuleName, const std::string &Filename,
            bool NoDate);
    VCDFile &operator=(const VCDFile &) = delete;
    ~VCDFile();

    void setDate(const char *D) { Date = chomp(D); }
    void setVersion(const char *V) { Version = chomp(V); }
    void setComment(const char *C) { Comment = chomp(C); }
    void setModuleName(const char *C) { Comment = chomp(C); }
    void setTimescale(TimeScale TS) { Timescale = TS; }

    VCDSignalIndex addBoolSignal(const char *Name)
    {
        return addSignal(VCDSignal(Name, VCDSignal::Type::Bool, 1));
    }
    VCDSignalIndex addBoolSignal(const std::string &Name)
    {
        return addBoolSignal(Name.c_str());
    }

    VCDSignalIndex addTextSignal(const char *Name)
    {
        return addSignal(VCDSignal(Name, VCDSignal::Type::Text, 1));
    }
    VCDSignalIndex addTextSignal(const std::string &Name)
    {
        return addTextSignal(Name.c_str());
    }

    VCDSignalIndex addIntSignal(const char *Name, unsigned BitWidth)
    {
        return addSignal(VCDSignal(Name, VCDSignal::Type::Int, BitWidth));
    }
    VCDSignalIndex addIntSignal(const std::string &Name, unsigned BitWidth)
    {
        return addIntSignal(Name.c_str(), BitWidth);
    }

    VCDSignalIndex addFloatSignal(const char *Name, unsigned BitWidth)
    {
        return addSignal(VCDSignal(Name, VCDSignal::Type::Float, BitWidth));
    }
    VCDSignalIndex addFloatSignal(const std::string &Name, unsigned BitWidth)
    {
        return addFloatSignal(Name.c_str(), BitWidth);
    }

    VCDScope &addVCDScope(const std::string &ModuleName)
    {
        return VariableDefinition.addVCDScope(ModuleName);
    }

    void writeHeader();
    void writeVariableDefinition();
    void writeVCDStart();
    void writeTime(unsigned long t);

    template <typename Ty> void writeValueChange(VCDSignalIndex SigIdx, Ty Val)
    {
        Signals[SigIdx].writeValueChange(*this, Val);
    }

  private:
    std::ofstream Output;
    std::string Date;
    std::string Version;
    std::string Comment;
    TimeScale Timescale;
    VCDScope VariableDefinition;
    std::vector<VCDSignal> Signals;

    static std::string chomp(std::string str);
    VCDSignalIndex addSignal(const VCDSignal &Signal);

    void writeValueChange(const std::string &v) { Output << v << '\n'; }

    void writeSignalDefinition(VCDSignalIndex Idx)
    {
        Signals[Idx].write(*this);
    }

    void addVCDKeyword(const char *kw, bool withCR = true)
    {
        Output << '$' << kw;
        if (withCR)
            Output << '\n';
    }
    void addVCDKeywords(const char *kw1, const char *kw2)
    {
        Output << '$' << kw1 << " $" << kw2 << '\n';
    }

    void addVCDTextSection(const char *kw, const std::string &text,
                           bool withCR = true)
    {
        addVCDKeyword(kw, withCR);
        if (!withCR)
            Output << ' ';
        Output << text;
        Output << (withCR ? '\n' : ' ');
        addVCDKeyword("end", true);
    }
};
} // namespace VCD

#endif // TARMAC_VCD_HH
