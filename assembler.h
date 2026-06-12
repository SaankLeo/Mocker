#pragma once
#include "cpu.h"
#include "encode.h"
#include <vector>
#include <string>
#include <sstream>
#include <fstream>
#include <iostream>
#include <unordered_map>
#include <cctype>
#include <algorithm>

using namespace std;

// ─────────────────────────────────────────────────────────────────
//  Assembler
//
//  Syntax (standard RV32I):
//    add  x1, x2, x3
//    addi x1, x2, 100
//    lw   x1, 8(x2)
//    sw   x1, 8(x2)
//    beq  x1, x2, label
//    jal  x1, label
//    jalr x1, x2, 0
//    lui  x1, 0xABCDE
//    auipc x1, 0x1000
//
//  Pseudo-instructions:
//    li   xD, imm        →  lui + addi  (or just addi if fits in 12 bits)
//    mv   xD, xS         →  addi xD, xS, 0
//    nop                 →  addi x0, x0, 0
//    ret                 →  jalr x0, x1, 0
//    j    label          →  jal  x0, label
//    beqz xS, label      →  beq  xS, x0, label
//    bnez xS, label      →  bne  xS, x0, label
//    call label          →  jal  x1, label
// ─────────────────────────────────────────────────────────────────

struct Assembler {
    vector<uint32_t>              program;
    unordered_map<string,int32_t> labelDefs;    // label → byte address
    // {word index in program, label name, instruction PC for relative calc}
    struct Patch { int idx; string label; uint32_t instrPC; };
    vector<Patch>                 patches;

    bool hasError = false;

    // ── Strip comment, trim ───────────────────────────────────────
    static string clean(const string& raw) {
        string s = raw;
        for (size_t i = 0; i < s.size(); i++)
            if (s[i]=='#'||s[i]==';') { s=s.substr(0,i); break; }
        // trim
        size_t a = s.find_first_not_of(" \t\r\n");
        if (a == string::npos) return "";
        size_t b = s.find_last_not_of(" \t\r\n");
        return s.substr(a, b-a+1);
    }

    // ── Comma-split tokens ────────────────────────────────────────
    static vector<string> tokenize(const string& s) {
        vector<string> out;
        string cur;
        for (char c : s) {
            if (c == ',' || c == '\t') {
                string t = clean(cur);
                if (!t.empty()) out.push_back(t);
                cur.clear();
            } else {
                cur += c;
            }
        }
        string t = clean(cur);
        if (!t.empty()) out.push_back(t);
        return out;
    }

    // ── Parse register  x0..x31 or ABI name ──────────────────────
    static const unordered_map<string,int>& abiMap() {
        static unordered_map<string,int> m = {
            {"zero",0},{"ra",1},{"sp",2},{"gp",3},{"tp",4},
            {"t0",5},{"t1",6},{"t2",7},
            {"s0",8},{"fp",8},{"s1",9},
            {"a0",10},{"a1",11},{"a2",12},{"a3",13},
            {"a4",14},{"a5",15},{"a6",16},{"a7",17},
            {"s2",18},{"s3",19},{"s4",20},{"s5",21},
            {"s6",22},{"s7",23},{"s8",24},{"s9",25},
            {"s10",26},{"s11",27},
            {"t3",28},{"t4",29},{"t5",30},{"t6",31}
        };
        return m;
    }

    int parseReg(const string& tok) {
        string s = tok;
        // lowercase
        transform(s.begin(),s.end(),s.begin(),[](unsigned char c){return tolower(c);});
        // xN form
        if (s.size()>=2 && s[0]=='x') {
            bool ok=true;
            for(size_t i=1;i<s.size();i++) if(!isdigit((unsigned char)s[i])){ok=false;break;}
            if(ok){ int n=stoi(s.substr(1)); if(n>=0&&n<=31) return n; }
        }
        auto it = abiMap().find(s);
        if(it!=abiMap().end()) return it->second;
        cout << "Bad register: '" << tok << "'\n";
        hasError = true;
        return 0;
    }

    // ── Parse immediate (decimal or 0x hex) ───────────────────────
    static int32_t parseImm(const string& tok) {
        try {
            size_t pos;
            int32_t v = (int32_t)stol(tok, &pos, 0);
            return v;
        } catch(...) {
            return 0;
        }
    }

