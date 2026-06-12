#pragma once
#include <cstdint>
#include <string>
using namespace std;

// ─────────────────────────────────────────────────────────────────
//  Privilege modes
// ─────────────────────────────────────────────────────────────────
enum class PrivMode : uint8_t { U = 0, M = 3 };

inline const char* privName(PrivMode m) {
    return m == PrivMode::M ? "M" : "U";
}

// ─────────────────────────────────────────────────────────────────
//  mcause values
// ─────────────────────────────────────────────────────────────────
namespace Cause {
    // Exceptions (interrupt=0)
    constexpr uint32_t INSN_MISALIGN   = 0;
    constexpr uint32_t INSN_FAULT      = 1;
    constexpr uint32_t ILLEGAL_INSN    = 2;
    constexpr uint32_t BREAKPOINT      = 3;
    constexpr uint32_t LOAD_FAULT      = 5;
    constexpr uint32_t STORE_FAULT     = 7;
    constexpr uint32_t ECALL_U         = 8;   // ecall from U-mode
    constexpr uint32_t ECALL_M         = 11;  // ecall from M-mode

    // Interrupts (interrupt=1, bit 31 set)
    constexpr uint32_t INTERRUPT_BIT   = 0x80000000u;
    constexpr uint32_t TIMER_INT       = INTERRUPT_BIT | 7;
}

// ─────────────────────────────────────────────────────────────────
//  CSR address map (subset we implement)
// ─────────────────────────────────────────────────────────────────
namespace CSRAddr {
    constexpr uint32_t MSTATUS  = 0x300;
    constexpr uint32_t MTVEC    = 0x305;
    constexpr uint32_t MEPC     = 0x341;
    constexpr uint32_t MCAUSE   = 0x342;
    constexpr uint32_t MTVAL    = 0x343;
    constexpr uint32_t MIP      = 0x344;  // interrupt pending
    constexpr uint32_t MIE      = 0x304;  // interrupt enable
    constexpr uint32_t MHARTID  = 0xF14;  // read-only: this hart's id
    constexpr uint32_t CYCLE    = 0xC00;  // read-only: cycle counter
}

// mstatus bit fields we care about
namespace MStatus {
    constexpr uint32_t MIE  = (1u << 3);   // machine interrupt enable
    constexpr uint32_t MPIE = (1u << 7);   // previous MIE
    constexpr uint32_t MPP_SHIFT = 11;
    constexpr uint32_t MPP_MASK  = (3u << 11);
    constexpr uint32_t MPP_U     = (0u << 11);
    constexpr uint32_t MPP_M     = (3u << 11);
}

// ─────────────────────────────────────────────────────────────────
//  CSR file — one per hart
// ─────────────────────────────────────────────────────────────────
struct CSRFile {
    uint32_t mstatus = 0;
    uint32_t mtvec   = 0;
    uint32_t mepc    = 0;
    uint32_t mcause  = 0;
    uint32_t mtval   = 0;
    uint32_t mip     = 0;
    uint32_t mie     = 0;
    uint32_t hartid  = 0;
    uint64_t cycle   = 0;

    uint32_t read(uint32_t addr, bool& ok) const {
        ok = true;
        switch(addr) {
            case CSRAddr::MSTATUS: return mstatus;
            case CSRAddr::MTVEC:   return mtvec;
            case CSRAddr::MEPC:    return mepc;
            case CSRAddr::MCAUSE:  return mcause;
            case CSRAddr::MTVAL:   return mtval;
            case CSRAddr::MIP:     return mip;
            case CSRAddr::MIE:     return mie;
            case CSRAddr::MHARTID: return hartid;
            case CSRAddr::CYCLE:   return (uint32_t)(cycle & 0xFFFFFFFF);
            default: ok = false; return 0;
        }
    }

    void write(uint32_t addr, uint32_t val, bool& ok) {
        ok = true;
        switch(addr) {
            case CSRAddr::MSTATUS: mstatus = val; break;
            case CSRAddr::MTVEC:   mtvec   = val; break;
            case CSRAddr::MEPC:    mepc    = val; break;
            case CSRAddr::MCAUSE:  mcause  = val; break;
            case CSRAddr::MTVAL:   mtval   = val; break;
            case CSRAddr::MIP:     mip     = val; break;
            case CSRAddr::MIE:     mie     = val; break;
            default: ok = false; break;
        }
    }

    // Save state on trap entry (M-mode trap)
    void trapEnter(uint32_t pc, uint32_t cause, uint32_t tval, PrivMode from) {
        mepc   = pc;
        mcause = cause;
        mtval  = tval;
        // Save previous privilege in MPP, clear MIE, save MIE→MPIE
        uint32_t prev_mie = (mstatus & MStatus::MIE) ? 1u : 0u;
        mstatus &= ~(MStatus::MIE | MStatus::MPIE | MStatus::MPP_MASK);
        mstatus |= (prev_mie ? MStatus::MPIE : 0);
        mstatus |= (from == PrivMode::M ? MStatus::MPP_M : MStatus::MPP_U);
    }

    // Restore state on MRET
    PrivMode trapReturn() {
        // MPP → current privilege
        uint32_t mpp = (mstatus & MStatus::MPP_MASK) >> MStatus::MPP_SHIFT;
        PrivMode ret = (mpp == 3) ? PrivMode::M : PrivMode::U;
        // MPIE → MIE, set MPIE=1, MPP=U
        uint32_t mpie = (mstatus & MStatus::MPIE) ? 1u : 0u;
        mstatus &= ~(MStatus::MIE | MStatus::MPIE | MStatus::MPP_MASK);
        mstatus |= (mpie ? MStatus::MIE : 0);
        mstatus |= MStatus::MPIE;
        mstatus |= MStatus::MPP_U;
        return ret;
    }

    bool interruptsEnabled() const {
        return (mstatus & MStatus::MIE) != 0;
    }
};