#pragma once
#include <array>
#include <iostream>

using namespace std;

enum Opcode {
    LOAD = 1,
    ADD,
    SUB,
    MUL,
    HALT
};

// Executes ONE instruction
// opcode = what to do, dest = where to store result
// src1, src2 = source registers (for LOAD, src1 is the data memory index)
void execute(
    int opcode,
    int dest,
    int src1,
    int src2,
    array<int, 8>& R,
    array<int, 128>& dataMemory
) {
    switch (opcode) {

        case LOAD:
            // Pull a value out of data memory and put it in a register
            R[dest] = dataMemory[src1];
            cout << "LOAD R" << dest << " <- " << dataMemory[src1] << endl;
            break;

        case ADD:
            // R[dest] = R[src1] + R[src2]
            R[dest] = R[src1] + R[src2];
            cout << "ADD R" << dest << " = R" << src1 << " + R" << src2 << endl;
            break;

        case SUB:
            // R[dest] = R[src1] - R[src2]
            R[dest] = R[src1] - R[src2];
            cout << "SUB R" << dest << " = R" << src1 << " - R" << src2 << endl;
            break;

        case MUL:
            // R[dest] = R[src1] * R[src2]
            R[dest] = R[src1] * R[src2];
            cout << "MUL R" << dest << " = R" << src1 << " * R" << src2 << endl;
            break;

        default:
            cout << "Unknown opcode encountered" << endl;
    }
}

// Runs the full program until HALT
// memory = code memory, programSize = total slots used, dataMemory = raw values
void runCPU(
    array<int, 128>& memory,
    int programSize,
    array<int, 128>& dataMemory
) {
    array<int, 8> R = {0};  // R0-R7, all start at 0
    int PC = 0;             // Program Counter, moves +4 each instruction

    while (PC < programSize) {

        // Read the 4-slot instruction at current PC
        int opcode = memory[PC];
        int dest   = memory[PC + 1];
        int src1   = memory[PC + 2];
        int src2   = memory[PC + 3];

        if (opcode == HALT) {
            cout << "HALT encountered" << endl;
            break;
        }

        execute(opcode, dest, src1, src2, R, dataMemory);

        PC += 4;  // Jump to next instruction
    }

    // Print all register values at the end
    cout << "\nFinal Registers:\n";
    for (int i = 0; i < 8; i++)
        cout << "R" << i << " = " << R[i] << endl;
}