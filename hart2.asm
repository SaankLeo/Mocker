# Attempt to access hart1 memory

lui x10, 0x20

lw x11, 0(x10)

addi x10, x0, 0
addi x17, x0, 3
ecall