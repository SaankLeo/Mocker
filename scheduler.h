#pragma once
#include "cpu.h"
#include "kernel.h"
#include <vector>
#include <iostream>
#include <iomanip>

using namespace std;

// ─────────────────────────────────────────────────────────────────
//  Scheduler config
// ─────────────────────────────────────────────────────────────────
struct SchedulerConfig {
    int  quantum       = 8;       // instructions per timeslice
    int  timerInterval = 32;      // global ticks between timer interrupts
    int  maxCycles     = 1000000;
    bool trace         = true;
    bool schedTrace    = true;
};

// ─────────────────────────────────────────────────────────────────
//  HartManager
// ─────────────────────────────────────────────────────────────────
struct HartManager {
    ProtectedMemory  mem;
    vector<Hart>     harts;
    SchedulerConfig  cfg;
    Kernel           kernel;

    int spawnHart(const vector<uint32_t>& program)
    {
        int id = (int)harts.size();
        uint32_t base = hartSlotBase(id);

        if(id >= MAX_HARTS) {
            cout << "Error: max harts reached\n"; return -1;
        }
        if(program.size() * 4 > HART_SLOT_SIZE - STACK_SIZE) {
            cout << "Error: program too large for hart " << id << "\n"; return -1;
        }

        mem.loadProgram(program, base);

        harts.emplace_back();
        harts.back().init(id, &mem, base);
        return id;
    }

    // Install a kernel trap handler at KERNEL_BASE.
    // The kernel itself runs in M-mode.
    // For simplicity: we handle traps in C++ (kernel.h) rather than
    // having a real RISC-V kernel binary. mtvec is set to a sentinel
    // address; the scheduler catches traps before they re-execute.
    void run()
    {
        if(harts.empty()) { cout << "No harts.\n"; return; }

        int totalSteps = 0;
        int round      = 0;
        int timerTick  = 0;

        while(totalSteps < cfg.maxCycles)
        {
            // Check if anything is still alive
            bool anyActive = false;
            for(auto& h : harts)
                if(h.state == HartState::RUNNING || h.state == HartState::WAITING)
                { anyActive = true; break; }
            if(!anyActive) break;

            if(cfg.schedTrace)
                cout << "\n+-- Round " << round
                     << " -------------------------------------------\n";

            for(auto& h : harts)
            {
                if(h.state != HartState::RUNNING) continue;

                if(cfg.schedTrace)
                    cout << "|  > hart" << h.id
                         << " [" << privName(h.mode) << "]"
                         << " (PC=0x" << hex << h.pc << dec << ")\n";

                int ran = 0;
                while(ran < cfg.quantum && h.state == HartState::RUNNING)
                {
                    bool ok = executeOne(h, cfg.trace);
                    totalSteps++;
                    timerTick++;

                    // Fire timer interrupt every timerInterval global ticks
                    if(timerTick >= cfg.timerInterval) {
                        timerTick = 0;
                        // Mark the currently running hart (could be any)
                        for(auto& th : harts)
                            if(th.state == HartState::RUNNING)
                                th.timerPending = true;
                    }

                    if(ok) {
                        ran++;
                    } else {
                        // Hart trapped or halted — call kernel
                        if(h.state != HartState::HALTED && h.state != HartState::FAULTED) {
                            kernel.handleTrap(h, harts, cfg.trace);
                        }
                        break;  // end this hart's quantum on any trap
                    }
                }

                if(cfg.schedTrace)
                    cout << "|    ran " << ran << " steps"
                         << "  state=" << stateName(h.state)
                         << "  mode="  << privName(h.mode) << "\n";
            }

            round++;
        }

        if(totalSteps >= cfg.maxCycles)
            cout << "\n[scheduler] Step limit (" << cfg.maxCycles << ")\n";

        printSummary(round, totalSteps);
    }

    void printSummary(int rounds, int totalSteps) const
    {
        cout << "\n+----------------------------------------------+\n";
        cout << "|  Scheduler finished                          |\n";
        cout << "+----------------------------------------------+\n";
        cout << "|  Rounds: " << setw(6) << rounds
             << "   Total steps: " << setw(8) << totalSteps
             << "         |\n";
        cout << "+----------------------------------------------+\n\n";
        for(auto& h : harts) printHart(h);
    }
};