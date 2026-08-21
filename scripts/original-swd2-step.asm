; Execute exactly one untouched SWD2 module and persist its 4000h transfer.
;
; This is deterministic capture tooling, never part of the modern runtime.
; STEP.IN must contain the complete 1352-byte marker+state transfer.  The
; build selects RPG.EXE at the OC/MT entry or FIG.EXE at the IF entry.
; After the child returns, STEP.OUT receives the complete updated transfer.
; RPG copies its live DATA block back to 4000:0002 before returning.  FIG
; returns only the OC marker and persists its live block to SAVE.DAQ, so the
; FIG variant reloads that released checkpoint into 4000:0002 first.
; Splitting the released module loop at this already-existing protocol edge
; avoids DOSBox video-mode capture resets without patching either original
; executable or fabricating any game state.
;
; FIG is a fresh child at every IF edge, so FIG_STEP can reproduce that edge
; exactly.  RPG is different: FIG normally returns to its still-resident RPG
; parent.  RPG_STEP starts a new RPG process at OC and remains only a direct
; compatibility/reverse-engineering probe.  RPG_TITLE_STEP instead enters the
; untouched title path at MT so RPG rebuilds load-time context from a staged
; released save slot.  Neither isolated variant is an uninterrupted original
; mainline playthrough.
;
; Build examples:
;   nasm -f bin -dRPG_STEP=1 scripts/original-swd2-step.asm -o RPGSTEP.COM
;   nasm -f bin -dRPG_TITLE_STEP=1 scripts/original-swd2-step.asm -o RPGMT.COM
;   nasm -f bin -dFIG_STEP=1 scripts/original-swd2-step.asm -o FIGSTEP.COM
;   nasm -f bin -dFIG_STEP=1 -dFIG_FIXED_HUNDREDTH=0 \
;       scripts/original-swd2-step.asm -o FIGCLK0.COM
;   nasm -f bin -dRPG_TITLE_STEP=1 -dRPG_FIXED_HUNDREDTH=0 \
;       scripts/original-swd2-step.asm -o RPGMT0.COM

bits 16
org 0x100

%assign STEP_SELECTOR_COUNT 0
%ifdef RPG_STEP
%assign STEP_SELECTOR_COUNT STEP_SELECTOR_COUNT + 1
%endif
%ifdef RPG_TITLE_STEP
%assign STEP_SELECTOR_COUNT STEP_SELECTOR_COUNT + 1
%endif
%ifdef FIG_STEP
%assign STEP_SELECTOR_COUNT STEP_SELECTOR_COUNT + 1
%endif
%if STEP_SELECTOR_COUNT != 1
%error "select exactly one SWD2 step module"
%endif

%ifdef RPG_FIXED_HUNDREDTH
%ifndef RPG_STEP
%ifndef RPG_TITLE_STEP
%error "RPG_FIXED_HUNDREDTH is valid only for RPG_STEP or RPG_TITLE_STEP"
%endif
%endif
%if RPG_FIXED_HUNDREDTH < 0 || RPG_FIXED_HUNDREDTH > 99
%error "RPG_FIXED_HUNDREDTH must be in 0..99"
%endif
%ifdef RPG_PHASE_HUNDREDTH
%error "RPG_FIXED_HUNDREDTH and RPG_PHASE_HUNDREDTH are mutually exclusive"
%endif
%ifdef FIG_FIXED_HUNDREDTH
%error "RPG_FIXED_HUNDREDTH and FIG_FIXED_HUNDREDTH are mutually exclusive"
%endif
%define FIXED_HUNDREDTH RPG_FIXED_HUNDREDTH
%endif

%ifdef FIG_FIXED_HUNDREDTH
%ifndef FIG_STEP
%error "FIG_FIXED_HUNDREDTH is valid only for FIG_STEP"
%endif
%if FIG_FIXED_HUNDREDTH < 0 || FIG_FIXED_HUNDREDTH > 99
%error "FIG_FIXED_HUNDREDTH must be in 0..99"
%endif
%ifdef FIG_PHASE_HUNDREDTH
%error "FIG_FIXED_HUNDREDTH and FIG_PHASE_HUNDREDTH are mutually exclusive"
%endif
%define FIXED_HUNDREDTH FIG_FIXED_HUNDREDTH
%endif

%ifdef RPG_STEP
%define CHILD_MARKER 0x434f       ; bytes "OC"
%define CHILD_NAME rpg_name
%elifdef RPG_TITLE_STEP
%define CHILD_MARKER 0x544d       ; bytes "MT"
%define CHILD_NAME rpg_name
%elifdef FIG_STEP
%define CHILD_MARKER 0x4649       ; bytes "IF"
%define CHILD_NAME fig_name
%endif

