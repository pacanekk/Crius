; void exec_iretq(uint64_t kstack_top, uint64_t entry);
; Builds iretq frame on kernel stack and jumps to user mode
section .text
global exec_iretq
exec_iretq:
    cli
    mov rsp, rdi              ; switch to kernel stack top
    push qword 0x1B           ; SS
    push qword 0x7FFFFFF0     ; RSP
    push qword 0x202          ; RFLAGS
    push qword 0x23           ; CS
    push rsi                  ; RIP = entry
    mov rdi, 0x70000000       ; argument for user program
    nop
    nop
    iretq
    hlt
    jmp $-1                   ; if iretq fails, halt forever
