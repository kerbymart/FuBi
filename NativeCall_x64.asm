; Project-owned Windows x64 integer and pointer invocation adapter.
; The C++ boundary supplies normalized values and never passes CLI text here.
OPTION CASEMAP:NONE
.code

PUBLIC NativeCallX64
NativeCallX64 PROC
    ; RCX = target, RDX = argument array, R8D = count, R9 = return storage.
    ; Reserve shadow space, eight argument slots, and alignment padding.
    mov rax, rcx
    mov r10, rdx
    mov r11d, r8d
    mov rdx, r9
    sub rsp, 58h
    mov qword ptr [rsp+50h], rdx

    xor ecx, ecx
    xor edx, edx
    xor r8d, r8d
    xor r9d, r9d
    cmp r11d, 1
    jb short arguments_ready
    mov rcx, qword ptr [r10]
    cmp r11d, 2
    jb short arguments_ready
    mov rdx, qword ptr [r10+8]
    cmp r11d, 3
    jb short arguments_ready
    mov r8, qword ptr [r10+16]
    cmp r11d, 4
    jb short arguments_ready
    mov r9, qword ptr [r10+24]
arguments_ready:
    cmp r11d, 5
    jb short stack_ready
    mov rdx, qword ptr [r10+20h]
    mov qword ptr [rsp+20h], rdx
    cmp r11d, 6
    jb short stack_ready
    mov rdx, qword ptr [r10+28h]
    mov qword ptr [rsp+28h], rdx
    cmp r11d, 7
    jb short stack_ready
    mov rdx, qword ptr [r10+30h]
    mov qword ptr [rsp+30h], rdx
    cmp r11d, 8
    jb short stack_ready
    mov rdx, qword ptr [r10+38h]
    mov qword ptr [rsp+38h], rdx
stack_ready:
    call rax
    mov r10, qword ptr [rsp+50h]
    mov qword ptr [r10], rax
    add rsp, 58h
    ret
NativeCallX64 ENDP
END
