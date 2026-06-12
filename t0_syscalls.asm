# Hart 0: tests print syscalls and exit
addi a7, x0, 7       # GET_HARTID
ecall                 # a0 = our hart id
addi a7, x0, 1       # PRINT_INT
ecall                 # prints hart id

addi a0, x0, 42
addi a7, x0, 1       # PRINT_INT
ecall                 # prints 42

addi a0, x0, 0
addi a7, x0, 3       # EXIT
ecall
