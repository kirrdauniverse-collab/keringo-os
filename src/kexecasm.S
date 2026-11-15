/* kexec.S — életterejtés trambulin: lapozás kikapcsolása, új kernel másolása 1 MB-ra, ugrás
 * A trambulin a C-kód által másolható, pozíciófüggetlen kód + paraméterblokk (64 bájtnál). */
.section .text
.global kexec_trampoline
kexec_trampoline:
    call 1f
1:  popl %ebx                    /* ebx = a pop címe = trambulin+5 (call = 5 bájt!) */
    subl $5, %ebx                /* ebx = trambulin kezdete */
    addl $64, %ebx               /* ebx = paraméterblokk */
    cli
    /* lapozás kikapcsolása */
    movl %cr0, %eax
    andl $0x7FFFFFFF, %eax
    movl %eax, %cr0
    jmp 2f
2:
    /* paraméterek */
    movl 0(%ebx), %esi           /* forrás (scratch) */
    movl 4(%ebx), %edi           /* cél (1 MB) */
    movl 8(%ebx), %ecx           /* dword-ok száma */
    movl 12(%ebx), %edx          /* belépési pont */
    rep movsd
    movl 16(%ebx), %ebx          /* multiboot_info* */
    movl $0x2BADB002, %eax       /* multiboot mágia */
    jmp *%edx
.global kexec_trampoline_end
kexec_trampoline_end:
