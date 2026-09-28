; ffs_ftruncate, bound correctly.
;
; MOS API 0x85 truncates a file at its current position. It works. agondev's
; libagon ships a stub for it that does not:
;
;     _ffs_ftruncate:
;         pop de              ; the return address
;         ex (sp), hl         ; hl = the FIL*
;         push de
;         ld a, 0x85
;         rst.lil 0x08
;         pop ix              ; <-- nothing ever pushed it
;         ret
;
; That `pop ix` has no matching `push ix`. It takes the return address off the
; stack into IX, and the `ret` then goes to whatever was above it -- so calling
; ffs_ftruncate from C hangs the machine. Every other one-argument ffs_ call in
; the same library is this exact shape ending in a bare `ret`; ffs_fclose (0x81)
; and ffs_feof (0x8e) were the comparison.
;
; This is that shape without the stray pop. Verified on MOS 3.0.2 and on the
; console8 firmware: FR_OK from both, and a 1,000 byte file left 500 bytes long
; after seeking to 500. See test/probes/ftruncate.c.
;
; Assembly rather than C because the fault is in the calling sequence, which no
; amount of C can reach. The standard makefile assembles src/*.asm already.
    .assume adl=1
    .text
    .globl _aed_ftruncate

_aed_ftruncate:
    pop de                  ; the return address
    ex (sp), hl             ; hl = the FIL*
    push de
    ld a, 0x85
    rst.lil 0x08
    ret
