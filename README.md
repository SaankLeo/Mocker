# RV32I Emulator and Mini Kernel

A from-scratch RISC-V RV32I emulator written in C++ that evolved from a simple custom CPU into a multi-hart machine capable of running protected user programs under a minimal kernel.

The project implements the complete execution pipeline including instruction encoding, decoding, memory access, privilege modes, trap handling, timer interrupts, inter-process communication, and scheduling.

## Features

* RV32I instruction execution engine
* Custom two-pass assembler with label resolution
* 32-register RISC-V register file
* Shared memory multi-hart architecture
* User Mode (U-mode) and Machine Mode (M-mode)
* Control and Status Registers (CSRs)
* Trap and exception handling
* Timer interrupts and preemptive scheduling
* Round-robin multi-hart scheduler
* Memory protection and fault detection
* ECALL-based syscall interface
* Inter-process communication via kernel-managed message queues
* Load/store memory operations
* Branching, jumps, and loop execution
* Instruction encoding and decoding from raw 32-bit machine words

This project was built to understand how CPUs, operating systems, schedulers, privilege levels, interrupts, and low-level execution environments work beneath modern software systems.
