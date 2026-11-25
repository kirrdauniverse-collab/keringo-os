/* boot.S — Keringő OS: multiboot1 fejléc + belépési pont
 * 0.5 Videó-kérés: 640x480x32 (BIZTONSÁGOS alap — régi CRT monitorok is
 * bírják; a grub.cfg menüjéből nagyobb mód is választható) */
.set MB_MAGIC, 0x1BADB002
.set MB_FLAGS, 0x00000007              /* align | mem_info | video_mode */
.set MB_CHECKSUM, -(MB_MAGIC + MB_FLAGS)

.section .multiboot, "a"
.align 4
    .long MB_MAGIC                     /* 0  magic   */
    .long MB_FLAGS                     /* 4  flags   */
    .long MB_CHECKSUM                  /* 8  checksum*/
    .long 0                            /* 12 mode_type: 0 = lineáris grafika */
    .long 640                          /* 16 width  */
    .long 480                          /* 20 height */
    .long 32                           /* 24 depth  */

.section .bss.stack, "aw", @nobits
.align 16
    .comm kstack, 49152

.section .text
.global _start
.type _start, @function
_start:
    movl $kstack + 49152, %esp
    pushl %ebx                         /* multiboot_info*  */
    pushl %eax                         /* mágia (0x2BADB002) */
    call kmain
.hang:
    hlt
    jmp .hang
.size _start, . - _start
