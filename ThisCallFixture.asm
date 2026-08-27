.386
.model flat
.code
PUBLIC ThisCallByte
PUBLIC ThisCallWord
PUBLIC ThisCallDword
PUBLIC ThisCallRepeated
ThisCallByte PROC
    cmp ecx, 13572468h
    jne bad_byte
    mov eax, 0A5h
    ret
bad_byte:
    xor eax, eax
    ret
ThisCallByte ENDP
ThisCallWord PROC
    cmp ecx, 13572468h
    jne bad_word
    movzx eax, word ptr [esp+4]
    xor eax, 5AA5h
    ret 4
bad_word:
    xor eax, eax
    ret 4
ThisCallWord ENDP
ThisCallDword PROC
    cmp ecx, 13572468h
    jne bad_dword
    mov eax, 10203040h
    mov edx, [esp+4]
    add eax, edx
    imul edx, [esp+8], 10
    add eax, edx
    imul edx, [esp+12], 100
    add eax, edx
    imul edx, [esp+16], 1000
    add eax, edx
    ret 16
bad_dword:
    xor eax, eax
    ret 16
ThisCallDword ENDP
ThisCallRepeated PROC
    cmp ecx, 13572468h
    jne bad_repeated
    mov eax, [esp+4]
    add eax, 7
    ret 4
bad_repeated:
    xor eax, eax
    ret 4
ThisCallRepeated ENDP
END
