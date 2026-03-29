#include <iostream>
#include <array>
using namespace std;

int main() {

// Memory loading and setup
// 8 registers (R0–R7)
    array<int, 8> R = {0};

// 256 memory units
    array<int, 256> memory = {0};

// program Counter (points to current instruction)
    int PC = 0;

    cout << "CPU initialized\n";


/*
 // LOAD R0 5
    memory[0] = 1; //this line gives oppcode; 1=load 2=add 3=sub 4=multiply
    memory[1] = 0; //operand 1
    memory[2] = 5; //operand 2


    // load R1 3
    memory[3] = 1;  
    memory[4] = 1;  
    memory[5] = 3;  

    // R0 = R0 + R1
    memory[6] = 2;  // ADD
    memory[7] = 0;  // R0 (destination)
    memory[8] = 1;  // R1 (source)

*/
   
//cooler way to do that 
   int program[] = {
    1, 0, 5,   // R0 = 5
    1, 1, 3,   // R1 = 3
    2, 0, 1,   // R0 = 8
    3, 0, 1,   // R0 = 5
    4, 0, 1    // R0 = 15
    };
    
 
//fetch

    int programSize = sizeof(program) / sizeof(program[0]);

    for (int i = 0; i < programSize; i++) {
    memory[i] = program[i];
    }

    while (PC < programSize) {
    int opcode = memory[PC];
    int op1 = memory[PC + 1];
    int op2 = memory[PC + 2];

//load
    if (opcode == 1) {
        R[op1] = op2;
        cout << "LOAD R" << op1 << " <- " << op2 << endl;
    }
//add
    else if (opcode == 2) {
        R[op1] = R[op1] + R[op2];
        cout << "ADD R" << op1 << " = R" << op1 << " + R" << op2 << endl;
    }
//subtract   
    else if (opcode == 3) {
     R[op1] = R[op1] - R[op2];
     cout << "SUB R" << op1 << " = R" << op1 << " - R" << op2 << endl;
    }

//multiply
    else if (opcode == 4) {
        R[op1] = R[op1] * R[op2];
        cout << "MUL R" << op1 << " = R" << op1 << " * R" << op2 << endl;
    }

     else{
        cout << "Unknown opcode\n";
        break;
    }

    PC += 3; // move to next instruction
    }

//print
    cout << "\nFinal Register Values:\n";
    for (int i = 0; i < 8; i++) {
    cout << "R" << i << " = " << R[i] << endl;
    }



return 0;
}