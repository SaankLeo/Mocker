#include "scheduler.h"
#include "assembler.h"
#include <iostream>
#include <string>
#include <vector>

using namespace std;

static void usage() {
    cout << "Usage:\n";
    cout << "  cpu [options] prog0.asm [prog1.asm ...]\n";
    cout << "Options:\n";
    cout << "  --quantum N     instructions per timeslice (default 8)\n";
    cout << "  --no-trace      suppress per-instruction output\n";
    cout << "  --no-sched      suppress context-switch log\n";
}

int main(int argc, char* argv[])
{
    HartManager mgr;

    // ── Parse flags ───────────────────────────────────────────────
    vector<string> files;
    for (int i = 1; i < argc; i++) {
        string a = argv[i];
        if (a == "--no-trace")       { mgr.cfg.trace      = false; }
        else if (a == "--no-sched")  { mgr.cfg.schedTrace = false; }
        else if (a == "--quantum" && i+1 < argc) {
            mgr.cfg.quantum = stoi(argv[++i]);
        }
        else if (a == "--help")      { usage(); return 0; }
        else                         { files.push_back(a); }
    }

    if (files.empty()) {
        cout << "No input files. Run with --help for usage.\n";
        return 1;
    }

    // ── Assemble each file → spawn a hart ─────────────────────────
    for (auto& path : files) {
        Assembler as;
        if (!as.loadFile(path) || !as.finalise()) {
            cout << "Assembly failed for: " << path << "\n";
            return 1;
        }
        as.dump(path);
        int id = mgr.spawnHart(as.program);
        cout << "  Spawned hart" << id
             << " at PC=0x" << hex << (id * HART_SPACING) << dec << "\n";
    }

    cout << "\n";

    // ── Run scheduler ─────────────────────────────────────────────
    mgr.run();
    return 0;
}