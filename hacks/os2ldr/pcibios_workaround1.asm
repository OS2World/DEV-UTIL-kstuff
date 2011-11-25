; $Id: $
;; @file
; Workaround for borked PCI BIOSes in recent UEFI BIOSES.
;

;
; Copyright (C) Knut St. Osmundsen
; All rights reserved.
;


;
; SMP OS2LDR constants.
;
%define ADDR_CallPciBios                0x2a73
%define ADDR_fpfnDevHlp                 0x2496
%define ADDR_fpPciBiosServiceVector     0x24ac
%define OS2LDR_DATA_SEG                 0x0108

%define PATCH_ORG       0c600h          ; End of OS2LDR stack + lots.
        ORG     PATCH_ORG
        BITS    16

;
; A patch header that can be processed by my patch utility.
;
patch_header:
        db      'bird'                  ; signature
        dw      PATCH_ORG               ; the ORG of this patch

patch_sizes:
        db      'size'
        dw      2                       ; two sizes to patch
        dw      07a21h, 0baa2h, 0000h   ; <offset>, <orignal value>, <delta>
        dw      07ce0h, 079f8h, 0000h   ; <offset>, <orignal value>, <delta>

patch_jmps:
        db      'jmps'
        dw      1                       ; one patch jmp
%if 1
        ; CallPciBios
        dw      ADDR_CallPciBios
        dw      CallPciBiosWrapper
        dw      6h                      ; length of the following instructions
CallPciBiosLeadIn:
        push    ebp
        mov     [024a8h], ss
        jmp     ADDR_CallPciBios + 6h
%endif
        db      'end',0

;
; OS/2 constants.
;
%define VMDHA_FIXED                     (1 << 1)
%define VMDHA_USEHIGHMEM                (1 << 11)
%define DevHlp_VMAlloc                  0x57
%define DevHlp_GetDOSVar                0x24
struc DosTable2
        .d2_Entries                     resb 1
        .d2_ErrMap24                    resd 1
        .d2_MsgMap24                    resd 1
        .d2_Err_Table_24                resd 1
        .d2_CDSAddr                     resd 1
        .d2_GDT_RDR1                    resd 1
        .d2_InterruptLevel              resd 1
        .d2__cInDos                     resd 1
        .d2_zero_1                      resd 1
        .d2_zero_2                      resd 1
        .d2_FlatCS                      resd 1
        .d2_FlatDS                      resd 1
        .d2__TKSSBase                   resd 1
        .d2_intSwitchStack              resd 1
        .d2_privateStack                resd 1
        .d2_PhysDiskTablePtr            resd 1
        .d2_forceEMHandler              resd 1
        .d2_ReserveVM                   resd 1
        .d2_pgpPageDir                  resd 1
        .d2_unknown                     resd 1
endstruc

;
; Globals
;
gdt_lock:
        dd      0
gdtr_saved:
        dd      0, 0
gdtr_copy:
        dd      0, 0
gdtr_inited:
        db      0
        db      0
flat_ds:
        dw      178h


db 'padding', 0
db 'padding', 0
db 'padding', 0
db 'padding', 0
db 'padding', 0
db 'padding', 0

;;
; Function which wrapps up the CallPciBios.
;
CallPciBiosWrapper:
        push    fs
        push    ds
        pop     fs                      ; load whatever DATA segment we're working with.

        ; Take the lock.
        push    eax
.retry:
        mov     al, 1
        xchg    [fs:gdt_lock], al
        cmp     al, 0
        je      .locked
        pause
        jmp     .retry
.locked:
        pop     eax

        ; Save the GDTR.
        db 066h
        sgdt    [fs:gdtr_saved]

        ; Lazy init detour.
        cmp     byte [fs:gdtr_inited], 0
        je      .set_up_gdt

        ; Update the GDT copy.
.update_gdt:
        push    ecx
        push    edi
        push    esi
        push    es
        push    ds

        mov     cx, [fs:flat_ds]
        mov     ds, cx
        mov     es, cx

        movzx   ecx, word [fs:gdtr_saved]
        inc     ecx
        cmp     ecx, 0f000h
        jbe     .small_gdt
        mov     ecx, 0f000h
.small_gdt:
        mov     esi, [fs:gdtr_saved + 2]
        mov     edi, [fs:gdtr_copy + 2]
        cld
        db 067h
        rep movsb

        pop     ds
        pop     es
        pop     esi
        pop     edi
        pop     ecx

        ; Load the GDT copy.
        pushf
        cli
        db 066h
        lgdt    [fs:gdtr_copy]
        popf

        ; Call what remains of CallPciBios instructions.
        call    CallPciBiosLeadIn

        ; Restore the OS/2 GDTR and release the lock.
        pushf
        push    eax
        cli

        db 066h
        lgdt    [fs:gdtr_saved]

        xor     al, al
        xchg    [fs:gdt_lock], al

        pop     eax
        popf

        pop     fs
        ret

        ;
        ; Set up the 'copy' GDT.
        ;
.set_up_gdt:
        push    es
        push    ds
        push    gs
        pushad

        ; Figure out what the 32-bit DS selector value is.
        mov     eax, 9                  ; Undocumented, dostable.
        xor     ecx, ecx
        mov     dl, DevHlp_GetDOSVar
        call far [fs:ADDR_fpfnDevHlp]
        jc near .failed
        mov     es, ax
        movzx   cx, byte [es:bx]        ; table length
        shl     cx, 2
        inc     cx
        add     bx, cx                  ; es:bx points to DosTable2.
        mov     ax, [es:bx + DosTable2.d2_FlatDS]
        mov     [fs:flat_ds], ax

        ; Allocate a 64KB GDT.
        mov     ecx, 10000h             ; Size = 64KB
        mov     edi, 0ffffffffh         ; PhysAddr = -1
        mov     eax, VMDHA_FIXED | VMDHA_USEHIGHMEM
        mov     dl, DevHlp_VMAlloc
        call far [fs:ADDR_fpfnDevHlp]
        jc      .failed
        mov     [fs:gdtr_copy+2], eax
        mov     word [fs:gdtr_copy], 0ffffh

        ; Clear it.
        cld
        mov     ax, [fs:flat_ds]        ; load flag gd
        mov     ds, ax
        mov     es, ax
        mov     edi, [fs:gdtr_copy + 2]
        xor     eax, eax
        mov     ecx, 10000h
        db 067h
        rep stosb

        ; Set up the 0f000h selector as a copy of the other ROM selector.
        mov     si, [fs:ADDR_fpPciBiosServiceVector + 2]
        and     esi, 0000fff8h
        add     esi, [fs:gdtr_saved + 2] ; = source gdt entry
        mov     edi, [fs:gdtr_copy + 2]
        add     edi, dword 0f000h       ;  = destination gdt entry  - ?dword for yasm bug? saw '66 81 c7 00 10 00 10    add edi, 010001000h'
        mov     ecx, 8h                 ; gdt entry size
        db 067h
        rep movsb

        ; Indicated that we've completed the GDT init.
        inc     word [fs:gdtr_inited]

.done:
        popad
        pop     gs
        pop     ds
        pop     es
        jmp     .update_gdt
.failed:
        int3
        jmp     .done


db 'Padding', 0
db 'Padding', 0
db 'Padding', 0
db 'Padding', 0
db 'Padding', 0
db 'Padding', 0

