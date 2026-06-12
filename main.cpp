#include "cpu.h"
#include "assembler.h"
#include <iostream>

using namespace std;

int main(int argc, char* argv[])
{
    Assembler as;
    CPU cpu;

    bool ok = (argc >= 2) ? as.loadFile(argv[1]) : as.loadInteractive();

    if (!ok) { cout << "Assembly failed.\n"; return 1; }
    if (!as.finalise(cpu)) { cout << "Label resolution failed.\n"; return 1; }

    as.dump();
    cout << "\nExecuting...\n\n";

    // trace=true for interactive, can pass --no-trace for file mode
    bool trace = true;
    if (argc >= 3 && string(argv[2]) == "--no-trace") trace = false;

    runCPU(cpu, trace);
    return 0;
}