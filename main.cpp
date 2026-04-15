#include "cpu.h"
using namespace std;

int main() {

    array<int,256> memory = {0};

    int program[] = {
        

    // LOAD R0 <- 5
    1, 0, 5, 0,

    // LOAD R1 <- 3
    1, 1, 3, 0,

    // R2 = R0 + R1
    2, 2, 0, 1,

    // R3 = R2 - R1
    3, 3, 2, 1,

    // R4 = R3 * R1
    4, 4, 3, 1,

    // HALT
    5, 0, 0, 0

    };

    int size = sizeof(program)/sizeof(program[0]); //each entity might not be of one byte

    for(int i=0;i<size;i++)
        memory[i] = program[i];

    runCPU(memory, size);
}