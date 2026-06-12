#pragma once
#include "cpu.h"
#include <vector>
#include <deque>
#include <string>
#include <iostream>
#include <unordered_map>

using namespace std;

// ─────────────────────────────────────────────────────────────────
//  Syscall numbers (a7 register)
// ─────────────────────────────────────────────────────────────────
namespace Syscall {
    constexpr int PRINT_INT   = 1;   // print a0 as integer
    constexpr int PRINT_STR   = 2;   // print null-terminated string at a0
    constexpr int EXIT        = 3;   // halt this hart, exit code in a0
    constexpr int YIELD       = 4;   // voluntarily give up timeslice
    constexpr int SEND        = 5;   // send msg to hart a0, value a1
    constexpr int RECV        = 6;   // receive msg → a0 (blocks if empty)
    constexpr int GET_HARTID  = 7;   // return own hart id in a0
}

// ─────────────────────────────────────────────────────────────────
//  IPC message
// ─────────────────────────────────────────────────────────────────
struct Message {
    int      from;
    uint32_t value;
};

// ─────────────────────────────────────────────────────────────────
//  Kernel
//  Owns nothing — just a stateless handler called by the scheduler
//  when a hart traps into M-mode.
// ─────────────────────────────────────────────────────────────────
struct Kernel {
    // Per-hart message queues
    unordered_map<int, deque<Message>> queues;

    // Handle a trap for hart h.
    // Called by the scheduler after executeOne returns false due to a trap.
    // The hart is already in M-mode with mepc/mcause/mtval set.
    // Returns true if the hart should be resumed (MRET will be faked),
    // false if it should stay blocked (WAITING) or be halted.
    bool handleTrap(Hart& h, vector<Hart>& allHarts, bool trace)
    {
        uint32_t cause = h.csr.mcause;

        // ── Timer interrupt ───────────────────────────────────────
        if(cause == Cause::TIMER_INT) {
    if(trace)
        cout << "[kernel] Timer interrupt on hart"
             << h.id
             << " -> preempted, returning to U-mode\n";

    doMRet(h, false);
    return true;
}

        // ── ECALL from U-mode ─────────────────────────────────────
        if(cause == Cause::ECALL_U) {
            int syscall = h.x[17]; // a7
            return handleSyscall(h, allHarts, syscall, trace);
        }

        // ── ECALL from M-mode (shouldn't normally happen) ─────────
        if(cause == Cause::ECALL_M) {
            if(trace) cout << "[kernel] M-mode ecall (treated as halt)\n";
            h.state = HartState::HALTED;
            return false;
        }

        // ── Memory fault ──────────────────────────────────────────
        if(cause == Cause::LOAD_FAULT || cause == Cause::STORE_FAULT ||
           cause == Cause::INSN_FAULT) {
            cout << "[kernel] FAULT hart" << h.id
                 << " cause=0x" << hex << cause
                 << " addr=0x"  << h.csr.mtval << dec << "\n";
            h.state = HartState::FAULTED;
            return false;
        }

        // ── Illegal instruction ───────────────────────────────────
        if(cause == Cause::ILLEGAL_INSN) {
            cout << "[kernel] Illegal instruction hart" << h.id
                 << " PC=0x" << hex << h.csr.mepc << dec << "\n";
            h.state = HartState::FAULTED;
            return false;
        }

        // Unknown trap — fault the hart
        cout << "[kernel] Unknown trap 0x" << hex << cause
             << " hart" << h.id << dec << "\n";
        h.state = HartState::FAULTED;
        return false;
    }

private:
    // Fake MRET: restore privilege from mepc, go back to U-mode
    void doMRet(Hart& h, bool advancePC = true)
{
    PrivMode prev = h.csr.trapReturn();
    h.mode = prev;

    if(advancePC)
        h.pc = h.csr.mepc + 4;
    else
        h.pc = h.csr.mepc;
}

    bool handleSyscall(Hart& h, vector<Hart>& allHarts, int sc, bool trace)
    {
        switch(sc)
        {
        case Syscall::PRINT_INT:
            cout << "[hart" << h.id << "] print: " << h.x[10] << "\n";
            doMRet(h);
            return true;

        case Syscall::PRINT_STR: {
            // Read null-terminated string from hart's own memory
            uint32_t addr = (uint32_t)h.x[10];
            string s;
            for(int i=0; i<256; i++) {
                MemFault f;
                uint8_t c = h.mem->loadByte(addr+i, h.id, f);
                if(f.occurred || c==0) break;
                s += (char)c;
            }
            cout << "[hart" << h.id << "] print: \"" << s << "\"\n";
            doMRet(h);
            return true;
        }

        case Syscall::EXIT:
            if(trace)
                cout << "[kernel] hart" << h.id
                     << " EXIT code=" << h.x[10] << "\n";
            h.state = HartState::HALTED;
            return false;

        case Syscall::YIELD:
            if(trace)
                cout << "[kernel] hart" << h.id << " YIELD\n";
            doMRet(h);
            return true;  // resumes next round

        case Syscall::GET_HARTID:
            h.x[10] = h.id;
            doMRet(h);
            return true;

        case Syscall::SEND: {
            int target = h.x[10];
            uint32_t val = (uint32_t)h.x[11];
            if(target < 0 || target >= (int)allHarts.size()) {
                cout << "[kernel] SEND: bad target " << target << "\n";
                h.x[10] = -1;  // error
            } else {
                queues[target].push_back({h.id, val});
                h.x[10] = 0;   // success
                // If target was WAITING, wake it
                if(allHarts[target].state == HartState::WAITING) {
                    allHarts[target].state = HartState::RUNNING;
                    if(trace)
                        cout << "[kernel] SEND woke hart" << target << "\n";
                }
            }
            doMRet(h);
            return true;
        }

        case Syscall::RECV: {
            auto& q = queues[h.id];
            if(q.empty()) {
                // Block: stay in WAITING, DO NOT advance mepc
                // When woken, the ecall will be re-executed
                if(trace)
                    cout << "[kernel] RECV hart" << h.id << " blocking\n";
                h.mode  = PrivMode::U;
                h.state = HartState::WAITING;
                h.pc    = h.csr.mepc;  // re-execute ecall when woken
                return false;
            }
            Message msg = q.front(); q.pop_front();
            h.x[10] = (int32_t)msg.value;
            h.x[11] = msg.from;
            if(trace)
                cout << "[kernel] RECV hart" << h.id
                     << " got " << msg.value
                     << " from hart" << msg.from << "\n";
            doMRet(h);
            return true;
        }

        default:
            cout << "[kernel] Unknown syscall " << sc
                 << " from hart" << h.id << "\n";
            doMRet(h);
            return true;
        }
    }
};