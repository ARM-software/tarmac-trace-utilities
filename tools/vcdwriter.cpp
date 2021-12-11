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

#include "libtarmac/calltree.hh"
#include "libtarmac/parser.hh"
#include "libtarmac/registers.hh"

#include "vcd.hh"
#include "vcdwriter.hh"

#include <cassert>
#include <functional>

using std::ref;
using std::string;
using std::vector;

namespace {
string trimSpacesAndComment(const string &str)
{
    string s(str);

    // Remove the comment if any
    size_t sc = s.find(';', 0);
    if (sc != string::npos)
        s.erase(sc);

    // Trim white spaces at the end
    sc = s.find_last_not_of(" \t");
    if (sc != string::npos)
        s.erase(sc + 1);

    // Collapse multiple spaces.
    size_t b = 0;
    do {
        b = s.find_first_of(" \t", b);
        if (b != string::npos) {
            size_t e = s.find_first_not_of(" \t", b + 1);
            if (e > b + 1)
                s.erase(b + 1, e - b - 1);
            b++;
        }
    } while (b != string::npos);

    return s;
}

struct CPUDescription {
    struct RegisterDesc {
        RegisterId RegId;
        string Name;
        size_t Size; // Register size, in bytes.
        VCD::VCDSignalIndex VCDIdx;
        RegisterDesc(VCD::VCDFile &VCD, RegPrefix Prefix, unsigned Index)
            : RegId{Prefix, Index}, Name(reg_name(RegId)), Size(reg_size(RegId))
        {
            switch (Prefix) {
            case RegPrefix::r:
            case RegPrefix::w:
            case RegPrefix::x:
            case RegPrefix::wsp:
            case RegPrefix::xsp:
            case RegPrefix::psr:
            case RegPrefix::fpscr:
            case RegPrefix::fpcr:
            case RegPrefix::fpsr:
            case RegPrefix::internal_flags:
                VCDIdx = VCD.addIntSignal(Name, 8 * Size);
                break;
            case RegPrefix::d:
            case RegPrefix::s:
                // Although this is a floating point value, we prefer to
                // manpulate then as integers as this ensure bit accuracy in the
                // displayed waveforms.
                VCDIdx = VCD.addIntSignal(Name, 8 * Size);
                break;
            case RegPrefix::v:
            case RegPrefix::q:
            case RegPrefix::vpr:
                // Not yet used by any CPUDescription::getFoo() function.
                break;
            }
        }
    };

    vector<RegisterDesc> CoreRegs;   // Core registers
    vector<RegisterDesc> DoubleRegs; // Double registers
    vector<RegisterDesc> SingleRegs; // Single registers
    unsigned DataBusSize;
    unsigned AddressBusSize;

    CPUDescription() = delete;
    CPUDescription(const CPUDescription &) = default;
    CPUDescription &operator=(const CPUDescription &) = default;

    static CPUDescription getV7M(VCD::VCDFile &VCD)
    {
        CPUDescription CPU(16, 16, 32, 32, 32);

        for (unsigned i = 0; i < 15; i++)
            CPU.CoreRegs.emplace_back(VCD, RegPrefix::r, i);
        CPU.CoreRegs.emplace_back(VCD, RegPrefix::psr, 0);

        for (unsigned i = 0; i < 16; i++)
            CPU.DoubleRegs.emplace_back(VCD, RegPrefix::d, i);

        for (unsigned i = 0; i < 32; i++)
            CPU.SingleRegs.emplace_back(VCD, RegPrefix::s, i);

        return CPU;
    }

    static CPUDescription getV8A(VCD::VCDFile &VCD)
    {
        CPUDescription CPU(33, 32, 32, 64, 64);

        for (unsigned i = 0; i < 31; i++)
            CPU.CoreRegs.emplace_back(VCD, RegPrefix::x, i);
        CPU.CoreRegs.emplace_back(VCD, RegPrefix::xsp, 0);
        CPU.CoreRegs.emplace_back(VCD, RegPrefix::psr, 0);

        for (unsigned i = 0; i < 32; i++)
            CPU.DoubleRegs.emplace_back(VCD, RegPrefix::d, i);

        // Although not strictly necessary, we are having separate loops here
        // because this alllows registers to be grouped by type in the GUI,
        // easing user interactions.
        for (unsigned i = 0; i < 32; i++)
            CPU.SingleRegs.emplace_back(VCD, RegPrefix::s, i);

        return CPU;
    }

