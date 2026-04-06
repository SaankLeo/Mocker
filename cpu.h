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
/* 1 = load, 2 = add, 3 = subtract, 4 =multiply, 5=halt */

void execute(int opcode, int op1, int op2, array<int,8>& R) {

    if (opcode == LOAD) {
        R[op1] = op2;
        cout << "LOAD R" << op1 << " <- " << op2 << endl;
    }
    else if (opcode == ADD) {
        R[op1] += R[op2];
        cout << "ADD R" << op1 << endl;
    }
    else if (opcode == SUB) {
        R[op1] -= R[op2];
        cout << "SUB R" << op1 << endl;
    }
    else if (opcode == MUL) {
        R[op1] *= R[op2];
        cout << "MUL R" << op1 << endl;
    }
}

void runCPU(array<int,256>& memory, int programSize) {

    array<int,8> R = {0};
    int PC = 0;

    while (PC < programSize) {

        int opcode = memory[PC];
        int op1 = memory[PC + 1];
        int op2 = memory[PC + 2];

        if (opcode == HALT) {
            cout << "HALT encountered\n";
            break;
        }

        execute(opcode, op1, op2, R);
        PC += 3;
    }

    cout << "\nFinal Registers:\n";
    for (int i = 0; i < 8; i++) {
        cout << "R" << i << " = " << R[i] << endl;
    }
}