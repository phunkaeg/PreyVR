00E8C5E0 48895c2408                     mov qword ptr [rsp + 8], rbx
00E8C5E5 48896c2418                     mov qword ptr [rsp + 0x18], rbp
00E8C5EA 4889742420                     mov qword ptr [rsp + 0x20], rsi
00E8C5EF 57                             push rdi
00E8C5F0 4883ec30                       sub rsp, 0x30
00E8C5F4 488b99e8000000                 mov rbx, qword ptr [rcx + 0xe8]
00E8C5FB 488bf1                         mov rsi, rcx
00E8C5FE 488bcb                         mov rcx, rbx
00E8C601 0fb6ea                         movzx ebp, dl
00E8C604 e827f020ff                     call 0x18009b630
00E8C609 488b8eb8000000                 mov rcx, qword ptr [rsi + 0xb8]
00E8C610 e85b799dff                     call 0x180863f70
00E8C615 33c9                           xor ecx, ecx
00E8C617 c644244800                     mov byte ptr [rsp + 0x48], 0
00E8C61C 894c2428                       mov dword ptr [rsp + 0x28], ecx
00E8C620 4c8d442448                     lea r8, [rsp + 0x48]
00E8C625 48894c2420                     mov qword ptr [rsp + 0x20], rcx
00E8C62A 4c8b10                         mov r10, qword ptr [rax]
00E8C62D 448d4901                       lea r9d, [rcx + 1]
00E8C631 8d510b                         lea edx, [rcx + 0xb]
00E8C634 488bc8                         mov rcx, rax
00E8C637 41ff9288080000                 call qword ptr [r10 + 0x888]
00E8C63E 4883bea800000000               cmp qword ptr [rsi + 0xa8], 0
00E8C646 0f84f5000000                   je 0x180e8c741
00E8C64C 807c244800                     cmp byte ptr [rsp + 0x48], 0
00E8C651 0f85ea000000                   jne 0x180e8c741
00E8C657 448b463c                       mov r8d, dword ptr [rsi + 0x3c]
00E8C65B 418bc8                         mov ecx, r8d
00E8C65E 83e1ef                         and ecx, 0xffffffef
00E8C661 f6461401                       test byte ptr [rsi + 0x14], 1
00E8C665 740d                           je 0x180e8c674
00E8C667 833d16b33e0100                 cmp dword ptr [rip + 0x13eb316], 0
00E8C66E 7404                           je 0x180e8c674
00E8C670 b201                           mov dl, 1
00E8C672 eb02                           jmp 0x180e8c676
00E8C674 32d2                           xor dl, dl
00E8C676 8bc1                           mov eax, ecx
00E8C678 83c810                         or eax, 0x10
00E8C67B 84d2                           test dl, dl
00E8C67D 0f44c1                         cmove eax, ecx
00E8C680 413bc0                         cmp eax, r8d
00E8C683 7403                           je 0x180e8c688
00E8C685 89463c                         mov dword ptr [rsi + 0x3c], eax
00E8C688 f30f1005f8b23e01               movss xmm0, dword ptr [rip + 0x13eb2f8]
00E8C690 0f2e4638                       ucomiss xmm0, dword ptr [rsi + 0x38]
00E8C694 742d                           je 0x180e8c6c3
00E8C696 f30f100d8a24df00               movss xmm1, dword ptr [rip + 0xdf248a]
00E8C69E 0f2fc1                         comiss xmm0, xmm1
00E8C6A1 730b                           jae 0x180e8c6ae
00E8C6A3 f30f110dddb23e01               movss dword ptr [rip + 0x13eb2dd], xmm1
00E8C6AB 0f28c1                         movaps xmm0, xmm1
00E8C6AE f30f5d05ba9ddf00               minss xmm0, dword ptr [rip + 0xdf9dba]
00E8C6B6 f30f5f058294df00               maxss xmm0, dword ptr [rip + 0xdf9482]
00E8C6BE f30f114638                     movss dword ptr [rsi + 0x38], xmm0
00E8C6C3 f30f104e10                     movss xmm1, dword ptr [rsi + 0x10]
00E8C6C8 488b8eb8000000                 mov rcx, qword ptr [rsi + 0xb8]
00E8C6CF e8ac12f3ff                     call 0x180dbd980
00E8C6D4 0fb65614                       movzx edx, byte ptr [rsi + 0x14]
00E8C6D8 488b8eb8000000                 mov rcx, qword ptr [rsi + 0xb8]
00E8C6DF d0ea                           shr dl, 1
00E8C6E1 80e201                         and dl, 1
00E8C6E4 e847c3f2ff                     call 0x180db8a30
00E8C6E9 0fb65614                       movzx edx, byte ptr [rsi + 0x14]
00E8C6ED 488b8eb8000000                 mov rcx, qword ptr [rsi + 0xb8]
00E8C6F4 c0ea02                         shr dl, 2
00E8C6F7 80e201                         and dl, 1
00E8C6FA e851f9f2ff                     call 0x180dbc050
00E8C6FF 0fb65615                       movzx edx, byte ptr [rsi + 0x15]
00E8C703 488b8eb8000000                 mov rcx, qword ptr [rsi + 0xb8]
00E8C70A c0ea05                         shr dl, 5
00E8C70D 80e201                         and dl, 1
00E8C710 e86bfbf2ff                     call 0x180dbc280
00E8C715 488d0d54ccc801                 lea rcx, [rip + 0x1c8cc54]
00E8C71C e80f3721ff                     call 0x18009fe30
00E8C721 84c0                           test al, al
00E8C723 741c                           je 0x180e8c741
00E8C725 488b8ea8000000                 mov rcx, qword ptr [rsi + 0xa8]
00E8C72C 488b01                         mov rax, qword ptr [rcx]
00E8C72F ff9030010000                   call qword ptr [rax + 0x130]
00E8C735 488d0d34ccc801                 lea rcx, [rip + 0x1c8cc34]
00E8C73C e8ef32cb00                     call 0x181b3fa30
00E8C741 488bcb                         mov rcx, rbx
00E8C744 e8e732cb00                     call 0x181b3fa30
00E8C749 4084ed                         test bpl, bpl
00E8C74C 740b                           je 0x180e8c759
00E8C74E 488b46f8                       mov rax, qword ptr [rsi - 8]
00E8C752 488d4ef8                       lea rcx, [rsi - 8]
00E8C756 ff5008                         call qword ptr [rax + 8]
00E8C759 488b5c2440                     mov rbx, qword ptr [rsp + 0x40]
00E8C75E 488b6c2450                     mov rbp, qword ptr [rsp + 0x50]