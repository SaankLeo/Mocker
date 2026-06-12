addi x5, x0, 10

loop0:
addi x10, x0, 0
addi x17, x0, 1
ecall

addi x5, x5, -1
bne x5, x0, loop0

addi x10, x0, 0
addi x17, x0, 3
ecall