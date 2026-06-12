#pragma once
#include "encode.h"
#include "memory.h"
#include "csr.h"
#include <array>
#include <cstdint>
#include <iostream>
#include <iomanip>
#include <string>
#include <vector>

using namespace std;

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
//  Hart
// ─────────────────────────────────────────────────────────────────
struct Hart {
    int                  id       = 0;
    HartState            state    = HartState::RUNNING;
    PrivMode             mode     = PrivMode::U;    // start in U-mode
    array<int32_t, 32>   x        = {0};
    uint32_t             pc       = 0;
    uint64_t             steps    = 0;
    CSRFile              csr;
    ProtectedMemory*     mem      = nullptr;

    // Pending timer interrupt flag (set by scheduler)
    bool                 timerPending = false;

    void init(int hartId, ProtectedMemory* sharedMem, uint32_t entryPC) {
        id    = hartId;
        mem   = sharedMem;
        pc    = entryPC;
        state = HartState::RUNNING;
        mode  = PrivMode::U;
        x.fill(0);
        x[2] = (int32_t)hartStackTop(hartId);   // sp
        csr.hartid = (uint32_t)hartId;
        // Timer interrupts enabled by default
        csr.mie     = (1u << 7);  // MTIE
        csr.mstatus = MStatus::MIE | MStatus::MPIE | MStatus::MPP_U;
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
//  Trap delivery
//  Saves state, switches to M-mode, jumps to mtvec.
//  Returns false so executeOne can return immediately.
// ─────────────────────────────────────────────────────────────────
inline void deliverTrap(Hart& h, uint32_t cause, uint32_t tval, bool trace)
{
    if (trace)
        cout << "[hart" << h.id << "]["<< privName(h.mode) <<"] TRAP"
             << " cause=0x" << hex << cause << dec
             << " mepc=0x"  << hex << h.pc  << dec
             << " tval=0x"  << hex << tval  << dec << "\n";

    h.csr.trapEnter(h.pc, cause, tval, h.mode);
    h.mode = PrivMode::M;
    h.pc   = h.csr.mtvec & ~3u;   // jump to trap handler
}

// ─────────────────────────────────────────────────────────────────
//  Execute ONE instruction.
//  Returns false → hart should pause this tick (trap, halt, ecall).
// ─────────────────────────────────────────────────────────────────
inline bool executeOne(Hart& h, bool trace)
{
    // ── Check for pending timer interrupt ─────────────────────────
    if (h.timerPending && h.csr.interruptsEnabled() && h.mode == PrivMode::U) {
        h.timerPending = false;
        deliverTrap(h, Cause::TIMER_INT, 0, trace);
        return false;   // used this tick to deliver the interrupt
    }

    // ── Fetch ─────────────────────────────────────────────────────
    int fetchHartId = (h.mode == PrivMode::M) ? -1 : h.id;
    MemFault fault;
    uint32_t instr = h.mem->loadWord(h.pc, fetchHartId, fault);
    if (fault.occurred) {
        deliverTrap(h, Cause::INSN_FAULT, h.pc, trace);
        return false;
    }

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

    // Memory access uses hart id in U-mode, -1 in M-mode
    int memId = (h.mode == PrivMode::M) ? -1 : h.id;

    auto reg = [&](uint32_t r) -> int32_t { return r==0 ? 0 : h.x[r]; };
    auto pfx = [&]() { return "[hart"+to_string(h.id)+"]["+string(privName(h.mode))+"] "; };

#define TRACE if(trace) cout << pfx()

    switch(opcode)
    {
    // ── R-type ───────────────────────────────────────────────────
    case OP::R_TYPE: {
        int32_t a=reg(rs1), b=reg(rs2), r=0;
        const char* n="?";
        if(funct7==F7::NORMAL){
            switch(funct3){
                case F3::ADD_SUB: r=a+b;                           n="ADD";  break;
                case F3::SLL:     r=a<<(b&31);                     n="SLL";  break;
                case F3::SLT:     r=(a<b)?1:0;                     n="SLT";  break;
                case F3::SLTU:    r=((uint32_t)a<(uint32_t)b)?1:0; n="SLTU"; break;
                case F3::XOR:     r=a^b;                           n="XOR";  break;
                case F3::SRL_SRA: r=(int32_t)((uint32_t)a>>(b&31));n="SRL";  break;
                case F3::OR:      r=a|b;                           n="OR";   break;
                case F3::AND:     r=a&b;                           n="AND";  break;
            }
        } else if(funct7==F7::ALT){
            switch(funct3){
                case F3::ADD_SUB: r=a-b;      n="SUB"; break;
                case F3::SRL_SRA: r=a>>(b&31);n="SRA"; break;
                default: deliverTrap(h,Cause::ILLEGAL_INSN,instr,trace); return false;
            }
        } else { deliverTrap(h,Cause::ILLEGAL_INSN,instr,trace); return false; }
        if(rd!=0) h.x[rd]=r;
        TRACE << n<<" x"<<rd<<"="<<a<<" op "<<b<<" -> "<<r<<"\n";
        break;
    }

    // ── I-type arith ─────────────────────────────────────────────
    case OP::I_ARITH: {
        int32_t a=reg(rs1), imm=decodeImmI(instr), r=0;
        const char* n="?";
        switch(funct3){
            case F3::ADD_SUB: r=a+imm;                              n="ADDI";  break;
            case F3::SLT:     r=(a<imm)?1:0;                        n="SLTI";  break;
            case F3::SLTU:    r=((uint32_t)a<(uint32_t)imm)?1:0;    n="SLTIU"; break;
            case F3::XOR:     r=a^imm;                              n="XORI";  break;
            case F3::OR:      r=a|imm;                              n="ORI";   break;
            case F3::AND:     r=a&imm;                              n="ANDI";  break;
            case F3::SLL:     r=a<<((uint32_t)imm&0x1F);            n="SLLI";  break;
            case F3::SRL_SRA: {
                uint32_t sh=(uint32_t)imm&0x1F;
                if(funct7==F7::ALT){r=a>>sh; n="SRAI";}
                else{r=(int32_t)((uint32_t)a>>sh); n="SRLI";}
                break;
            }
            default: deliverTrap(h,Cause::ILLEGAL_INSN,instr,trace); return false;
        }
        if(rd!=0) h.x[rd]=r;
        TRACE << n<<" x"<<rd<<"="<<a<<" op "<<imm<<" -> "<<r<<"\n";
        break;
    }

    // ── Load ─────────────────────────────────────────────────────
    case OP::LOAD: {
        uint32_t addr=(uint32_t)(reg(rs1)+decodeImmI(instr));
        int32_t r=0; const char* n="?";
        switch(funct3){
            case F3::BYTE: { auto v=h.mem->loadByte(addr,memId,fault); r=signExt(v,8);  n="LB";  break; }
            case F3::HALF: { auto v=h.mem->loadHalf(addr,memId,fault); r=signExt(v,16); n="LH";  break; }
            case F3::WORD: { auto v=h.mem->loadWord(addr,memId,fault); r=(int32_t)v;    n="LW";  break; }
            case 0b100:    { auto v=h.mem->loadByte(addr,memId,fault); r=(int32_t)v;    n="LBU"; break; }
            case 0b101:    { auto v=h.mem->loadHalf(addr,memId,fault); r=(int32_t)v;    n="LHU"; break; }
            default: deliverTrap(h,Cause::ILLEGAL_INSN,instr,trace); return false;
        }
        if(fault.occurred){ deliverTrap(h,Cause::LOAD_FAULT,addr,trace); return false; }
        if(rd!=0) h.x[rd]=r;
        TRACE << n<<" x"<<rd<<" <- MEM[0x"<<hex<<addr<<dec<<"]="<<r<<"\n";
        break;
    }

    // ── Store ────────────────────────────────────────────────────
    case OP::STORE: {
        uint32_t addr=(uint32_t)(reg(rs1)+decodeImmS(instr));
        int32_t val=reg(rs2); const char* n="?";
        switch(funct3){
            case F3::BYTE: h.mem->storeByte(addr,(uint8_t)val, memId,fault); n="SB"; break;
            case F3::HALF: h.mem->storeHalf(addr,(uint16_t)val,memId,fault); n="SH"; break;
            case F3::WORD: h.mem->storeWord(addr,(uint32_t)val,memId,fault); n="SW"; break;
            default: deliverTrap(h,Cause::ILLEGAL_INSN,instr,trace); return false;
        }
        if(fault.occurred){ deliverTrap(h,Cause::STORE_FAULT,addr,trace); return false; }
        TRACE << n<<" x"<<rs2<<"("<<val<<") -> MEM[0x"<<hex<<addr<<dec<<"]\n";
        break;
    }

    // ── Branch ───────────────────────────────────────────────────
    case OP::BRANCH: {
        int32_t a=reg(rs1), b=reg(rs2), imm=decodeImmB(instr);
        bool taken=false; const char* n="?";
        switch(funct3){
            case F3::BEQ:  taken=(a==b);                     n="BEQ";  break;
            case F3::BNE:  taken=(a!=b);                     n="BNE";  break;
            case F3::BLT:  taken=(a<b);                      n="BLT";  break;
            case F3::BGE:  taken=(a>=b);                     n="BGE";  break;
            case 0b110:    taken=((uint32_t)a<(uint32_t)b);  n="BLTU"; break;
            case 0b111:    taken=((uint32_t)a>=(uint32_t)b); n="BGEU"; break;
            default: deliverTrap(h,Cause::ILLEGAL_INSN,instr,trace); return false;
        }
        if(taken) nextPC=(uint32_t)((int32_t)h.pc+imm);
        TRACE << n<<" "<<a<<" vs "<<b<<(taken?" TAKEN":" not taken")
              <<" -> 0x"<<hex<<nextPC<<dec<<"\n";
        break;
    }

    // ── JAL ──────────────────────────────────────────────────────
    case OP::JAL: {
        if(rd!=0) h.x[rd]=(int32_t)(h.pc+4);
        nextPC=(uint32_t)((int32_t)h.pc+decodeImmJ(instr));
        TRACE << "JAL x"<<rd<<" -> 0x"<<hex<<nextPC<<dec<<"\n";
        break;
    }

    // ── JALR ─────────────────────────────────────────────────────
    case OP::JALR: {
        uint32_t ret=h.pc+4;
        nextPC=(uint32_t)((reg(rs1)+decodeImmI(instr))&~1u);
        if(rd!=0) h.x[rd]=(int32_t)ret;
        TRACE << "JALR x"<<rd<<" -> 0x"<<hex<<nextPC<<dec<<"\n";
        break;
    }

    // ── LUI ──────────────────────────────────────────────────────
    case OP::LUI: {
        int32_t imm=decodeImmU(instr);
        if(rd!=0) h.x[rd]=imm;
        TRACE << "LUI x"<<rd<<" = 0x"<<hex<<(uint32_t)imm<<dec<<"\n";
        break;
    }

    // ── AUIPC ────────────────────────────────────────────────────
    case OP::AUIPC: {
        int32_t imm=decodeImmU(instr);
        if(rd!=0) h.x[rd]=(int32_t)(h.pc+(uint32_t)imm);
        TRACE << "AUIPC x"<<rd<<" = 0x"<<hex<<(uint32_t)h.x[rd]<<dec<<"\n";
        break;
    }

    // ── CSR instructions ─────────────────────────────────────────
    // Only M-mode can access CSRs (simplified: U-mode traps)
    case 0b1110011: {  // SYSTEM
        uint32_t funct12 = instr >> 20;

        // ECALL
        if(instr == 0x00000073) {
            uint32_t cause = (h.mode==PrivMode::M) ? Cause::ECALL_M : Cause::ECALL_U;
            if(trace) cout << pfx() << "ECALL a7="<<h.x[17]<<" a0="<<h.x[10]<<"\n";
            deliverTrap(h, cause, 0, false);  // already printed
            return false;
        }

        // EBREAK
        if(instr == 0x00100073) {
            deliverTrap(h, Cause::BREAKPOINT, h.pc, trace);
            return false;
        }

        // MRET — return from trap (M-mode only)
        if(instr == 0x30200073) {
            if(h.mode != PrivMode::M) {
                deliverTrap(h, Cause::ILLEGAL_INSN, instr, trace);
                return false;
            }
            PrivMode prev = h.csr.trapReturn();
            nextPC = h.csr.mepc;
            h.mode = prev;
            TRACE << "MRET -> 0x"<<hex<<nextPC<<dec
                  <<" mode="<<privName(h.mode)<<"\n";
            h.x[0]=0; h.pc=nextPC; h.steps++;
            return true;
        }

        // CSR read/write — U-mode traps on any CSR access
        if(h.mode != PrivMode::M) {
            deliverTrap(h, Cause::ILLEGAL_INSN, instr, trace);
            return false;
        }

        uint32_t csrAddr = funct12;
        bool ok;
        uint32_t old = h.csr.read(csrAddr, ok);
        if(!ok) { deliverTrap(h,Cause::ILLEGAL_INSN,instr,trace); return false; }

        uint32_t newVal = 0;
        switch(funct3){
            case 0b001: newVal=reg(rs1);         h.csr.write(csrAddr,newVal,ok); break; // CSRRW
            case 0b010: newVal=old|reg(rs1);      h.csr.write(csrAddr,newVal,ok); break; // CSRRS
            case 0b011: newVal=old&~reg(rs1);     h.csr.write(csrAddr,newVal,ok); break; // CSRRC
            case 0b101: newVal=rs1;               h.csr.write(csrAddr,newVal,ok); break; // CSRRWI
            case 0b110: newVal=old|rs1;           h.csr.write(csrAddr,newVal,ok); break; // CSRRSI
            case 0b111: newVal=old&~rs1;          h.csr.write(csrAddr,newVal,ok); break; // CSRRCI
            default: deliverTrap(h,Cause::ILLEGAL_INSN,instr,trace); return false;
        }
        if(rd!=0) h.x[rd]=(int32_t)old;
        TRACE << "CSR[0x"<<hex<<csrAddr<<dec<<"] "<<old<<" -> "<<newVal<<"\n";
        break;
    }

    default:
        TRACE << "ILLEGAL opcode=0x"<<hex<<opcode<<" at PC=0x"<<h.pc<<dec<<"\n";
        deliverTrap(h, Cause::ILLEGAL_INSN, instr, trace);
        return false;
    }

#undef TRACE

    h.x[0] = 0;
    h.pc   = nextPC;
    h.steps++;
    h.csr.cycle++;
    return true;
}

// ─────────────────────────────────────────────────────────────────
//  Print hart state
// ─────────────────────────────────────────────────────────────────
inline void printHart(const Hart& h)
{
    cout << "--- Hart " << h.id
         << " [" << stateName(h.state) << "]"
         << " [" << privName(h.mode)   << "-mode]"
         << "  steps=" << h.steps
         << "  PC=0x"  << hex << h.pc  << dec << "\n";
    for(int i=0;i<32;i++)
        if(h.x[i]!=0||i<3)
            cout<<"  x"<<setw(2)<<i<<" ("<<setw(4)<<left<<ABI_NAMES[i]<<right<<") = "<<h.x[i]<<"\n";
    cout << "  mepc=0x"<<hex<<h.csr.mepc
         <<" mcause=0x"<<h.csr.mcause
         <<" mtvec=0x"<<h.csr.mtvec<<dec<<"\n";
}