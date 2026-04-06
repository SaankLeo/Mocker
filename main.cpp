#include "cpu.h"
using namespace std;

int main() {

    array<int,256> memory = {0};

    int program[] = {
        1,0,5, //R0=5
        1,1,3, //R1=3
        2,0,1, //R0=5+3=8
        3,0,1, //R0=8-3=5
        4,0,1, //RO=5*3=15
        5,0,0 //HALT
    };

    int size = sizeof(program)/sizeof(program[0]); //each entity might not be of one byte

    for(int i=0;i<size;i++)
        memory[i] = program[i];

    runCPU(memory, size);
}