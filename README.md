# Mocker — A RV32I CPU Emulator & Mini Kernel

A from-scratch CPU emulator written in C++ that evolved phase-by-phase from a custom toy architecture into a functionally complete **RISC-V RV32I emulator** running a minimal kernel.

---

## Table of Contents

- [Overview](#overview)
- [Features](#features)
- [Build & Run](#build--run)
- [Project Phases](#project-phases)
  - [Phase 1 — Custom CPU](#phase-1--custom-cpu)
  - [Phase 2 — RISC-V RV32I](#phase-2--risc-v-rv32i)
  - [Phase 3 — Multi-Hart Execution](#phase-3--multi-hart-execution)
  - [Phase 4 — Memory Protection](#phase-4--memory-protection)
  - [Phase 5 — Privilege Levels](#phase-5--privilege-levels)
  - [Phase 6 — CSRs](#phase-6--control-and-status-registers-csrs)
  - [Phase 7 — Trap Handling](#phase-7--trap-handling)
  - [Phase 8 — Timer Interrupts](#phase-8--timer-interrupts)
  - [Phase 9 — IPC via Message Queues](#phase-9--inter-process-communication-ipc)
  - [Phase 10 — System Calls](#phase-10--system-calls)
- [Instruction Set Reference](#instruction-set-reference)
- [Memory Layout](#memory-layout)
- [Exception & Interrupt Table](#exception--interrupt-table)
- [File Structure](#file-structure)
- [Complexity Analysis](#complexity-analysis)

---

## Overview

Mocker is a ground-up CPU emulator that teaches systems concepts by forcing you to implement them — not just read about them.

It started as a minimal 8-register custom CPU and grew into an emulator with:

- A real **two-pass assembler** with label resolution
- **32-register RV32I register file**
- Spec-compliant **32-bit instruction encoding/decoding**
- **Multi-hart (multi-thread)** execution
- **Shared memory SMP** model
- **User Mode and Machine Mode** privilege levels
- **Control and Status Registers (CSRs)**
- **Trap and exception handling**
- **Timer interrupts** and preemptive round-robin scheduling
- **Memory protection** with per-hart address space isolation
- **ECALL-based syscall interface**
- **Kernel-managed IPC** via message queues

---

## Features



---

## Build & Run

```bash
# Build
g++ main.cpp -o cpu  

cpu t_timer0.asm t_timer1.asm

cpu hart0.asm hart1.asm hart2.asm
```

---

## Project Phases

---

### Phase 1 — Custom CPU

#### Architecture

The initial CPU was intentionally minimal — the goal was to understand the fetch-decode-execute loop without being buried in encoding formats.

| Component | Detail |
|---|---|
| Registers | 8 general-purpose (R0–R7) |
| Memory | Separate code memory and data memory (Harvard) |
| Program Counter | Points to current instruction |
| Zero Flag | 1-bit state for conditional branching |
| Instruction width | 4 slots: `[opcode, op1, op2, op3]` |

#### Instruction Format

```
[opcode, operand1, operand2, operand3]
```

Example — `ADD R2, R0, R1`:
```
[ADD, 2, 0, 1]
```

Decoding is just array indexing. This is the conceptual model that real CPUs implement with bit fields.

#### Fetch-Decode-Execute Cycle

```
Fetch:   instruction = codeMemory[PC]
Decode:  opcode = instruction[0]
Execute: if (opcode == ADD) registers[op1] = registers[op2] + registers[op3]
Advance: PC = PC + 4
```

The `+4` stride matches fixed-width instruction sets — including RV32I.

#### Instruction Set

| Category | Instructions |
|---|---|
| Arithmetic | `LOAD`, `STORE`, `ADD`, `SUB`, `MUL` |
| Control Flow | `CMP`, `JMP`, `JZ`, `JNZ`, `HALT` |

#### Zero Flag & Conditional Branching

```cpp
// CMP
zeroFlag = (registers[Ra] == registers[Rb]);

// JZ
if (zeroFlag) PC = target;

// JNZ
if (!zeroFlag) PC = target;
```

> **Note:** RISC-V eliminates the flag register entirely — comparisons are baked into branch instructions (`BEQ rs1, rs2, offset`). This reduces register pressure and removes the need to save/restore flags on context switches.

#### Two-Pass Assembler

Without labels, jump targets must be calculated by hand. Labels give symbolic names to instruction addresses and enable readable programs:

```asm
loop:
    cmp r0 r1
    jz done
    r0 = r0 - r1
    jmp loop
done:
```

**Pass 1 — Symbol Table Construction:**  
Scan the program without emitting instructions. Record every label and its instruction address.
```
loop → 8
done → 24
```

**Pass 2 — Code Emission:**  
Re-scan. Emit instructions. Substitute label references with concrete addresses from the symbol table.
```
jmp loop  →  JMP 8
```

#### Loop Execution Example

```asm
r0 = 5
r1 = 1

loop:
    cmp r0 r1
    jz done
    r0 = r0 - r1
    jmp loop
done:
```

Execution trace:
```
R0=5: 5 != 1 → subtract → R0=4
R0=4: 4 != 1 → subtract → R0=3
R0=3: 3 != 1 → subtract → R0=2
R0=2: 2 != 1 → subtract → R0=1
R0=1: 1 == 1 → Zero Flag set → JZ fires → exit loop
Final: R0 = 1
```

---

### Phase 2 — RISC-V RV32I

#### What is RV32I?

RISC-V is an open-standard ISA originally designed at UC Berkeley. Unlike x86 (Intel-controlled) or ARM (ARM Ltd-controlled), it is freely licensed — no royalties, no NDAs.

```
RV  = RISC-V family
32  = 32-bit address space and data registers
I   = Base integer instruction set (no float, no multiply extension)
```

RV32I has exactly 47 instructions. Minimalism is intentional — RISC philosophy: simple hardware, complexity in software.

#### Architectural Differences

| Custom CPU | RV32I |
|---|---|
| 8 registers | 32 registers (x0–x31) |
| Made-up ISA | RISC-V spec compliant |
| 4-slot instructions | Single 32-bit word |
| Custom assembly | Standard RV32I assembly |
| Made-up encoding | Spec-compliant bit-field encoding |

#### Register File

```
x0        → zero   (hardwired to 0, writes discarded)
x1        → ra     (return address)
x2        → sp     (stack pointer)
x3        → gp     (global pointer)
x4        → tp     (thread pointer)
x5–x7     → t0–t2  (temporaries)
x8–x9     → s0–s1  (saved registers)
x10–x17   → a0–a7  (function arguments / syscall args)
x18–x27   → s2–s11 (saved registers)
x28–x31   → t3–t6  (temporaries)
```

x0 is hardwired to zero — any write is silently discarded:
```cpp
if (rd != 0) registers[rd] = result;
```

#### Instruction Encoding

Every RV32I instruction is one 32-bit word. The emulator must encode (assembler → machine code) and decode (machine code → operation) using bit manipulation.

**R-Type** (register-register operations)
```
31    25  24  20  19  15  14  12  11   7  6     0
 funct7    rs2      rs1    funct3    rd    opcode
```
Used by: `ADD`, `SUB`, `AND`, `OR`, `XOR`, `SLL`, `SRL`, `SRA`

Example — `add x3, x1, x2` → `0x002081B3`
```
0000000  00010  00001  000  00011  0110011
funct7   rs2    rs1    f3   rd     opcode
```
`ADD` vs `SUB` share opcode and funct3 — distinguished by funct7 (`0x00` vs `0x20`).

---

**I-Type** (immediate operations)
```
31         20  19  15  14  12  11   7  6     0
   imm[11:0]    rs1    funct3    rd    opcode
```
Used by: `ADDI`, `LW`, `LB`, `LH`, `JALR`, `XORI`, `ORI`, `ANDI`

The 12-bit immediate is **sign-extended** to 32 bits before use.

---

**S-Type** (stores)
```
31    25  24  20  19  15  14  12  11   7  6     0
 imm[11:5]  rs2      rs1    funct3  imm[4:0]  opcode
```
The immediate is split across two fields — this keeps rs1 and rs2 in consistent bit positions across all formats, simplifying hardware decode.

Used by: `SW`, `SB`, `SH`

---

**B-Type** (branches)
```
31  30    25  24  20  19  15  14  12  11  8  7  6     0
imm[12] imm[10:5] rs2   rs1   funct3  imm[4:1] imm[11] opcode
```
Branches use a **PC-relative offset**. The offset is always even (instruction-aligned), so bit 0 is always 0 and is not stored.

Used by: `BEQ`, `BNE`, `BLT`, `BGE`, `BLTU`, `BGEU`

---

**U-Type** (upper immediate)
```
31              12  11   7  6     0
     imm[31:12]        rd    opcode
```
Used by: `LUI`, `AUIPC`

`LUI` + `ADDI` can load any 32-bit constant:
```asm
lui   x1, 0x12345    ; x1 = 0x12345000
addi  x1, x1, 0x678  ; x1 = 0x12345678
```

---

**J-Type** (jump and link)
```
31  30      21  20  19      12  11   7  6     0
imm[20] imm[10:1] imm[11]  imm[19:12]   rd    opcode
```
Used by: `JAL`

`JAL rd, offset` — jumps to PC+offset, stores PC+4 in rd. The immediate bits are scrambled (same trick as B/S-type: preserve rs1/rs2 positions).

---

#### Sign Extension

When a sub-32-bit immediate is used in a 32-bit operation, it must be **sign-extended** — the sign bit is replicated into all higher bits.

```
12-bit value 111111111111 (4095)
  bit 11 = 1 → sign bit set
  extended:   11111111111111111111111111111111 = -1
```

Formula:
```
If sign_bit == 1:
    value = raw_value - 2^(bit_width)
Example: 4095 - 4096 = -1  ✓
```

In C++:
```cpp
int32_t signExtend(uint32_t value, int bits) {
    int shift = 32 - bits;
    return (int32_t)(value << shift) >> shift;
}
```

#### Instruction Decoding

Every cycle:
```cpp
uint32_t insn = memory.read32(pc);

uint32_t opcode = insn & 0x7F;           // bits [6:0]
uint32_t rd     = (insn >> 7)  & 0x1F;  // bits [11:7]
uint32_t funct3 = (insn >> 12) & 0x07;  // bits [14:12]
uint32_t rs1    = (insn >> 15) & 0x1F;  // bits [19:15]
uint32_t rs2    = (insn >> 20) & 0x1F;  // bits [24:20]
uint32_t funct7 = (insn >> 25) & 0x7F;  // bits [31:25]
```

A switch on `opcode` dispatches to the appropriate handler. The opcode alone determines the format, which determines how to extract the immediate.

#### Full Execution Pipeline

```
┌─────────────┐
│    FETCH    │  insn = MEM[PC]  (32-bit load)
└──────┬──────┘
       ↓
┌──────┴──────┐
│   DECODE    │  extract opcode, rd, rs1, rs2, funct3, funct7, imm
└──────┬──────┘
       ↓
┌──────┴──────┐
│   EXECUTE   │  compute result based on opcode
└──────┬──────┘
       ↓
┌──────┴──────┐
│   MEMORY    │  (load/store only) validate address, read/write
└──────┬──────┘
       ↓
┌──────┴──────┐
│  WRITEBACK  │  if rd != 0: registers[rd] = result
└──────┬──────┘
       ↓
      PC += 4  (or branch/jump target)
```

**Trace — `add x3, x1, x2` (`0x002081B3`):**
```
Fetch:     insn = 0x002081B3
Decode:    opcode=0x33, rd=3, funct3=0x0, rs1=1, rs2=2, funct7=0x00
Execute:   result = registers[1] + registers[2]
Memory:    (skip — not a load/store)
Writeback: registers[3] = result
Advance:   PC += 4
```

**Trace — `lw x3, 4(x2)`:**
```
Fetch:     insn = 0x00412183
Decode:    opcode=0x03, rd=3, funct3=0x2, rs1=2, imm=4
Execute:   addr = registers[2] + 4
Memory:    validate(addr); result = MEM[addr..addr+3] (little-endian)
Writeback: registers[3] = result
Advance:   PC += 4
```

---

### Phase 3 — Multi-Hart Execution

#### What is a Hart?

RISC-V uses **hart** (hardware thread) to mean an independent instruction stream. Each hart has its own:

- 32 general-purpose registers
- Program Counter
- Execution state
- Stack pointer
- Pointer to shared memory

#### Hart State Machine

```cpp
enum HartState {
    RUNNING,   // actively executing
    HALTED,    // completed (EXIT syscall)
    WAITING,   // blocked on RECV with empty queue
    FAULTED    // memory fault or illegal instruction
};
```

The scheduler only dispatches `RUNNING` harts.

#### Shared Memory (SMP Model)

A single `Memory` object is shared across all harts:

```cpp
struct Memory {
    std::array<uint8_t, MEM_SIZE> data;
};
```

Every hart holds a pointer to it. This models **Symmetric Multiprocessing (SMP)** — the same physical address space, multiple execution contexts.

#### Memory Layout (Per-Hart)

```
Hart 0 code → 0x0000
Hart 1 code → 0x1000
Hart 2 code → 0x2000
Hart 3 code → 0x3000
```

Stacks grow downward, separated by 16KB per hart:
```cpp
SP = MEM_SIZE - hartId * 0x4000;
```

#### Round-Robin Scheduler

```cpp
while (anyHartRunning()) {
    for (auto& hart : harts) {
        if (hart.state != RUNNING) continue;
        runQuantum(hart);  // execute N instructions
    }
}
```

Properties: O(H) per cycle, fair (equal rounds), no starvation, bounded latency.

#### Concurrent Program Example

| Hart | Program | Result |
|---|---|---|
| Hart 0 | Shared memory writer | Increments value at addr 0 repeatedly |
| Hart 1 | Factorial (10!) | x2 = 3628800 |
| Hart 2 | Fibonacci sequence | 1, 1, 2, 3, 5, 8, 13, 21, 34, 55 |

Sample scheduler output:
```
Rounds executed: 28
Total instructions: 285

Hart 0:  19 instructions
Hart 1: 218 instructions  (factorial is compute-heavy)
Hart 2:  48 instructions
```

---

### Phase 4 — Memory Protection

Without protection, any program can corrupt any address:
```asm
lui  x1, 0x3      ; x1 = Hart 2's code region
sw   x0, 0(x1)    ; overwrite Hart 2's code
```

#### Per-Hart Address Spaces

```
Kernel:  0x00000 – 0x0FFFF
Hart 0:  0x10000 – 0x1FFFF
Hart 1:  0x20000 – 0x2FFFF
Hart 2:  0x30000 – 0x3FFFF
```

#### Address Validation

Every memory operation goes through a bounds check:
```cpp
bool isValidAddress(uint32_t addr, Hart& hart) {
    return addr >= hart.baseAddress &&
           addr < hart.baseAddress + hart.regionSize;
}
```

Violations generate faults:

| Fault | Cause |
|---|---|
| `LOAD_FAULT` | Invalid address on load |
| `STORE_FAULT` | Invalid address on store |
| `INSN_FAULT` | PC outside code region |

---

### Phase 5 — Privilege Levels

Without privilege levels, user programs can do anything — modify the scheduler, disable interrupts, corrupt kernel data.

#### Two Modes

**Machine Mode (M-Mode)** — highest privilege. The kernel runs here.
- Full memory access
- Full CSR access
- Trap handling
- Scheduler control

**User Mode (U-Mode)** — restricted. All user programs run here.
- Can only access own memory region
- Cannot access privileged CSRs
- Must use `ECALL` to request kernel services

#### Mode Transitions

```
Normal execution → U-Mode
ECALL            → U-Mode to M-Mode (trap)
MRET             → M-Mode back to U-Mode
```

---

### Phase 6 — Control and Status Registers (CSRs)

CSRs are a separate register file that controls processor behavior, accessed via `csrr`/`csrw`/`csrrw` instructions.

#### Key CSRs

**`mstatus`** — Machine Status Register  
Tracks processor state. Key bits:
- `MPP` [12:11] — previous privilege mode (saved on trap, restored on mret)
- `MIE` [3] — global interrupt enable
- `MPIE` [7] — previous interrupt enable

On trap: `MPIE = MIE`, `MIE = 0`, `MPP = current_mode`  
On mret: `MIE = MPIE`, `privilege = MPP`

---

**`mepc`** — Machine Exception Program Counter  
Stores the PC of the instruction that caused the trap. `mret` does `PC = mepc`.

---

**`mcause`** — Machine Cause Register  
- Bit 31 = 0 → exception (synchronous)
- Bit 31 = 1 → interrupt (asynchronous)

---

**`mtvec`** — Machine Trap Vector  
Stores the trap handler address. On any trap: `PC = mtvec`

---

**`mtval`** — Machine Trap Value  
Stores extra context — faulting address for memory faults, instruction word for illegal instructions.

#### CSR Access Instructions

```asm
csrr  x1, mepc        ; read mepc into x1
csrw  mtvec, x1       ; write x1 to mtvec
csrrw x1, mstatus, x2 ; atomically: x1=mstatus, mstatus=x2
```

---

### Phase 7 — Trap Handling

A trap is any transfer of control from normal execution to the kernel. Three categories:

1. **Exceptions** — synchronous faults (illegal instruction, memory fault, ecall)
2. **Interrupts** — asynchronous hardware events (timer)
3. **System Calls** — deliberate `ecall` to request kernel services

All three go through the same mechanism.

#### Trap Sequence

```
1. mepc   = pc                         // save faulting PC
2. mcause = trap_reason                // record cause
3. mtval  = faulting_address or 0     // save extra info
4. mstatus.MPIE = mstatus.MIE         // save interrupt enable
   mstatus.MIE  = 0                   // disable interrupts during handling
   mstatus.MPP  = current_mode        // save previous mode
5. current_mode = M_MODE              // switch to machine mode
6. pc = mtvec                         // jump to trap handler
```

#### Trap Dispatch

```cpp
void trapHandler(Hart& hart) {
    switch (hart.csr.mcause) {
        case 0x8:          // ECALL from U-mode
            handleSyscall(hart);
            hart.csr.mepc += 4;  // advance past ecall
            break;
        case 0x5:          // LOAD_FAULT
            handleFault(hart, "Load fault at", hart.csr.mtval);
            break;
        case 0x7:          // STORE_FAULT
            handleFault(hart, "Store fault at", hart.csr.mtval);
            break;
        case 0x80000007:   // Timer Interrupt
            handleTimerInterrupt(hart);
            break;
    }
}
```

#### Returning from a Trap

`mret` instruction:
```
mstatus.MIE  = mstatus.MPIE   // restore interrupt enable
current_mode = mstatus.MPP    // restore previous privilege
pc           = mepc           // return to saved PC
```

---

### Phase 8 — Timer Interrupts

Without timer interrupts, a spinning hart blocks the scheduler forever:
```asm
loop:
    jal x0, loop    ; spin forever — starves all other harts
```

#### Mechanism

A global tick counter increments every instruction. At the interval threshold, a timer interrupt is delivered:

```cpp
void tick() {
    globalTicks++;
    if (globalTicks % TIMER_INTERVAL == 0) {
        for (auto& hart : harts) {
            if (hart.state == RUNNING && hart.mode == U_MODE)
                deliverTimerInterrupt(hart);
        }
    }
}
```

Interrupt delivery:
```cpp
void deliverTimerInterrupt(Hart& hart) {
    hart.csr.mepc   = hart.pc;
    hart.csr.mcause = 0x80000007;  // Machine Timer Interrupt
    hart.pc         = hart.csr.mtvec;
    hart.mode       = M_MODE;
}
```

The kernel gains control at `mtvec`, runs the scheduler, switches to the next hart. This is **preemptive multitasking** — the same mechanism underlying all modern OS scheduling.

---

### Phase 9 — Inter-Process Communication (IPC)

Isolated address spaces protect processes from each other — but sometimes they need to communicate. IPC provides kernel-mediated communication.

#### Message Structure

```cpp
struct Message {
    int      from;   // sender hart ID
    uint32_t value;  // 32-bit payload
};
```

The kernel maintains one queue per hart — in kernel space, inaccessible directly from user programs.

#### SEND Syscall

```asm
li a0, 1       ; target = Hart 1
li a1, 42      ; value = 42
li a7, SEND
ecall
```

Kernel handler:
```cpp
void handleSend(Hart& sender) {
    int target      = sender.registers[A0];
    uint32_t value  = sender.registers[A1];

    messageQueues[target].push({ sender.id, value });

    if (harts[target].state == WAITING)
        harts[target].state = RUNNING;  // wake up blocked receiver
}
```

#### RECV Syscall

```asm
li a7, RECV
ecall
; on return: a0 = value, a1 = sender_id
```

Kernel handler:
```cpp
void handleRecv(Hart& receiver) {
    if (messageQueues[receiver.id].empty()) {
        receiver.state = WAITING;  // block — scheduler will skip this hart
    } else {
        Message msg = messageQueues[receiver.id].front();
        messageQueues[receiver.id].pop();
        receiver.registers[A0] = msg.value;
        receiver.registers[A1] = msg.from;
    }
}
```

#### Blocking & Wake-Up Flow

```
1. Hart calls RECV — queue empty → state = WAITING
2. Scheduler sees WAITING → skips this hart
3. Another hart calls SEND → message enqueued → receiver state = RUNNING
4. Scheduler sees RUNNING → hart executes, message delivered
```

This is the same pattern as POSIX message queues and Go channels.

---

### Phase 10 — System Calls

User programs cannot directly access kernel functionality. `ECALL` is the boundary crossing point.

#### Syscall Convention (matches RISC-V Linux ABI)

```
a7 (x17) → syscall number
a0–a5    → arguments
a0       → return value
ecall    → invoke the kernel
```

#### Implemented Syscalls

| Number | Name | Args | Effect |
|---|---|---|---|
| 1 | `PRINT_INT` | a0 = integer | Print integer to stdout |
| 2 | `PRINT_STR` | a0 = string address | Print null-terminated string from user memory |
| 3 | `EXIT` | a0 = exit code | Halt this hart |
| 4 | `YIELD` | — | Voluntarily give up CPU |
| 5 | `SEND` | a0 = target, a1 = value | Send message to hart |
| 6 | `RECV` | — | Block until message received |
| 7 | `GET_HARTID` | — | Return this hart's ID in a0 |

Every memory access during syscall handling (e.g. `PRINT_STR`) validates addresses — kernel operations on user data still go through the protection layer.

---

## Instruction Set Reference

```asm
; ── Arithmetic ──────────────────────────────────────────
add  x3, x1, x2       ; x3 = x1 + x2
sub  x3, x1, x2       ; x3 = x1 - x2
addi x1, x0, 10       ; x1 = x0 + 10  (immediate)
and  x3, x1, x2
or   x3, x1, x2
xor  x3, x1, x2

; ── Memory ──────────────────────────────────────────────
lw   x3, 0(x2)        ; load word  (32-bit)
lh   x3, 0(x2)        ; load halfword (16-bit, sign-extended)
lb   x3, 0(x2)        ; load byte  (8-bit, sign-extended)
sw   x1, 0(x2)        ; store word
sh   x1, 0(x2)        ; store halfword
sb   x1, 0(x2)        ; store byte

; ── Branches (PC-relative) ──────────────────────────────
beq  x1, x2, label    ; branch if x1 == x2
bne  x1, x2, label    ; branch if x1 != x2
blt  x1, x2, label    ; branch if x1 < x2  (signed)
bge  x1, x2, label    ; branch if x1 >= x2 (signed)

; ── Jumps ───────────────────────────────────────────────
jal  x0, loop         ; unconditional jump (discard return addr)
jal  x1, func         ; call func, return address in x1
jalr x0, 0(x1)        ; return (jump to address in x1)

; ── Upper Immediate ─────────────────────────────────────
lui   x3, 0x8         ; x3 = 0x8000
auipc x4, 0x10        ; x4 = PC + 0x10000

; ── System ──────────────────────────────────────────────
ecall                  ; syscall (see syscall table above)
ebreak                 ; breakpoint

; ── Pseudo Instructions (assembler expands these) ───────
li x1, 100            ; → addi x1, x0, 100
mv x1, x2             ; → addi x1, x2, 0
```

---

## Memory Layout

```
0x00000 – 0x0FFFF   Kernel (64KB)
                    ├─ Kernel code
                    ├─ Trap handlers
                    ├─ CSR state
                    └─ Kernel data

0x10000 – 0x1FFFF   Hart 0 user region (64KB)
                    ├─ Code loaded from hart0.asm
                    └─ Stack at top of region (grows downward)

0x20000 – 0x2FFFF   Hart 1 user region (64KB)

0x30000 – 0x3FFFF   Hart 2 user region (64KB)

0x40000 – 0xBFFFF   Future user regions

0xC0000 – 0xFBFFF   Shared heap (IPC / shared data)

0xFC000 – 0xFFFFF   Kernel stack
```

Each region is 64KB. Stack pointer starts at the top of the region and grows downward. Code is loaded at the base.

---

## Exception & Interrupt Table

| `mcause` | Type | Meaning | Response |
|---|---|---|---|
| `0x2` | Exception | Illegal Instruction | FAULTED, log instruction word |
| `0x5` | Exception | Load Access Fault | FAULTED, log address in mtval |
| `0x7` | Exception | Store/AMO Access Fault | FAULTED, log address in mtval |
| `0x8` | Exception | ECALL from U-Mode | Dispatch syscall, mepc += 4 |
| `0xB` | Exception | ECALL from M-Mode | Kernel self-call (rare) |
| `0x80000007` | Interrupt | Machine Timer Interrupt | Save state, run scheduler |

---

## File Structure

```
main.cpp        — Entry point, multi-hart setup, CLI argument parsing
cpu.h           — Hart struct, register file, PC, state machine
assembler.h     — Two-pass assembler, label resolution, encoding
encode.h        — RV32I instruction encoding functions
csr.h           — CSR register file (mstatus, mepc, mcause, mtvec, mtval)
memory.h        — Shared flat memory model, bounds checking
scheduler.h     — Round-robin scheduler, quantum management
kernel.h        — Trap handler, syscall dispatcher, IPC queues

hart0.asm       — Demo: shared memory writer
hart1.asm       — Demo: factorial computation (10! = 3628800)
hart2.asm       — Demo: Fibonacci sequence generation

t0_syscalls.asm — Test: syscall coverage
t1_ipc.asm      — Test: SEND side
t2_recv.asm     — Test: RECV side
t_fault.asm     — Test: deliberate fault injection
t_timer0.asm    — Test: timer interrupt hart 0
t_timer1.asm    — Test: timer interrupt hart 1
```

---

## Complexity Analysis

| Operation | Complexity |
|---|---|
| Instruction fetch | O(1) — direct array index |
| Instruction decode | O(1) — bit masking |
| Register read/write | O(1) — array index |
| Memory access | O(1) — array index + bounds check |
| CSR read/write | O(1) — struct field |
| Scheduler cycle | O(H) — iterate over all harts |
| Trap delivery | O(1) — CSR writes + PC redirect |
| SEND syscall | O(1) — queue push |
| RECV syscall (hit) | O(1) — queue pop |
| RECV syscall (miss) | O(1) — state transition |
| Label resolution | O(N) — two linear scans |

The emulator scales **linearly with hart count** and **linearly with program length**. Per-instruction overhead is O(1).
