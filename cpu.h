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

void execute(int opcode, int dest,int src1, int src2, array<int,8>& R) {

    if (opcode == LOAD) {
        R[dest] = src1;
        cout << "LOAD R" << dest << " <- " << src1 << endl;
    }

    else if (opcode == ADD) {
        R[dest] = R[src1] + R[src2];
        cout << "ADD R" << dest << " = R" << src1 << " + R" << src2 << endl;
    }

    else if (opcode == SUB) {
        R[dest] = R[src1] - R[src2];
        cout << "SUB R" << dest << " = R" << src1 << " - R" << src2 << endl;
    }

    else if (opcode == MUL) {
        R[dest] = R[src1] * R[src2];
        cout << "MUL R" << dest << " = R" << src1 << " * R" << src2 << endl;
    }

}

void runCPU(array<int,256>& memory, int programSize) {

    array<int,8> R = {0};
    int PC = 0;

    while (PC < programSize) {

        int opcode = memory[PC];
        int dest = memory[PC + 1];
        int src1 = memory[PC + 2];
        int src2 = memory[PC + 3];


        if (opcode == HALT) {
            cout << "HALT encountered\n";
            break;
        }

        execute(opcode, dest, src1, src2, R);
        PC += 4;
    }

    cout << "\nFinal Registers:\n";
    for (int i = 0; i < 8; i++) {
        cout << "R" << i << " = " << R[i] << endl;
    }
}