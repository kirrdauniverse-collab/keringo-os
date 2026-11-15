/* boot.S — Keringő OS: multiboot1 fejléc + belépési pont
 * Videó-kérés: 1024x768x32 lineáris framebuffer (flags bit2) */
.set MB_MAGIC, 0x1BADB002
.set MB_FLAGS, 0x00000007              /* align | mem_info | video_mode */
.set MB_CHECKSUM, -(MB_MAGIC + MB_FLAGS)

.section .multiboot, "a"
.align 4
    .long MB_MAGIC                     /* 0  magic   */
    .long MB_FLAGS                     /* 4  flags   */
    .long MB_CHECKSUM                  /* 8  checksum*/
    .long 0                            /* 12 mode_type: 0 = lineáris grafika */
    .long 1024                         /* 16 width  */
    .long 768                          /* 20 height */
    .long 32                           /* 24 depth  */

.section .bss.stack, "aw", @nobits
.align 16
    .comm kstack, 16384

.section .text
.global _start
.type _start, @function
_start:
    movl $kstack + 16384, %esp
    pushl %ebx                         /* multiboot_info*  */
    pushl %eax                         /* mágia (0x2BADB002) */
    call kmain
.hang:
    hlt
    jmp .hang
.size _start, . - _start
