/* boot.S — Keringő OS: multiboot1 fejléc + belépési pont
 * 0.5.1 Videó-kérés: 640x480x16 (BIZTONSÁGOS alap — a 16 bites színmélység
 * szinte minden VBE2-képes kártyán/monitoron megy; a grub.cfg menüjéből
 * 8 bites, 32 bites és nagyobb módok is választhatók) */
.set MB_MAGIC, 0x1BADB002
.set MB_FLAGS, 0x00000007              /* align | mem_info | video_mode */
.set MB_CHECKSUM, -(MB_MAGIC + MB_FLAGS)

.section .multiboot, "a"
.align 4
    .long MB_MAGIC                     /* 0  magic   */
    .long MB_FLAGS                     /* 4  flags   */
    .long MB_CHECKSUM                  /* 8  checksum*/
    /* aout-kludge mezők (bit 16 nincs beállítva — de a helyük a spec szerint foglalt) */
    .long 0                            /* 12 header_addr   */
    .long 0                            /* 16 load_addr     */
    .long 0                            /* 20 load_end_addr */
    .long 0                            /* 24 bss_end_addr  */
    .long 0                            /* 28 entry_addr    */
    /* VIDEO-MEZŐK: csak flags bit 2 (0x4) esetén értelmesek — A 32. OFFSETTŐL! */
    .long 0                            /* 32 mode_type: 0 = lineáris grafika */
    .long 640                          /* 36 width  */
    .long 480                          /* 40 height */
    .long 16                           /* 44 depth  */

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
