# Hart 2: receives a message (blocks until hart 1 sends), prints it
addi a7, x0, 6       # RECV  (blocks if queue empty)
ecall                 # a0 = value received

addi a7, x0, 1       # PRINT_INT
ecall                 # should print 99

addi a0, x0, 0
addi a7, x0, 3       # EXIT
ecall
