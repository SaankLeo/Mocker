#pragma once
#include "encode.h"
#include <array>
#include <cstdint>
#include <iostream>
#include <iomanip>
#include <string>
#include <vector>

using namespace std;

// ─────────────────────────────────────────────────────────────────
//  Memory — shared flat 1 MB, byte-addressable
// ─────────────────────────────────────────────────────────────────
constexpr uint32_t MEM_SIZE     = 1 << 20;   // 1 MB
constexpr uint32_t HART_SPACING = 0x1000;    // 4 KB per hart code slot

// ─────────────────────────────────────────────────────────────────
//  Hart states
// ─────────────────────────────────────────────────────────────────
enum class HartState { RUNNING, HALTED, WAITING, FAULTED };

inline const char* stateName(HartState s) {
    switch(s) {
        case HartState::RUNNING: return "RUNNING";
        case HartState::HALTED:  return "HALTED";
        case HartState::WAITING: return "WAITING";
        case HartState::FAULTED: return "FAULTED";
    }
    return "?";
}

// ─────────────────────────────────────────────────────────────────
//  Shared memory (one instance, owned by HartManager)
// ─────────────────────────────────────────────────────────────────
struct Memory {
    array<uint8_t, MEM_SIZE> data = {0};

    uint32_t loadWord(uint32_t addr) const {
        return  (uint32_t)data[addr]         |
               ((uint32_t)data[addr+1] <<  8)|
               ((uint32_t)data[addr+2] << 16)|
               ((uint32_t)data[addr+3] << 24);
    }
    void storeWord(uint32_t addr, uint32_t val) {
        data[addr]   =  val        & 0xFF;
        data[addr+1] = (val >>  8) & 0xFF;
        data[addr+2] = (val >> 16) & 0xFF;
        data[addr+3] = (val >> 24) & 0xFF;
    }
    uint16_t loadHalf(uint32_t addr) const {
        return (uint16_t)data[addr] | ((uint16_t)data[addr+1] << 8);
    }
    void storeHalf(uint32_t addr, uint16_t val) {
        data[addr]   =  val       & 0xFF;
        data[addr+1] = (val >> 8) & 0xFF;
    }
    uint8_t  loadByte(uint32_t addr)          const { return data[addr]; }
    void     storeByte(uint32_t addr, uint8_t v)    { data[addr] = v; }

    void loadProgram(const vector<uint32_t>& words, uint32_t base) {
        for (size_t i = 0; i < words.size(); i++)
            storeWord(base + (uint32_t)(i * 4), words[i]);
    }
};

// ─────────────────────────────────────────────────────────────────
//  Hart  (hardware thread) — registers + PC + state
//  Does NOT own memory; takes a pointer to shared Memory.
// ─────────────────────────────────────────────────────────────────
struct Hart {
    int                  id       = 0;
    HartState            state    = HartState::RUNNING;
    array<int32_t, 32>   x        = {0};   // x0–x31
    uint32_t             pc       = 0;
    uint64_t             steps    = 0;     // total instructions executed
    Memory*              mem      = nullptr;

    // Stack pointer init: each hart gets its own stack region
    // stack top = MEM_SIZE - id * 0x4000  (16 KB per hart)
    void init(int hartId, Memory* sharedMem, uint32_t entryPC) {
        id    = hartId;
        mem   = sharedMem;
        pc    = entryPC;
        state = HartState::RUNNING;
        x.fill(0);
        // x2 = sp, grows down from top of this hart's stack region
        x[2] = (int32_t)(MEM_SIZE - hartId * 0x4000 - 4);
    }
};

// ─────────────────────────────────────────────────────────────────
//  ABI names
// ─────────────────────────────────────────────────────────────────
static const char* ABI_NAMES[32] = {
    "zero","ra","sp","gp","tp","t0","t1","t2",
    "s0",  "s1","a0","a1","a2","a3","a4","a5",
    "a6",  "a7","s2","s3","s4","s5","s6","s7",
    "s8",  "s9","s10","s11","t3","t4","t5","t6"
};