    // ── Parse  imm(rs1)  → fills imm and reg ─────────────────────
    bool parseMemOperand(const string& tok, int& reg, int32_t& imm) {
        size_t lp = tok.find('(');
        size_t rp = tok.find(')');
        if (lp==string::npos||rp==string::npos) {
            cout << "Expected imm(reg), got: " << tok << "\n";
            hasError = true; return false;
        }
        imm = parseImm(tok.substr(0, lp));
        reg = parseReg(tok.substr(lp+1, rp-lp-1));
        return true;
    }

    // ── Current byte address of next instruction ──────────────────
    uint32_t currentPC() const { return (uint32_t)(program.size() * 4); }

    // ── Emit one word; record patch if label needed ───────────────
    void emit(uint32_t word) { program.push_back(word); }

    // Emit a branch/jump with a label target; we'll fix the offset in pass 2
    void emitBranchPatch(const string& label, uint32_t partialWord, bool isJtype) {
        patches.push_back({ (int)program.size(), label, currentPC() });
        emit(partialWord);  // placeholder — will be rewritten
    }

    // ─────────────────────────────────────────────────────────────
    //  Assemble one line
    // ─────────────────────────────────────────────────────────────
    bool assembleLine(const string& rawLine)
    {
        string line = clean(rawLine);
        if (line.empty()) return true;

        // Label definition
        if (line.back() == ':') {
            string name = line.substr(0, line.size()-1);
            labelDefs[name] = (int32_t)currentPC();
            return true;
        }
        // Label on same line as instruction: "loop: addi x1,x1,1"
        {
            size_t col = line.find(':');
            if (col != string::npos && col > 0) {
                string maybe = line.substr(0, col);
                bool isLabel = true;
                for(char c:maybe) if(!isalnum(c)&&c!='_'){isLabel=false;break;}
                if(isLabel){
                    labelDefs[maybe] = (int32_t)currentPC();
                    line = clean(line.substr(col+1));
                    if(line.empty()) return true;
                }
            }
        }

        // Split mnemonic from operands
        size_t sp = line.find_first_of(" \t");
        string mnem = line.substr(0, sp);
        string rest = (sp==string::npos) ? "" : clean(line.substr(sp));
        transform(mnem.begin(),mnem.end(),mnem.begin(),[](unsigned char c){return tolower(c);});

        vector<string> ops = tokenize(rest);

        // Helper lambdas
        auto reg = [&](int i) -> int { return parseReg(ops[i]); };
        auto imm = [&](int i) -> int32_t { return parseImm(ops[i]); };
        auto need = [&](size_t n) -> bool {
            if(ops.size()<n){ cout<<"Too few operands for "<<mnem<<"\n"; hasError=true; return false; }
            return true;
        };

        // ── Pseudo-instructions ───────────────────────────────────

        if (mnem == "nop") {
            emit(encodeI(0, 0, F3::ADD_SUB, 0, OP::I_ARITH));
            return !hasError;
        }
        if (mnem == "ret") {
            emit(encodeI(0, 1/*ra*/, F3::JALR, 0, OP::JALR));
            return !hasError;
        }
        if (mnem == "mv") {
            if(!need(2)) return false;
            emit(encodeI(0, reg(1), F3::ADD_SUB, reg(0), OP::I_ARITH));
            return !hasError;
        }
        if (mnem == "li") {
            if(!need(2)) return false;
            int rd = reg(0); int32_t v = imm(1);
            if (v >= -2048 && v <= 2047) {
                emit(encodeI(v, 0, F3::ADD_SUB, rd, OP::I_ARITH));
            } else {
                // lui + addi
                int32_t hi = (v + 0x800) >> 12;
                int32_t lo = v - (hi << 12);
                emit(encodeU(hi << 12, rd, OP::LUI));
                emit(encodeI(lo, rd, F3::ADD_SUB, rd, OP::I_ARITH));
            }
            return !hasError;
        }
        if (mnem == "j") {
            if(!need(1)) return false;
            // jal x0, label
            emitBranchPatch(ops[0], encodeJ(0, 0, OP::JAL), true);
            return !hasError;
        }
        if (mnem == "call") {
            if(!need(1)) return false;
            emitBranchPatch(ops[0], encodeJ(0, 1/*ra*/, OP::JAL), true);
            return !hasError;
        }
        if (mnem == "beqz") {
            if(!need(2)) return false;
            emitBranchPatch(ops[1], encodeB(0, 0, reg(0), F3::BEQ, OP::BRANCH), false);
            return !hasError;
        }
        if (mnem == "bnez") {
            if(!need(2)) return false;
            emitBranchPatch(ops[1], encodeB(0, 0, reg(0), F3::BNE, OP::BRANCH), false);
            return !hasError;
        }

        // ── R-type ───────────────────────────────────────────────
        auto emitR = [&](uint32_t f7, uint32_t f3) {
            if(!need(3)) return;
            emit(encodeR(f7, reg(2), reg(1), f3, reg(0), OP::R_TYPE));
        };
        if(mnem=="add")  { emitR(F7::NORMAL, F3::ADD_SUB); return !hasError; }
        if(mnem=="sub")  { emitR(F7::ALT,    F3::ADD_SUB); return !hasError; }
        if(mnem=="and")  { emitR(F7::NORMAL, F3::AND);     return !hasError; }
        if(mnem=="or")   { emitR(F7::NORMAL, F3::OR);      return !hasError; }
        if(mnem=="xor")  { emitR(F7::NORMAL, F3::XOR);     return !hasError; }
        if(mnem=="sll")  { emitR(F7::NORMAL, F3::SLL);     return !hasError; }
        if(mnem=="srl")  { emitR(F7::NORMAL, F3::SRL_SRA); return !hasError; }
        if(mnem=="sra")  { emitR(F7::ALT,    F3::SRL_SRA); return !hasError; }
        if(mnem=="slt")  { emitR(F7::NORMAL, F3::SLT);     return !hasError; }
        if(mnem=="sltu") { emitR(F7::NORMAL, F3::SLTU);    return !hasError; }

        // ── I-type arithmetic ─────────────────────────────────────
        auto emitI = [&](uint32_t f3, uint32_t f7_override=F7::NORMAL) {
            if(!need(3)) return;
            int32_t i = imm(2);
            // SLLI/SRLI/SRAI: funct7 is embedded in upper imm bits
            if(f3==F3::SRL_SRA && f7_override==F7::ALT)
                i = (int32_t)(((uint32_t)i & 0x1F) | (F7::ALT << 5));
            emit(encodeI(i, reg(1), f3, reg(0), OP::I_ARITH));
        };
        if(mnem=="addi")  { emitI(F3::ADD_SUB);         return !hasError; }
        if(mnem=="andi")  { emitI(F3::AND);              return !hasError; }
        if(mnem=="ori")   { emitI(F3::OR);               return !hasError; }
        if(mnem=="xori")  { emitI(F3::XOR);              return !hasError; }
        if(mnem=="slti")  { emitI(F3::SLT);              return !hasError; }
        if(mnem=="sltiu") { emitI(F3::SLTU);             return !hasError; }
        if(mnem=="slli")  { emitI(F3::SLL);              return !hasError; }
        if(mnem=="srli")  { emitI(F3::SRL_SRA);          return !hasError; }
        if(mnem=="srai")  { emitI(F3::SRL_SRA, F7::ALT); return !hasError; }

        // ── Loads ─────────────────────────────────────────────────
        auto emitLoad = [&](uint32_t f3) {
            if(!need(2)) return;
            int base; int32_t offset;
            if(!parseMemOperand(ops[1], base, offset)) return;
            emit(encodeI(offset, base, f3, reg(0), OP::LOAD));
        };
        if(mnem=="lw")  { emitLoad(F3::WORD); return !hasError; }
        if(mnem=="lh")  { emitLoad(F3::HALF); return !hasError; }
        if(mnem=="lb")  { emitLoad(F3::BYTE); return !hasError; }
        if(mnem=="lhu") { emitLoad(0b101);    return !hasError; }
        if(mnem=="lbu") { emitLoad(0b100);    return !hasError; }

        // ── Stores ────────────────────────────────────────────────
        auto emitStore = [&](uint32_t f3) {
            if(!need(2)) return;
            int base; int32_t offset;
            if(!parseMemOperand(ops[1], base, offset)) return;
            emit(encodeS(offset, reg(0), base, f3, OP::STORE));
        };
        if(mnem=="sw") { emitStore(F3::WORD); return !hasError; }
        if(mnem=="sh") { emitStore(F3::HALF); return !hasError; }
        if(mnem=="sb") { emitStore(F3::BYTE); return !hasError; }

        // ── Branches ──────────────────────────────────────────────
        auto emitBranch = [&](uint32_t f3) {
            if(!need(3)) return;
            emitBranchPatch(ops[2], encodeB(0, reg(1), reg(0), f3, OP::BRANCH), false);
        };
        if(mnem=="beq")  { emitBranch(F3::BEQ);  return !hasError; }
        if(mnem=="bne")  { emitBranch(F3::BNE);  return !hasError; }
        if(mnem=="blt")  { emitBranch(F3::BLT);  return !hasError; }
        if(mnem=="bge")  { emitBranch(F3::BGE);  return !hasError; }
        if(mnem=="bltu") { emitBranch(0b110);    return !hasError; }
        if(mnem=="bgeu") { emitBranch(0b111);    return !hasError; }

        // ── JAL ───────────────────────────────────────────────────
        if(mnem=="jal") {
            if(!need(2)) return false;
            emitBranchPatch(ops[1], encodeJ(0, reg(0), OP::JAL), true);
            return !hasError;
        }

        // ── JALR ──────────────────────────────────────────────────
        if(mnem=="jalr") {
            if(!need(3)) return false;
            emit(encodeI(imm(2), reg(1), F3::JALR, reg(0), OP::JALR));
            return !hasError;
        }

        // ── LUI / AUIPC ───────────────────────────────────────────
        if(mnem=="lui") {
            if(!need(2)) return false;
            int32_t v = imm(1);
            emit(encodeU(v << 12, reg(0), OP::LUI));
            return !hasError;
        }
        if(mnem=="auipc") {
            if(!need(2)) return false;
            int32_t v = imm(1);
            emit(encodeU(v << 12, reg(0), OP::AUIPC));
            return !hasError;
        }

        // ── ECALL / EBREAK ────────────────────────────────────────
        if(mnem=="ecall")  { emit(0x00000073); return !hasError; }
        if(mnem=="ebreak") { emit(0x00100073); return !hasError; }

        cout << "Unknown mnemonic: '" << mnem << "'\n";
        hasError = true;
        return false;
    }

