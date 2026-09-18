00DBE1E0 48895c2410                     mov qword ptr [rsp + 0x10], rbx
00DBE1E5 57                             push rdi
00DBE1E6 4883ec20                       sub rsp, 0x20
00DBE1EA 4883b94803000000               cmp qword ptr [rcx + 0x348], 0
00DBE1F2 488bfa                         mov rdi, rdx
00DBE1F5 488bd9                         mov rbx, rcx
00DBE1F8 0f84c2000000                   je 0x180dbe2c0
00DBE1FE ff15bc51eb00                   call qword ptr [rip + 0xeb51bc]
00DBE204 3b833c030000                   cmp eax, dword ptr [rbx + 0x33c]
00DBE20A 0f85b0000000                   jne 0x180dbe2c0
00DBE210 4889742430                     mov qword ptr [rsp + 0x30], rsi
00DBE215 488bb348030000                 mov rsi, qword ptr [rbx + 0x348]
00DBE21C 488b5628                       mov rdx, qword ptr [rsi + 0x28]
00DBE220 488d4e10                       lea rcx, [rsi + 0x10]
00DBE224 482b5110                       sub rdx, qword ptr [rcx + 0x10]
00DBE228 488b4608                       mov rax, qword ptr [rsi + 8]
00DBE22C 4883c004                       add rax, 4
00DBE230 483bc2                         cmp rax, rdx
00DBE233 760c                           jbe 0x180dbe241
00DBE235 4883c204                       add rdx, 4
00DBE239 4c8bc1                         mov r8, rcx
00DBE23C e8ff8effff                     call 0x180db7140
00DBE241 488b4608                       mov rax, qword ptr [rsi + 8]
00DBE245 488b4e20                       mov rcx, qword ptr [rsi + 0x20]
00DBE249 c704010b000000                 mov dword ptr [rcx + rax], 0xb
00DBE250 4883460804                     add qword ptr [rsi + 8], 4
00DBE255 488b9b48030000                 mov rbx, qword ptr [rbx + 0x348]
00DBE25C 488b5328                       mov rdx, qword ptr [rbx + 0x28]
00DBE260 488d4b10                       lea rcx, [rbx + 0x10]
00DBE264 482b5110                       sub rdx, qword ptr [rcx + 0x10]
00DBE268 488b742430                     mov rsi, qword ptr [rsp + 0x30]
00DBE26D 488b4308                       mov rax, qword ptr [rbx + 8]
00DBE271 4883c040                       add rax, 0x40
00DBE275 483bc2                         cmp rax, rdx
00DBE278 760c                           jbe 0x180dbe286
00DBE27A 4883c240                       add rdx, 0x40
00DBE27E 4c8bc1                         mov r8, rcx
00DBE281 e8ba8effff                     call 0x180db7140
00DBE286 0f1007                         movups xmm0, xmmword ptr [rdi]
00DBE289 488b4308                       mov rax, qword ptr [rbx + 8]
00DBE28D 488b4b20                       mov rcx, qword ptr [rbx + 0x20]
00DBE291 0f110401                       movups xmmword ptr [rcx + rax], xmm0
00DBE295 0f104f10                       movups xmm1, xmmword ptr [rdi + 0x10]
00DBE299 0f114c0110                     movups xmmword ptr [rcx + rax + 0x10], xmm1
00DBE29E 0f104720                       movups xmm0, xmmword ptr [rdi + 0x20]
00DBE2A2 0f11440120                     movups xmmword ptr [rcx + rax + 0x20], xmm0
00DBE2A7 0f104f30                       movups xmm1, xmmword ptr [rdi + 0x30]
00DBE2AB 0f114c0130                     movups xmmword ptr [rcx + rax + 0x30], xmm1
00DBE2B0 4883430840                     add qword ptr [rbx + 8], 0x40
00DBE2B5 488b5c2438                     mov rbx, qword ptr [rsp + 0x38]
00DBE2BA 4883c420                       add rsp, 0x20
00DBE2BE 5f                             pop rdi
00DBE2BF c3                             ret 
00DBE2C0 0f1007                         movups xmm0, xmmword ptr [rdi]
00DBE2C3 0f1183f8010000                 movups xmmword ptr [rbx + 0x1f8], xmm0
00DBE2CA 0f104f10                       movups xmm1, xmmword ptr [rdi + 0x10]
00DBE2CE 0f118b08020000                 movups xmmword ptr [rbx + 0x208], xmm1
00DBE2D5 0f104720                       movups xmm0, xmmword ptr [rdi + 0x20]
00DBE2D9 0f118318020000                 movups xmmword ptr [rbx + 0x218], xmm0
00DBE2E0 0f104f30                       movups xmm1, xmmword ptr [rdi + 0x30]
00DBE2E4 66c783790200000101             mov word ptr [rbx + 0x279], 0x101
00DBE2ED 0f118b28020000                 movups xmmword ptr [rbx + 0x228], xmm1
00DBE2F4 488b5c2438                     mov rbx, qword ptr [rsp + 0x38]
00DBE2F9 4883c420                       add rsp, 0x20
00DBE2FD 5f                             pop rdi
00DBE2FE c3                             ret 
00DBE2FF cc                             int3 