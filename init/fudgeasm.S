bits 64

org 0x10000
section .text
    mov     rax, 57
    syscall
    cmp     rax, 0
    jnz     .Wait
    mov     rax, 60
    mov     rdi, 0
    syscall
.Wait:
    mov rax, 61
    mov rdi, 0xFFFFFFFF
    mov rsi, 0
    mov rdx, 0
    syscall

    mov rax, 1
    mov rdi, 1
    mov rsi, msg
    mov rdx, len_msg
    syscall

    mov rax, 2
    mov rdi, testfile_path
    mov rsi, (1 << 0)
    syscall
 
    mov r15, rax
 
    mov rax, 0
    mov rdi, r15
    mov rsi, buf
    mov rdx, 256
    syscall

    mov rdx, rax
    mov rax, 1
    mov rsi, buf
    mov rdi, 1
    syscall

    mov rax, 3
    mov rdi, r15
    syscall

.Loop:
    jmp     .Loop

msg db "test message :)", 0x0A
len_msg equ $-msg

testfile_path db "/test/path/a/testfile.txt"

buf times 256 db 0
