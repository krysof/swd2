; Reproduce RPG.EXE's original "OC" overlay-entry contract without patching
; the executable.  This harness is capture tooling only; it is not part of the
; modern runtime or published game.
;
; Build with:
;   nasm -f bin scripts/original-rpg-oc.asm -o RPGOC.COM
;
; Run from a temporary copy of the original game directory.  SAVE.DA1 is read
; into the transfer segment used by the shipped launcher, the literal marker
; "OC" is installed, then DOS EXEC starts the untouched RPG.EXE.

bits 16
org 0x100

start:
    push cs
    pop ds
    push cs
    pop es

    ; Release the parent's unused memory before EXEC.
    mov bx, (program_end - $$ + 0x10f) / 16
    mov ah, 0x4a
    int 0x21
    jc fail

    mov dx, save_name
    mov ax, 0x3d00
    int 0x21
    jc fail
    mov bx, ax
    push ds
    mov ax, 0x4000
    mov ds, ax
    mov dx, 2
    mov cx, 0x0546
    mov ah, 0x3f
    int 0x21
    pop ds
    jc fail_close
    cmp ax, 0x0546
    jne fail_close
    mov ah, 0x3e
    int 0x21

    mov ax, 0x4000
    mov es, ax
    mov word [es:0], 0x434f       ; bytes "OC"

    push cs
    pop es
    mov [exec_block + 4], ds
    mov [exec_block + 8], ds
    mov [exec_block + 12], ds
    mov bx, exec_block
    mov dx, rpg_name
    mov ax, 0x4b00
    int 0x21
    jc fail
    mov ax, 0x4c00
    int 0x21

fail_close:
    mov ah, 0x3e
    int 0x21
fail:
    mov ax, 0x4c01
    int 0x21

save_name db 'SAVE.DA1',0
rpg_name db 'RPG.EXE',0
align 2
exec_block:
    dw 0
    dw 0x80, 0
    dw 0x5c, 0
    dw 0x6c, 0
program_end:
