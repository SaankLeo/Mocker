#pragma once
#include <array>
#include <iostream>
#include <string>
#include <unordered_map>

using namespace std;

// ─────────────────────────────────────────────
//  ISA
// ─────────────────────────────────────────────
enum Opcode {
    LOAD  = 1,
    ADD,
    SUB,
    MUL,
    CMP,   // compare two registers → sets zeroFlag
    JMP,   // unconditional jump
    JZ,    // jump if zeroFlag == true
    JNZ,   // jump if zeroFlag == false
    STORE, // store register → data memory
    HALT
};

// ─────────────────────────────────────────────
//  CPU state
// ─────────────────────────────────────────────
struct CPU {
    array<int, 8>   R        = {0};   // R0–R7
    int             PC       = 0;     // program counter (byte index into codeMemory)
    bool            zeroFlag = false;

    array<int, 128>* codeMemory = nullptr;
    array<int, 128>* dataMemory = nullptr;
};

// ─────────────────────────────────────────────
//  Execute ONE instruction
// ─────────────────────────────────────────────
inline void execute(CPU& cpu, int opcode, int dest, int src1, int src2)
{
    switch (opcode)
    {
        case LOAD:
            cpu.R[dest] = (*cpu.dataMemory)[src1];
            cout << "LOAD  R" << dest
                 << " <- MEM[" << src1 << "]"
                 << "  (= " << cpu.R[dest] << ")\n";
            break;

        case STORE:
            (*cpu.dataMemory)[src1] = cpu.R[dest];
            cout << "STORE R" << dest
                 << " -> MEM[" << src1 << "]"
                 << "  (= " << cpu.R[dest] << ")\n";
            break;

        case ADD:
            cpu.R[dest] = cpu.R[src1] + cpu.R[src2];
            cout << "ADD   R" << dest
                 << " = R" << src1 << "(" << cpu.R[src1] << ")"
                 << " + R" << src2 << "(" << cpu.R[src2] << ")"
                 << " = " << cpu.R[dest] << "\n";
            break;

        case SUB:
            cpu.R[dest] = cpu.R[src1] - cpu.R[src2];
            cout << "SUB   R" << dest
                 << " = R" << src1 << "(" << cpu.R[src1] << ")"
                 << " - R" << src2 << "(" << cpu.R[src2] << ")"
                 << " = " << cpu.R[dest] << "\n";
            break;

        case MUL:
            cpu.R[dest] = cpu.R[src1] * cpu.R[src2];
            cout << "MUL   R" << dest
                 << " = R" << src1 << "(" << cpu.R[src1] << ")"
                 << " * R" << src2 << "(" << cpu.R[src2] << ")"
                 << " = " << cpu.R[dest] << "\n";
            break;

        case CMP:
            cpu.zeroFlag = (cpu.R[src1] == cpu.R[src2]);
            cout << "CMP   R" << src1 << "(" << cpu.R[src1] << ")"
                 << " == R" << src2 << "(" << cpu.R[src2] << ")"
                 << "  -> zeroFlag=" << cpu.zeroFlag << "\n";
            break;

        case JMP:
            cout << "JMP   " << dest << "\n";
            cpu.PC = dest - 4;   // -4 because the loop always adds 4
            break;

        case JZ:
            if (cpu.zeroFlag) {
                cout << "JZ    taken -> " << dest << "\n";
                cpu.PC = dest - 4;
            } else {
                cout << "JZ    not taken\n";
            }
            break;

        case JNZ:
            if (!cpu.zeroFlag) {
                cout << "JNZ   taken -> " << dest << "\n";
                cpu.PC = dest - 4;
            } else {
                cout << "JNZ   not taken\n";
            }
            break;

        default:
            cout << "ERROR: Unknown opcode " << opcode << "\n";
            break;
    }
}

// ─────────────────────────────────────────────
//  Run until HALT (or end of program)
// ─────────────────────────────────────────────
inline void runCPU(CPU& cpu, int programSize)
{
    while (cpu.PC < programSize)
    {
        int opcode = (*cpu.codeMemory)[cpu.PC    ];
        int dest   = (*cpu.codeMemory)[cpu.PC + 1];
        int src1   = (*cpu.codeMemory)[cpu.PC + 2];
        int src2   = (*cpu.codeMemory)[cpu.PC + 3];

        if (opcode == HALT) {
            cout << "HALT\n";
            break;
        }

        execute(cpu, opcode, dest, src1, src2);
        cpu.PC += 4;
    }

    cout << "\nFinal Registers:\n";
    for (int i = 0; i < 8; i++)
        cout << "  R" << i << " = " << cpu.R[i] << "\n";

    cout << "\nData Memory (non-zero slots):\n";
    for (int i = 0; i < 128; i++)
        if ((*cpu.dataMemory)[i] != 0)
            cout << "  MEM[" << i << "] = " << (*cpu.dataMemory)[i] << "\n";
}