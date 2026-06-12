#pragma once
#include "encode.h"
#include <vector>
#include <array>
#include <cstdint>
#include <iostream>
#include <iomanip>
#include <string>

using namespace std;

// ─────────────────────────────────────────────────────────────────
//  Memory
//  Unified 1 MB byte-addressable address space.
//  Instructions live at 0x00000 upward (word-aligned).
//  Stack grows down from 0xFFFFC.
// ─────────────────────────────────────────────────────────────────
constexpr int MEM_SIZE = 1 << 20;   // 1 MB

// ─────────────────────────────────────────────────────────────────
//  CPU state
// ─────────────────────────────────────────────────────────────────
struct CPU {
    array<int32_t, 32>  x      = {0};  // x0–x31  (x0 always 0)
    uint32_t            pc     = 0;
    array<uint8_t, MEM_SIZE> mem = {0};

    // ── Memory helpers ───────────────────────────────────────────
    uint32_t loadWord(uint32_t addr) const {
        return  (uint32_t)mem[addr]         |
               ((uint32_t)mem[addr+1] <<  8)|
               ((uint32_t)mem[addr+2] << 16)|
               ((uint32_t)mem[addr+3] << 24);
    }
    void storeWord(uint32_t addr, uint32_t val) {
        mem[addr]   =  val        & 0xFF;
        mem[addr+1] = (val >>  8) & 0xFF;
        mem[addr+2] = (val >> 16) & 0xFF;
        mem[addr+3] = (val >> 24) & 0xFF;
    }
    uint16_t loadHalf(uint32_t addr) const {
        return (uint16_t)mem[addr] | ((uint16_t)mem[addr+1] << 8);
    }
    void storeHalf(uint32_t addr, uint16_t val) {
        mem[addr]   =  val       & 0xFF;
        mem[addr+1] = (val >> 8) & 0xFF;
    }
    uint8_t loadByte(uint32_t addr) const { return mem[addr]; }
    void storeByte(uint32_t addr, uint8_t val) { mem[addr] = val; }

    // Load a program (array of 32-bit words) starting at address 0
    void loadProgram(const vector<uint32_t>& words) {
        for (size_t i = 0; i < words.size(); i++)
            storeWord((uint32_t)(i * 4), words[i]);
    }
};

// ─────────────────────────────────────────────────────────────────
//  ABI register names  (for pretty-printing)
// ─────────────────────────────────────────────────────────────────
static const char* ABI_NAMES[32] = {
    "zero","ra","sp","gp","tp","t0","t1","t2",
    "s0",  "s1","a0","a1","a2","a3","a4","a5",
    "a6",  "a7","s2","s3","s4","s5","s6","s7",
    "s8",  "s9","s10","s11","t3","t4","t5","t6"
};

