addi x5, x0, 5

loop:
addi x10, x0, 0
addi x17, x0, 1
ecall

addi x5, x5, -1
bne x5, x0, loop

addi x10, x0, 0
addi x17, x0, 3
ecall