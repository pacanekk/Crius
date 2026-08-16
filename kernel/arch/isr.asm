section .text
bits 64

extern isr_handler
extern irq_handler
extern syscall_handler
extern has_smap
extern need_reschedule
extern saved_rsp
extern syscall_saved_rsp
extern do_schedule
extern sched_stack_top

%macro isr_noerror 1
    global isr%1
    isr%1:
        push qword 0
        push qword %1
        jmp isr_common
%endmacro

%macro isr_error 1
    global isr%1
    isr%1:
        push qword %1
        jmp isr_common
%endmacro

%macro irq_stub 1
    global irq%1
    irq%1:
        push qword 0
        push qword %1
        jmp irq_common
%endmacro

isr_noerror 0
isr_noerror 1
isr_noerror 2
isr_noerror 3
isr_noerror 4
isr_noerror 5
isr_noerror 6
isr_noerror 7
isr_error    8
isr_noerror 9
isr_error    10
isr_error    11
isr_error    12
isr_error    13
isr_error    14
isr_noerror 15
isr_noerror 16
isr_noerror 17
isr_noerror 18
isr_noerror 19
isr_noerror 20
isr_noerror 21
isr_noerror 22
isr_noerror 23
isr_noerror 24
isr_noerror 25
isr_noerror 26
isr_noerror 27
isr_noerror 28
isr_noerror 29
isr_noerror 30
isr_noerror 31

irq_stub 32
irq_stub 33
irq_stub 34
irq_stub 35
irq_stub 36
irq_stub 37
irq_stub 38
irq_stub 39
irq_stub 40
irq_stub 41
irq_stub 42
irq_stub 43
irq_stub 44
irq_stub 45
irq_stub 46
irq_stub 47

; In 64-bit mode, iretq pops SS:RSP only when CS.RPL > CPL (privilege
; level change). For ring 0 → ring 0 returns, it pops only RIP, CS,
; RFLAGS (3 entries). For ring 3 → ring 0 returns, it pops all 5.
; Therefore we do NOT normalize: ring 0 frames have 3 CPU entries,
; ring 3 frames have 5. iretq handles both correctly.

isr_common:
    ; Stack: [vector, error_code, RIP, CS, RFLAGS] (ring0, 5 entries)
    ;     or [vector, error_code, RIP, CS, RFLAGS, RSP, SS] (ring3, 7 entries)

    push rax
    push rbx
    push rcx
    push rdx
    push rsi
    push rdi
    push rbp
    push r8
    push r9
    push r10
    push r11
    push r12
    push r13
    push r14
    push r15

    mov rdi, [rsp + 120]    ; vector
    mov rsi, [rsp + 128]    ; error_code
    mov rdx, [rsp + 136]    ; RIP
    mov rcx, [rsp + 144]    ; CS
    call isr_handler

    pop r15
    pop r14
    pop r13
    pop r12
    pop r11
    pop r10
    pop r9
    pop r8
    pop rbp
    pop rdi
    pop rsi
    pop rdx
    pop rcx
    pop rbx
    pop rax

    add rsp, 16             ; skip vector + error_code
    iretq

irq_common:
    ; Stack: [vector, error_code, RIP, CS, RFLAGS] (ring0, 5 entries)
    ;     or [vector, error_code, RIP, CS, RFLAGS, RSP, SS] (ring3, 7 entries)

    push rax
    push rbx
    push rcx
    push rdx
    push rsi
    push rdi
    push rbp
    push r8
    push r9
    push r10
    push r11
    push r12
    push r13
    push r14
    push r15

    mov rdi, [rsp + 120]    ; vector
    call irq_handler

    cmp byte [rel need_reschedule], 0
    je .no_switch

    mov [rel saved_rsp], rsp
    ; Switch to dedicated scheduler stack to avoid corrupting
    ; the current task's kernel stack frames below irq_common.
    mov rsp, [rel sched_stack_top]
    call do_schedule
    mov rsp, [rel saved_rsp]
    mov byte [rel need_reschedule], 0

.no_switch:
    pop r15
    pop r14
    pop r13
    pop r12
    pop r11
    pop r10
    pop r9
    pop r8
    pop rbp
    pop rdi
    pop rsi
    pop rdx
    pop rcx
    pop rbx
    pop rax

    add rsp, 16             ; skip vector + error_code
    iretq

global syscall_isr
syscall_isr:
    cli
    ; In 64-bit mode, int 0x80 from ring 3 pushes: RIP, CS, RFLAGS, RSP, SS
    ; From ring 0, CPU pushes only: RIP, CS, RFLAGS (no RSP, SS)
    ; iretq pops SS:RSP only when CS.RPL > CPL, so both cases are handled
    ; correctly without normalization.

    ; Push dummy vector + error_code to match irq_common layout
    push 0                  ; error_code
    push 0x80               ; vector

    ; Push registers in SAME order as irq_common
    push rax
    push rbx
    push rcx
    push rdx
    push rsi
    push rdi
    push rbp
    push r8
    push r9
    push r10
    push r11
    push r12
    push r13
    push r14
    push r15

    ; Stack layout (rsp-relative) - identical to irq_common:
    ; [0]  = r15    [5]  = r10    [10] = rsi
    ; [1]  = r14    [6]  = r9     [11] = rdx
    ; [2]  = r13    [7]  = r8     [12] = rcx
    ; [3]  = r12    [8]  = rbp    [13] = rbx
    ; [4]  = r11    [9]  = rdi    [14] = rax
    ; [15] = vector [16] = error_code
    ; [17] = RIP    [18] = CS     [19] = RFLAGS
    ; [20] = RSP    [21] = SS

    mov rdi, [rsp + 8*14]   ; rax = syscall number
    mov rsi, [rsp + 8*9]    ; saved rdi = arg1
    mov rdx, [rsp + 8*10]   ; saved rsi = arg2
    mov rcx, [rsp + 8*11]   ; saved rdx = arg3
    mov r8,  [rsp + 8*5]    ; saved r10 = arg4
    mov r9,  [rsp + 8*7]    ; saved r8  = arg5
    mov [rel syscall_saved_rsp], rsp
    cmp qword [rel has_smap], 0
    je .no_smap
    stac                    ; allow user memory access (SMAP)
    call syscall_handler
    clac                    ; disallow user memory access (SMAP)
    jmp .smap_done
.no_smap:
    call syscall_handler
.smap_done:

    mov [rsp + 8*14], rax   ; store return value in rax slot

    ; Check if we need to reschedule (e.g. yield, sleep, etc.)
    cmp byte [rel need_reschedule], 0
    je .no_syscall_switch

    mov [rel saved_rsp], rsp
    mov rsp, [rel sched_stack_top]
    call do_schedule
    mov rsp, [rel saved_rsp]
    mov byte [rel need_reschedule], 0

.no_syscall_switch:
    pop r15
    pop r14
    pop r13
    pop r12
    pop r11
    pop r10
    pop r9
    pop r8
    pop rbp
    pop rdi
    pop rsi
    pop rdx
    pop rcx
    pop rbx
    pop rax

    add rsp, 16             ; skip vector + error_code
    cli
    iretq
