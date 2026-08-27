.386
.model flat
.code
PUBLIC _FastCallByte
PUBLIC _FastCallWord
PUBLIC _FastCallDword
PUBLIC _FastCallRegisterCheck
_FastCallByte PROC
    cmp ecx, 13572468h
    jne bad_byte
    mov eax, 0A5h
    ret
bad_byte:
    xor eax, eax
    ret
_FastCallByte ENDP
_FastCallWord PROC
    cmp ecx, 13572468h
    jne bad_word
    cmp edx, 1234h
    jne bad_word
    mov eax, 5AA5h
    ret
bad_word:
    xor eax, eax
    ret
_FastCallWord ENDP
_FastCallDword PROC
    cmp ecx, 13572468h
    jne bad_dword
    cmp edx, 2468h
    jne bad_dword
    mov eax, [esp+4]
    add eax, [esp+8]
    ret 8
bad_dword:
    xor eax, eax
    ret 8
_FastCallDword ENDP
_FastCallRegisterCheck PROC
    push ebx
    push esi
    push edi
    cmp ecx, 13572468h
    jne bad_registers
    mov ebx, edx
    mov esi, [esp+16]
    mov edi, [esp+20]
    mov eax, ebx
    xor eax, esi
    xor eax, edi
    pop edi
    pop esi
    pop ebx
    ret 8
bad_registers:
    xor eax, eax
    pop edi
    pop esi
    pop ebx
    ret 8
_FastCallRegisterCheck ENDP
END
