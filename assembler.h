#pragma once
#include "cpu.h"
#include <array>
#include <vector>
#include <string>
#include <sstream>
#include <fstream>
#include <iostream>
#include <unordered_map>
#include <cctype>

using namespace std;

// ─────────────────────────────────────────────
//  Assembler state
// ─────────────────────────────────────────────
struct Assembler {
    vector<int>                    program;    // raw 4-slot words, before label fixup
    array<int, 128>                dataMemory = {0};
    int                            dataIndex  = 0;

    // label name → slot index (byte address) in program where it is defined
    unordered_map<string, int>     labelDefs;

    // {slot index of the operand that needs patching, label name}
    vector<pair<int, string>>      patchList;

    // ─────────────────────────────────────────
    //  Parse a single register token "rN" → N
    //  Returns -1 on error
    // ─────────────────────────────────────────
    int parseReg(const string& tok)
    {
        if (tok.size() < 2 || tok[0] != 'r') return -1;
        for (size_t i = 1; i < tok.size(); i++)
            if (!isdigit(tok[i])) return -1;
        int n = stoi(tok.substr(1));
        if (n < 0 || n > 7) return -1;
        return n;
    }

    // ─────────────────────────────────────────
    //  Assemble one source line.
    //  Returns false if the line was invalid.
    // ─────────────────────────────────────────
    bool assembleLine(const string& rawLine)
    {
        // Strip inline comments (#  or ;)
        string line = rawLine;
        for (size_t i = 0; i < line.size(); i++) {
            if (line[i] == '#' || line[i] == ';') {
                line = line.substr(0, i);
                break;
            }
        }

        // Trim trailing whitespace
        while (!line.empty() && isspace((unsigned char)line.back()))
            line.pop_back();

        if (line.empty()) return true;   // blank / comment-only

        stringstream ss(line);
        string token;
        ss >> token;

        // ── Label definition: "loop:" ──────────
        if (!token.empty() && token.back() == ':')
        {
            string name = token.substr(0, token.size() - 1);
            if (name.empty()) {
                cout << "Error: empty label name\n";
                return false;
            }
            if (labelDefs.count(name)) {
                cout << "Error: duplicate label '" << name << "'\n";
                return false;
            }
            labelDefs[name] = (int)program.size();   // current byte address

            // There may be an instruction on the same line after the label
            string rest;
            if (getline(ss, rest) && !rest.empty()) {
                // Trim leading whitespace
                size_t start = rest.find_first_not_of(" \t");
                if (start != string::npos)
                    return assembleLine(rest.substr(start));
            }
            return true;
        }

        // ── CMP rX rY ──────────────────────────
        if (token == "cmp")
        {
            string r1, r2;
            if (!(ss >> r1 >> r2)) { cout << "Usage: cmp rX rY\n"; return false; }
            int a = parseReg(r1), b = parseReg(r2);
            if (a < 0 || b < 0) { cout << "Bad registers in cmp\n"; return false; }
            program.push_back(CMP); program.push_back(0);
            program.push_back(a);  program.push_back(b);
            return true;
        }

        // ── JMP <label or address> ─────────────
        if (token == "jmp")
        {
            string target;
            if (!(ss >> target)) { cout << "Usage: jmp <label|address>\n"; return false; }
            program.push_back(JMP);
            emitJumpTarget(target);
            program.push_back(0); program.push_back(0);
            return true;
        }

        // ── JZ <label or address> ──────────────
        if (token == "jz")
        {
            string target;
            if (!(ss >> target)) { cout << "Usage: jz <label|address>\n"; return false; }
            program.push_back(JZ);
            emitJumpTarget(target);
            program.push_back(0); program.push_back(0);
            return true;
        }

        // ── JNZ <label or address> ─────────────
        if (token == "jnz")
        {
            string target;
            if (!(ss >> target)) { cout << "Usage: jnz <label|address>\n"; return false; }
            program.push_back(JNZ);
            emitJumpTarget(target);
            program.push_back(0); program.push_back(0);
            return true;
        }

        // ── STORE  rN -> mem[idx]  ─────────────
        //    syntax:  store r0 2    (store R0 into dataMemory[2])
        if (token == "store")
        {
            string reg, idx;
            if (!(ss >> reg >> idx)) { cout << "Usage: store rN <mem_index>\n"; return false; }
            int r = parseReg(reg);
            if (r < 0) { cout << "Bad register in store\n"; return false; }
            int mi = stoi(idx);
            program.push_back(STORE); program.push_back(r);
            program.push_back(mi);    program.push_back(0);
            return true;
        }

        // ── Assignment:  rD = ...  ─────────────
        //    rD = <number>
        //    rD = rA + rB   (or - or *)
        string eq, first;
        if (!(ss >> eq >> first)) { cout << "Invalid instruction: " << rawLine << "\n"; return false; }
        if (eq != "=") { cout << "Expected '=' in: " << rawLine << "\n"; return false; }

        int dest = parseReg(token);
        if (dest < 0) { cout << "Bad destination register: " << token << "\n"; return false; }

        // rD = <literal>
        if (isdigit((unsigned char)first[0]) ||
            (first[0] == '-' && first.size() > 1 && isdigit((unsigned char)first[1])))
        {
            int value = stoi(first);
            dataMemory[dataIndex] = value;
            program.push_back(LOAD); program.push_back(dest);
            program.push_back(dataIndex); program.push_back(0);
            dataIndex++;
            return true;
        }

        // rD = rA op rB
        string op, second;
        if (!(ss >> op >> second)) { cout << "Invalid arithmetic: " << rawLine << "\n"; return false; }
        int src1 = parseReg(first), src2 = parseReg(second);
        if (src1 < 0 || src2 < 0) { cout << "Bad source registers in: " << rawLine << "\n"; return false; }

        int opcode;
        if      (op == "+") opcode = ADD;
        else if (op == "-") opcode = SUB;
        else if (op == "*") opcode = MUL;
        else { cout << "Unknown operator '" << op << "'\n"; return false; }

        program.push_back(opcode); program.push_back(dest);
        program.push_back(src1);  program.push_back(src2);
        return true;
    }

