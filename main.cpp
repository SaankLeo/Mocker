#include "cpu.h"
#include "assembler.h"
#include <iostream>
#include <string>

using namespace std;

int main(int argc, char* argv[])
{
    array<int, 128> codeMemory = {0};

    Assembler as;
    bool ok = false;

    // ── File mode: ./cpu program.asm ──────────────────────────────────────
    if (argc >= 2) {
        cout << "Loading " << argv[1] << " ...\n";
        ok = as.loadFile(argv[1]);
    }
    // ── Interactive REPL ──────────────────────────────────────────────────
    else {
        ok = as.loadInteractive();
    }

    if (!ok) {
        cout << "Assembly failed.\n";
        return 1;
    }

    if (!as.finalise(codeMemory)) {
        cout << "Finalisation failed.\n";
        return 1;
    }

    as.dumpCode(codeMemory);
    as.dumpData();

    // ── Wire up CPU ───────────────────────────────────────────────────────
    CPU cpu;
    cpu.codeMemory = &codeMemory;
    cpu.dataMemory = &as.dataMemory;

    cout << "\nExecuting...\n\n";
    runCPU(cpu, (int)as.program.size());

    return 0;
}