# Hart 1: sends value 99 to hart 2, then exits
addi a0, x0, 2       # target = hart 2
addi a1, x0, 99      # value = 99
addi a7, x0, 5       # SEND
ecall

addi a0, x0, 0
addi a7, x0, 3       # EXIT
ecall