    // ─────────────────────────────────────────
    //  Emit the target slot of a jump, recording
    //  a patch if the target is a label name.
    // ─────────────────────────────────────────
    void emitJumpTarget(const string& target)
    {
        // Is it a bare integer?
        bool isNum = !target.empty();
        for (char c : target)
            if (!isdigit((unsigned char)c)) { isNum = false; break; }

        if (isNum) {
            program.push_back(stoi(target));
        } else {
            // Record that slot program.size() needs patching with label 'target'
            patchList.push_back({ (int)program.size(), target });
            program.push_back(0);   // placeholder
        }
    }

    // ─────────────────────────────────────────
    //  Resolve all label references
    // ─────────────────────────────────────────
    bool resolveLabels()
    {
        bool ok = true;
        for (auto& [slot, name] : patchList)
        {
            auto it = labelDefs.find(name);
            if (it == labelDefs.end()) {
                cout << "Error: undefined label '" << name << "'\n";
                ok = false;
                continue;
            }
            program[slot] = it->second;
        }
        return ok;
    }

    // ─────────────────────────────────────────
    //  Finalise: append HALT, resolve labels,
    //  copy into codeMemory
    // ─────────────────────────────────────────
    bool finalise(array<int, 128>& codeMemory)
    {
        program.push_back(HALT); program.push_back(0);
        program.push_back(0);    program.push_back(0);

        if (!resolveLabels()) return false;

        if (program.size() > 128) {
            cout << "Error: program too large (" << program.size() << " slots, max 128)\n";
            return false;
        }
        for (size_t i = 0; i < program.size(); i++)
            codeMemory[i] = program[i];
        return true;
    }

    // ─────────────────────────────────────────
    //  Load from a .asm file
    // ─────────────────────────────────────────
    bool loadFile(const string& path)
    {
        ifstream f(path);
        if (!f.is_open()) {
            cout << "Error: cannot open file '" << path << "'\n";
            return false;
        }
        string line;
        int lineNo = 0;
        while (getline(f, line)) {
            lineNo++;
            if (!assembleLine(line)) {
                cout << "  (at line " << lineNo << ": " << line << ")\n";
                return false;
            }
        }
        return true;
    }

    // ─────────────────────────────────────────
    //  Interactive REPL input
    // ─────────────────────────────────────────
    bool loadInteractive()
    {
        cout << "Enter instructions (type 'run' to execute)\n";
        cout << "Syntax examples:\n";
        cout << "  r0 = 5\n";
        cout << "  r1 = 3\n";
        cout << "  r2 = r0 + r1\n";
        cout << "  cmp r0 r1\n";
        cout << "  jnz loop\n";
        cout << "  store r2 0\n";
        cout << "  loop:  r0 = r0 - r1   # label on same line as instruction\n\n";

        string line;
        while (true) {
            cout << "asm> ";
            getline(cin, line);
            if (line == "run") break;
            assembleLine(line);
        }
        return true;
    }

    // ─────────────────────────────────────────
    //  Debug dump
    // ─────────────────────────────────────────
    void dumpCode(const array<int, 128>& codeMemory) const
    {
        cout << "\nCode Memory (" << program.size() << " slots, "
             << program.size() / 4 << " instructions):\n";

        // Print label map
        if (!labelDefs.empty()) {
            cout << "Labels:\n";
            for (auto& [name, addr] : labelDefs)
                cout << "  " << name << " -> [" << addr << "]\n";
        }

        for (size_t i = 0; i < program.size(); i += 4)
            cout << "  [" << i << "] "
                 << codeMemory[i]   << " "
                 << codeMemory[i+1] << " "
                 << codeMemory[i+2] << " "
                 << codeMemory[i+3] << "\n";
    }

    void dumpData() const
    {
        if (dataIndex == 0) return;
        cout << "\nData Memory:\n";
        for (int i = 0; i < dataIndex; i++)
            cout << "  MEM[" << i << "] = " << dataMemory[i] << "\n";
    }
};