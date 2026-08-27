; Project-owned x86 fixture that checks callee-saved register preservation.
; The wrappers are invoked through both x86 calling-convention adapters.
.386
.model flat
.code

PUBLIC X86CdeclRegisterSentinel
PUBLIC X86StdcallRegisterSentinel

; This child uses only volatile registers. The wrappers below put sentinels in
; EBX, ESI, and EDI, call it, and verify that the ABI preserved those values.
PreservingChild PROC
    mov eax, 1
    mov ecx, 2
    mov edx, 3
    add eax, ecx
    add eax, edx
    ret
PreservingChild ENDP

X86CdeclRegisterSentinel PROC
    push ebx
    push esi
    push edi
    mov ebx, 13579BDFh
    mov esi, 2468ACE0h
    mov edi, 0BADF00Dh
    call PreservingChild
    cmp ebx, 13579BDFh
    jne short cdecl_failed
    cmp esi, 2468ACE0h
    jne short cdecl_failed
    cmp edi, 0BADF00Dh
    jne short cdecl_failed
    mov eax, 1
    jmp short cdecl_done
cdecl_failed:
    xor eax, eax
cdecl_done:
    pop edi
    pop esi
    pop ebx
    ret
X86CdeclRegisterSentinel ENDP

X86StdcallRegisterSentinel PROC
    push ebx
    push esi
    push edi
    mov ebx, 13579BDFh
    mov esi, 2468ACE0h
    mov edi, 0BADF00Dh
    call PreservingChild
    cmp ebx, 13579BDFh
    jne short stdcall_failed
    cmp esi, 2468ACE0h
    jne short stdcall_failed
    cmp edi, 0BADF00Dh
    jne short stdcall_failed
    mov eax, 1
    jmp short stdcall_done
stdcall_failed:
    xor eax, eax
stdcall_done:
    pop edi
    pop esi
    pop ebx
    ret 0
X86StdcallRegisterSentinel ENDP

END