  private:
    CPUDescription(unsigned NumCoreRegs, unsigned NumDoubleRegs,
                   unsigned NumSingleRegs, unsigned DataBusSize,
                   unsigned AddressBusSize)
        : CoreRegs(), DoubleRegs(), SingleRegs(), DataBusSize(DataBusSize),
          AddressBusSize(AddressBusSize)
    {
        CoreRegs.reserve(NumCoreRegs);
        DoubleRegs.reserve(NumDoubleRegs);
        SingleRegs.reserve(NumSingleRegs);
    }
};

class VCDVisitor : public ParseReceiver {
    struct FunctionChange {
        Time Cycle;
        string Name;
        FunctionChange(Time t, const string &n) : Cycle(t), Name(n) {}
    };

    struct MemoryAccess {
        Addr Address;
        unsigned long long Data;
        bool Read;
        MemoryAccess(Addr Address, unsigned long long Data, bool Read)
            : Address(Address), Data(Data), Read(Read)
        {
        }
    };

    class FCVisitor : public CallTreeVisitor {
        vector<FunctionChange> &FC;

      public:
        FCVisitor(const CallTree &CT, vector<FunctionChange> &FC)
            : CallTreeVisitor(CT), FC(FC)
        {
        }
        void onFunctionEntry(const TarmacSite &function_entry,
                             const TarmacSite &function_exit)
        {
            FC.emplace_back(function_entry.time,
                            CT.getFunctionName(function_entry));
        }
        void onResumeSite(const TarmacSite &function_entry,
                          const TarmacSite &function_exit,
                          const TarmacSite &resume_site)
        {
            FC.emplace_back(resume_site.time,
                            CT.getFunctionName(function_entry));
        }
    };

  public:
    VCDVisitor(VCD::VCDFile &VCD, IndexNavigator &IN)
        : TLP(IN.index.isBigEndian(), *this), VCD(VCD), IN(IN),
          CPU(IN.index.isAArch64() ? CPUDescription::getV8A(VCD)
                                   : CPUDescription::getV7M(VCD)),
          Functions(), Cycle(VCD.addIntSignal("Cycle", 32)),
          Function(VCD.addTextSignal("Function")),
          Inst(VCD.addIntSignal("Inst", 32)),
          InstAsm(VCD.addTextSignal("InstAsm")),
          InstExecuted(VCD.addBoolSignal("InstExecuted")),
          PC(VCD.addIntSignal("PC", CPU.AddressBusSize)),
          MemReadWrite(VCD.addTextSignal("MemRW")),
          MemAddr(VCD.addIntSignal("MemAddr", CPU.AddressBusSize)),
          MemData(VCD.addIntSignal("MemData", CPU.DataBusSize)), Tick(0),
          PrevInstExecuted(), PrevInstPC(-1), PrevInst(-1),
          hadMemoryAccesses(false)
    {
        CallTree CT(IN);
        FCVisitor FCV(CT, Functions);
        CT.rvisit(FCV);
        VCD.writeVariableDefinition();
        VCD.writeVCDStart();
        // If there were any initial state to write, this should be done here,
        // in between the call to writeVCDStart and the first writeTime.
    }

    void operator()(const SeqOrderPayload &sop, off_t)
    {
        tick();

        // Output current simulation cycle.
        VCD.writeValueChange<long unsigned int>(Cycle, sop.mod_time);
        vector<std::string> lines = IN.index.get_trace_lines(sop);
        for (const string &line : lines) {
            try {
                TLP.parse(line);
            } catch (TarmacParseError err) {
                // Ignore parse failures; we just leave the output event
                // fields set to null.
            }
        }
        // Output the name of the current function if it changed.
        if (!Functions.empty() && Functions.back().Cycle == sop.mod_time) {
            VCD.writeValueChange(Function, Functions.back().Name);
            Functions.pop_back();
        }

        // Let's find the updated registers.
        unsigned iflags = IN.get_iflags(sop.memory_root);
        findRegisterChanges(CPU.CoreRegs, iflags, sop);
        findRegisterChanges(CPU.SingleRegs, iflags, sop);
        findRegisterChanges(CPU.DoubleRegs, iflags, sop);

        // And the memory accesses...
        if (hadMemoryAccesses && MemoryAccesses.empty()) {
            VCD.writeValueChange(MemReadWrite,
                                 VCD::VCDSignal::ExtraState::TriState);
            VCD.writeValueChange(MemAddr, VCD::VCDSignal::ExtraState::TriState);
            VCD.writeValueChange(MemData, VCD::VCDSignal::ExtraState::TriState);
        }
        hadMemoryAccesses = !MemoryAccesses.empty();

        while (!MemoryAccesses.empty()) {
            MemoryAccess &M = MemoryAccesses.back();
            VCD.writeValueChange(MemReadWrite, M.Read ? "R" : "W");
            VCD.writeValueChange<long unsigned int>(MemAddr, M.Address);
            VCD.writeValueChange<long unsigned int>(MemData, M.Data);
            MemoryAccesses.pop_back();
            if (!MemoryAccesses.empty())
                tick();
        }

        // Note : it would be good reorder the register updates / memory
        // accesses so they display nicely.
    }