start:
    ; A COM process begins with SP near FFFEh.  Once the resident block is
    ; shrunk to SWD2.EXE's 24h paragraphs that address belongs to the child;
    ; leaving it there makes a long-running child overwrite the parent's DOS
    ; return stack.  SWD2.EXE's relocated SS:SP is PSP+1fh:0050h, whose linear
    ; top is PSP:0240h.  Reproduce that exact in-block stack top.
    mov ax, cs
    cli
    mov ss, ax
    mov sp, 0x0240
    sti

    push cs
    pop ds
    push cs
    pop es

    ; Match SWD2.EXE:0000..000c exactly.  Its relocated 0014h value minus
    ; PSP segment ES produces a 0024h-paragraph resident parent.  A smaller
    ; COM-sized block shifts the maximum-allocation child and changes where
    ; its heap lies relative to the absolute 4000h transfer segment.
%ifdef FIXED_HUNDREDTH
    ; The temporary INT 21h hook/data extends this diagnostic launcher by
    ; four paragraphs.  The ordinary evidence harnesses retain SWD2.EXE's
    ; exact 24h-paragraph footprint; the fixed-clock variant declares its
    ; 28h footprint and is accepted only when the released SAVE bytes match.
    mov bx, 0x0028
%else
    mov bx, 0x0024
%endif
    mov ah, 0x4a
    int 0x21
    jc fail

    ; STEP.IN is deliberately the whole transfer, including its marker, so
    ; every staged boundary can be hashed without an implicit two-byte prefix.
    mov dx, input_name
    mov ax, 0x3d00
    int 0x21
    jc fail
    mov bx, ax
    push ds
    mov ax, 0x4000
    mov ds, ax
    xor dx, dx
    mov cx, 0x0548
    mov ah, 0x3f
    int 0x21
    pop ds
    jc fail_close_input
    cmp ax, 0x0548
    jne fail_close_input
    mov ah, 0x3e
    int 0x21
    jc fail

    mov ax, 0x4000
    mov es, ax
    mov word [es:0], CHILD_MARKER

%ifdef RPG_PHASE_HUNDREDTH
%ifndef RPG_STEP
%error "RPG_PHASE_HUNDREDTH is valid only for RPG_STEP"
%endif
%if RPG_PHASE_HUNDREDTH < 0 || RPG_PHASE_HUNDREDTH > 99
%error "RPG_PHASE_HUNDREDTH must be in 0..99"
%endif
    ; Optional reverse-engineering probe: start the untouched RPG child at a
    ; selected DOS hundredth so its 4c16/4cae load-time cursor perturbation
    ; can be separated from the persisted entry state.  Production evidence
    ; uses a recorded define value; the default harness omits this loop and
    ; retains its established byte identity.
.wait_rpg_phase:
    mov ah, 0x2c
    int 0x21
    cmp dl, RPG_PHASE_HUNDREDTH
    jne .wait_rpg_phase
%endif

%ifdef FIG_PHASE_HUNDREDTH
%ifndef FIG_STEP
%error "FIG_PHASE_HUNDREDTH is valid only for FIG_STEP"
%endif
%if FIG_PHASE_HUNDREDTH < 0 || FIG_PHASE_HUNDREDTH > 99
%error "FIG_PHASE_HUNDREDTH must be in 0..99"
%endif
    ; Random encounters (SAVE+4a0 == 0) use DOS hundredths during FIG's
    ; startup to select one of eight adjacent ORC directory entries.  A
    ; phase-controlled capture can therefore reproduce a declared original
    ; formation instead of silently comparing it with the rewrite's
    ; deterministic hundredth-zero choice.  The child still performs its own
    ; untouched INT 21h/2ch read; this loop controls only its launch phase.
.wait_fig_phase:
    mov ah, 0x2c
    int 0x21
    cmp dl, FIG_PHASE_HUNDREDTH
    jne .wait_fig_phase
%endif

%ifdef FIXED_HUNDREDTH
    ; Deterministic original/random-battle comparison: intercept only DOS
    ; Get Time while the untouched child is resident and replace DL with the
    ; declared hundredth.  Every other INT 21h call chains directly to DOS.
    ; This mirrors the rewrite replay clock without patching RPG.EXE/FIG.EXE
    ; and is stronger than launch phasing when startup crosses a clock tick.
    mov ax, 0x3521
    int 0x21
    mov [old_int21], bx
    mov [old_int21 + 2], es
    mov dx, int21_hook
    mov ax, 0x2521
    int 0x21
