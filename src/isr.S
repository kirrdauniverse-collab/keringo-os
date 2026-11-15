/* isr.S — interrupt stubok (0..47) + IRQ0 ütemező út */
.section .text

/* makró: egy stub, amely err_code-ot (ha kell) és vektort nyom */
.macro STUB n, err
.global isr_stub_\n
isr_stub_\n:
    .if \err == 0
    pushl $0
    .endif
    pushl $\n
    jmp isr_common
.endm

/* kivételek: 8,10,11,12,13,14,17 CPU-hibakódot nyom */
STUB  0, 0
STUB  1, 0
STUB  2, 0
STUB  3, 0
STUB  4, 0
STUB  5, 0
STUB  6, 0
STUB  7, 0
STUB  8, 1
STUB  9, 0
STUB 10, 1
STUB 11, 1
STUB 12, 1
STUB 13, 1
STUB 14, 1
STUB 15, 0
STUB 16, 0
STUB 17, 1
STUB 18, 0
STUB 19, 0
STUB 20, 0
STUB 21, 0
STUB 22, 0
STUB 23, 0
STUB 24, 0
STUB 25, 0
STUB 26, 0
STUB 27, 0
STUB 28, 0
STUB 29, 0
STUB 30, 0
STUB 31, 0
STUB 32, 0
STUB 33, 0
STUB 34, 0
STUB 35, 0
STUB 36, 0
STUB 37, 0
STUB 38, 0
STUB 39, 0
STUB 40, 0
STUB 41, 0
STUB 42, 0
STUB 43, 0
STUB 44, 0
STUB 45, 0
STUB 46, 0
STUB 47, 0

isr_common:
    pusha
    pushl %ds
    pushl %esp                    /* regs_t* */
    call isr_handler_c
    addl $4, %esp
    popl %ds
    popa
    addl $8, %esp                 /* int_no + err_code */
    iret

/* IRQ0: a többfeladat-ütemező saját útja.
 * Layout megegyezik az isr_common-nal: err, int_no, pusha, ds (r = &ds).
 * schedule_tick(regs_t *r) -> eax = a folytatandó task r-mutatója */
.global irq0_entry
irq0_entry:
    pushl $0                      /* err_code */
    pushl $32                     /* int_no   */
    pusha
    pushl %ds
    pushl %esp                    /* regs_t *r */
    call schedule_tick
    addl $4, %esp
    movl %eax, %esp               /* átlépés a (esetleg másik) task frame-jére */
    popl %ds
    popa
    addl $8, %esp                 /* int_no + err_code */
    iret

.section .rodata
.global isr_stubs
isr_stubs:
    .long isr_stub_0
    .long isr_stub_1
    .long isr_stub_2
    .long isr_stub_3
    .long isr_stub_4
    .long isr_stub_5
    .long isr_stub_6
    .long isr_stub_7
    .long isr_stub_8
    .long isr_stub_9
    .long isr_stub_10
    .long isr_stub_11
    .long isr_stub_12
    .long isr_stub_13
    .long isr_stub_14
    .long isr_stub_15
    .long isr_stub_16
    .long isr_stub_17
    .long isr_stub_18
    .long isr_stub_19
    .long isr_stub_20
    .long isr_stub_21
    .long isr_stub_22
    .long isr_stub_23
    .long isr_stub_24
    .long isr_stub_25
    .long isr_stub_26
    .long isr_stub_27
    .long isr_stub_28
    .long isr_stub_29
    .long isr_stub_30
    .long isr_stub_31
    .long isr_stub_32
    .long isr_stub_33
    .long isr_stub_34
    .long isr_stub_35
    .long isr_stub_36
    .long isr_stub_37
    .long isr_stub_38
    .long isr_stub_39
    .long isr_stub_40
    .long isr_stub_41
    .long isr_stub_42
    .long isr_stub_43
    .long isr_stub_44
    .long isr_stub_45
    .long isr_stub_46
    .long isr_stub_47

/* int 0x30: task_yield — ütemezés EOI és tick NÉLKÜL */
.global sched_entry
sched_entry:
    pushl $0
    pushl $48
    pusha
    pushl %ds
    pushl %esp
    call schedule_only
    addl $4, %esp
    movl %eax, %esp
    popl %ds
    popa
    addl $8, %esp
    iret
