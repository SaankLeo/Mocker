#include "cpu.h"
#include <vector>
#include <string>
#include <sstream>
#include <cctype>
using namespace std;

int main() {
     
    array<int,128> codeMemory = {0}; // Stores instructions like LOAD, ADD, etc.
    array<int,128> dataMemory = {0}; // Stores actual values like 5, 3, 10, etc
    vector<int> program;
    
    int dataIndex = 0;    // Tracks the next empty slot in data memory

    cout << "Enter instructions\n";
    cout << "Examples:\n";
    cout << "r0 = 5\n";
    cout << "r1 = 3\n";
    cout << "r2 = r0 + r1\n";
    cout << "Type run to execute\n\n";

    string line;  // Stores the entire line the user types

    while (true) {
        cout << "cpu> ";
        getline(cin, line); // Reads full line like: r2 = r0 + r1

    if (line == "run") {  // Stop taking input if user types run
            break;
        }
    
    stringstream ss(line);   // Lets us break the line into smaller words

    string left, equalSign, first, op, second; // Variables for different parts of the instruction

     // Example:
        // r2 = r0 + r1
        // left = "r2"
        // equalSign = "="
        // first = "r0"
    
    ss >> left >> equalSign >> first;
    // Reads first 3 parts of the instruction
    // Example: r2 = r0
    // left = "r2"
    // equalSign = "="
    // first = "r0"

    int dest = stoi(left.substr(1));
    // Removes the 'r' from "r2"
    // Converts "2" into integer 2

    if (isdigit(first[0])) {
        // Checks if first is a number
        // Example: "5"

        int value = stoi(first);
        // Converts string "5" into integer 5

        dataMemory[dataIndex] = value;
        // Stores value in next free data memory slot

        program.push_back(LOAD);
        // Opcode for LOAD instruction

        program.push_back(dest);
        // Destination register

        program.push_back(dataIndex);
        // Location in data memory where value is stored

        program.push_back(0);
        // Unused field

        dataIndex++;
        // Move to next free data memory slot
    }
    else {
        // Means this is something like:
        // r2 = r0 + r1

        ss >> op >> second;
        // Reads remaining parts
        // op = "+"
        // second = "r1"

        int src1 = stoi(first.substr(1));
        // Converts "r0" into 0

        int src2 = stoi(second.substr(1));
        // Converts "r1" into 1

        int opcode;
        // Stores which operation to perform

        if (op == "+")
            opcode = ADD;
        else if (op == "-")
            opcode = SUB;
        else
            opcode = MUL;
        // Decide which opcode to use

        program.push_back(opcode);
        // Store opcode

        program.push_back(dest);
        // Store destination register

        program.push_back(src1);
        // Store first source register

        program.push_back(src2);
        // Store second source register
}
    
    
}
