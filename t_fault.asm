# This hart tries to read hart1's memory slot (0x20000)
# Should trigger a LOAD_FAULT
li   a0, 0x20000     # address in hart1's slot
lw   a1, 0(a0)       # cross-hart read -> FAULT
addi a7, x0, 1
ecall                 # should never reach here
