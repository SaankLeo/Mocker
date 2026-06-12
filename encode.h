#pragma once
#include <cstdint>

// ─────────────────────────────────────────────────────────────────
//  RV32I instruction encoding / decoding
//
//  Every instruction is one 32-bit word.
//  Fields:
//    opcode  [6:0]
//    rd      [11:7]
//    funct3  [14:12]
//    rs1     [19:15]
//    rs2     [24:20]
//    funct7  [31:25]
// ─────────────────────────────────────────────────────────────────

// ── Opcode map (bits [6:0]) ───────────────────────────────────────
namespace OP {
    constexpr uint32_t R_TYPE  = 0b0110011;  // ADD SUB AND OR XOR SLL SRL SRA
    constexpr uint32_t I_ARITH = 0b0010011;  // ADDI ANDI ORI XORI SLLI SRLI SRAI
    constexpr uint32_t LOAD    = 0b0000011;  // LW (funct3=010)
    constexpr uint32_t STORE   = 0b0100011;  // SW (funct3=010)
    constexpr uint32_t BRANCH  = 0b1100011;  // BEQ BNE BLT BGE
    constexpr uint32_t JAL     = 0b1101111;
    constexpr uint32_t JALR    = 0b1100111;
    constexpr uint32_t LUI     = 0b0110111;
    constexpr uint32_t AUIPC   = 0b0010111;
    constexpr uint32_t SYSTEM  = 0b1110011;  // ECALL / EBREAK (Phase 6)
}

// ── funct3 ────────────────────────────────────────────────────────
namespace F3 {
    // R / I arith
    constexpr uint32_t ADD_SUB = 0b000;
    constexpr uint32_t SLL     = 0b001;
    constexpr uint32_t SLT     = 0b010;
    constexpr uint32_t SLTU    = 0b011;
    constexpr uint32_t XOR     = 0b100;
    constexpr uint32_t SRL_SRA = 0b101;
    constexpr uint32_t OR      = 0b110;
    constexpr uint32_t AND     = 0b111;
    // Load / Store width
    constexpr uint32_t BYTE    = 0b000;
    constexpr uint32_t HALF    = 0b001;
    constexpr uint32_t WORD    = 0b010;
    // Branch
    constexpr uint32_t BEQ     = 0b000;
    constexpr uint32_t BNE     = 0b001;
    constexpr uint32_t BLT     = 0b100;
    constexpr uint32_t BGE     = 0b101;
    // JALR
    constexpr uint32_t JALR    = 0b000;
}

// ── funct7 ────────────────────────────────────────────────────────
namespace F7 {
    constexpr uint32_t NORMAL  = 0b0000000;
    constexpr uint32_t ALT     = 0b0100000;  // SUB, SRA, SRAI
}

// ─────────────────────────────────────────────────────────────────
//  Encode helpers
// ─────────────────────────────────────────────────────────────────

// R-type:  funct7 | rs2 | rs1 | funct3 | rd | opcode
inline uint32_t encodeR(uint32_t funct7, uint32_t rs2, uint32_t rs1,
                         uint32_t funct3, uint32_t rd,  uint32_t opcode)
{
    return (funct7 << 25) | (rs2 << 20) | (rs1 << 15) |
           (funct3 << 12) | (rd  <<  7) | opcode;
}

// I-type:  imm[11:0] | rs1 | funct3 | rd | opcode
inline uint32_t encodeI(int32_t imm, uint32_t rs1,
                         uint32_t funct3, uint32_t rd, uint32_t opcode)
{
    uint32_t i = (uint32_t)(imm & 0xFFF);
    return (i << 20) | (rs1 << 15) | (funct3 << 12) | (rd << 7) | opcode;
}

// S-type:  imm[11:5] | rs2 | rs1 | funct3 | imm[4:0] | opcode
inline uint32_t encodeS(int32_t imm, uint32_t rs2, uint32_t rs1,
                         uint32_t funct3, uint32_t opcode)
{
    uint32_t i  = (uint32_t)(imm & 0xFFF);
    uint32_t lo = i & 0x1F;          // imm[4:0]
    uint32_t hi = (i >> 5) & 0x7F;   // imm[11:5]
    return (hi << 25) | (rs2 << 20) | (rs1 << 15) |
           (funct3 << 12) | (lo << 7) | opcode;
}

