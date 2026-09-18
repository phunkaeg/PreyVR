00DBE300 48895c2410                     mov qword ptr [rsp + 0x10], rbx
00DBE305 57                             push rdi
00DBE306 4883ec20                       sub rsp, 0x20
00DBE30A 4883b94803000000               cmp qword ptr [rcx + 0x348], 0
00DBE312 488bfa                         mov rdi, rdx
00DBE315 488bd9                         mov rbx, rcx
00DBE318 0f8416010000                   je 0x180dbe434
00DBE31E ff159c50eb00                   call qword ptr [rip + 0xeb509c]
00DBE324 3b833c030000                   cmp eax, dword ptr [rbx + 0x33c]
00DBE32A 0f8504010000                   jne 0x180dbe434
00DBE330 4885ff                         test rdi, rdi
00DBE333 0f84b0000000                   je 0x180dbe3e9
00DBE339 4889742430                     mov qword ptr [rsp + 0x30], rsi
00DBE33E 488bb348030000                 mov rsi, qword ptr [rbx + 0x348]
00DBE345 488b5628                       mov rdx, qword ptr [rsi + 0x28]
00DBE349 488d4e10                       lea rcx, [rsi + 0x10]
00DBE34D 482b5110                       sub rdx, qword ptr [rcx + 0x10]
00DBE351 488b4608                       mov rax, qword ptr [rsi + 8]
00DBE355 4883c004                       add rax, 4
00DBE359 483bc2                         cmp rax, rdx
00DBE35C 760c                           jbe 0x180dbe36a
00DBE35E 4883c204                       add rdx, 4
00DBE362 4c8bc1                         mov r8, rcx
00DBE365 e8d68dffff                     call 0x180db7140
00DBE36A 488b4608                       mov rax, qword ptr [rsi + 8]
00DBE36E 488b4e20                       mov rcx, qword ptr [rsi + 0x20]
00DBE372 c704010c000000                 mov dword ptr [rcx + rax], 0xc
00DBE379 4883460804                     add qword ptr [rsi + 8], 4
00DBE37E 488b9b48030000                 mov rbx, qword ptr [rbx + 0x348]
00DBE385 488b5328                       mov rdx, qword ptr [rbx + 0x28]
00DBE389 488d4b10                       lea rcx, [rbx + 0x10]
00DBE38D 482b5110                       sub rdx, qword ptr [rcx + 0x10]
00DBE391 488b742430                     mov rsi, qword ptr [rsp + 0x30]
00DBE396 488b4308                       mov rax, qword ptr [rbx + 8]
00DBE39A 4883c040                       add rax, 0x40
00DBE39E 483bc2                         cmp rax, rdx
00DBE3A1 760c                           jbe 0x180dbe3af
00DBE3A3 4883c240                       add rdx, 0x40
00DBE3A7 4c8bc1                         mov r8, rcx
00DBE3AA e8918dffff                     call 0x180db7140
00DBE3AF 0f1007                         movups xmm0, xmmword ptr [rdi]
00DBE3B2 488b4308                       mov rax, qword ptr [rbx + 8]
00DBE3B6 488b4b20                       mov rcx, qword ptr [rbx + 0x20]
00DBE3BA 0f110401                       movups xmmword ptr [rcx + rax], xmm0
00DBE3BE 0f104f10                       movups xmm1, xmmword ptr [rdi + 0x10]
00DBE3C2 0f114c0110                     movups xmmword ptr [rcx + rax + 0x10], xmm1
00DBE3C7 0f104720                       movups xmm0, xmmword ptr [rdi + 0x20]
00DBE3CB 0f11440120                     movups xmmword ptr [rcx + rax + 0x20], xmm0
00DBE3D0 0f104f30                       movups xmm1, xmmword ptr [rdi + 0x30]
00DBE3D4 0f114c0130                     movups xmmword ptr [rcx + rax + 0x30], xmm1
00DBE3D9 4883430840                     add qword ptr [rbx + 8], 0x40
00DBE3DE 488b5c2438                     mov rbx, qword ptr [rsp + 0x38]
00DBE3E3 4883c420                       add rsp, 0x20
00DBE3E7 5f                             pop rdi
00DBE3E8 c3                             ret 
00DBE3E9 488b9b48030000                 mov rbx, qword ptr [rbx + 0x348]
00DBE3F0 488b5328                       mov rdx, qword ptr [rbx + 0x28]
00DBE3F4 488d4b10                       lea rcx, [rbx + 0x10]
00DBE3F8 482b5110                       sub rdx, qword ptr [rcx + 0x10]
00DBE3FC 488b4308                       mov rax, qword ptr [rbx + 8]
00DBE400 4883c004                       add rax, 4
00DBE404 483bc2                         cmp rax, rdx
00DBE407 760c                           jbe 0x180dbe415
00DBE409 4883c204                       add rdx, 4
00DBE40D 4c8bc1                         mov r8, rcx
00DBE410 e82b8dffff                     call 0x180db7140
00DBE415 488b4308                       mov rax, qword ptr [rbx + 8]
00DBE419 488b4b20                       mov rcx, qword ptr [rbx + 0x20]
00DBE41D c704010d000000                 mov dword ptr [rcx + rax], 0xd
00DBE424 4883430804                     add qword ptr [rbx + 8], 4
00DBE429 488b5c2438                     mov rbx, qword ptr [rsp + 0x38]
00DBE42E 4883c420                       add rsp, 0x20
00DBE432 5f                             pop rdi
00DBE433 c3                             ret 
00DBE434 4885ff                         test rdi, rdi
00DBE437 4889bbf0010000                 mov qword ptr [rbx + 0x1f0], rdi
00DBE43E 0f95c0                         setne al
00DBE441 88837a020000                   mov byte ptr [rbx + 0x27a], al
00DBE447 888379020000                   mov byte ptr [rbx + 0x279], al
00DBE44D 488b5c2438                     mov rbx, qword ptr [rsp + 0x38]
00DBE452 4883c420                       add rsp, 0x20
00DBE456 5f                             pop rdi
00DBE457 c3                             ret 
00DBE458 cc                             int3 
00DBE459 cc                             int3 
00DBE45A cc                             int3 
00DBE45B cc                             int3 
00DBE45C cc                             int3 
00DBE45D cc                             int3 
00DBE45E cc                             int3 
00DBE45F cc                             int3 
00DBE460 48895c2418                     mov qword ptr [rsp + 0x18], rbx
00DBE465 4889742420                     mov qword ptr [rsp + 0x20], rsi
00DBE46A 4156                           push r14
00DBE46C 4883ec20                       sub rsp, 0x20