    // ─────────────────────────────────────────────────────────────
    //  Pass 2: resolve all label patches
    // ─────────────────────────────────────────────────────────────
    bool resolveLabels()
    {
        bool ok = true;
        for (auto& p : patches) {
            auto it = labelDefs.find(p.label);
            if (it == labelDefs.end()) {
                cout << "Undefined label: '" << p.label << "'\n";
                ok = false; continue;
            }
            int32_t offset = it->second - (int32_t)p.instrPC;
            uint32_t old = program[p.idx];
            uint32_t op  = decodeOpcode(old);

            if (op == OP::JAL) {
                uint32_t rd = decodeRd(old);
                program[p.idx] = encodeJ(offset, rd, OP::JAL);
            } else if (op == OP::BRANCH) {
                uint32_t f3  = decodeFunct3(old);
                uint32_t rs1 = decodeRs1(old);
                uint32_t rs2 = decodeRs2(old);
                program[p.idx] = encodeB(offset, rs2, rs1, f3, OP::BRANCH);
            } else {
                cout << "Internal: unexpected opcode in patch\n";
                ok = false;
            }
        }
        return ok;
    }

    // ─────────────────────────────────────────────────────────────
    //  Public interface
    // ─────────────────────────────────────────────────────────────
    bool loadFile(const string& path)
    {
        ifstream f(path);
        if (!f.is_open()) { cout << "Cannot open '" << path << "'\n"; return false; }
        string line; int n = 0;
        while (getline(f, line)) {
            n++;
            if (!assembleLine(line)) {
                cout << "  at line " << n << ": " << line << "\n";
            }
        }
        return !hasError;
    }

    bool loadInteractive()
    {
        cout << "RV32I assembler  (type 'run' to execute)\n";
        cout << "Examples:\n";
        cout << "  addi x1, x0, 10\n";
        cout << "  add  x3, x1, x2\n";
        cout << "  sw   x3, 0(x2)\n";
        cout << "  beq  x1, x0, done\n";
        cout << "  done: ecall\n\n";
        string line;
        while (true) {
            cout << "rv32i> ";
            getline(cin, line);
            if (line == "run") break;
            assembleLine(line);
        }
        return !hasError;
    }

    bool finalise(CPU& cpu)
    {
        if (!resolveLabels()) return false;
        cpu.loadProgram(program);
        return true;
    }

    void dump() const
    {
        cout << "\nAssembled " << program.size() << " instructions:\n";
        if (!labelDefs.empty()) {
            cout << "Labels:\n";
            for (auto& [k,v] : labelDefs)
                cout << "  " << k << " -> 0x" << hex << v << dec << "\n";
        }
        for (size_t i = 0; i < program.size(); i++)
            cout << "  [0x" << hex << setw(4) << setfill('0') << i*4
                 << "] 0x" << setw(8) << program[i] << dec << "\n";
        cout << setfill(' ');
    }
};