%endif

    push cs
    pop es
    mov [exec_block + 4], ds
    mov [exec_block + 8], ds
    mov [exec_block + 12], ds
    mov bx, exec_block
    mov dx, CHILD_NAME
    mov ax, 0x4b00
    int 0x21
    jc fail

    ; Re-establish data registers; DOS EXEC does not promise to preserve them.
    push cs
    pop ds
    push cs
    pop es

%ifdef FIXED_HUNDREDTH
    ; Restore DOS before this wrapper performs its own file and clock calls.
    push ds
    mov dx, [cs:old_int21]
    mov ax, [cs:old_int21 + 2]
    mov ds, ax
    mov ax, 0x2521
    int 0x21
    pop ds
%endif

%ifdef FIG_STEP
    ; FIG:01bah only writes the OC marker into the shared segment.  Its
    ; updated 0546h-byte state is committed through the released SAVE.DAQ
    ; path.  Rebuild the same marker+state transfer that RPG will observe on
    ; the following OC entry instead of recording FIG's unrelated 4000h
    ; scratch contents as if they were state.
    mov dx, save_q_name
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
    jc fail_close_input
    cmp ax, 0x0546
    jne fail_close_input
    mov ah, 0x3e
    int 0x21
    jc fail
%endif

    ; Function 5Bh refuses an existing output instead of silently truncating
    ; evidence from a prior step.
    mov dx, output_name
    xor cx, cx
    mov ah, 0x5b
    int 0x21
    jc fail
    mov bx, ax
    push ds
    mov ax, 0x4000
    mov ds, ax
    xor dx, dx
    mov cx, 0x0548
    mov ah, 0x40
    int 0x21
    pop ds
    jc fail_close_output
    cmp ax, 0x0548
    jne fail_close_output
    mov ah, 0x3e
    int 0x21
    jc fail

    ; Persist the byte evidence before the acknowledgement grace period.  If
    ; the external capture reaches its declared time limit during the grace
    ; loop, the already-closed STEP.OUT remains complete and collectible.
    ; A key can legitimately cause the child to return while its key-up half
    ; (or a redundant retry) is still scheduled; exiting then makes DOSBox-X
    ; report a truncated AUTOTYPE stream.  BIOS INT 15h/86h returns
    ; immediately for this multi-second request in the capture target, so
    ; observe twelve DOS wall-clock second changes instead.  The fixed capture
    ; clock starts at noon, while direct second-field comparison is also safe
    ; across the 59->0 wrap.
    mov ah, 0x2c
    int 0x21
    mov bl, dh
    mov si, 12
.wait_second:
    mov ah, 0x2c
    int 0x21
    cmp dh, bl
    je .wait_second
    mov bl, dh
    dec si
    jnz .wait_second

    mov ax, 0x4c00
    int 0x21

fail_close_input:
    mov ah, 0x3e
    int 0x21
    jmp fail
fail_close_output:
    mov ah, 0x3e
    int 0x21
fail:
    mov ax, 0x4c01
    int 0x21

%ifdef FIXED_HUNDREDTH
    ; INT 21h uses enough of the 0240h parent stack to overwrite nearby data.
    ; Keep the EXEC filename wholly above that stack top; the lower names are
    ; consumed either before EXEC or only after the child has returned.
input_name db 'STEP.IN',0
output_name db 'STEP.OUT',0
save_q_name db 'SAVE.DAQ',0
%ifdef RPG_FIXED_HUNDREDTH
    ; RPG's launcher path is shorter than FIG's SAVE reconstruction path, so
    ; pad its selected child name to the same safe 0242h memory address.
times 0x0142 - ($ - $$) db 0
rpg_name db 'RPG.EXE',0
fig_name db 'FIG.EXE',0
%else
rpg_name db 'RPG.EXE',0
times 5 db 0
fig_name db 'FIG.EXE',0
%endif
%else
input_name db 'STEP.IN',0
output_name db 'STEP.OUT',0
%ifdef FIG_STEP
save_q_name db 'SAVE.DAQ',0
%endif
rpg_name db 'RPG.EXE',0
fig_name db 'FIG.EXE',0
%endif
%ifdef FIXED_HUNDREDTH
align 2
old_int21 dw 0, 0
int21_hook:
    cmp ah, 0x2c
    jne .chain
    pushf
    call far [cs:old_int21]
    mov dl, FIXED_HUNDREDTH
    iret
.chain:
    jmp far [cs:old_int21]
%endif
align 2
exec_block:
    dw 0
    dw 0x80, 0
    dw 0x5c, 0
    dw 0x6c, 0
program_end:
