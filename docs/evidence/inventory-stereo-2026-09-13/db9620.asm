00DB9620 488bc4                         mov rax, rsp
00DB9623 55                             push rbp
00DB9624 53                             push rbx
00DB9625 56                             push rsi
00DB9626 57                             push rdi
00DB9627 488da848ffffff                 lea rbp, [rax - 0xb8]
00DB962E 4881ec98010000                 sub rsp, 0x198
00DB9635 80b97a02000000                 cmp byte ptr [rcx + 0x27a], 0
00DB963C 498bf8                         mov rdi, r8
00DB963F 0f2970c8                       movaps xmmword ptr [rax - 0x38], xmm6
00DB9643 488bf2                         mov rsi, rdx
00DB9646 0f2978b8                       movaps xmmword ptr [rax - 0x48], xmm7
00DB964A 488bd9                         mov rbx, rcx
00DB964D 440f2940a8                     movaps xmmword ptr [rax - 0x58], xmm8
00DB9652 440f294898                     movaps xmmword ptr [rax - 0x68], xmm9
00DB9657 440f295088                     movaps xmmword ptr [rax - 0x78], xmm10
00DB965C 440f299878ffffff               movaps xmmword ptr [rax - 0x88], xmm11
00DB9664 440f29a068ffffff               movaps xmmword ptr [rax - 0x98], xmm12
00DB966C 440f29a858ffffff               movaps xmmword ptr [rax - 0xa8], xmm13
00DB9674 440f29b048ffffff               movaps xmmword ptr [rax - 0xb8], xmm14
00DB967C 440f29b838ffffff               movaps xmmword ptr [rax - 0xc8], xmm15
00DB9684 0f8487040000                   je 0x180db9b11
00DB968A 488b81f0010000                 mov rax, qword ptr [rcx + 0x1f0]
00DB9691 4c8d81f8010000                 lea r8, [rcx + 0x1f8]
00DB9698 0f108138020000                 movups xmm0, xmmword ptr [rcx + 0x238]
00DB969F 488d542470                     lea rdx, [rsp + 0x70]
00DB96A4 0f108948020000                 movups xmm1, xmmword ptr [rcx + 0x248]
00DB96AB 0f2945b0                       movaps xmmword ptr [rbp - 0x50], xmm0
00DB96AF 0f108158020000                 movups xmm0, xmmword ptr [rcx + 0x258]
00DB96B6 0f294dc0                       movaps xmmword ptr [rbp - 0x40], xmm1
00DB96BA 0f108968020000                 movups xmm1, xmmword ptr [rcx + 0x268]
00DB96C1 488d4c2430                     lea rcx, [rsp + 0x30]
00DB96C6 0f2945d0                       movaps xmmword ptr [rbp - 0x30], xmm0
00DB96CA 0f294de0                       movaps xmmword ptr [rbp - 0x20], xmm1
00DB96CE c745d000000000                 mov dword ptr [rbp - 0x30], 0
00DB96D5 c745e000000000                 mov dword ptr [rbp - 0x20], 0
00DB96DC 0f1000                         movups xmm0, xmmword ptr [rax]
00DB96DF 0f29442430                     movaps xmmword ptr [rsp + 0x30], xmm0
00DB96E4 0f104810                       movups xmm1, xmmword ptr [rax + 0x10]
00DB96E8 0f294c2440                     movaps xmmword ptr [rsp + 0x40], xmm1
00DB96ED 0f104020                       movups xmm0, xmmword ptr [rax + 0x20]
00DB96F1 0f29442450                     movaps xmmword ptr [rsp + 0x50], xmm0
00DB96F6 0f104830                       movups xmm1, xmmword ptr [rax + 0x30]
00DB96FA 0f294c2460                     movaps xmmword ptr [rsp + 0x60], xmm1
00DB96FF 0f1000                         movups xmm0, xmmword ptr [rax]
00DB9702 0f104810                       movups xmm1, xmmword ptr [rax + 0x10]
00DB9706 0f29442470                     movaps xmmword ptr [rsp + 0x70], xmm0
00DB970B 0f104020                       movups xmm0, xmmword ptr [rax + 0x20]
00DB970F 0f294d80                       movaps xmmword ptr [rbp - 0x80], xmm1
00DB9713 0f104830                       movups xmm1, xmmword ptr [rax + 0x30]
00DB9717 0f294590                       movaps xmmword ptr [rbp - 0x70], xmm0
00DB971B 0f294da0                       movaps xmmword ptr [rbp - 0x60], xmm1
00DB971F e86cb8a700                     call 0x181834f90
00DB9724 0f28442430                     movaps xmm0, xmmword ptr [rsp + 0x30]
00DB9729 4c8d45b0                       lea r8, [rbp - 0x50]
00DB972D 0f284c2440                     movaps xmm1, xmmword ptr [rsp + 0x40]
00DB9732 488d542470                     lea rdx, [rsp + 0x70]
00DB9737 0f29442470                     movaps xmmword ptr [rsp + 0x70], xmm0
00DB973C 488d4c2430                     lea rcx, [rsp + 0x30]
00DB9741 0f28442450                     movaps xmm0, xmmword ptr [rsp + 0x50]
00DB9746 0f294d80                       movaps xmmword ptr [rbp - 0x80], xmm1
00DB974A 0f284c2460                     movaps xmm1, xmmword ptr [rsp + 0x60]
00DB974F 0f294590                       movaps xmmword ptr [rbp - 0x70], xmm0
00DB9753 0f294da0                       movaps xmmword ptr [rbp - 0x60], xmm1
00DB9757 e834b8a700                     call 0x181834f90
00DB975C f30f105c2434                   movss xmm3, dword ptr [rsp + 0x34]
00DB9762 f3440f10742430                 movss xmm14, dword ptr [rsp + 0x30]
00DB9769 0f28c3                         movaps xmm0, xmm3
00DB976C f30f1054243c                   movss xmm2, dword ptr [rsp + 0x3c]
00DB9772 410f28e6                       movaps xmm4, xmm14
00DB9776 f3440f10ab9c000000             movss xmm13, dword ptr [rbx + 0x9c]
00DB977F 0f28ca                         movaps xmm1, xmm2
00DB9782 f30f598ba4000000               mulss xmm1, dword ptr [rbx + 0xa4]
00DB978A 410f28ee                       movaps xmm5, xmm14
00DB978E f3440f109b98000000             movss xmm11, dword ptr [rbx + 0x98]
00DB9797 f3440f1093ac000000             movss xmm10, dword ptr [rbx + 0xac]
00DB97A0 f30f10bba8000000               movss xmm7, dword ptr [rbx + 0xa8]
00DB97A8 f3440f10a3b4000000             movss xmm12, dword ptr [rbx + 0xb4]
00DB97B1 f3440f1083bc000000             movss xmm8, dword ptr [rbx + 0xbc]
00DB97BA f3440f108bc4000000             movss xmm9, dword ptr [rbx + 0xc4]
00DB97C3 f3410f59e3                     mulss xmm4, xmm11
00DB97C8 f3410f59c5                     mulss xmm0, xmm13
00DB97CD f30f58e0                       addss xmm4, xmm0
00DB97D1 0f28c3                         movaps xmm0, xmm3
00DB97D4 f3410f59c2                     mulss xmm0, xmm10
00DB97D9 f30f58e1                       addss xmm4, xmm1
00DB97DD 0f28ca                         movaps xmm1, xmm2
00DB97E0 f3410f59cc                     mulss xmm1, xmm12
00DB97E5 f30f11a5c0000000               movss dword ptr [rbp + 0xc0], xmm4
00DB97ED 410f28e6                       movaps xmm4, xmm14
00DB97F1 f30f59e7                       mulss xmm4, xmm7
00DB97F5 f30f58e0                       addss xmm4, xmm0
00DB97F9 0f28c3                         movaps xmm0, xmm3
00DB97FC f3410f59c0                     mulss xmm0, xmm8
00DB9801 f30f58e1                       addss xmm4, xmm1
00DB9805 0f28ca                         movaps xmm1, xmm2
00DB9808 f3410f59c9                     mulss xmm1, xmm9
00DB980D f30f11a5c8000000               movss dword ptr [rbp + 0xc8], xmm4
00DB9815 f30f10a3b8000000               movss xmm4, dword ptr [rbx + 0xb8]
00DB981D f30f59ec                       mulss xmm5, xmm4
00DB9821 f30f58e8                       addss xmm5, xmm0
00DB9825 f30f58e9                       addss xmm5, xmm1
00DB9829 f30f11add0000000               movss dword ptr [rbp + 0xd0], xmm5
00DB9831 f30f10b3d4000000               movss xmm6, dword ptr [rbx + 0xd4]
00DB9839 f30f10abcc000000               movss xmm5, dword ptr [rbx + 0xcc]
00DB9841 f3440f10bbc8000000             movss xmm15, dword ptr [rbx + 0xc8]
00DB984A f30f59d6                       mulss xmm2, xmm6
00DB984E f3450f59f7                     mulss xmm14, xmm15
00DB9853 f30f59dd                       mulss xmm3, xmm5
00DB9857 f3440f58f3                     addss xmm14, xmm3
00DB985C f30f105c2444                   movss xmm3, dword ptr [rsp + 0x44]
00DB9862 0f28c3                         movaps xmm0, xmm3
00DB9865 f3410f59c5                     mulss xmm0, xmm13
00DB986A f3440f58f2                     addss xmm14, xmm2
00DB986F f30f1054244c                   movss xmm2, dword ptr [rsp + 0x4c]
00DB9875 0f28ca                         movaps xmm1, xmm2
00DB9878 f30f598ba4000000               mulss xmm1, dword ptr [rbx + 0xa4]
00DB9880 f3440f11b5d8000000             movss dword ptr [rbp + 0xd8], xmm14
00DB9889 f3440f10742440                 movss xmm14, dword ptr [rsp + 0x40]
00DB9890 f3450f59f3                     mulss xmm14, xmm11
00DB9895 f3440f105c2440                 movss xmm11, dword ptr [rsp + 0x40]
00DB989C 450f28eb                       movaps xmm13, xmm11
00DB98A0 f3440f58f0                     addss xmm14, xmm0
00DB98A5 f3440f59ef                     mulss xmm13, xmm7
00DB98AA 0f28c3                         movaps xmm0, xmm3
00DB98AD f3410f59c2                     mulss xmm0, xmm10
00DB98B2 f3440f58f1                     addss xmm14, xmm1
00DB98B7 0f28ca                         movaps xmm1, xmm2
00DB98BA f3410f59cc                     mulss xmm1, xmm12
00DB98BF 450f28e3                       movaps xmm12, xmm11
00DB98C3 f3440f58e8                     addss xmm13, xmm0
00DB98C8 f3450f59df                     mulss xmm11, xmm15
00DB98CD 0f28c3                         movaps xmm0, xmm3
00DB98D0 f3440f59e4                     mulss xmm12, xmm4
00DB98D5 f3410f59c0                     mulss xmm0, xmm8
00DB98DA f3440f58e9                     addss xmm13, xmm1
00DB98DF f30f59dd                       mulss xmm3, xmm5
00DB98E3 f3440f58e0                     addss xmm12, xmm0
00DB98E8 0f28ca                         movaps xmm1, xmm2
00DB98EB f30f59d6                       mulss xmm2, xmm6
00DB98EF f3410f59c9                     mulss xmm1, xmm9
00DB98F4 f3440f58db                     addss xmm11, xmm3
00DB98F9 f30f105c2454                   movss xmm3, dword ptr [rsp + 0x54]
00DB98FF 0f28c3                         movaps xmm0, xmm3
00DB9902 f30f59839c000000               mulss xmm0, dword ptr [rbx + 0x9c]
00DB990A f3440f58e1                     addss xmm12, xmm1
00DB990F f3440f58da                     addss xmm11, xmm2
00DB9914 f30f1054245c                   movss xmm2, dword ptr [rsp + 0x5c]
00DB991A 0f28ca                         movaps xmm1, xmm2
00DB991D f30f598ba4000000               mulss xmm1, dword ptr [rbx + 0xa4]
00DB9925 f3440f115c2420                 movss dword ptr [rsp + 0x20], xmm11
00DB992C f3440f105c2450                 movss xmm11, dword ptr [rsp + 0x50]
00DB9933 450f28d3                       movaps xmm10, xmm11
00DB9937 450f28cb                       movaps xmm9, xmm11
00DB993B f3440f599398000000             mulss xmm10, dword ptr [rbx + 0x98]
00DB9944 450f28c3                       movaps xmm8, xmm11
00DB9948 f3440f59cf                     mulss xmm9, xmm7
00DB994D f30f107c2460                   movss xmm7, dword ptr [rsp + 0x60]
00DB9953 f3440f58d0                     addss xmm10, xmm0
00DB9958 0f28c3                         movaps xmm0, xmm3
00DB995B f3440f59c4                     mulss xmm8, xmm4
00DB9960 f30f5983ac000000               mulss xmm0, dword ptr [rbx + 0xac]
00DB9968 f30f10642464                   movss xmm4, dword ptr [rsp + 0x64]
00DB996E f3440f58d1                     addss xmm10, xmm1
00DB9973 0f28ca                         movaps xmm1, xmm2
00DB9976 f3450f59df                     mulss xmm11, xmm15
00DB997B f30f598bb4000000               mulss xmm1, dword ptr [rbx + 0xb4]
00DB9983 f3440f58c8                     addss xmm9, xmm0
00DB9988 0f28c3                         movaps xmm0, xmm3
00DB998B f30f59dd                       mulss xmm3, xmm5
00DB998F f30f5983bc000000               mulss xmm0, dword ptr [rbx + 0xbc]
00DB9997 f3440f58c9                     addss xmm9, xmm1
00DB999C 0f28ca                         movaps xmm1, xmm2
00DB999F f30f598bc4000000               mulss xmm1, dword ptr [rbx + 0xc4]
00DB99A7 f3440f58db                     addss xmm11, xmm3
00DB99AC f30f59d6                       mulss xmm2, xmm6
00DB99B0 f3440f58c0                     addss xmm8, xmm0
00DB99B5 0f28f7                         movaps xmm6, xmm7
00DB99B8 0f28c4                         movaps xmm0, xmm4
00DB99BB f30f59b398000000               mulss xmm6, dword ptr [rbx + 0x98]
00DB99C3 f30f59839c000000               mulss xmm0, dword ptr [rbx + 0x9c]
00DB99CB f3440f58da                     addss xmm11, xmm2
00DB99D0 f3440f58c1                     addss xmm8, xmm1
00DB99D5 f30f58f0                       addss xmm6, xmm0
00DB99D9 f30f105c246c                   movss xmm3, dword ptr [rsp + 0x6c]
00DB99DF 0f28ef                         movaps xmm5, xmm7
00DB99E2 f30f59aba8000000               mulss xmm5, dword ptr [rbx + 0xa8]
00DB99EA 0f28d7                         movaps xmm2, xmm7
00DB99ED f30f5993b8000000               mulss xmm2, dword ptr [rbx + 0xb8]
00DB99F5 0f28cb                         movaps xmm1, xmm3
00DB99F8 f30f598ba4000000               mulss xmm1, dword ptr [rbx + 0xa4]
00DB9A00 0f28c4                         movaps xmm0, xmm4
00DB9A03 f30f5983ac000000               mulss xmm0, dword ptr [rbx + 0xac]
00DB9A0B f30f58f1                       addss xmm6, xmm1
00DB9A0F f3410f59ff                     mulss xmm7, xmm15
00DB9A14 f30f58e8                       addss xmm5, xmm0
00DB9A18 0f28cb                         movaps xmm1, xmm3
00DB9A1B f30f598bb4000000               mulss xmm1, dword ptr [rbx + 0xb4]
00DB9A23 0f28c4                         movaps xmm0, xmm4
00DB9A26 f30f5983bc000000               mulss xmm0, dword ptr [rbx + 0xbc]
00DB9A2E f30f59a3cc000000               mulss xmm4, dword ptr [rbx + 0xcc]
00DB9A36 f30f58e9                       addss xmm5, xmm1
00DB9A3A f30f58d0                       addss xmm2, xmm0
00DB9A3E 0f28cb                         movaps xmm1, xmm3
00DB9A41 f30f599bd4000000               mulss xmm3, dword ptr [rbx + 0xd4]
00DB9A49 f30f598bc4000000               mulss xmm1, dword ptr [rbx + 0xc4]
00DB9A51 f30f58fc                       addss xmm7, xmm4
00DB9A55 f30f1085c0000000               movss xmm0, dword ptr [rbp + 0xc0]
00DB9A5D f30f118398010000               movss dword ptr [rbx + 0x198], xmm0
00DB9A65 f30f1085c8000000               movss xmm0, dword ptr [rbp + 0xc8]
00DB9A6D f30f58d1                       addss xmm2, xmm1
00DB9A71 f30f1183a8010000               movss dword ptr [rbx + 0x1a8], xmm0
00DB9A79 f30f58fb                       addss xmm7, xmm3
00DB9A7D f30f1085d0000000               movss xmm0, dword ptr [rbp + 0xd0]
00DB9A85 f30f1183b8010000               movss dword ptr [rbx + 0x1b8], xmm0
00DB9A8D f30f1085d8000000               movss xmm0, dword ptr [rbp + 0xd8]
00DB9A95 f30f1183c8010000               movss dword ptr [rbx + 0x1c8], xmm0
00DB9A9D f30f10442420                   movss xmm0, dword ptr [rsp + 0x20]
00DB9AA3 f30f1183cc010000               movss dword ptr [rbx + 0x1cc], xmm0
00DB9AAB f3440f11b39c010000             movss dword ptr [rbx + 0x19c], xmm14
00DB9AB4 f3440f1193a0010000             movss dword ptr [rbx + 0x1a0], xmm10
00DB9ABD f30f11b3a4010000               movss dword ptr [rbx + 0x1a4], xmm6
00DB9AC5 f3440f11abac010000             movss dword ptr [rbx + 0x1ac], xmm13
00DB9ACE f3440f118bb0010000             movss dword ptr [rbx + 0x1b0], xmm9
00DB9AD7 f30f11abb4010000               movss dword ptr [rbx + 0x1b4], xmm5
00DB9ADF f3440f11a3bc010000             movss dword ptr [rbx + 0x1bc], xmm12
00DB9AE8 f3440f1183c0010000             movss dword ptr [rbx + 0x1c0], xmm8
00DB9AF1 f30f1193c4010000               movss dword ptr [rbx + 0x1c4], xmm2
00DB9AF9 f3440f119bd0010000             movss dword ptr [rbx + 0x1d0], xmm11
00DB9B02 f30f11bbd4010000               movss dword ptr [rbx + 0x1d4], xmm7
00DB9B0A c6837a02000000                 mov byte ptr [rbx + 0x27a], 0
00DB9B11 f30f1016                       movss xmm2, dword ptr [rsi]
00DB9B15 f30f104e0c                     movss xmm1, dword ptr [rsi + 0xc]
00DB9B1A f3440f108b98010000             movss xmm9, dword ptr [rbx + 0x198]
00DB9B23 f30f107604                     movss xmm6, dword ptr [rsi + 4]
00DB9B28 410f28c1                       movaps xmm0, xmm9
00DB9B2C f30f105e10                     movss xmm3, dword ptr [rsi + 0x10]
00DB9B31 f3440f1093a8010000             movss xmm10, dword ptr [rbx + 0x1a8]
00DB9B3A f3440f109bb8010000             movss xmm11, dword ptr [rbx + 0x1b8]
00DB9B43 f3440f10bbcc010000             movss xmm15, dword ptr [rbx + 0x1cc]
00DB9B4C f3440f10a3c8010000             movss xmm12, dword ptr [rbx + 0x1c8]
00DB9B55 450f28ef                       movaps xmm13, xmm15
00DB9B59 f3440f10839c010000             movss xmm8, dword ptr [rbx + 0x19c]
00DB9B62 f30f10bbac010000               movss xmm7, dword ptr [rbx + 0x1ac]
00DB9B6A 410f28e0                       movaps xmm4, xmm8
00DB9B6E f30f10abbc010000               movss xmm5, dword ptr [rbx + 0x1bc]
00DB9B76 f30f59c2                       mulss xmm0, xmm2
00DB9B7A 440f28f5                       movaps xmm14, xmm5
00DB9B7E f30f59e1                       mulss xmm4, xmm1
00DB9B82 f3440f59f1                     mulss xmm14, xmm1
00DB9B87 f30f58e0                       addss xmm4, xmm0
00DB9B8B f3440f59e9                     mulss xmm13, xmm1
00DB9B90 410f28c2                       movaps xmm0, xmm10
00DB9B94 f3440f59c3                     mulss xmm8, xmm3
00DB9B99 f30f59c2                       mulss xmm0, xmm2
00DB9B9D f30f11a5c0000000               movss dword ptr [rbp + 0xc0], xmm4
00DB9BA5 0f28e7                         movaps xmm4, xmm7
00DB9BA8 f30f59e1                       mulss xmm4, xmm1
00DB9BAC f30f108ba0010000               movss xmm1, dword ptr [rbx + 0x1a0]
00DB9BB4 f30f59fb                       mulss xmm7, xmm3
00DB9BB8 f30f58e0                       addss xmm4, xmm0
00DB9BBC f30f59eb                       mulss xmm5, xmm3
00DB9BC0 410f28c3                       movaps xmm0, xmm11
00DB9BC4 f30f114f08                     movss dword ptr [rdi + 8], xmm1
00DB9BC9 f30f59c2                       mulss xmm0, xmm2
00DB9BCD f30f11a5d0000000               movss dword ptr [rbp + 0xd0], xmm4
00DB9BD5 410f28e7                       movaps xmm4, xmm15
00DB9BD9 f30f59e3                       mulss xmm4, xmm3
00DB9BDD f30f109bd0010000               movss xmm3, dword ptr [rbx + 0x1d0]
00DB9BE5 f3440f58f0                     addss xmm14, xmm0
00DB9BEA 410f28c4                       movaps xmm0, xmm12
00DB9BEE f30f59c2                       mulss xmm0, xmm2
00DB9BF2 f30f1093c0010000               movss xmm2, dword ptr [rbx + 0x1c0]
00DB9BFA f3440f58e8                     addss xmm13, xmm0
00DB9BFF 410f28c1                       movaps xmm0, xmm9
00DB9C03 f3440f594e08                   mulss xmm9, dword ptr [rsi + 8]
00DB9C09 f30f59c6                       mulss xmm0, xmm6
00DB9C0D f3440f58c0                     addss xmm8, xmm0
00DB9C12 410f28c2                       movaps xmm0, xmm10
00DB9C16 f3440f595608                   mulss xmm10, dword ptr [rsi + 8]
00DB9C1C f30f59c6                       mulss xmm0, xmm6
00DB9C20 f3440f114704                   movss dword ptr [rdi + 4], xmm8
00DB9C26 f30f58f8                       addss xmm7, xmm0
00DB9C2A 410f28c3                       movaps xmm0, xmm11
00DB9C2E f3440f595e08                   mulss xmm11, dword ptr [rsi + 8]
00DB9C34 f30f59c6                       mulss xmm0, xmm6
00DB9C38 f30f58e8                       addss xmm5, xmm0
00DB9C3C 410f28c4                       movaps xmm0, xmm12
00DB9C40 f3440f596608                   mulss xmm12, dword ptr [rsi + 8]
00DB9C46 f30f59c6                       mulss xmm0, xmm6
00DB9C4A f30f10b39c010000               movss xmm6, dword ptr [rbx + 0x19c]
00DB9C52 f30f597614                     mulss xmm6, dword ptr [rsi + 0x14]
00DB9C57 f30f58e0                       addss xmm4, xmm0
00DB9C5B f30f1083b0010000               movss xmm0, dword ptr [rbx + 0x1b0]
00DB9C63 f3410f58f1                     addss xmm6, xmm9
00DB9C68 f3440f104e14                   movss xmm9, dword ptr [rsi + 0x14]
00DB9C6E f3450f59f9                     mulss xmm15, xmm9
00DB9C73 f30f58b3a4010000               addss xmm6, dword ptr [rbx + 0x1a4]
00DB9C7B f3450f58fc                     addss xmm15, xmm12
00DB9C80 f30f11b5c8000000               movss dword ptr [rbp + 0xc8], xmm6
00DB9C88 f30f10b3ac010000               movss xmm6, dword ptr [rbx + 0x1ac]
00DB9C90 f3440f58bbd4010000             addss xmm15, dword ptr [rbx + 0x1d4]
00DB9C99 f3410f59f1                     mulss xmm6, xmm9
00DB9C9E f3410f58f2                     addss xmm6, xmm10
00DB9CA3 f30f58b3b4010000               addss xmm6, dword ptr [rbx + 0x1b4]
00DB9CAB f30f11b5d8000000               movss dword ptr [rbp + 0xd8], xmm6
00DB9CB3 f30f10b3bc010000               movss xmm6, dword ptr [rbx + 0x1bc]
00DB9CBB f3410f59f1                     mulss xmm6, xmm9
00DB9CC0 f3440f108dc0000000             movss xmm9, dword ptr [rbp + 0xc0]
00DB9CC9 f3440f110f                     movss dword ptr [rdi], xmm9
00DB9CCE f3410f58f3                     addss xmm6, xmm11
00DB9CD3 f30f58b3c4010000               addss xmm6, dword ptr [rbx + 0x1c4]
00DB9CDB f3440f1085c8000000             movss xmm8, dword ptr [rbp + 0xc8]
00DB9CE4 4c8d9c2498010000               lea r11, [rsp + 0x198]
00DB9CEC f30f108dd0000000               movss xmm1, dword ptr [rbp + 0xd0]
00DB9CF4 450f284bb8                     movaps xmm9, xmmword ptr [r11 - 0x48]
00DB9CF9 450f2853a8                     movaps xmm10, xmmword ptr [r11 - 0x58]
00DB9CFE 450f285b98                     movaps xmm11, xmmword ptr [r11 - 0x68]
00DB9D03 450f286388                     movaps xmm12, xmmword ptr [r11 - 0x78]
00DB9D08 f30f117f14                     movss dword ptr [rdi + 0x14], xmm7
00DB9D0D f30f10bdd8000000               movss xmm7, dword ptr [rbp + 0xd8]
00DB9D15 f30f117f1c                     movss dword ptr [rdi + 0x1c], xmm7
00DB9D1A 410f287bd8                     movaps xmm7, xmmword ptr [r11 - 0x28]
00DB9D1F f3440f11470c                   movss dword ptr [rdi + 0xc], xmm8
00DB9D25 450f2843c8                     movaps xmm8, xmmword ptr [r11 - 0x38]
00DB9D2A f3440f117720                   movss dword ptr [rdi + 0x20], xmm14
00DB9D30 450f28b368ffffff               movaps xmm14, xmmword ptr [r11 - 0x98]
00DB9D38 f30f11772c                     movss dword ptr [rdi + 0x2c], xmm6
00DB9D3D 410f2873e8                     movaps xmm6, xmmword ptr [r11 - 0x18]
00DB9D42 f3440f116f30                   movss dword ptr [rdi + 0x30], xmm13
00DB9D48 450f28ab78ffffff               movaps xmm13, xmmword ptr [r11 - 0x88]
00DB9D50 f3440f117f3c                   movss dword ptr [rdi + 0x3c], xmm15
00DB9D56 450f28bb58ffffff               movaps xmm15, xmmword ptr [r11 - 0xa8]
00DB9D5E f30f114f10                     movss dword ptr [rdi + 0x10], xmm1
00DB9D63 f30f114718                     movss dword ptr [rdi + 0x18], xmm0
00DB9D68 f30f116f24                     movss dword ptr [rdi + 0x24], xmm5
00DB9D6D f30f115728                     movss dword ptr [rdi + 0x28], xmm2
00DB9D72 f30f116734                     movss dword ptr [rdi + 0x34], xmm4
00DB9D77 f30f115f38                     movss dword ptr [rdi + 0x38], xmm3
00DB9D7C 498be3                         mov rsp, r11
00DB9D7F 5f                             pop rdi
00DB9D80 5e                             pop rsi
00DB9D81 5b                             pop rbx
00DB9D82 5d                             pop rbp
00DB9D83 c3                             ret 