#pragma once
#include "cpu.h"
#include <vector>
#include <iostream>
#include <iomanip>
#include <string>

using namespace std;

struct SchedulerConfig {
    int      quantum    = 8;
    int      maxCycles  = 1000000;
    bool     trace      = true;
    bool     schedTrace = true;
};

struct HartManager {
    Memory             mem;
    vector<Hart>       harts;
    SchedulerConfig    cfg;

    int spawnHart(const vector<uint32_t>& program)
    {
        int id = (int)harts.size();
        uint32_t base = (uint32_t)id * HART_SPACING;

        if (base + program.size() * 4 > MEM_SIZE) {
            cout << "Error: program too large for hart " << id << "\n";
            return -1;
        }

        mem.loadProgram(program, base);
        harts.emplace_back();
        harts.back().init(id, &mem, base);
        return id;
    }

    void run()
    {
        if (harts.empty()) { cout << "No harts to run.\n"; return; }

        int totalSteps = 0;
        int round      = 0;

        while (totalSteps < cfg.maxCycles)
        {
            bool anyRunning = false;
            for (auto& h : harts)
                if (h.state == HartState::RUNNING) { anyRunning = true; break; }
            if (!anyRunning) break;

            if (cfg.schedTrace)
                cout << "\n+-- Round " << round
                     << " -------------------------------------------\n";

            for (auto& h : harts)
            {
                if (h.state != HartState::RUNNING) continue;

                if (cfg.schedTrace)
                    cout << "|  > hart" << h.id
                         << " (PC=0x" << hex << h.pc << dec << ")\n";

                int ran = 0;
                while (ran < cfg.quantum && h.state == HartState::RUNNING)
                {
                    if (!executeOne(h, cfg.trace)) break;
                    ran++;
                    totalSteps++;
                }

                if (cfg.schedTrace)
                    cout << "|    ran " << ran << " steps"
                         << "  state=" << stateName(h.state) << "\n";
            }

            round++;
        }

        if (totalSteps >= cfg.maxCycles)
            cout << "\n[scheduler] Step limit reached ("
                 << cfg.maxCycles << " steps).\n";

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

        for (auto& h : harts)
            printHart(h);
    }
};