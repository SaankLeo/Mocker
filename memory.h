#pragma once
#include <array>
#include <cstdint>
#include <vector>
#include <iostream>

using namespace std;

// ─────────────────────────────────────────────────────────────────
//  Memory map
// ─────────────────────────────────────────────────────────────────
constexpr uint32_t MEM_SIZE        = 1 << 20;   // 1 MB total

constexpr uint32_t KERNEL_BASE     = 0x00000;   // kernel code/data
constexpr uint32_t KERNEL_SIZE     = 0x10000;   // 64 KB

constexpr uint32_t HART_SLOT_BASE  = 0x10000;   // first hart slot
constexpr uint32_t HART_SLOT_SIZE  = 0x10000;   // 64 KB per hart (code+stack)
constexpr uint32_t MAX_HARTS       = 8;

constexpr uint32_t STACK_SIZE      = 0x2000;    // 8 KB stack per hart
// hart N stack top = HART_SLOT_BASE + (N+1)*HART_SLOT_SIZE - 4

inline uint32_t hartSlotBase(int id) {
    return HART_SLOT_BASE + (uint32_t)id * HART_SLOT_SIZE;
}
inline uint32_t hartSlotTop(int id) {
    return hartSlotBase(id) + HART_SLOT_SIZE;  // exclusive
}
inline uint32_t hartStackTop(int id) {
    return hartSlotTop(id) - 4;
}

// ─────────────────────────────────────────────────────────────────
//  Access type — used for protection checks
// ─────────────────────────────────────────────────────────────────
enum class AccessType { FETCH, LOAD, STORE };

// ─────────────────────────────────────────────────────────────────
//  Memory fault info (returned when access is denied)
// ─────────────────────────────────────────────────────────────────
struct MemFault {
    bool        occurred = false;
    uint32_t    addr     = 0;
    AccessType  type     = AccessType::LOAD;
    const char* reason   = "";
};

// ─────────────────────────────────────────────────────────────────
//  ProtectedMemory
//  Single flat array + per-access permission check.
//  M-mode (hartId == -1) bypasses all checks.
//  U-mode (hartId >= 0) may only access its own slot.
// ─────────────────────────────────────────────────────────────────
struct ProtectedMemory {
    array<uint8_t, MEM_SIZE> data = {0};

    // ── Raw accessors (no protection, used by kernel) ─────────────
    uint32_t rawLoadWord(uint32_t addr) const {
        return (uint32_t)data[addr]          |
               ((uint32_t)data[addr+1] <<  8)|
               ((uint32_t)data[addr+2] << 16)|
               ((uint32_t)data[addr+3] << 24);
    }
    void rawStoreWord(uint32_t addr, uint32_t val) {
        data[addr]   =  val        & 0xFF;
        data[addr+1] = (val >>  8) & 0xFF;
        data[addr+2] = (val >> 16) & 0xFF;
        data[addr+3] = (val >> 24) & 0xFF;
    }
    uint8_t  rawLoadByte(uint32_t a)         const { return data[a]; }
    void     rawStoreByte(uint32_t a, uint8_t v)   { data[a] = v; }
    uint16_t rawLoadHalf(uint32_t a) const {
        return (uint16_t)data[a] | ((uint16_t)data[a+1] << 8);
    }
    void rawStoreHalf(uint32_t a, uint16_t v) {
        data[a] = v & 0xFF; data[a+1] = (v>>8) & 0xFF;
    }

    void loadProgram(const vector<uint32_t>& words, uint32_t base) {
        for (size_t i = 0; i < words.size(); i++)
            rawStoreWord(base + (uint32_t)(i*4), words[i]);
    }

    // ── Protection check ──────────────────────────────────────────
    // hartId: -1 = M-mode (kernel), >= 0 = U-mode hart index
    MemFault check(uint32_t addr, AccessType t, int hartId) const {
        // Alignment
        if ((t == AccessType::FETCH || t == AccessType::LOAD) &&
            (t == AccessType::FETCH) && (addr & 3)) {
            return {true, addr, t, "misaligned fetch"};
        }
        // Out of range
        if (addr >= MEM_SIZE) {
            return {true, addr, t, "out of range"};
        }
        // M-mode: unrestricted
        if (hartId < 0) return {false};

        // U-mode: must be within own slot
        uint32_t lo = hartSlotBase(hartId);
        uint32_t hi = hartSlotTop(hartId);
        if (addr < lo || addr >= hi) {
            return {true, addr, t, "cross-hart or kernel access"};
        }
        return {false};
    }

    // ── Protected accessors (set fault on violation) ─────────────
    uint32_t loadWord(uint32_t addr, int hartId, MemFault& fault) const {
        fault = check(addr, AccessType::LOAD, hartId);
        if (fault.occurred) return 0;
        return rawLoadWord(addr);
    }
    void storeWord(uint32_t addr, uint32_t val, int hartId, MemFault& fault) {
        fault = check(addr, AccessType::STORE, hartId);
        if (fault.occurred) return;
        rawStoreWord(addr, val);
    }
    uint8_t loadByte(uint32_t addr, int hartId, MemFault& fault) const {
        fault = check(addr, AccessType::LOAD, hartId);
        if (fault.occurred) return 0;
        return rawLoadByte(addr);
    }
    void storeByte(uint32_t addr, uint8_t val, int hartId, MemFault& fault) {
        fault = check(addr, AccessType::STORE, hartId);
        if (fault.occurred) return;
        rawStoreByte(addr, val);
    }
    uint16_t loadHalf(uint32_t addr, int hartId, MemFault& fault) const {
        fault = check(addr, AccessType::LOAD, hartId);
        if (fault.occurred) return 0;
        return rawLoadHalf(addr);
    }
    void storeHalf(uint32_t addr, uint16_t val, int hartId, MemFault& fault) {
        fault = check(addr, AccessType::STORE, hartId);
        if (fault.occurred) return;
        rawStoreHalf(addr, val);
    }
};