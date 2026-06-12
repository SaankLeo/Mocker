# Hart 2: fibonacci — compute fib(10) = 55
# x1=a, x2=b, x3=temp, x4=counter
addi x1, x0, 0    # fib(0) = 0
addi x2, x0, 1    # fib(1) = 1
addi x4, x0, 9    # 8 more steps to reach fib(10)

fib_loop:
    add  x3, x1, x2   # temp = a + b
    mv   x1, x2        # a = b
    mv   x2, x3        # b = temp
    addi x4, x4, -1
    bnez x4, fib_loop

# x2 = fib(10) = 55
ecall