// SPDX-License-Identifier: GPL-3.0-or-later
// Synthetic regressions for upstream melonDS #2747 and #2782. No game files.
#include <array>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <string>
#include "NDS.h"
#include "ARMJIT_Internal.h"
#include "ARMJIT_Global.h"
#include "Savestate.h"
#ifndef _WIN32
#include <csignal>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace melonDS
{
// Keep fault-handler internals private while exercising the real rejection path.
struct JITRegressionAccess
{
    static bool RejectsDisabledFastmem(NDS& ds)
    {
        ARMJIT_Memory::FaultDescription fault {};
        fault.FaultPC = reinterpret_cast<u8*>(ds.JIT.JitBlocks9.at(0x02002000)->EntryPoint);
        if (!ds.JIT.JITCompiler.IsJITFault(fault.FaultPC))
            throw std::runtime_error("fault fixture is outside generated code");
        fault.EmulatedFaultAddr = 0xFFFFFFFF;
        return !ARMJIT_Memory::FaultHandler(fault, ds);
    }
};
}

namespace
{
using namespace melonDS;
constexpr u32 Code = 0x02002000;
std::string Context;
void Check(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(Context + ": " + message);
}

struct Console
{
    std::unique_ptr<NDS> DS;
    NDS* Previous = NDS::Current;
    Console(bool jit, bool fastmem, bool folding = true, bool branches = true)
    {
        NDSArgs args;
        args.JIT = jit ? std::optional<JITArgs>(JITArgs{32, folding, branches, fastmem}) : std::nullopt;
        DS = std::make_unique<NDS>(std::move(args));
        NDS::Current = DS.get();
        DS->Reset();
        Check(DS->JIT.FastMemoryEnabled() == (jit && fastmem && ARMJIT_Memory::IsFastMemSupported()),
              "unexpected effective fastmem setting");
    }
    ~Console() { DS.reset(); NDS::Current = Previous; }
};

// Execute real guest instructions through the production CPU/JIT dispatcher.
// A terminal B . keeps execution in mapped RAM until the cycle target.
void Run(NDS& ds, unsigned num, bool jit, u32 entry, u32 flags = 0)
{
    ARM& cpu = num ? static_cast<ARM&>(ds.ARM7) : static_cast<ARM&>(ds.ARM9);
    for (unsigned i = 0; i < 15; ++i) cpu.R[i] = 0x20 + i;
    cpu.CPSR = 0xDF | flags;
    cpu.StopExecution = 0;
    cpu.Cycles = cpu.CodeCycles = cpu.DataCycles = 0;
    cpu.DataRegion = 0;
    cpu.JumpTo(entry);
    ds.CurCPU = num;
    if (num)
    {
        ds.ARM7Timestamp = 0;
        ds.ARM7Target = 2048;
        if (jit) ds.ARM7.Execute<CPUExecuteMode::JIT>();
        else ds.ARM7.Execute<CPUExecuteMode::Interpreter>();
    }
    else
    {
        ds.ARM9Timestamp = 0;
        ds.ARM9Target = 2048;
        if (jit) ds.ARM9.Execute<CPUExecuteMode::JIT>();
        else ds.ARM9.Execute<CPUExecuteMode::Interpreter>();
    }
}

void Literals()
{
    unsigned cases = 0;
    for (bool jit : {false, true})
    for (bool fastmem : {false, true})
    for (bool folding : {false, true})
    for (bool branches : {false, true})
    for (unsigned num : {0u, 1u})
    for (u32 alignment : {0u, 2u})
    {
        Context = "literal jit=" + std::to_string(jit) + " fastmem=" + std::to_string(fastmem)
            + " folding=" + std::to_string(folding) + " branches=" + std::to_string(branches)
            + " cpu=" + std::to_string(num) + " align=" + std::to_string(alignment);
        Console c(jit, fastmem, folding, branches);
        auto& ds = *c.DS;
        const u32 entry = Code + alignment;
        const u32 literal = Code + 0x100;
        ds.ARM7Write16(entry, 0x4800 | ((literal - ((entry + 4) & ~3u)) / 4)); // LDR r0,[pc,#imm]
        ds.ARM7Write16(entry + 2, 0xE7FE); // B .
        ds.ARM7Write32(literal, 0x11223344);
        const auto verify = [&](u32 expected) {
            for (int pass = 0; pass < 2; ++pass) // compilation, then native dispatch
            {
                Run(ds, num, jit, entry | 1);
                Check((num ? ds.ARM7.R[0] : ds.ARM9.R[0]) == expected, "Thumb literal returned stale/unaligned data");
                Check((num ? ds.ARM7.R[1] : ds.ARM9.R[1]) == 0x21, "Thumb load damaged an unrelated register");
            }
            ++cases;
        };
        verify(0x11223344);
        ds.ARM7Write32(literal, 0x55667788);
        verify(0x55667788);
        ds.ARM7Write16(literal + 2, 0x99AA);
        verify(0x99AA7788);
        ds.ARM7Write8(literal, 0xCC);
        verify(0x99AA77CC);
        // Round-trip production CPU serialization and RAM. Full console loads
        // also touch frontend devices (microphone), covered by game replays.
        Savestate saved;
        saved.Section("RAM.");
        saved.VarArray(ds.MainRAM, MainRAMMaxSize);
        ds.ARM9.DoSavestate(&saved);
        ds.ARM7.DoSavestate(&saved);
        saved.Finish();
        Check(!saved.Error, "save failed");
        ds.ARM7Write32(literal, 0xDEADBEEF);
        verify(0xDEADBEEF);
        Savestate loaded(saved.Buffer(), saved.Length(), false);
        loaded.Section("RAM.");
        loaded.VarArray(ds.MainRAM, MainRAMMaxSize);
        ds.ARM9.DoSavestate(&loaded);
        ds.ARM7.DoSavestate(&loaded);
        Check(!loaded.Error, "reload failed");
        ds.JIT.Reset();
        verify(0x99AA77CC);
    }
    std::printf("JIT literals: %u value checks, each compilation + cached execution\n", cases);
}

void Invalidation()
{
    unsigned cases = 0;
    for (bool fastmem : {false, true})
    for (unsigned num : {0u, 1u})
    for (unsigned width : {1u, 2u, 4u})
    {
        Console c(true, fastmem);
        Context = "invalidation cpu=" + std::to_string(num) + " width=" + std::to_string(width);
        auto& ds = *c.DS;
        const u32 other = Code + 0x40; // Same 512-byte range, disjoint 16-byte granule.
        for (u32 addr : {Code, other})
        {
            ds.ARM7Write32(addr, 0xE3A00001); // MOV r0,#1
            ds.ARM7Write32(addr + 4, 0xEAFFFFFE);
            Run(ds, num, true, addr);
            Run(ds, num, true, addr);
        }
        auto& blocks = num ? ds.JIT.JitBlocks7 : ds.JIT.JitBlocks9;
        Check(blocks.count(Code) && blocks.count(other), "fixtures did not compile");
        JitBlock* retained = blocks.at(other);
        if (width == 1) ds.ARM7Write8(Code, 2);
        else if (width == 2) ds.ARM7Write16(Code, 2);
        else ds.ARM7Write32(Code, 0xE3A00002);
        Check(!blocks.count(Code), "overlapping code was not invalidated");
        Check(blocks.count(other) && blocks.at(other) == retained, "disjoint block unnecessarily invalidated");
        Run(ds, num, true, Code);
        Check((num ? ds.ARM7.R[0] : ds.ARM9.R[0]) == 2, "modified instruction was not executed");
        Run(ds, num, true, other);
        Check((num ? ds.ARM7.R[0] : ds.ARM9.R[0]) == 1, "retained block changed behavior");
        ++cases;
    }
    std::printf("JIT invalidation: %u overlapping/disjoint byte, halfword and word cases\n", cases);
}

std::array<u32, 16> Registers(NDS& ds, unsigned num)
{
    ARM& cpu = num ? static_cast<ARM&>(ds.ARM7) : static_cast<ARM&>(ds.ARM9);
    std::array<u32, 16> result;
    std::copy(cpu.R, cpu.R + 15, result.begin());
    result[15] = cpu.CPSR;
    return result;
}

void BranchLayout(bool farCode, unsigned& cases)
{
    // Exhaust the small static pool so the normal allocator must use dynamic
    // code memory, reproducing the far-call layout from the Intel Mac report.
    struct CodeReservations
    {
        std::array<void*, 4> Memory {};
        explicit CodeReservations(bool reserve)
        {
#ifdef ARCHITECTURE_x86_64
            if (reserve)
                for (auto& p : Memory) p = ARMJIT_Global::AllocateCodeMem();
#endif
        }
        ~CodeReservations()
        {
            for (auto p : Memory) if (p) ARMJIT_Global::FreeCodeMem(p);
        }
    } reservations(farCode);
    for (bool fastmem : {false, true})
    for (bool branches : {false, true})
    for (unsigned num : {0u, 1u})
    for (u32 cond = 0; cond < 14; ++cond)
    {
        u32 flags = 0;
        while (ARM::ConditionTable[cond] & (1u << (flags >> 28))) flags += 1u << 28;
        std::array<u32, 16> reference;
        for (bool jit : {false, true})
        {
            Context = "branch cond=" + std::to_string(cond) + " cpu=" + std::to_string(num)
                + " jit=" + std::to_string(jit) + " dynamic=" + std::to_string(farCode)
                + " fastmem=" + std::to_string(fastmem) + " branches=" + std::to_string(branches);
            Console c(jit, fastmem, true, branches);
            auto& ds = *c.DS;
            // Dirty all general registers before a skipped conditional PC write.
            // Its emitted exit must spill them and can exceed a short Jcc.
            for (u32 i = 0; i < 15; ++i)
                ds.ARM7Write32(Code + i * 4, 0xE2800001 | (i << 16) | (i << 12));
            ds.ARM7Write32(Code + 60, (cond << 28) | 0x01B0F719); // MOV{cond}S pc,r9,lsl r7
            ds.ARM7Write32(Code + 64, 0xE2800003); // ADD r0,r0,#3
            ds.ARM7Write32(Code + 68, 0xEAFFFFFE);
            for (int pass = 0; pass < 3; ++pass)
            {
                Run(ds, num, jit, Code, flags);
                const auto actual = Registers(ds, num);
                Check(actual[0] == 0x24 && actual[14] == 0x2F, "conditional guest did not reach expected end");
                if (!jit) reference = actual;
                else Check(actual == reference, "conditional PC write differs from interpreter");
            }
            ++cases;
        }
    }
}

void Branches()
{
    unsigned cases = 0;
    for (bool farCode : {true, false}) BranchLayout(farCode, cases);
    std::printf("JIT branches: %u interpreter/JIT runs, 3 executions each, 14 conditions\n", cases);
}

void Faults()
{
    {
        Console c(true, false);
        // This is a real code address with an intentionally invalid guest fault
        // address. The fastmem-off guard must reject it before touching tables.
        c.DS->ARM7Write32(Code, 0xE3A00001);
        c.DS->ARM7Write32(Code + 4, 0xEAFFFFFE);
        Run(*c.DS, 0, true, Code);
        Check(JITRegressionAccess::RejectsDisabledFastmem(*c.DS), "fastmem-off fault was claimed");
    }
#ifndef _WIN32
    for (bool previousHandler : {false, true})
    {
        const pid_t child = fork();
        Check(child >= 0, "fork failed");
        if (child == 0)
        {
            signal(SIGSEGV, previousHandler ? +[](int) { _exit(0); } : SIG_DFL);
            Console c(true, false);
            raise(SIGSEGV); // Non-access signal must reach the previous/default handler.
            _exit(2);
        }
        int status = 0;
        Check(waitpid(child, &status, 0) == child, "waitpid failed");
        if (previousHandler) Check(WIFEXITED(status) && WEXITSTATUS(status) == 0, "previous signal handler was not called");
        else Check(WIFSIGNALED(status) && WTERMSIG(status) == SIGSEGV, "default signal action was not restored");
    }
    std::puts("JIT faults: fastmem-off rejection and POSIX previous/default handler tests passed");
#else
    std::puts("JIT faults: fastmem-off rejection passed; POSIX signal tests require macOS/Linux");
#endif
}
}

int RunJITRegressionTests(const char* group)
{
    setvbuf(stdout, nullptr, _IONBF, 0);
    try
    {
        if (!std::strcmp(group, "literals")) Literals();
        else if (!std::strcmp(group, "invalidation")) Invalidation();
        else if (!std::strcmp(group, "branches")) Branches();
        else if (!std::strcmp(group, "faults")) Faults();
        else throw std::runtime_error("unknown JIT test group");
        return 0;
    }
    catch (const std::exception& e)
    {
        std::fprintf(stderr, "JIT %s FAILED: %s\n", group, e.what());
        return 1;
    }
}