// ─────────────────────────────────────────────────────────────────
//  Execute ONE decoded instruction.
//  Returns false if HALT/EBREAK or illegal instruction.
// ─────────────────────────────────────────────────────────────────
inline bool executeOne(CPU& cpu, uint32_t instr, bool trace)
{
    // All-zeros and all-ones are illegal — treat as HALT
    if (instr == 0x00000000 || instr == 0xFFFFFFFF) {
        if (trace) cout << "HALT\n";
        return false;
    }

    uint32_t opcode = decodeOpcode(instr);
    uint32_t rd     = decodeRd(instr);
    uint32_t funct3 = decodeFunct3(instr);
    uint32_t rs1    = decodeRs1(instr);
    uint32_t rs2    = decodeRs2(instr);
    uint32_t funct7 = decodeFunct7(instr);

    // We compute the next PC here; branches/jumps overwrite it
    uint32_t nextPC = cpu.pc + 4;

    auto reg = [&](uint32_t r) -> int32_t { return r == 0 ? 0 : cpu.x[r]; };

    switch (opcode)
    {
    // ── R-type ───────────────────────────────────────────────────
    case OP::R_TYPE:
    {
        int32_t a = reg(rs1), b = reg(rs2);
        int32_t result = 0;
        const char* name = "?";

        if (funct7 == F7::NORMAL) {
            switch (funct3) {
                case F3::ADD_SUB: result = a + b;              name = "ADD";  break;
                case F3::SLL:     result = a << (b & 31);      name = "SLL";  break;
                case F3::SLT:     result = (a < b) ? 1 : 0;   name = "SLT";  break;
                case F3::SLTU:    result = ((uint32_t)a < (uint32_t)b) ? 1 : 0; name = "SLTU"; break;
                case F3::XOR:     result = a ^ b;              name = "XOR";  break;
                case F3::SRL_SRA: result = (int32_t)((uint32_t)a >> (b & 31)); name = "SRL"; break;
                case F3::OR:      result = a | b;              name = "OR";   break;
                case F3::AND:     result = a & b;              name = "AND";  break;
            }
        } else if (funct7 == F7::ALT) {
            switch (funct3) {
                case F3::ADD_SUB: result = a - b;              name = "SUB";  break;
                case F3::SRL_SRA: result = a >> (b & 31);      name = "SRA";  break;
                default:
                    if (trace) cout << "Illegal funct3 with funct7=ALT\n";
                    return false;
            }
        } else {
            if (trace) cout << "Illegal funct7=" << funct7 << "\n";
            return false;
        }

        if (rd != 0) cpu.x[rd] = result;
        if (trace)
            cout << name << " x" << rd << "(" << ABI_NAMES[rd] << ")"
                 << " = x" << rs1 << "(" << a << ")"
                 << " op x" << rs2 << "(" << b << ")"
                 << " -> " << result << "\n";
        break;
    }

    // ── I-type arithmetic ─────────────────────────────────────────
    case OP::I_ARITH:
    {
        int32_t a   = reg(rs1);
        int32_t imm = decodeImmI(instr);
        int32_t result = 0;
        const char* name = "?";

        switch (funct3) {
            case F3::ADD_SUB: result = a + imm;              name = "ADDI";  break;
            case F3::SLT:     result = (a < imm) ? 1 : 0;   name = "SLTI";  break;
            case F3::SLTU:    result = ((uint32_t)a < (uint32_t)imm) ? 1 : 0; name = "SLTIU"; break;
            case F3::XOR:     result = a ^ imm;              name = "XORI";  break;
            case F3::OR:      result = a | imm;              name = "ORI";   break;
            case F3::AND:     result = a & imm;              name = "ANDI";  break;
            case F3::SLL: {
                uint32_t shamt = (uint32_t)imm & 0x1F;
                result = a << shamt;
                name = "SLLI";
                break;
            }
            case F3::SRL_SRA: {
                uint32_t shamt = (uint32_t)imm & 0x1F;
                if (funct7 == F7::ALT) { result = a >> shamt;                     name = "SRAI"; }
                else                   { result = (int32_t)((uint32_t)a >> shamt); name = "SRLI"; }
                break;
            }
            default:
                if (trace) cout << "Illegal I-arith funct3\n";
                return false;
        }

        if (rd != 0) cpu.x[rd] = result;
        if (trace)
            cout << name << " x" << rd << "(" << ABI_NAMES[rd] << ")"
                 << " = x" << rs1 << "(" << a << ") op " << imm
                 << " -> " << result << "\n";
        break;
    }

    // ── Load ─────────────────────────────────────────────────────
    case OP::LOAD:
    {
        int32_t  base = reg(rs1);
        int32_t  imm  = decodeImmI(instr);
        uint32_t addr = (uint32_t)(base + imm);
        int32_t  result = 0;
        const char* name = "?";

        switch (funct3) {
            case F3::BYTE: result = signExt(cpu.loadByte(addr), 8);  name = "LB";  break;
            case F3::HALF: result = signExt(cpu.loadHalf(addr), 16); name = "LH";  break;
            case F3::WORD: result = (int32_t)cpu.loadWord(addr);     name = "LW";  break;
            case 0b100:    result = cpu.loadByte(addr);              name = "LBU"; break;
            case 0b101:    result = cpu.loadHalf(addr);              name = "LHU"; break;
            default:
                if (trace) cout << "Illegal load funct3\n";
                return false;
        }

        if (rd != 0) cpu.x[rd] = result;
        if (trace)
            cout << name << " x" << rd << "(" << ABI_NAMES[rd] << ")"
                 << " <- MEM[0x" << hex << addr << dec
                 << "] = " << result << "\n";
        break;
    }

    // ── Store ────────────────────────────────────────────────────
    case OP::STORE:
    {
        int32_t  base = reg(rs1);
        int32_t  imm  = decodeImmS(instr);
        uint32_t addr = (uint32_t)(base + imm);
        int32_t  val  = reg(rs2);
        const char* name = "?";

        switch (funct3) {
            case F3::BYTE: cpu.storeByte(addr, (uint8_t)val);  name = "SB"; break;
            case F3::HALF: cpu.storeHalf(addr, (uint16_t)val); name = "SH"; break;
            case F3::WORD: cpu.storeWord(addr, (uint32_t)val); name = "SW"; break;
            default:
                if (trace) cout << "Illegal store funct3\n";
                return false;
        }

        if (trace)
            cout << name << " x" << rs2 << "(" << val << ")"
                 << " -> MEM[0x" << hex << addr << dec << "]\n";
        break;
    }

    // ── Branch ───────────────────────────────────────────────────
    case OP::BRANCH:
    {
        int32_t a   = reg(rs1), b = reg(rs2);
        int32_t imm = decodeImmB(instr);
        bool taken  = false;
        const char* name = "?";

        switch (funct3) {
            case F3::BEQ: taken = (a == b);                         name = "BEQ"; break;
            case F3::BNE: taken = (a != b);                         name = "BNE"; break;
            case F3::BLT: taken = (a <  b);                         name = "BLT"; break;
            case F3::BGE: taken = (a >= b);                         name = "BGE"; break;
            case 0b110:   taken = ((uint32_t)a <  (uint32_t)b);     name = "BLTU"; break;
            case 0b111:   taken = ((uint32_t)a >= (uint32_t)b);     name = "BGEU"; break;
            default:
                if (trace) cout << "Illegal branch funct3\n";
                return false;
        }

        if (taken) nextPC = (uint32_t)((int32_t)cpu.pc + imm);

        if (trace)
            cout << name << " x" << rs1 << "(" << a << ")"
                 << " vs x" << rs2 << "(" << b << ")"
                 << (taken ? "  TAKEN" : "  not taken")
                 << " -> 0x" << hex << nextPC << dec << "\n";
        break;
    }

    // ── JAL ──────────────────────────────────────────────────────
    case OP::JAL:
    {
        int32_t imm = decodeImmJ(instr);
        if (rd != 0) cpu.x[rd] = (int32_t)(cpu.pc + 4);  // return address
        nextPC = (uint32_t)((int32_t)cpu.pc + imm);

        if (trace)
            cout << "JAL x" << rd << "(" << ABI_NAMES[rd] << ")"
                 << " -> 0x" << hex << nextPC << dec << "\n";
        break;
    }

    // ── JALR ─────────────────────────────────────────────────────
    case OP::JALR:
    {
        int32_t imm = decodeImmI(instr);
        uint32_t ret = cpu.pc + 4;
        nextPC = (uint32_t)((reg(rs1) + imm) & ~1u);  // clear lowest bit per spec
        if (rd != 0) cpu.x[rd] = (int32_t)ret;

        if (trace)
            cout << "JALR x" << rd << "(" << ABI_NAMES[rd] << ")"
                 << " = 0x" << hex << ret << " -> 0x" << nextPC << dec << "\n";
        break;
    }

    // ── LUI ──────────────────────────────────────────────────────
    case OP::LUI:
    {
        int32_t imm = decodeImmU(instr);
        if (rd != 0) cpu.x[rd] = imm;
        if (trace)
            cout << "LUI x" << rd << "(" << ABI_NAMES[rd] << ")"
                 << " = 0x" << hex << (uint32_t)imm << dec << "\n";
        break;
    }

    // ── AUIPC ────────────────────────────────────────────────────
    case OP::AUIPC:
    {
        int32_t imm = decodeImmU(instr);
        if (rd != 0) cpu.x[rd] = (int32_t)(cpu.pc + (uint32_t)imm);
        if (trace)
            cout << "AUIPC x" << rd << "(" << ABI_NAMES[rd] << ")"
                 << " = PC(0x" << hex << cpu.pc << ") + 0x" << (uint32_t)imm
                 << " = 0x" << (uint32_t)cpu.x[rd] << dec << "\n";
        break;
    }

    // ── SYSTEM (ECALL / EBREAK treated as HALT for now) ──────────
    case OP::SYSTEM:
        if (trace) cout << "ECALL/EBREAK -> halting\n";
        return false;

    default:
        if (trace)
            cout << "Illegal opcode 0x" << hex << opcode << dec
                 << " at PC=0x" << hex << cpu.pc << dec << "\n";
        return false;
    }

    cpu.x[0] = 0;   // x0 always 0
    cpu.pc   = nextPC;
    return true;
}

// ─────────────────────────────────────────────────────────────────
//  Run until illegal instruction / ECALL / all-zero word
// ─────────────────────────────────────────────────────────────────
inline void runCPU(CPU& cpu, bool trace = true, int maxSteps = 100000)
{
    int steps = 0;
    while (steps++ < maxSteps) {
        if (cpu.pc + 3 >= MEM_SIZE) {
            cout << "PC out of bounds: 0x" << hex << cpu.pc << dec << "\n";
            break;
        }
        uint32_t instr = cpu.loadWord(cpu.pc);
        if (!executeOne(cpu, instr, trace))
            break;
    }
    if (steps > maxSteps)
        cout << "Step limit reached (infinite loop?)\n";

    cout << "\nFinal Registers:\n";
    for (int i = 0; i < 32; i++) {
        if (cpu.x[i] != 0 || i < 4)
            cout << "  x" << setw(2) << i << " (" << setw(4) << left
                 << ABI_NAMES[i] << right << ") = " << cpu.x[i] << "\n";
    }
}