# Hart 0: counts up from 0 to 4, stores result at address 0x8000
addi x1, x0, 0      # counter
addi x2, x0, 5      # limit
li   x3, 0x8000     # output address

loop0:
    sw   x1, 0(x3)           # write counter to shared mem
    addi x1, x1, 1
    blt  x1, x2, loop0

ecall