// ─────────────────────────────────────────────────────────────────
//  Execute ONE instruction on a hart.
//  Returns false → hart should stop (HALT / ECALL / fault).
// ─────────────────────────────────────────────────────────────────
inline bool executeOne(Hart& h, bool trace)
{
    if (h.pc + 3 >= MEM_SIZE) {
        if (trace)
            cout << "[hart" << h.id << "] PC out of bounds: 0x"
                 << hex << h.pc << dec << "\n";
        h.state = HartState::FAULTED;
        return false;
    }

    uint32_t instr = h.mem->loadWord(h.pc);

    if (instr == 0x00000000 || instr == 0xFFFFFFFF) {
        if (trace) cout << "[hart" << h.id << "] HALT\n";
        h.state = HartState::HALTED;
        return false;
    }

    uint32_t opcode = decodeOpcode(instr);
    uint32_t rd     = decodeRd(instr);
    uint32_t funct3 = decodeFunct3(instr);
    uint32_t rs1    = decodeRs1(instr);
    uint32_t rs2    = decodeRs2(instr);
    uint32_t funct7 = decodeFunct7(instr);

    uint32_t nextPC = h.pc + 4;

    auto reg = [&](uint32_t r) -> int32_t { return r == 0 ? 0 : h.x[r]; };
    auto prefix = [&]() -> string {
        return trace ? ("[hart" + to_string(h.id) + "] ") : "";
    };

    switch (opcode)
    {
    case OP::R_TYPE: {
        int32_t a = reg(rs1), b = reg(rs2), result = 0;
        const char* name = "?";
        if (funct7 == F7::NORMAL) {
            switch(funct3) {
                case F3::ADD_SUB: result=a+b;                          name="ADD";  break;
                case F3::SLL:     result=a<<(b&31);                    name="SLL";  break;
                case F3::SLT:     result=(a<b)?1:0;                    name="SLT";  break;
                case F3::SLTU:    result=((uint32_t)a<(uint32_t)b)?1:0;name="SLTU"; break;
                case F3::XOR:     result=a^b;                          name="XOR";  break;
                case F3::SRL_SRA: result=(int32_t)((uint32_t)a>>(b&31));name="SRL"; break;
                case F3::OR:      result=a|b;                          name="OR";   break;
                case F3::AND:     result=a&b;                          name="AND";  break;
            }
        } else if (funct7 == F7::ALT) {
            switch(funct3) {
                case F3::ADD_SUB: result=a-b;       name="SUB"; break;
                case F3::SRL_SRA: result=a>>(b&31); name="SRA"; break;
                default: h.state=HartState::FAULTED; return false;
            }
        } else { h.state=HartState::FAULTED; return false; }
        if (rd!=0) h.x[rd]=result;
        if (trace) cout << prefix() << name << " x"<<rd<<"="<<a<<" op "<<b<<" -> "<<result<<"\n";
        break;
    }

    case OP::I_ARITH: {
        int32_t a=reg(rs1), imm=decodeImmI(instr), result=0;
        const char* name="?";
        switch(funct3) {
            case F3::ADD_SUB: result=a+imm;                             name="ADDI";  break;
            case F3::SLT:     result=(a<imm)?1:0;                       name="SLTI";  break;
            case F3::SLTU:    result=((uint32_t)a<(uint32_t)imm)?1:0;   name="SLTIU"; break;
            case F3::XOR:     result=a^imm;                             name="XORI";  break;
            case F3::OR:      result=a|imm;                             name="ORI";   break;
            case F3::AND:     result=a&imm;                             name="ANDI";  break;
            case F3::SLL:     result=a<<((uint32_t)imm&0x1F);           name="SLLI";  break;
            case F3::SRL_SRA: {
                uint32_t shamt=(uint32_t)imm&0x1F;
                if(funct7==F7::ALT){result=a>>shamt; name="SRAI";}
                else{result=(int32_t)((uint32_t)a>>shamt); name="SRLI";}
                break;
            }
            default: h.state=HartState::FAULTED; return false;
        }
        if (rd!=0) h.x[rd]=result;
        if (trace) cout<<prefix()<<name<<" x"<<rd<<"="<<a<<" op "<<imm<<" -> "<<result<<"\n";
        break;
    }

    case OP::LOAD: {
        int32_t base=reg(rs1), imm=decodeImmI(instr);
        uint32_t addr=(uint32_t)(base+imm);
        int32_t result=0; const char* name="?";
        switch(funct3){
            case F3::BYTE: result=signExt(h.mem->loadByte(addr),8);  name="LB";  break;
            case F3::HALF: result=signExt(h.mem->loadHalf(addr),16); name="LH";  break;
            case F3::WORD: result=(int32_t)h.mem->loadWord(addr);    name="LW";  break;
            case 0b100:    result=h.mem->loadByte(addr);             name="LBU"; break;
            case 0b101:    result=h.mem->loadHalf(addr);             name="LHU"; break;
            default: h.state=HartState::FAULTED; return false;
        }
        if (rd!=0) h.x[rd]=result;
        if (trace) cout<<prefix()<<name<<" x"<<rd<<" <- MEM[0x"<<hex<<addr<<dec<<"]="<<result<<"\n";
        break;
    }

    case OP::STORE: {
        int32_t base=reg(rs1), imm=decodeImmS(instr);
        uint32_t addr=(uint32_t)(base+imm);
        int32_t val=reg(rs2); const char* name="?";
        switch(funct3){
            case F3::BYTE: h.mem->storeByte(addr,(uint8_t)val);  name="SB"; break;
            case F3::HALF: h.mem->storeHalf(addr,(uint16_t)val); name="SH"; break;
            case F3::WORD: h.mem->storeWord(addr,(uint32_t)val); name="SW"; break;
            default: h.state=HartState::FAULTED; return false;
        }
        if (trace) cout<<prefix()<<name<<" x"<<rs2<<"("<<val<<") -> MEM[0x"<<hex<<addr<<dec<<"]\n";
        break;
    }

    case OP::BRANCH: {
        int32_t a=reg(rs1), b=reg(rs2), imm=decodeImmB(instr);
        bool taken=false; const char* name="?";
        switch(funct3){
            case F3::BEQ:  taken=(a==b);                        name="BEQ";  break;
            case F3::BNE:  taken=(a!=b);                        name="BNE";  break;
            case F3::BLT:  taken=(a<b);                         name="BLT";  break;
            case F3::BGE:  taken=(a>=b);                        name="BGE";  break;
            case 0b110:    taken=((uint32_t)a<(uint32_t)b);     name="BLTU"; break;
            case 0b111:    taken=((uint32_t)a>=(uint32_t)b);    name="BGEU"; break;
            default: h.state=HartState::FAULTED; return false;
        }
        if (taken) nextPC=(uint32_t)((int32_t)h.pc+imm);
        if (trace) cout<<prefix()<<name<<" "<<a<<" vs "<<b<<(taken?" TAKEN":" not taken")
                       <<" -> 0x"<<hex<<nextPC<<dec<<"\n";
        break;
    }

    case OP::JAL: {
        int32_t imm=decodeImmJ(instr);
        if(rd!=0) h.x[rd]=(int32_t)(h.pc+4);
        nextPC=(uint32_t)((int32_t)h.pc+imm);
        if (trace) cout<<prefix()<<"JAL x"<<rd<<" -> 0x"<<hex<<nextPC<<dec<<"\n";
        break;
    }

    case OP::JALR: {
        int32_t imm=decodeImmI(instr);
        uint32_t ret=h.pc+4;
        nextPC=(uint32_t)((reg(rs1)+imm)&~1u);
        if(rd!=0) h.x[rd]=(int32_t)ret;
        if (trace) cout<<prefix()<<"JALR x"<<rd<<" -> 0x"<<hex<<nextPC<<dec<<"\n";
        break;
    }

    case OP::LUI: {
        int32_t imm=decodeImmU(instr);
        if(rd!=0) h.x[rd]=imm;
        if (trace) cout<<prefix()<<"LUI x"<<rd<<" = 0x"<<hex<<(uint32_t)imm<<dec<<"\n";
        break;
    }

    case OP::AUIPC: {
        int32_t imm=decodeImmU(instr);
        if(rd!=0) h.x[rd]=(int32_t)(h.pc+(uint32_t)imm);
        if (trace) cout<<prefix()<<"AUIPC x"<<rd<<" = 0x"<<hex<<(uint32_t)h.x[rd]<<dec<<"\n";
        break;
    }

    case OP::SYSTEM: {
        // ecall: a7 = syscall number (we'll expand in Phase 6)
        // For now: any ecall halts the hart cleanly
        uint32_t syscall = (uint32_t)h.x[17]; // a7
        if (trace)
            cout << prefix() << "ECALL a7=" << syscall
                 << " a0=" << h.x[10] << "\n";
        h.state = HartState::HALTED;
        return false;
    }

    default:
        if (trace)
            cout << prefix() << "ILLEGAL opcode=0x" << hex << opcode
                 << " at PC=0x" << h.pc << dec << "\n";
        h.state = HartState::FAULTED;
        return false;
    }

    h.x[0] = 0;
    h.pc   = nextPC;
    h.steps++;
    return true;
}

// ─────────────────────────────────────────────────────────────────
//  Pretty-print one hart's final state
// ─────────────────────────────────────────────────────────────────
inline void printHart(const Hart& h)
{
    cout << "--- Hart " << h.id << " [" << stateName(h.state) << "]"
         << "  steps=" << h.steps
         << "  PC=0x" << hex << h.pc << dec << "\n";
    for (int i = 0; i < 32; i++) {
        if (h.x[i] != 0 || i < 3)
            cout << "  x" << setw(2) << i
                 << " (" << setw(4) << left << ABI_NAMES[i] << right
                 << ") = " << h.x[i] << "\n";
    }
}