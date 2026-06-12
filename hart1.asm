# Hart 1: computes 10! (3628800) iteratively
# x1 = i (1..10), x2 = accumulator
addi x1, x0, 1
addi x2, x0, 1
addi x4, x0, 10     # limit

loop1:
    # multiply x2 = x2 * x1 via repeated addition
    addi x5, x0, 0   # temp result
    addi x6, x0, 0   # inner counter
inner:
    add  x5, x5, x2
    addi x6, x6, 1
    blt  x6, x1, inner

    mv   x2, x5
    addi x1, x1, 1
    ble: bge x4, x1, loop1   # while i <= 10

ecall                # x2 = 10! = 3628800