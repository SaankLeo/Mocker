#include "cpu.h"
#include <array>
#include <vector>
#include <string>
#include <sstream>
#include <iostream>
#include <cctype>

using namespace std;

int main() {

    // Two memory banks:
    // codeMemory = the instructions (LOAD, ADD, MUL etc. encoded as numbers)
    // dataMemory = the raw values (5, 3, 10 etc.)
    array<int, 128> codeMemory = {0};
    array<int, 128> dataMemory = {0};

    vector<int> program;  // We build the program here first, then copy to codeMemory
    int dataIndex = 0;    // Next free slot in dataMemory

    cout << "Enter instructions\n";
    cout << "Examples:\n";
    cout << "r0 = 5\n";
    cout << "r1 = 3\n";
    cout << "r2 = r0 + r1\n";
    cout << "r3 = r2 * r1\n";
    cout << "Type run to execute\n\n";

    string line;

    while (true) {

        cout << "cpu> ";
        getline(cin, line);

        if (line == "run")  break;
        if (line.empty())   continue;

        stringstream ss(line);
        string left, equalSign, first, op, second;

        // Parse: left = equalSign = first  (e.g. r2 = r0)
        ss >> left >> equalSign >> first;

        if (left.empty() || first.empty()) {
            cout << "Invalid instruction\n";
            continue;
        }

        // "r2" -> 2
        int dest = stoi(left.substr(1));

        // ----- CASE 1: r0 = 5  (plain number) -----
        if (isdigit(first[0]) || (first[0] == '-' && isdigit(first[1]))) {

            int value = stoi(first);

            if (value < -8 || value > 7)
                cout << "Warning: " << value << " is outside signed 4-bit range\n";

            // Store value in data memory
            dataMemory[dataIndex] = value;

            // Emit: LOAD dest dataIndex 0
            program.push_back(LOAD);
            program.push_back(dest);
            program.push_back(dataIndex);
            program.push_back(0);

            dataIndex++;
        }

        // CASE 2: r2 = r0 + r1
        else {

            ss >> op >> second;

            int src1 = stoi(first.substr(1));   // "r0" -> 0
            int src2 = stoi(second.substr(1));  // "r1" -> 1

            int opcode;
            if      (op == "+") opcode = ADD;
            else if (op == "-") opcode = SUB;
            else if (op == "*") opcode = MUL;
            else {
                cout << "Unknown operator\n";
                continue;
            }

            // Emit: opcode dest src1 src2
            program.push_back(opcode);
            program.push_back(dest);
            program.push_back(src1);
            program.push_back(src2);
        }
    }

    // Append HALT to signal end of program
    program.push_back(HALT);
    program.push_back(0);
    program.push_back(0);
    program.push_back(0);

    // Copy assembled program into codeMemory
    for (int i = 0; i < program.size(); i++)
        codeMemory[i] = program[i];

    // Print code memory (4 slots per instruction)
    cout << "\nCode Memory:\n";
    for (int i = 0; i < program.size(); i += 4)
        cout << "[" << i << "] "
             << codeMemory[i]   << " "
             << codeMemory[i+1] << " "
             << codeMemory[i+2] << " "
             << codeMemory[i+3] << endl;

    // Print data memory
    cout << "\nData Memory:\n";
    for (int i = 0; i < dataIndex; i++)
        cout << "dataMemory[" << i << "] = " << dataMemory[i] << endl;

    cout << "\nExecuting Program...\n\n";

    runCPU(codeMemory, program.size(), dataMemory);

    return 0;
}