    virtual void got_event(MemoryEvent &ev) override
    {
        MemoryAccesses.emplace_back(ev.addr, ev.contents, ev.read);
    }

    virtual void got_event(InstructionEvent &ev) override
    {
        if (PrevInstExecuted != ev.executed) {
            VCD.writeValueChange(InstExecuted, ev.executed);
            PrevInstExecuted = ev.executed;
        }

        if (PrevInstPC != ev.pc) {
            VCD.writeValueChange<long unsigned int>(PC, ev.pc);
            PrevInstPC = ev.pc;
        }

        if (PrevInst != ev.instruction) {
            VCD.writeValueChange<long unsigned int>(Inst, ev.instruction);
            VCD.writeValueChange(InstAsm, trimSpacesAndComment(ev.disassembly));
            PrevInst = ev.instruction;
        }
    }

    void finish() { tick(); }

  private:
    TarmacLineParser TLP;
    VCD::VCDFile &VCD;
    IndexNavigator &IN;
    const CPUDescription CPU;
    vector<FunctionChange> Functions;
    vector<MemoryAccess> MemoryAccesses;
    const VCD::VCDSignalIndex Cycle;
    const VCD::VCDSignalIndex Function;
    const VCD::VCDSignalIndex Inst;
    const VCD::VCDSignalIndex InstAsm;
    const VCD::VCDSignalIndex InstExecuted;
    const VCD::VCDSignalIndex PC;
    const VCD::VCDSignalIndex MemReadWrite;
    const VCD::VCDSignalIndex MemAddr;
    const VCD::VCDSignalIndex MemData;
    unsigned long long Tick;

    bool PrevInstExecuted;
    Addr PrevInstPC;
    unsigned PrevInst;
    bool hadMemoryAccesses;

    void tick()
    {
        VCD.writeTime(Tick);
        Tick += 1;
    }

    void
    findRegisterChanges(const vector<CPUDescription::RegisterDesc> &RegBank,
                        unsigned iflags, const SeqOrderPayload &sop)
    {
        for (const auto &R : RegBank) {
            Addr roffset = reg_offset(R.RegId, iflags);
            Addr diff_lo, diff_hi;
            if (IN.find_next_mod(sop.memory_root, 'r', roffset,
                                 sop.trace_file_firstline, +1, diff_lo,
                                 diff_hi) &&
                diff_lo < roffset + R.Size) {
                vector<unsigned char> val(R.Size);
                if (IN.get_reg_bytes(sop.memory_root, R.RegId, val))
                    VCD.writeValueChange(R.VCDIdx, [&val](unsigned bit) {
                        return bit / 8 < val.size()
                                   ? (1 & (val[bit / 8] >> (bit % 8)))
                                   : 0;
                    });
            }
        }
    }
};

} // namespace

void VCDWriter::run(const string &VCDFilename, bool NoDate)
{
    VCD::VCDFile VCD("CPU", VCDFilename, NoDate);
    VCD.setComment("Generated by tarmac-vcd.");
    VCD.setVersion("tarmac-vcd 0.0");
    VCD.writeHeader();

    VCDVisitor V(VCD, *this);
    index.seqtree.visit(index.seqroot, ref(V));
    V.finish();
}

#include "libtarmac/argparse.hh"
#include "libtarmac/reporter.hh"
#include "libtarmac/tarmacutil.hh"

std::unique_ptr<Reporter> reporter = make_cli_reporter();

int main(int argc, char **argv)
{
    string vcd_filename("");
    bool no_date = false;

    Argparse ap("tarmac-vcd", argc, argv);
    TarmacUtility tu(ap);

    ap.optval({"-o", "--output"}, "VCDFILE",
              "VCD file name (default: tarmac_filename.vcd)",
              [&](const string &s) { vcd_filename = s; });
    ap.optnoval({"--no-date"}, "Do not emit the date field in the vcd file",
                [&]() { no_date = true; });

    ap.parse();
    tu.setup();

    if (vcd_filename.size() == 0)
        vcd_filename = tu.trace.tarmac_filename + ".vcd";

    VCDWriter VW(tu.trace, tu.image_filename);
    VW.run(vcd_filename, no_date);

    return 0;
}
