018AB710 4055                           push rbp
018AB712 56                             push rsi
018AB713 4881ec68030000                 sub rsp, 0x368
018AB71A 488b05a783b600                 mov rax, qword ptr [rip + 0xb683a7]
018AB721 4833c4                         xor rax, rsp
018AB724 4889842450030000               mov qword ptr [rsp + 0x350], rax
018AB72C 33ed                           xor ebp, ebp
018AB72E 488bf1                         mov rsi, rcx
018AB731 896c2440                       mov dword ptr [rsp + 0x40], ebp
018AB735 48396958                       cmp qword ptr [rcx + 0x58], rbp
018AB739 0f84d8020000                   je 0x1818aba17
018AB73F 48899c2488030000               mov qword ptr [rsp + 0x388], rbx
018AB747 8bdd                           mov ebx, ebp
018AB749 48395948                       cmp qword ptr [rcx + 0x48], rbx
018AB74D 0f86bc020000                   jbe 0x1818aba0f
018AB753 8bcd                           mov ecx, ebp
018AB755 6666660f1f840000000000         nop word ptr [rax + rax]
018AB760 488b4640                       mov rax, qword ptr [rsi + 0x40]
018AB764 4803c9                         add rcx, rcx
018AB767 488b4cc808                     mov rcx, qword ptr [rax + rcx*8 + 8]
018AB76C 488b01                         mov rax, qword ptr [rcx]
018AB76F ff5020                         call qword ptr [rax + 0x20]
018AB772 84c0                           test al, al
018AB774 750f                           jne 0x1818ab785
018AB776 ffc3                           inc ebx
018AB778 8bcb                           mov ecx, ebx
018AB77A 483b4e48                       cmp rcx, qword ptr [rsi + 0x48]
018AB77E 72e0                           jb 0x1818ab760
018AB780 e98a020000                     jmp 0x1818aba0f
018AB785 488b8e40010000                 mov rcx, qword ptr [rsi + 0x140]
018AB78C 4c8d96e8000000                 lea r10, [rsi + 0xe8]
018AB793 488b4660                       mov rax, qword ptr [rsi + 0x60]
018AB797 f30f1086ac000000               movss xmm0, dword ptr [rsi + 0xac]
018AB79F 40886c2438                     mov byte ptr [rsp + 0x38], bpl
018AB7A4 488d5110                       lea rdx, [rcx + 0x10]
018AB7A8 48896c2430                     mov qword ptr [rsp + 0x30], rbp
018AB7AD 4c8b4838                       mov r9, qword ptr [rax + 0x38]
018AB7B1 4c8b4028                       mov r8, qword ptr [rax + 0x28]
018AB7B5 4983c130                       add r9, 0x30
018AB7B9 4885c9                         test rcx, rcx
018AB7BC 4c89542428                     mov qword ptr [rsp + 0x28], r10
018AB7C1 488d4c2450                     lea rcx, [rsp + 0x50]
018AB7C6 f30f11442420                   movss dword ptr [rsp + 0x20], xmm0
018AB7CC 480f44d5                       cmove rdx, rbp
018AB7D0 48896c2448                     mov qword ptr [rsp + 0x48], rbp
018AB7D5 4d8b4030                       mov r8, qword ptr [r8 + 0x30]
018AB7D9 e8b2b30d00                     call 0x181986b90
018AB7DE 488b8c24b8000000               mov rcx, qword ptr [rsp + 0xb8]
018AB7E6 4885c9                         test rcx, rcx
018AB7E9 0f8416020000                   je 0x1818aba05
018AB7EF 48396918                       cmp qword ptr [rcx + 0x18], rbp
018AB7F3 0f840c020000                   je 0x1818aba05
018AB7F9 838ee825000002                 or dword ptr [rsi + 0x25e8], 2
018AB800 488b8424f0000000               mov rax, qword ptr [rsp + 0xf0]
018AB808 4c89b42460030000               mov qword ptr [rsp + 0x360], r14
018AB810 4885c0                         test rax, rax
018AB813 7418                           je 0x1818ab82d
018AB815 488bc8                         mov rcx, rax
018AB818 e86376f8ff                     call 0x181832e80
018AB81D 488b8424f0000000               mov rax, qword ptr [rsp + 0xf0]
018AB825 488b8c24b8000000               mov rcx, qword ptr [rsp + 0xb8]
018AB82D 488b9650010000                 mov rdx, qword ptr [rsi + 0x150]
018AB834 4885d2                         test rdx, rdx
018AB837 7418                           je 0x1818ab851
018AB839 488bca                         mov rcx, rdx
018AB83C e85f76f8ff                     call 0x181832ea0
018AB841 488b8424f0000000               mov rax, qword ptr [rsp + 0xf0]
018AB849 488b8c24b8000000               mov rcx, qword ptr [rsp + 0xb8]
018AB851 48898650010000                 mov qword ptr [rsi + 0x150], rax
018AB858 4c8d4678                       lea r8, [rsi + 0x78]
018AB85C 488b4918                       mov rcx, qword ptr [rcx + 0x18]
018AB860 488d542440                     lea rdx, [rsp + 0x40]
018AB865 8b86a8090000                   mov eax, dword ptr [rsi + 0x9a8]
018AB86B f30f1086d4000000               movss xmm0, dword ptr [rsi + 0xd4]
018AB873 f30f108ecc000000               movss xmm1, dword ptr [rsi + 0xcc]
018AB87B f30f109ec8000000               movss xmm3, dword ptr [rsi + 0xc8]
018AB883 f30f11442430                   movss dword ptr [rsp + 0x30], xmm0
018AB889 f30f1086d0000000               movss xmm0, dword ptr [rsi + 0xd0]
018AB891 89442440                       mov dword ptr [rsp + 0x40], eax
018AB895 488b01                         mov rax, qword ptr [rcx]
018AB898 f30f114c2428                   movss dword ptr [rsp + 0x28], xmm1
018AB89E f30f11442420                   movss dword ptr [rsp + 0x20], xmm0
018AB8A4 ff5060                         call qword ptr [rax + 0x60]
018AB8A7 488d542450                     lea rdx, [rsp + 0x50]
018AB8AC 488bce                         mov rcx, rsi
018AB8AF e82c6f0000                     call 0x1818b27e0
018AB8B4 4c8db648010000                 lea r14, [rsi + 0x148]
018AB8BB 498b06                         mov rax, qword ptr [r14]
018AB8BE 4885c0                         test rax, rax
018AB8C1 0f84c9000000                   je 0x1818ab990
018AB8C7 483928                         cmp qword ptr [rax], rbp
018AB8CA 0f86c0000000                   jbe 0x1818ab990
018AB8D0 4889bc2490030000               mov qword ptr [rsp + 0x390], rdi
018AB8D8 488bfd                         mov rdi, rbp
018AB8DB 4885c0                         test rax, rax
018AB8DE 7505                           jne 0x1818ab8e5
018AB8E0 4c8bf5                         mov r14, rbp
018AB8E3 eb1d                           jmp 0x1818ab902
018AB8E5 488b4808                       mov rcx, qword ptr [rax + 8]
018AB8E9 4883c010                       add rax, 0x10
018AB8ED 0f1f00                         nop dword ptr [rax]
018AB8F0 488338fe                       cmp qword ptr [rax], -2
018AB8F4 750c                           jne 0x1818ab902
018AB8F6 48ffc7                         inc rdi
018AB8F9 4883c018                       add rax, 0x18
018AB8FD 483bf9                         cmp rdi, rcx
018AB900 76ee                           jbe 0x1818ab8f0
018AB902 4d85f6                         test r14, r14
018AB905 0f847d000000                   je 0x1818ab988
018AB90B 498b1e                         mov rbx, qword ptr [r14]
018AB90E 4885db                         test rbx, rbx
018AB911 7475                           je 0x1818ab988
018AB913 483b7b08                       cmp rdi, qword ptr [rbx + 8]
018AB917 7f6f                           jg 0x1818ab988
018AB919 488d147f                       lea rdx, [rdi + rdi*2]
018AB91D 488b4cd320                     mov rcx, qword ptr [rbx + rdx*8 + 0x20]
018AB922 4885c9                         test rcx, rcx
018AB925 7408                           je 0x1818ab92f
018AB927 8b4108                         mov eax, dword ptr [rcx + 8]
018AB92A ffc0                           inc eax
018AB92C 894108                         mov dword ptr [rcx + 8], eax
018AB92F 488b5cd320                     mov rbx, qword ptr [rbx + rdx*8 + 0x20]
018AB934 488b9424b8000000               mov rdx, qword ptr [rsp + 0xb8]
018AB93C 488bcb                         mov rcx, rbx
018AB93F 488b03                         mov rax, qword ptr [rbx]
018AB942 488b5218                       mov rdx, qword ptr [rdx + 0x18]
018AB946 ff5018                         call qword ptr [rax + 0x18]
018AB949 488bcb                         mov rcx, rbx
018AB94C e87f75f8ff                     call 0x181832ed0
018AB951 498b0e                         mov rcx, qword ptr [r14]
018AB954 488b5108                       mov rdx, qword ptr [rcx + 8]
018AB958 483bfa                         cmp rdi, rdx
018AB95B 7fa5                           jg 0x1818ab902
018AB95D 48ffc7                         inc rdi
018AB960 483bfa                         cmp rdi, rdx
018AB963 779d                           ja 0x1818ab902
018AB965 488d047f                       lea rax, [rdi + rdi*2]
018AB969 488d0cc1                       lea rcx, [rcx + rax*8]
018AB96D 4883c110                       add rcx, 0x10
018AB971 488339fe                       cmp qword ptr [rcx], -2
018AB975 758b                           jne 0x1818ab902
018AB977 48ffc7                         inc rdi
018AB97A 4883c118                       add rcx, 0x18
018AB97E 483bfa                         cmp rdi, rdx
018AB981 76ee                           jbe 0x1818ab971
018AB983 e97affffff                     jmp 0x1818ab902
018AB988 488bbc2490030000               mov rdi, qword ptr [rsp + 0x390]
018AB990 4c8bb42460030000               mov r14, qword ptr [rsp + 0x360]
018AB998 48396e48                       cmp qword ptr [rsi + 0x48], rbp
018AB99C 7627                           jbe 0x1818ab9c5
018AB99E 488bcd                         mov rcx, rbp
018AB9A1 488b4640                       mov rax, qword ptr [rsi + 0x40]
018AB9A5 488d542450                     lea rdx, [rsp + 0x50]
018AB9AA 4803c9                         add rcx, rcx
018AB9AD 488b4cc808                     mov rcx, qword ptr [rax + rcx*8 + 8]
018AB9B2 488b01                         mov rax, qword ptr [rcx]
018AB9B5 ff90e8000000                   call qword ptr [rax + 0xe8]
018AB9BB ffc5                           inc ebp
018AB9BD 8bcd                           mov ecx, ebp
018AB9BF 483b4e48                       cmp rcx, qword ptr [rsi + 0x48]
018AB9C3 72dc                           jb 0x1818ab9a1
018AB9C5 488d542450                     lea rdx, [rsp + 0x50]
018AB9CA 488bce                         mov rcx, rsi
018AB9CD e8ce560000                     call 0x1818b10a0
018AB9D2 488d542450                     lea rdx, [rsp + 0x50]
018AB9D7 488bce                         mov rcx, rsi
018AB9DA e8913f0000                     call 0x1818af970
018AB9DF 488b8424b8000000               mov rax, qword ptr [rsp + 0xb8]
018AB9E7 488b4818                       mov rcx, qword ptr [rax + 0x18]
018AB9EB 488b01                         mov rax, qword ptr [rcx]
018AB9EE ff5068                         call qword ptr [rax + 0x68]
018AB9F1 488b8c24f8000000               mov rcx, qword ptr [rsp + 0xf8]
018AB9F9 e852e5f8ff                     call 0x181839f50
018AB9FE 83a6e8250000fd                 and dword ptr [rsi + 0x25e8], 0xfffffffd
018ABA05 488d4c2450                     lea rcx, [rsp + 0x50]
018ABA0A e881b20d00                     call 0x181986c90
018ABA0F 488b9c2488030000               mov rbx, qword ptr [rsp + 0x388]
018ABA17 488b8c2450030000               mov rcx, qword ptr [rsp + 0x350]
018ABA1F 4833cc                         xor rcx, rsp
018ABA22 e879c62c00                     call 0x181b780a0
018ABA27 4881c468030000                 add rsp, 0x368
018ABA2E 5e                             pop rsi
018ABA2F 5d                             pop rbp
018ABA30 c3                             ret 
018ABA31 cc                             int3 
018ABA32 cc                             int3 
018ABA33 cc                             int3 
018ABA34 cc                             int3 
018ABA35 cc                             int3 
018ABA36 cc                             int3 
018ABA37 cc                             int3 
018ABA38 cc                             int3 
018ABA39 cc                             int3 
018ABA3A cc                             int3 
018ABA3B cc                             int3 
018ABA3C cc                             int3 
018ABA3D cc                             int3 
018ABA3E cc                             int3 
018ABA3F cc                             int3 