// B-type:  imm[12|10:5] | rs2 | rs1 | funct3 | imm[4:1|11] | opcode
inline uint32_t encodeB(int32_t imm, uint32_t rs2, uint32_t rs1,
                         uint32_t funct3, uint32_t opcode)
{
    // imm is a byte offset; must be even
    uint32_t i   = (uint32_t)(imm & 0x1FFF);
    uint32_t b12 = (i >> 12) & 1;
    uint32_t b11 = (i >> 11) & 1;
    uint32_t b10_5 = (i >> 5) & 0x3F;
    uint32_t b4_1  = (i >> 1) & 0xF;
    return (b12   << 31) | (b10_5 << 25) | (rs2 << 20) | (rs1 << 15) |
           (funct3 << 12) | (b4_1 <<  8) | (b11 <<  7) | opcode;
}

// U-type:  imm[31:12] | rd | opcode
inline uint32_t encodeU(int32_t imm, uint32_t rd, uint32_t opcode)
{
    return ((uint32_t)(imm & 0xFFFFF000)) | (rd << 7) | opcode;
}

// J-type:  imm[20|10:1|11|19:12] | rd | opcode
inline uint32_t encodeJ(int32_t imm, uint32_t rd, uint32_t opcode)
{
    uint32_t i    = (uint32_t)(imm & 0x1FFFFF);
    uint32_t b20   = (i >> 20) & 1;
    uint32_t b19_12 = (i >> 12) & 0xFF;
    uint32_t b11   = (i >> 11) & 1;
    uint32_t b10_1  = (i >>  1) & 0x3FF;
    return (b20    << 31) | (b10_1 << 21) | (b11  << 20) |
           (b19_12 << 12) | (rd   <<  7)  | opcode;
}

// ─────────────────────────────────────────────────────────────────
//  Decode helpers  (extract sign-extended immediates)
// ─────────────────────────────────────────────────────────────────

inline uint32_t decodeOpcode(uint32_t w) { return w & 0x7F; }
inline uint32_t decodeRd    (uint32_t w) { return (w >>  7) & 0x1F; }
inline uint32_t decodeFunct3(uint32_t w) { return (w >> 12) & 0x7; }
inline uint32_t decodeRs1   (uint32_t w) { return (w >> 15) & 0x1F; }
inline uint32_t decodeRs2   (uint32_t w) { return (w >> 20) & 0x1F; }
inline uint32_t decodeFunct7(uint32_t w) { return (w >> 25) & 0x7F; }

// Sign-extend a value of 'bits' width to 32 bits
inline int32_t signExt(uint32_t val, int bits)
{
    uint32_t mask = 1u << (bits - 1);
    return (int32_t)((val ^ mask) - mask);
}

inline int32_t decodeImmI(uint32_t w)
{
    return signExt(w >> 20, 12);
}

inline int32_t decodeImmS(uint32_t w)
{
    uint32_t lo = (w >> 7)  & 0x1F;
    uint32_t hi = (w >> 25) & 0x7F;
    return signExt((hi << 5) | lo, 12);
}

inline int32_t decodeImmB(uint32_t w)
{
    uint32_t b12  = (w >> 31) & 1;
    uint32_t b11  = (w >>  7) & 1;
    uint32_t b10_5 = (w >> 25) & 0x3F;
    uint32_t b4_1  = (w >>  8) & 0xF;
    uint32_t raw  = (b12 << 12) | (b11 << 11) | (b10_5 << 5) | (b4_1 << 1);
    return signExt(raw, 13);
}

inline int32_t decodeImmU(uint32_t w)
{
    return (int32_t)(w & 0xFFFFF000);
}

inline int32_t decodeImmJ(uint32_t w)
{
    uint32_t b20    = (w >> 31) & 1;
    uint32_t b19_12  = (w >> 12) & 0xFF;
    uint32_t b11    = (w >> 20) & 1;
    uint32_t b10_1   = (w >> 21) & 0x3FF;
    uint32_t raw    = (b20 << 20) | (b19_12 << 12) | (b11 << 11) | (b10_1 << 1);
    return signExt(raw, 21);
}