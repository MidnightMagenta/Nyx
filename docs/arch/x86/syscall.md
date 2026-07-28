# AMD64 System Call ABI

System calls on AMD64 are invoked using the `syscall` instruction in 64 bit mode.

The syscall number is passed in the `rax` register
The arguments are passed, in this order, in the `rdi`, `rsi`, `rdx`, `r10`, `r8`, `r9` registers.
The return value of the system call is passed in the `rax` register.
