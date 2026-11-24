/* Auto-generated ARMv7 Thumb-2 GLIFunctionDispatch -> Mesa trampoline table. */
.syntax unified
.thumb
.text
.align 2
.globl _gld_trampoline_idx
.thumb_func _gld_trampoline_idx
_gld_trampoline_idx:
    push    {r1, r2, r3, r4, r5, lr}
    movw    r4, :lower16:(_g_tramp_fast - (10f + 4))
    movt    r4, :upper16:(_g_tramp_fast - (10f + 4))
10: add     r4, pc
    ldr     r5, [r4, #0]
    cbz     r5, 11f
    cbz     r0, 12f
    cmp     r0, r5
    bne     11f
12: mrc     p15, 0, r5, c13, c0, 3
    ldr     r1, [r4, #4]
    cmp     r5, r1
    bne     11f
    ldr     r1, [r4, #8]
    ldr     r4, [r1, r12, lsl #2]
    b       13f
11: mov     r1, r12
    bl      _mali_prepare_call
    mov     r4, r0
13: add     r5, sp, #24
    sub     sp, sp, #24
    ldr     r0, [r5, #4]
    ldr     r1, [r5, #8]
    ldr     r2, [r5, #12]
    ldr     r3, [r5, #16]
    stmia   sp, {r0, r1, r2, r3}
    ldr     r0, [r5, #20]
    ldr     r1, [r5, #24]
    str     r0, [sp, #16]
    str     r1, [sp, #20]
    ldr     r0, [sp, #24]
    ldr     r1, [sp, #28]
    ldr     r2, [sp, #32]
    ldr     r3, [r5, #0]
    blx     r4
    add     sp, sp, #36
    pop     {r4, r5, pc}

.data
.align 2
.globl _g_tramp_fast
_g_tramp_fast:
    .long 0, 0, 0

.text
.align 2
.globl _gld_fast4_idx
.thumb_func _gld_fast4_idx
_gld_fast4_idx:
    push    {r4, r5, r6}
    movw    r4, :lower16:(_g_tramp_fast - (1f + 4))
    movt    r4, :upper16:(_g_tramp_fast - (1f + 4))
1:  add     r4, pc
    ldr     r5, [r4, #0]
    cbz     r5, 2f
    cbz     r0, 3f
    cmp     r0, r5
    bne     2f
3:  mrc     p15, 0, r5, c13, c0, 3
    ldr     r6, [r4, #4]
    cmp     r5, r6
    bne     2f
    ldr     r6, [r4, #8]
    ldr     r12, [r6, r12, lsl #2]
    mov     r0, r1
    mov     r1, r2
    mov     r2, r3
    ldr     r3, [sp, #12]
    pop     {r4, r5, r6}
    bx      r12
2:  pop     {r4, r5, r6}
    b.w     _gld_trampoline_idx

.align 1
.globl _gld_stub_0
.thumb_func _gld_stub_0
_gld_stub_0:
    movw r12, #0
    b.w _gld_trampoline_idx
.align 1
.globl _gld_stub_1
.thumb_func _gld_stub_1
_gld_stub_1:
    movw r12, #1
    b.w _gld_fast4_idx
.align 1
.globl _gld_stub_2
.thumb_func _gld_stub_2
_gld_stub_2:
    movw r12, #2
    b.w _gld_fast4_idx
.align 1
.globl _gld_stub_3
.thumb_func _gld_stub_3
_gld_stub_3:
    movw r12, #3
    b.w _gld_fast4_idx
.align 1
.globl _gld_stub_4
.thumb_func _gld_stub_4
_gld_stub_4:
    movw r12, #4
    b.w _gld_fast4_idx
.align 1
.globl _gld_stub_5
.thumb_func _gld_stub_5
_gld_stub_5:
    movw r12, #5
    b.w _gld_fast4_idx
.align 1
.globl _gld_stub_6
.thumb_func _gld_stub_6
_gld_stub_6:
    movw r12, #6
    b.w _gld_trampoline_idx
.align 1
.globl _gld_stub_7
.thumb_func _gld_stub_7
_gld_stub_7:
    movw r12, #7
    b.w _gld_trampoline_idx
.align 1
.globl _gld_stub_8
.thumb_func _gld_stub_8
_gld_stub_8:
    movw r12, #8
    b.w _gld_fast4_idx
.align 1
.globl _gld_stub_9
.thumb_func _gld_stub_9
_gld_stub_9:
    movw r12, #9
    b.w _gld_trampoline_idx
.align 1
.globl _gld_stub_10
.thumb_func _gld_stub_10
_gld_stub_10:
    movw r12, #10
    b.w _gld_trampoline_idx
.align 1
.globl _gld_stub_11
.thumb_func _gld_stub_11
_gld_stub_11:
    movw r12, #11
    b.w _gld_trampoline_idx
.align 1
.globl _gld_stub_12
.thumb_func _gld_stub_12
_gld_stub_12:
    movw r12, #12
    b.w _gld_fast4_idx
.align 1
.globl _gld_stub_13
.thumb_func _gld_stub_13
_gld_stub_13:
    movw r12, #13
    b.w _gld_trampoline_idx
.align 1
.globl _gld_stub_14
.thumb_func _gld_stub_14
_gld_stub_14:
    movw r12, #14
    b.w _gld_fast4_idx
.align 1
.globl _gld_stub_15
.thumb_func _gld_stub_15
_gld_stub_15:
    movw r12, #15
    b.w _gld_fast4_idx
.align 1
.globl _gld_stub_16
.thumb_func _gld_stub_16
_gld_stub_16:
    movw r12, #16
    b.w _gld_fast4_idx
.align 1
.globl _gld_stub_17
.thumb_func _gld_stub_17
_gld_stub_17:
    movw r12, #17
    b.w _gld_trampoline_idx
.align 1
.globl _gld_stub_18
.thumb_func _gld_stub_18
_gld_stub_18:
    movw r12, #18
    b.w _gld_fast4_idx
.align 1
.globl _gld_stub_19
.thumb_func _gld_stub_19
_gld_stub_19:
    movw r12, #19
    b.w _gld_fast4_idx
.align 1
.globl _gld_stub_20
.thumb_func _gld_stub_20
_gld_stub_20:
    movw r12, #20
    b.w _gld_fast4_idx
.align 1
.globl _gld_stub_21
.thumb_func _gld_stub_21
_gld_stub_21:
    movw r12, #21
    b.w _gld_trampoline_idx
.align 1
.globl _gld_stub_22
.thumb_func _gld_stub_22
_gld_stub_22:
    movw r12, #22
    b.w _gld_fast4_idx
.align 1
.globl _gld_stub_23
.thumb_func _gld_stub_23
_gld_stub_23:
    movw r12, #23
    b.w _gld_fast4_idx
.align 1
.globl _gld_stub_24
.thumb_func _gld_stub_24
_gld_stub_24:
    movw r12, #24
    b.w _gld_trampoline_idx
.align 1
.globl _gld_stub_25
.thumb_func _gld_stub_25
_gld_stub_25:
    movw r12, #25
    b.w _gld_trampoline_idx
.align 1
.globl _gld_stub_26
.thumb_func _gld_stub_26
_gld_stub_26:
    movw r12, #26
    b.w _gld_fast4_idx
.align 1
.globl _gld_stub_27
.thumb_func _gld_stub_27
_gld_stub_27:
    movw r12, #27
    b.w _gld_trampoline_idx
.align 1
.globl _gld_stub_28
.thumb_func _gld_stub_28
_gld_stub_28:
    movw r12, #28
    b.w _gld_trampoline_idx
.align 1
.globl _gld_stub_29
.thumb_func _gld_stub_29
_gld_stub_29:
    movw r12, #29
    b.w _gld_trampoline_idx
.align 1
.globl _gld_stub_30
.thumb_func _gld_stub_30
_gld_stub_30:
    movw r12, #30
    b.w _gld_trampoline_idx
.align 1
.globl _gld_stub_31
.thumb_func _gld_stub_31
_gld_stub_31:
    movw r12, #31
    b.w _gld_trampoline_idx
.align 1
.globl _gld_stub_32
.thumb_func _gld_stub_32
_gld_stub_32:
    movw r12, #32
    b.w _gld_trampoline_idx
.align 1
.globl _gld_stub_33
.thumb_func _gld_stub_33
_gld_stub_33:
    movw r12, #33
    b.w _gld_trampoline_idx
.align 1
.globl _gld_stub_34
.thumb_func _gld_stub_34
_gld_stub_34:
    movw r12, #34
    b.w _gld_trampoline_idx
.align 1
.globl _gld_stub_35
.thumb_func _gld_stub_35
_gld_stub_35:
    movw r12, #35
    b.w _gld_trampoline_idx
.align 1
.globl _gld_stub_36
.thumb_func _gld_stub_36
_gld_stub_36:
    movw r12, #36
    b.w _gld_trampoline_idx
.align 1
.globl _gld_stub_37
.thumb_func _gld_stub_37
_gld_stub_37:
    movw r12, #37
    b.w _gld_trampoline_idx
.align 1
.globl _gld_stub_38
.thumb_func _gld_stub_38
_gld_stub_38:
    movw r12, #38
    b.w _gld_trampoline_idx
.align 1
.globl _gld_stub_39
.thumb_func _gld_stub_39
_gld_stub_39:
    movw r12, #39
    b.w _gld_trampoline_idx
.align 1
.globl _gld_stub_40
.thumb_func _gld_stub_40
_gld_stub_40:
    movw r12, #40
    b.w _gld_trampoline_idx
.align 1
.globl _gld_stub_41
.thumb_func _gld_stub_41
_gld_stub_41:
    movw r12, #41
    b.w _gld_trampoline_idx
.align 1
.globl _gld_stub_42
.thumb_func _gld_stub_42
_gld_stub_42:
    movw r12, #42
    b.w _gld_trampoline_idx
.align 1
.globl _gld_stub_43
.thumb_func _gld_stub_43
_gld_stub_43:
    movw r12, #43
    b.w _gld_trampoline_idx
.align 1
.globl _gld_stub_44
.thumb_func _gld_stub_44
_gld_stub_44:
    movw r12, #44
    b.w _gld_fast4_idx
.align 1
.globl _gld_stub_45
.thumb_func _gld_stub_45
_gld_stub_45:
    movw r12, #45
    b.w _gld_trampoline_idx
.align 1
.globl _gld_stub_46
.thumb_func _gld_stub_46
_gld_stub_46:
    movw r12, #46
    b.w _gld_trampoline_idx
.align 1
.globl _gld_stub_47
.thumb_func _gld_stub_47
_gld_stub_47:
    movw r12, #47
    b.w _gld_trampoline_idx
.align 1
.globl _gld_stub_48
.thumb_func _gld_stub_48
_gld_stub_48:
    movw r12, #48
    b.w _gld_trampoline_idx
.align 1
.globl _gld_stub_49
.thumb_func _gld_stub_49
_gld_stub_49:
    movw r12, #49
    b.w _gld_trampoline_idx
.align 1
.globl _gld_stub_50
.thumb_func _gld_stub_50
_gld_stub_50:
    movw r12, #50
    b.w _gld_trampoline_idx
.align 1
.globl _gld_stub_51
.thumb_func _gld_stub_51
_gld_stub_51:
    movw r12, #51
    b.w _gld_trampoline_idx
.align 1
.globl _gld_stub_52
.thumb_func _gld_stub_52
_gld_stub_52:
    movw r12, #52
    b.w _gld_trampoline_idx
.align 1
.globl _gld_stub_53
.thumb_func _gld_stub_53
_gld_stub_53:
    movw r12, #53
    b.w _gld_trampoline_idx
.align 1
.globl _gld_stub_54
.thumb_func _gld_stub_54
_gld_stub_54:
    movw r12, #54
    b.w _gld_trampoline_idx
.align 1
.globl _gld_stub_55
.thumb_func _gld_stub_55
_gld_stub_55:
    movw r12, #55
    b.w _gld_trampoline_idx
.align 1
.globl _gld_stub_56
.thumb_func _gld_stub_56
_gld_stub_56:
    movw r12, #56
    b.w _gld_trampoline_idx
.align 1
.globl _gld_stub_57
.thumb_func _gld_stub_57
_gld_stub_57:
    movw r12, #57
    b.w _gld_trampoline_idx
.align 1
.globl _gld_stub_58
.thumb_func _gld_stub_58
_gld_stub_58:
    movw r12, #58
    b.w _gld_trampoline_idx
.align 1
.globl _gld_stub_59
.thumb_func _gld_stub_59
_gld_stub_59:
    movw r12, #59
    b.w _gld_trampoline_idx
.align 1
.globl _gld_stub_60
.thumb_func _gld_stub_60
_gld_stub_60:
    movw r12, #60
    b.w _gld_fast4_idx
.align 1
.globl _gld_stub_61
.thumb_func _gld_stub_61
_gld_stub_61:
    movw r12, #61
    b.w _gld_trampoline_idx
.align 1
.globl _gld_stub_62
.thumb_func _gld_stub_62
_gld_stub_62:
    movw r12, #62
    b.w _gld_trampoline_idx
.align 1
.globl _gld_stub_63
.thumb_func _gld_stub_63
_gld_stub_63:
    movw r12, #63
    b.w _gld_trampoline_idx
.align 1
.globl _gld_stub_64
.thumb_func _gld_stub_64
_gld_stub_64:
    movw r12, #64
    b.w _gld_trampoline_idx
.align 1
.globl _gld_stub_65
.thumb_func _gld_stub_65
_gld_stub_65:
    movw r12, #65
    b.w _gld_trampoline_idx
.align 1
.globl _gld_stub_66
.thumb_func _gld_stub_66
_gld_stub_66:
    movw r12, #66
    b.w _gld_trampoline_idx
.align 1
.globl _gld_stub_67
.thumb_func _gld_stub_67
_gld_stub_67:
    movw r12, #67
    b.w _gld_trampoline_idx
.align 1
.globl _gld_stub_68
.thumb_func _gld_stub_68
_gld_stub_68:
    movw r12, #68
    b.w _gld_trampoline_idx
.align 1
.globl _gld_stub_69
.thumb_func _gld_stub_69
_gld_stub_69:
    movw r12, #69
    b.w _gld_fast4_idx
.align 1
.globl _gld_stub_70
.thumb_func _gld_stub_70
_gld_stub_70:
    movw r12, #70
    b.w _gld_trampoline_idx
.align 1
.globl _gld_stub_71
.thumb_func _gld_stub_71
_gld_stub_71:
    movw r12, #71
    b.w _gld_fast4_idx
.align 1
.globl _gld_stub_72
.thumb_func _gld_stub_72
_gld_stub_72:
    movw r12, #72
    b.w _gld_fast4_idx
.align 1
.globl _gld_stub_73
.thumb_func _gld_stub_73
_gld_stub_73:
    movw r12, #73
    b.w _gld_fast4_idx
.align 1
.globl _gld_stub_74
.thumb_func _gld_stub_74
_gld_stub_74:
    movw r12, #74
    b.w _gld_trampoline_idx
.align 1
.globl _gld_stub_75
.thumb_func _gld_stub_75
_gld_stub_75:
    movw r12, #75
    b.w _gld_trampoline_idx
.align 1
.globl _gld_stub_76
.thumb_func _gld_stub_76
_gld_stub_76:
    movw r12, #76
    b.w _gld_trampoline_idx
.align 1
.globl _gld_stub_77
.thumb_func _gld_stub_77
_gld_stub_77:
    movw r12, #77
    b.w _gld_trampoline_idx
.align 1
.globl _gld_stub_78
.thumb_func _gld_stub_78
_gld_stub_78:
    movw r12, #78
    b.w _gld_trampoline_idx
.align 1
.globl _gld_stub_79
.thumb_func _gld_stub_79
_gld_stub_79:
    movw r12, #79
    b.w _gld_trampoline_idx
.align 1
.globl _gld_stub_80
.thumb_func _gld_stub_80
_gld_stub_80:
    movw r12, #80
    b.w _gld_fast4_idx
.align 1
.globl _gld_stub_81
.thumb_func _gld_stub_81
_gld_stub_81:
    movw r12, #81
    b.w _gld_fast4_idx
.align 1
.globl _gld_stub_82
.thumb_func _gld_stub_82
_gld_stub_82:
    movw r12, #82
    b.w _gld_fast4_idx
.align 1
.globl _gld_stub_83
.thumb_func _gld_stub_83
_gld_stub_83:
    movw r12, #83
    b.w _gld_fast4_idx
.align 1
.globl _gld_stub_84
.thumb_func _gld_stub_84
_gld_stub_84:
    movw r12, #84
    b.w _gld_trampoline_idx
.align 1
.globl _gld_stub_85
.thumb_func _gld_stub_85
_gld_stub_85:
    movw r12, #85
    b.w _gld_trampoline_idx
.align 1
.globl _gld_stub_86
.thumb_func _gld_stub_86
_gld_stub_86:
    movw r12, #86
    b.w _gld_trampoline_idx
.align 1
.globl _gld_stub_87
.thumb_func _gld_stub_87
_gld_stub_87:
    movw r12, #87
    b.w _gld_fast4_idx
.align 1
.globl _gld_stub_88
.thumb_func _gld_stub_88
_gld_stub_88:
    movw r12, #88
    b.w _gld_fast4_idx
.align 1
.globl _gld_stub_89
.thumb_func _gld_stub_89
_gld_stub_89:
    movw r12, #89
    b.w _gld_fast4_idx
.align 1
.globl _gld_stub_90
.thumb_func _gld_stub_90
_gld_stub_90:
    movw r12, #90
    b.w _gld_fast4_idx
.align 1
.globl _gld_stub_91
.thumb_func _gld_stub_91
_gld_stub_91:
    movw r12, #91
    b.w _gld_trampoline_idx
.align 1
.globl _gld_stub_92
.thumb_func _gld_stub_92
_gld_stub_92:
    movw r12, #92
    b.w _gld_fast4_idx
.align 1
.globl _gld_stub_93
.thumb_func _gld_stub_93
_gld_stub_93:
    movw r12, #93
    b.w _gld_trampoline_idx
.align 1
.globl _gld_stub_94
.thumb_func _gld_stub_94
_gld_stub_94:
    movw r12, #94
    b.w _gld_trampoline_idx
.align 1
.globl _gld_stub_95
.thumb_func _gld_stub_95
_gld_stub_95:
    movw r12, #95
    b.w _gld_trampoline_idx
.align 1
.globl _gld_stub_96
.thumb_func _gld_stub_96
_gld_stub_96:
    movw r12, #96
    b.w _gld_trampoline_idx
.align 1
.globl _gld_stub_97
.thumb_func _gld_stub_97
_gld_stub_97:
    movw r12, #97
    b.w _gld_trampoline_idx
.align 1
.globl _gld_stub_98
.thumb_func _gld_stub_98
_gld_stub_98:
    movw r12, #98
    b.w _gld_trampoline_idx
.align 1
.globl _gld_stub_99
.thumb_func _gld_stub_99
_gld_stub_99:
    movw r12, #99
    b.w _gld_trampoline_idx
.align 1
.globl _gld_stub_100
.thumb_func _gld_stub_100
_gld_stub_100:
    movw r12, #100
    b.w _gld_trampoline_idx
.align 1
.globl _gld_stub_101
.thumb_func _gld_stub_101
_gld_stub_101:
    movw r12, #101
    b.w _gld_trampoline_idx
.align 1
.globl _gld_stub_102
.thumb_func _gld_stub_102
_gld_stub_102:
    movw r12, #102
    b.w _gld_trampoline_idx
.align 1
.globl _gld_stub_103
.thumb_func _gld_stub_103
_gld_stub_103:
    movw r12, #103
    b.w _gld_trampoline_idx
.align 1
.globl _gld_stub_104
.thumb_func _gld_stub_104
_gld_stub_104:
    movw r12, #104
    b.w _gld_trampoline_idx
.align 1
.globl _gld_stub_105
.thumb_func _gld_stub_105
_gld_stub_105:
    movw r12, #105
    b.w _gld_trampoline_idx
.align 1
.globl _gld_stub_106
.thumb_func _gld_stub_106
_gld_stub_106:
    movw r12, #106
    b.w _gld_trampoline_idx
.align 1
.globl _gld_stub_107
.thumb_func _gld_stub_107
_gld_stub_107:
    movw r12, #107
    b.w _gld_fast4_idx
.align 1
.globl _gld_stub_108
.thumb_func _gld_stub_108
_gld_stub_108:
    movw r12, #108
    b.w _gld_trampoline_idx
.align 1
.globl _gld_stub_109
.thumb_func _gld_stub_109
_gld_stub_109:
    movw r12, #109
    b.w _gld_trampoline_idx
.align 1
.globl _gld_stub_110
.thumb_func _gld_stub_110
_gld_stub_110:
    movw r12, #110
    b.w _gld_trampoline_idx
.align 1
.globl _gld_stub_111
.thumb_func _gld_stub_111
_gld_stub_111:
    movw r12, #111
    b.w _gld_trampoline_idx
.align 1
.globl _gld_stub_112
.thumb_func _gld_stub_112
_gld_stub_112:
    movw r12, #112
    b.w _gld_trampoline_idx
.align 1
.globl _gld_stub_113
.thumb_func _gld_stub_113
_gld_stub_113:
    movw r12, #113
    b.w _gld_trampoline_idx
.align 1
.globl _gld_stub_114
.thumb_func _gld_stub_114
_gld_stub_114:
    movw r12, #114
    b.w _gld_fast4_idx
.align 1
.globl _gld_stub_115
.thumb_func _gld_stub_115
_gld_stub_115:
    movw r12, #115
    b.w _gld_trampoline_idx
.align 1
.globl _gld_stub_116
.thumb_func _gld_stub_116
_gld_stub_116:
    movw r12, #116
    b.w _gld_fast4_idx
.align 1
.globl _gld_stub_117
.thumb_func _gld_stub_117
_gld_stub_117:
    movw r12, #117
    b.w _gld_fast4_idx
.align 1
.globl _gld_stub_118
.thumb_func _gld_stub_118
_gld_stub_118:
    movw r12, #118
    b.w _gld_fast4_idx
.align 1
.globl _gld_stub_119
.thumb_func _gld_stub_119
_gld_stub_119:
    movw r12, #119
    b.w _gld_fast4_idx
.align 1
.globl _gld_stub_120
.thumb_func _gld_stub_120
_gld_stub_120:
    movw r12, #120
    b.w _gld_trampoline_idx
.align 1
.globl _gld_stub_121
.thumb_func _gld_stub_121
_gld_stub_121:
    movw r12, #121
    b.w _gld_fast4_idx
.align 1
.globl _gld_stub_122
.thumb_func _gld_stub_122
_gld_stub_122:
    movw r12, #122
    b.w _gld_fast4_idx
.align 1
.globl _gld_stub_123
.thumb_func _gld_stub_123
_gld_stub_123:
    movw r12, #123
    b.w _gld_fast4_idx
.align 1
.globl _gld_stub_124
.thumb_func _gld_stub_124
_gld_stub_124:
    movw r12, #124
    b.w _gld_fast4_idx
.align 1
.globl _gld_stub_125
.thumb_func _gld_stub_125
_gld_stub_125:
    movw r12, #125
    b.w _gld_trampoline_idx
.align 1
.globl _gld_stub_126
.thumb_func _gld_stub_126
_gld_stub_126:
    movw r12, #126
    b.w _gld_fast4_idx
.align 1
.globl _gld_stub_127
.thumb_func _gld_stub_127
_gld_stub_127:
    movw r12, #127
    b.w _gld_fast4_idx
.align 1
.globl _gld_stub_128
.thumb_func _gld_stub_128
_gld_stub_128:
    movw r12, #128
    b.w _gld_trampoline_idx
.align 1
.globl _gld_stub_129
.thumb_func _gld_stub_129
_gld_stub_129:
    movw r12, #129
    b.w _gld_trampoline_idx
.align 1
.globl _gld_stub_130
.thumb_func _gld_stub_130
_gld_stub_130:
    movw r12, #130
    b.w _gld_trampoline_idx
.align 1
.globl _gld_stub_131
.thumb_func _gld_stub_131
_gld_stub_131:
    movw r12, #131
    b.w _gld_trampoline_idx
.align 1
.globl _gld_stub_132
.thumb_func _gld_stub_132
_gld_stub_132:
    movw r12, #132
    b.w _gld_trampoline_idx
.align 1
.globl _gld_stub_133
.thumb_func _gld_stub_133
_gld_stub_133:
    movw r12, #133
    b.w _gld_fast4_idx
.align 1
.globl _gld_stub_134
.thumb_func _gld_stub_134
_gld_stub_134:
    movw r12, #134
    b.w _gld_trampoline_idx
.align 1
.globl _gld_stub_135
.thumb_func _gld_stub_135
_gld_stub_135:
    movw r12, #135
    b.w _gld_trampoline_idx
.align 1
.globl _gld_stub_136
.thumb_func _gld_stub_136
_gld_stub_136:
    movw r12, #136
    b.w _gld_trampoline_idx
.align 1
.globl _gld_stub_137
.thumb_func _gld_stub_137
_gld_stub_137:
    movw r12, #137
    b.w _gld_trampoline_idx
.align 1
.globl _gld_stub_138
.thumb_func _gld_stub_138
_gld_stub_138:
    movw r12, #138
    b.w _gld_trampoline_idx
.align 1
.globl _gld_stub_139
.thumb_func _gld_stub_139
_gld_stub_139:
    movw r12, #139
    b.w _gld_trampoline_idx
.align 1
.globl _gld_stub_140
.thumb_func _gld_stub_140
_gld_stub_140:
    movw r12, #140
    b.w _gld_trampoline_idx
.align 1
.globl _gld_stub_141
.thumb_func _gld_stub_141
_gld_stub_141:
    movw r12, #141
    b.w _gld_trampoline_idx
.align 1
.globl _gld_stub_142
.thumb_func _gld_stub_142
_gld_stub_142:
    movw r12, #142
    b.w _gld_fast4_idx
.align 1
.globl _gld_stub_143
.thumb_func _gld_stub_143
_gld_stub_143:
    movw r12, #143
    b.w _gld_trampoline_idx
.align 1
.globl _gld_stub_144
.thumb_func _gld_stub_144
_gld_stub_144:
    movw r12, #144
    b.w _gld_fast4_idx
.align 1
.globl _gld_stub_145
.thumb_func _gld_stub_145
_gld_stub_145:
    movw r12, #145
    b.w _gld_fast4_idx
.align 1
.globl _gld_stub_146
.thumb_func _gld_stub_146
_gld_stub_146:
    movw r12, #146
    b.w _gld_fast4_idx
.align 1
.globl _gld_stub_147
.thumb_func _gld_stub_147
_gld_stub_147:
    movw r12, #147
    b.w _gld_trampoline_idx
.align 1
.globl _gld_stub_148
.thumb_func _gld_stub_148
_gld_stub_148:
    movw r12, #148
    b.w _gld_fast4_idx
.align 1
.globl _gld_stub_149
.thumb_func _gld_stub_149
_gld_stub_149:
    movw r12, #149
    b.w _gld_fast4_idx
.align 1
.globl _gld_stub_150
.thumb_func _gld_stub_150
_gld_stub_150:
    movw r12, #150
    b.w _gld_fast4_idx
.align 1
.globl _gld_stub_151
.thumb_func _gld_stub_151
_gld_stub_151:
    movw r12, #151
    b.w _gld_trampoline_idx
.align 1
.globl _gld_stub_152
.thumb_func _gld_stub_152
_gld_stub_152:
    movw r12, #152
    b.w _gld_fast4_idx
.align 1
.globl _gld_stub_153
.thumb_func _gld_stub_153
_gld_stub_153:
    movw r12, #153
    b.w _gld_fast4_idx
.align 1
.globl _gld_stub_154
.thumb_func _gld_stub_154
_gld_stub_154:
    movw r12, #154
    b.w _gld_fast4_idx
.align 1
.globl _gld_stub_155
.thumb_func _gld_stub_155
_gld_stub_155:
    movw r12, #155
    b.w _gld_fast4_idx
.align 1
.globl _gld_stub_156
.thumb_func _gld_stub_156
_gld_stub_156:
    movw r12, #156
    b.w _gld_fast4_idx
.align 1
.globl _gld_stub_157
.thumb_func _gld_stub_157
_gld_stub_157:
    movw r12, #157
    b.w _gld_fast4_idx
.align 1
.globl _gld_stub_158
.thumb_func _gld_stub_158
_gld_stub_158:
    movw r12, #158
    b.w _gld_fast4_idx
.align 1
.globl _gld_stub_159
.thumb_func _gld_stub_159
_gld_stub_159:
    movw r12, #159
    b.w _gld_fast4_idx
.align 1
.globl _gld_stub_160
.thumb_func _gld_stub_160
_gld_stub_160:
    movw r12, #160
    b.w _gld_fast4_idx
.align 1
.globl _gld_stub_161
.thumb_func _gld_stub_161
_gld_stub_161:
    movw r12, #161
    b.w _gld_fast4_idx
.align 1
.globl _gld_stub_162
.thumb_func _gld_stub_162
_gld_stub_162:
    movw r12, #162
    b.w _gld_fast4_idx
.align 1
.globl _gld_stub_163
.thumb_func _gld_stub_163
_gld_stub_163:
    movw r12, #163
    b.w _gld_fast4_idx
.align 1
.globl _gld_stub_164
.thumb_func _gld_stub_164
_gld_stub_164:
    movw r12, #164
    b.w _gld_trampoline_idx
.align 1
.globl _gld_stub_165
.thumb_func _gld_stub_165
_gld_stub_165:
    movw r12, #165
    b.w _gld_trampoline_idx
.align 1
.globl _gld_stub_166
.thumb_func _gld_stub_166
_gld_stub_166:
    movw r12, #166
    b.w _gld_trampoline_idx
.align 1
.globl _gld_stub_167
.thumb_func _gld_stub_167
_gld_stub_167:
    movw r12, #167
    b.w _gld_trampoline_idx
.align 1
.globl _gld_stub_168
.thumb_func _gld_stub_168
_gld_stub_168:
    movw r12, #168
    b.w _gld_trampoline_idx
.align 1
.globl _gld_stub_169
.thumb_func _gld_stub_169
_gld_stub_169:
    movw r12, #169
    b.w _gld_trampoline_idx
.align 1
.globl _gld_stub_170
.thumb_func _gld_stub_170
_gld_stub_170:
    movw r12, #170
    b.w _gld_trampoline_idx
.align 1
.globl _gld_stub_171
.thumb_func _gld_stub_171
_gld_stub_171:
    movw r12, #171
    b.w _gld_trampoline_idx
.align 1
.globl _gld_stub_172
.thumb_func _gld_stub_172
_gld_stub_172:
    movw r12, #172
    b.w _gld_trampoline_idx
.align 1
.globl _gld_stub_173
.thumb_func _gld_stub_173
_gld_stub_173:
    movw r12, #173
    b.w _gld_trampoline_idx
.align 1
.globl _gld_stub_174
.thumb_func _gld_stub_174
_gld_stub_174:
    movw r12, #174
    b.w _gld_trampoline_idx
.align 1
.globl _gld_stub_175
.thumb_func _gld_stub_175
_gld_stub_175:
    movw r12, #175
    b.w _gld_trampoline_idx
.align 1
.globl _gld_stub_176
.thumb_func _gld_stub_176
_gld_stub_176:
    movw r12, #176
    b.w _gld_trampoline_idx
.align 1
.globl _gld_stub_177
.thumb_func _gld_stub_177
_gld_stub_177:
    movw r12, #177
    b.w _gld_trampoline_idx
.align 1
.globl _gld_stub_178
.thumb_func _gld_stub_178
_gld_stub_178:
    movw r12, #178
    b.w _gld_trampoline_idx
.align 1
.globl _gld_stub_179
.thumb_func _gld_stub_179
_gld_stub_179:
    movw r12, #179
    b.w _gld_fast4_idx
.align 1
.globl _gld_stub_180
.thumb_func _gld_stub_180
_gld_stub_180:
    movw r12, #180
    b.w _gld_fast4_idx
.align 1
.globl _gld_stub_181
.thumb_func _gld_stub_181
_gld_stub_181:
    movw r12, #181
    b.w _gld_fast4_idx
.align 1
.globl _gld_stub_182
.thumb_func _gld_stub_182
_gld_stub_182:
    movw r12, #182
    b.w _gld_trampoline_idx
.align 1
.globl _gld_stub_183
.thumb_func _gld_stub_183
_gld_stub_183:
    movw r12, #183
    b.w _gld_fast4_idx
.align 1
.globl _gld_stub_184
.thumb_func _gld_stub_184
_gld_stub_184:
    movw r12, #184
    b.w _gld_fast4_idx
.align 1
.globl _gld_stub_185
.thumb_func _gld_stub_185
_gld_stub_185:
    movw r12, #185
    b.w _gld_fast4_idx
.align 1
.globl _gld_stub_186
.thumb_func _gld_stub_186
_gld_stub_186:
    movw r12, #186
    b.w _gld_fast4_idx
.align 1
.globl _gld_stub_187
.thumb_func _gld_stub_187
_gld_stub_187:
    movw r12, #187
    b.w _gld_trampoline_idx
.align 1
.globl _gld_stub_188
.thumb_func _gld_stub_188
_gld_stub_188:
    movw r12, #188
    b.w _gld_trampoline_idx
.align 1
.globl _gld_stub_189
.thumb_func _gld_stub_189
_gld_stub_189:
    movw r12, #189
    b.w _gld_trampoline_idx
.align 1
.globl _gld_stub_190
.thumb_func _gld_stub_190
_gld_stub_190:
    movw r12, #190
    b.w _gld_trampoline_idx
.align 1
.globl _gld_stub_191
.thumb_func _gld_stub_191
_gld_stub_191:
    movw r12, #191
    b.w _gld_trampoline_idx
.align 1
.globl _gld_stub_192
.thumb_func _gld_stub_192
_gld_stub_192:
    movw r12, #192
    b.w _gld_trampoline_idx
.align 1
.globl _gld_stub_193
.thumb_func _gld_stub_193
_gld_stub_193:
    movw r12, #193
    b.w _gld_trampoline_idx
.align 1
.globl _gld_stub_194
.thumb_func _gld_stub_194
_gld_stub_194:
    movw r12, #194
    b.w _gld_trampoline_idx
.align 1
.globl _gld_stub_195
.thumb_func _gld_stub_195
_gld_stub_195:
    movw r12, #195
    b.w _gld_trampoline_idx
.align 1
.globl _gld_stub_196
.thumb_func _gld_stub_196
_gld_stub_196:
    movw r12, #196
    b.w _gld_fast4_idx
.align 1
.globl _gld_stub_197
.thumb_func _gld_stub_197
_gld_stub_197:
    movw r12, #197
    b.w _gld_fast4_idx
.align 1
.globl _gld_stub_198
.thumb_func _gld_stub_198
_gld_stub_198:
    movw r12, #198
    b.w _gld_trampoline_idx
.align 1
.globl _gld_stub_199
.thumb_func _gld_stub_199
_gld_stub_199:
    movw r12, #199
    b.w _gld_fast4_idx
.align 1
.globl _gld_stub_200
.thumb_func _gld_stub_200
_gld_stub_200:
    movw r12, #200
    b.w _gld_trampoline_idx
.align 1
.globl _gld_stub_201
.thumb_func _gld_stub_201
_gld_stub_201:
    movw r12, #201
    b.w _gld_trampoline_idx
.align 1
.globl _gld_stub_202
.thumb_func _gld_stub_202
_gld_stub_202:
    movw r12, #202
    b.w _gld_trampoline_idx
.align 1
.globl _gld_stub_203
.thumb_func _gld_stub_203
_gld_stub_203:
    movw r12, #203
    b.w _gld_trampoline_idx
.align 1
.globl _gld_stub_204
.thumb_func _gld_stub_204
_gld_stub_204:
    movw r12, #204
    b.w _gld_trampoline_idx
.align 1
.globl _gld_stub_205
.thumb_func _gld_stub_205
_gld_stub_205:
    movw r12, #205
    b.w _gld_fast4_idx
.align 1
.globl _gld_stub_206
.thumb_func _gld_stub_206
_gld_stub_206:
    movw r12, #206
    b.w _gld_trampoline_idx
.align 1
.globl _gld_stub_207
.thumb_func _gld_stub_207
_gld_stub_207:
    movw r12, #207
    b.w _gld_trampoline_idx
.align 1
.globl _gld_stub_208
.thumb_func _gld_stub_208
_gld_stub_208:
    movw r12, #208
    b.w _gld_trampoline_idx
.align 1
.globl _gld_stub_209
.thumb_func _gld_stub_209
_gld_stub_209:
    movw r12, #209
    b.w _gld_trampoline_idx
.align 1
.globl _gld_stub_210
.thumb_func _gld_stub_210
_gld_stub_210:
    movw r12, #210
    b.w _gld_trampoline_idx
.align 1
.globl _gld_stub_211
.thumb_func _gld_stub_211
_gld_stub_211:
    movw r12, #211
    b.w _gld_trampoline_idx
.align 1
.globl _gld_stub_212
.thumb_func _gld_stub_212
_gld_stub_212:
    movw r12, #212
    b.w _gld_fast4_idx
.align 1
.globl _gld_stub_213
.thumb_func _gld_stub_213
_gld_stub_213:
    movw r12, #213
    b.w _gld_trampoline_idx
.align 1
.globl _gld_stub_214
.thumb_func _gld_stub_214
_gld_stub_214:
    movw r12, #214
    b.w _gld_trampoline_idx
.align 1
.globl _gld_stub_215
.thumb_func _gld_stub_215
_gld_stub_215:
    movw r12, #215
    b.w _gld_trampoline_idx
.align 1
.globl _gld_stub_216
.thumb_func _gld_stub_216
_gld_stub_216:
    movw r12, #216
    b.w _gld_trampoline_idx
.align 1
.globl _gld_stub_217
.thumb_func _gld_stub_217
_gld_stub_217:
    movw r12, #217
    b.w _gld_trampoline_idx
.align 1
.globl _gld_stub_218
.thumb_func _gld_stub_218
_gld_stub_218:
    movw r12, #218
    b.w _gld_trampoline_idx
.align 1
.globl _gld_stub_219
.thumb_func _gld_stub_219
_gld_stub_219:
    movw r12, #219
    b.w _gld_trampoline_idx
.align 1
.globl _gld_stub_220
.thumb_func _gld_stub_220
_gld_stub_220:
    movw r12, #220
    b.w _gld_trampoline_idx
.align 1
.globl _gld_stub_221
.thumb_func _gld_stub_221
_gld_stub_221:
    movw r12, #221
    b.w _gld_trampoline_idx
.align 1
.globl _gld_stub_222
.thumb_func _gld_stub_222
_gld_stub_222:
    movw r12, #222
    b.w _gld_trampoline_idx
.align 1
.globl _gld_stub_223
.thumb_func _gld_stub_223
_gld_stub_223:
    movw r12, #223
    b.w _gld_trampoline_idx
.align 1
.globl _gld_stub_224
.thumb_func _gld_stub_224
_gld_stub_224:
    movw r12, #224
    b.w _gld_trampoline_idx
.align 1
.globl _gld_stub_225
.thumb_func _gld_stub_225
_gld_stub_225:
    movw r12, #225
    b.w _gld_trampoline_idx
.align 1
.globl _gld_stub_226
.thumb_func _gld_stub_226
_gld_stub_226:
    movw r12, #226
    b.w _gld_trampoline_idx
.align 1
.globl _gld_stub_227
.thumb_func _gld_stub_227
_gld_stub_227:
    movw r12, #227
    b.w _gld_trampoline_idx
.align 1
.globl _gld_stub_228
.thumb_func _gld_stub_228
_gld_stub_228:
    movw r12, #228
    b.w _gld_trampoline_idx
.align 1
.globl _gld_stub_229
.thumb_func _gld_stub_229
_gld_stub_229:
    movw r12, #229
    b.w _gld_trampoline_idx
.align 1
.globl _gld_stub_230
.thumb_func _gld_stub_230
_gld_stub_230:
    movw r12, #230
    b.w _gld_trampoline_idx
.align 1
.globl _gld_stub_231
.thumb_func _gld_stub_231
_gld_stub_231:
    movw r12, #231
    b.w _gld_trampoline_idx
.align 1
.globl _gld_stub_232
.thumb_func _gld_stub_232
_gld_stub_232:
    movw r12, #232
    b.w _gld_trampoline_idx
.align 1
.globl _gld_stub_233
.thumb_func _gld_stub_233
_gld_stub_233:
    movw r12, #233
    b.w _gld_trampoline_idx
.align 1
.globl _gld_stub_234
.thumb_func _gld_stub_234
_gld_stub_234:
    movw r12, #234
    b.w _gld_trampoline_idx
.align 1
.globl _gld_stub_235
.thumb_func _gld_stub_235
_gld_stub_235:
    movw r12, #235
    b.w _gld_trampoline_idx
.align 1
.globl _gld_stub_236
.thumb_func _gld_stub_236
_gld_stub_236:
    movw r12, #236
    b.w _gld_trampoline_idx
.align 1
.globl _gld_stub_237
.thumb_func _gld_stub_237
_gld_stub_237:
    movw r12, #237
    b.w _gld_trampoline_idx
.align 1
.globl _gld_stub_238
.thumb_func _gld_stub_238
_gld_stub_238:
    movw r12, #238
    b.w _gld_trampoline_idx
.align 1
.globl _gld_stub_239
.thumb_func _gld_stub_239
_gld_stub_239:
    movw r12, #239
    b.w _gld_trampoline_idx
.align 1
.globl _gld_stub_240
.thumb_func _gld_stub_240
_gld_stub_240:
    movw r12, #240
    b.w _gld_trampoline_idx
.align 1
.globl _gld_stub_241
.thumb_func _gld_stub_241
_gld_stub_241:
    movw r12, #241
    b.w _gld_trampoline_idx
.align 1
.globl _gld_stub_242
.thumb_func _gld_stub_242
_gld_stub_242:
    movw r12, #242
    b.w _gld_trampoline_idx
.align 1
.globl _gld_stub_243
.thumb_func _gld_stub_243
_gld_stub_243:
    movw r12, #243
    b.w _gld_trampoline_idx
.align 1
.globl _gld_stub_244
.thumb_func _gld_stub_244
_gld_stub_244:
    movw r12, #244
    b.w _gld_trampoline_idx
.align 1
.globl _gld_stub_245
.thumb_func _gld_stub_245
_gld_stub_245:
    movw r12, #245
    b.w _gld_trampoline_idx
.align 1
.globl _gld_stub_246
.thumb_func _gld_stub_246
_gld_stub_246:
    movw r12, #246
    b.w _gld_trampoline_idx
.align 1
.globl _gld_stub_247
.thumb_func _gld_stub_247
_gld_stub_247:
    movw r12, #247
    b.w _gld_trampoline_idx
.align 1
.globl _gld_stub_248
.thumb_func _gld_stub_248
_gld_stub_248:
    movw r12, #248
    b.w _gld_trampoline_idx
.align 1
.globl _gld_stub_249
.thumb_func _gld_stub_249
_gld_stub_249:
    movw r12, #249
    b.w _gld_trampoline_idx
.align 1
.globl _gld_stub_250
.thumb_func _gld_stub_250
_gld_stub_250:
    movw r12, #250
    b.w _gld_trampoline_idx
.align 1
.globl _gld_stub_251
.thumb_func _gld_stub_251
_gld_stub_251:
    movw r12, #251
    b.w _gld_trampoline_idx
.align 1
.globl _gld_stub_252
.thumb_func _gld_stub_252
_gld_stub_252:
    movw r12, #252
    b.w _gld_trampoline_idx
.align 1
.globl _gld_stub_253
.thumb_func _gld_stub_253
_gld_stub_253:
    movw r12, #253
    b.w _gld_trampoline_idx
.align 1
.globl _gld_stub_254
.thumb_func _gld_stub_254
_gld_stub_254:
    movw r12, #254
    b.w _gld_trampoline_idx
.align 1
.globl _gld_stub_255
.thumb_func _gld_stub_255
_gld_stub_255:
    movw r12, #255
    b.w _gld_trampoline_idx
.align 1
.globl _gld_stub_256
.thumb_func _gld_stub_256
_gld_stub_256:
    movw r12, #256
    b.w _gld_trampoline_idx
.align 1
.globl _gld_stub_257
.thumb_func _gld_stub_257
_gld_stub_257:
    movw r12, #257
    b.w _gld_trampoline_idx
.align 1
.globl _gld_stub_258
.thumb_func _gld_stub_258
_gld_stub_258:
    movw r12, #258
    b.w _gld_trampoline_idx
.align 1
.globl _gld_stub_259
.thumb_func _gld_stub_259
_gld_stub_259:
    movw r12, #259
    b.w _gld_trampoline_idx
.align 1
.globl _gld_stub_260
.thumb_func _gld_stub_260
_gld_stub_260:
    movw r12, #260
    b.w _gld_trampoline_idx
.align 1
.globl _gld_stub_261
.thumb_func _gld_stub_261
_gld_stub_261:
    movw r12, #261
    b.w _gld_trampoline_idx
.align 1
.globl _gld_stub_262
.thumb_func _gld_stub_262
_gld_stub_262:
    movw r12, #262
    b.w _gld_trampoline_idx
.align 1
.globl _gld_stub_263
.thumb_func _gld_stub_263
_gld_stub_263:
    movw r12, #263
    b.w _gld_trampoline_idx
.align 1
.globl _gld_stub_264
.thumb_func _gld_stub_264
_gld_stub_264:
    movw r12, #264
    b.w _gld_trampoline_idx
.align 1
.globl _gld_stub_265
.thumb_func _gld_stub_265
_gld_stub_265:
    movw r12, #265
    b.w _gld_trampoline_idx
.align 1
.globl _gld_stub_266
.thumb_func _gld_stub_266
_gld_stub_266:
    movw r12, #266
    b.w _gld_trampoline_idx
.align 1
.globl _gld_stub_267
.thumb_func _gld_stub_267
_gld_stub_267:
    movw r12, #267
    b.w _gld_trampoline_idx
.align 1
.globl _gld_stub_268
.thumb_func _gld_stub_268
_gld_stub_268:
    movw r12, #268
    b.w _gld_trampoline_idx
.align 1
.globl _gld_stub_269
.thumb_func _gld_stub_269
_gld_stub_269:
    movw r12, #269
    b.w _gld_trampoline_idx
.align 1
.globl _gld_stub_270
.thumb_func _gld_stub_270
_gld_stub_270:
    movw r12, #270
    b.w _gld_trampoline_idx
.align 1
.globl _gld_stub_271
.thumb_func _gld_stub_271
_gld_stub_271:
    movw r12, #271
    b.w _gld_trampoline_idx
.align 1
.globl _gld_stub_272
.thumb_func _gld_stub_272
_gld_stub_272:
    movw r12, #272
    b.w _gld_trampoline_idx
.align 1
.globl _gld_stub_273
.thumb_func _gld_stub_273
_gld_stub_273:
    movw r12, #273
    b.w _gld_trampoline_idx
.align 1
.globl _gld_stub_274
.thumb_func _gld_stub_274
_gld_stub_274:
    movw r12, #274
    b.w _gld_trampoline_idx
.align 1
.globl _gld_stub_275
.thumb_func _gld_stub_275
_gld_stub_275:
    movw r12, #275
    b.w _gld_trampoline_idx
.align 1
.globl _gld_stub_276
.thumb_func _gld_stub_276
_gld_stub_276:
    movw r12, #276
    b.w _gld_trampoline_idx
.align 1
.globl _gld_stub_277
.thumb_func _gld_stub_277
_gld_stub_277:
    movw r12, #277
    b.w _gld_trampoline_idx
.align 1
.globl _gld_stub_278
.thumb_func _gld_stub_278
_gld_stub_278:
    movw r12, #278
    b.w _gld_trampoline_idx
.align 1
.globl _gld_stub_279
.thumb_func _gld_stub_279
_gld_stub_279:
    movw r12, #279
    b.w _gld_trampoline_idx
.align 1
.globl _gld_stub_280
.thumb_func _gld_stub_280
_gld_stub_280:
    movw r12, #280
    b.w _gld_trampoline_idx
.align 1
.globl _gld_stub_281
.thumb_func _gld_stub_281
_gld_stub_281:
    movw r12, #281
    b.w _gld_trampoline_idx
.align 1
.globl _gld_stub_282
.thumb_func _gld_stub_282
_gld_stub_282:
    movw r12, #282
    b.w _gld_trampoline_idx
.align 1
.globl _gld_stub_283
.thumb_func _gld_stub_283
_gld_stub_283:
    movw r12, #283
    b.w _gld_trampoline_idx
.align 1
.globl _gld_stub_284
.thumb_func _gld_stub_284
_gld_stub_284:
    movw r12, #284
    b.w _gld_trampoline_idx
.align 1
.globl _gld_stub_285
.thumb_func _gld_stub_285
_gld_stub_285:
    movw r12, #285
    b.w _gld_trampoline_idx
.align 1
.globl _gld_stub_286
.thumb_func _gld_stub_286
_gld_stub_286:
    movw r12, #286
    b.w _gld_trampoline_idx
.align 1
.globl _gld_stub_287
.thumb_func _gld_stub_287
_gld_stub_287:
    movw r12, #287
    b.w _gld_trampoline_idx
.align 1
.globl _gld_stub_288
.thumb_func _gld_stub_288
_gld_stub_288:
    movw r12, #288
    b.w _gld_trampoline_idx
.align 1
.globl _gld_stub_289
.thumb_func _gld_stub_289
_gld_stub_289:
    movw r12, #289
    b.w _gld_trampoline_idx
.align 1
.globl _gld_stub_290
.thumb_func _gld_stub_290
_gld_stub_290:
    movw r12, #290
    b.w _gld_trampoline_idx
.align 1
.globl _gld_stub_291
.thumb_func _gld_stub_291
_gld_stub_291:
    movw r12, #291
    b.w _gld_trampoline_idx
.align 1
.globl _gld_stub_292
.thumb_func _gld_stub_292
_gld_stub_292:
    movw r12, #292
    b.w _gld_trampoline_idx
.align 1
.globl _gld_stub_293
.thumb_func _gld_stub_293
_gld_stub_293:
    movw r12, #293
    b.w _gld_trampoline_idx
.align 1
.globl _gld_stub_294
.thumb_func _gld_stub_294
_gld_stub_294:
    movw r12, #294
    b.w _gld_trampoline_idx
.align 1
.globl _gld_stub_295
.thumb_func _gld_stub_295
_gld_stub_295:
    movw r12, #295
    b.w _gld_trampoline_idx
.align 1
.globl _gld_stub_296
.thumb_func _gld_stub_296
_gld_stub_296:
    movw r12, #296
    b.w _gld_trampoline_idx
.align 1
.globl _gld_stub_297
.thumb_func _gld_stub_297
_gld_stub_297:
    movw r12, #297
    b.w _gld_trampoline_idx
.align 1
.globl _gld_stub_298
.thumb_func _gld_stub_298
_gld_stub_298:
    movw r12, #298
    b.w _gld_trampoline_idx
.align 1
.globl _gld_stub_299
.thumb_func _gld_stub_299
_gld_stub_299:
    movw r12, #299
    b.w _gld_trampoline_idx
.align 1
.globl _gld_stub_300
.thumb_func _gld_stub_300
_gld_stub_300:
    movw r12, #300
    b.w _gld_trampoline_idx
.align 1
.globl _gld_stub_301
.thumb_func _gld_stub_301
_gld_stub_301:
    movw r12, #301
    b.w _gld_trampoline_idx
.align 1
.globl _gld_stub_302
.thumb_func _gld_stub_302
_gld_stub_302:
    movw r12, #302
    b.w _gld_trampoline_idx
.align 1
.globl _gld_stub_303
.thumb_func _gld_stub_303
_gld_stub_303:
    movw r12, #303
    b.w _gld_trampoline_idx
.align 1
.globl _gld_stub_304
.thumb_func _gld_stub_304
_gld_stub_304:
    movw r12, #304
    b.w _gld_trampoline_idx
.align 1
.globl _gld_stub_305
.thumb_func _gld_stub_305
_gld_stub_305:
    movw r12, #305
    b.w _gld_trampoline_idx
.align 1
.globl _gld_stub_306
.thumb_func _gld_stub_306
_gld_stub_306:
    movw r12, #306
    b.w _gld_trampoline_idx
.align 1
.globl _gld_stub_307
.thumb_func _gld_stub_307
_gld_stub_307:
    movw r12, #307
    b.w _gld_trampoline_idx
.align 1
.globl _gld_stub_308
.thumb_func _gld_stub_308
_gld_stub_308:
    movw r12, #308
    b.w _gld_trampoline_idx
.align 1
.globl _gld_stub_309
.thumb_func _gld_stub_309
_gld_stub_309:
    movw r12, #309
    b.w _gld_trampoline_idx
.align 1
.globl _gld_stub_310
.thumb_func _gld_stub_310
_gld_stub_310:
    movw r12, #310
    b.w _gld_trampoline_idx
.align 1
.globl _gld_stub_311
.thumb_func _gld_stub_311
_gld_stub_311:
    movw r12, #311
    b.w _gld_trampoline_idx
.align 1
.globl _gld_stub_312
.thumb_func _gld_stub_312
_gld_stub_312:
    movw r12, #312
    b.w _gld_trampoline_idx
.align 1
.globl _gld_stub_313
.thumb_func _gld_stub_313
_gld_stub_313:
    movw r12, #313
    b.w _gld_trampoline_idx
.align 1
.globl _gld_stub_314
.thumb_func _gld_stub_314
_gld_stub_314:
    movw r12, #314
    b.w _gld_trampoline_idx
.align 1
.globl _gld_stub_315
.thumb_func _gld_stub_315
_gld_stub_315:
    movw r12, #315
    b.w _gld_trampoline_idx
.align 1
.globl _gld_stub_316
.thumb_func _gld_stub_316
_gld_stub_316:
    movw r12, #316
    b.w _gld_trampoline_idx
.align 1
.globl _gld_stub_317
.thumb_func _gld_stub_317
_gld_stub_317:
    movw r12, #317
    b.w _gld_trampoline_idx
.align 1
.globl _gld_stub_318
.thumb_func _gld_stub_318
_gld_stub_318:
    movw r12, #318
    b.w _gld_trampoline_idx
.align 1
.globl _gld_stub_319
.thumb_func _gld_stub_319
_gld_stub_319:
    movw r12, #319
    b.w _gld_trampoline_idx
.align 1
.globl _gld_stub_320
.thumb_func _gld_stub_320
_gld_stub_320:
    movw r12, #320
    b.w _gld_trampoline_idx
.align 1
.globl _gld_stub_321
.thumb_func _gld_stub_321
_gld_stub_321:
    movw r12, #321
    b.w _gld_trampoline_idx
.align 1
.globl _gld_stub_322
.thumb_func _gld_stub_322
_gld_stub_322:
    movw r12, #322
    b.w _gld_trampoline_idx
.align 1
.globl _gld_stub_323
.thumb_func _gld_stub_323
_gld_stub_323:
    movw r12, #323
    b.w _gld_trampoline_idx
.align 1
.globl _gld_stub_324
.thumb_func _gld_stub_324
_gld_stub_324:
    movw r12, #324
    b.w _gld_trampoline_idx
.align 1
.globl _gld_stub_325
.thumb_func _gld_stub_325
_gld_stub_325:
    movw r12, #325
    b.w _gld_trampoline_idx
.align 1
.globl _gld_stub_326
.thumb_func _gld_stub_326
_gld_stub_326:
    movw r12, #326
    b.w _gld_trampoline_idx
.align 1
.globl _gld_stub_327
.thumb_func _gld_stub_327
_gld_stub_327:
    movw r12, #327
    b.w _gld_trampoline_idx
.align 1
.globl _gld_stub_328
.thumb_func _gld_stub_328
_gld_stub_328:
    movw r12, #328
    b.w _gld_trampoline_idx
.align 1
.globl _gld_stub_329
.thumb_func _gld_stub_329
_gld_stub_329:
    movw r12, #329
    b.w _gld_trampoline_idx
.align 1
.globl _gld_stub_330
.thumb_func _gld_stub_330
_gld_stub_330:
    movw r12, #330
    b.w _gld_trampoline_idx
.align 1
.globl _gld_stub_331
.thumb_func _gld_stub_331
_gld_stub_331:
    movw r12, #331
    b.w _gld_trampoline_idx
.align 1
.globl _gld_stub_332
.thumb_func _gld_stub_332
_gld_stub_332:
    movw r12, #332
    b.w _gld_trampoline_idx
.align 1
.globl _gld_stub_333
.thumb_func _gld_stub_333
_gld_stub_333:
    movw r12, #333
    b.w _gld_trampoline_idx
.align 1
.globl _gld_stub_334
.thumb_func _gld_stub_334
_gld_stub_334:
    movw r12, #334
    b.w _gld_trampoline_idx
.align 1
.globl _gld_stub_335
.thumb_func _gld_stub_335
_gld_stub_335:
    movw r12, #335
    b.w _gld_trampoline_idx
.align 1
.globl _gld_stub_336
.thumb_func _gld_stub_336
_gld_stub_336:
    movw r12, #336
    b.w _gld_trampoline_idx
.align 1
.globl _gld_stub_337
.thumb_func _gld_stub_337
_gld_stub_337:
    movw r12, #337
    b.w _gld_trampoline_idx
.align 1
.globl _gld_stub_338
.thumb_func _gld_stub_338
_gld_stub_338:
    movw r12, #338
    b.w _gld_trampoline_idx
.align 1
.globl _gld_stub_339
.thumb_func _gld_stub_339
_gld_stub_339:
    movw r12, #339
    b.w _gld_trampoline_idx
.align 1
.globl _gld_stub_340
.thumb_func _gld_stub_340
_gld_stub_340:
    movw r12, #340
    b.w _gld_trampoline_idx
.align 1
.globl _gld_stub_341
.thumb_func _gld_stub_341
_gld_stub_341:
    movw r12, #341
    b.w _gld_trampoline_idx
.align 1
.globl _gld_stub_342
.thumb_func _gld_stub_342
_gld_stub_342:
    movw r12, #342
    b.w _gld_trampoline_idx
.align 1
.globl _gld_stub_343
.thumb_func _gld_stub_343
_gld_stub_343:
    movw r12, #343
    b.w _gld_trampoline_idx
.align 1
.globl _gld_stub_344
.thumb_func _gld_stub_344
_gld_stub_344:
    movw r12, #344
    b.w _gld_trampoline_idx
.align 1
.globl _gld_stub_345
.thumb_func _gld_stub_345
_gld_stub_345:
    movw r12, #345
    b.w _gld_trampoline_idx
.align 1
.globl _gld_stub_346
.thumb_func _gld_stub_346
_gld_stub_346:
    movw r12, #346
    b.w _gld_trampoline_idx
.align 1
.globl _gld_stub_347
.thumb_func _gld_stub_347
_gld_stub_347:
    movw r12, #347
    b.w _gld_trampoline_idx
.align 1
.globl _gld_stub_348
.thumb_func _gld_stub_348
_gld_stub_348:
    movw r12, #348
    b.w _gld_trampoline_idx
.align 1
.globl _gld_stub_349
.thumb_func _gld_stub_349
_gld_stub_349:
    movw r12, #349
    b.w _gld_trampoline_idx
.align 1
.globl _gld_stub_350
.thumb_func _gld_stub_350
_gld_stub_350:
    movw r12, #350
    b.w _gld_trampoline_idx
.align 1
.globl _gld_stub_351
.thumb_func _gld_stub_351
_gld_stub_351:
    movw r12, #351
    b.w _gld_trampoline_idx
.align 1
.globl _gld_stub_352
.thumb_func _gld_stub_352
_gld_stub_352:
    movw r12, #352
    b.w _gld_trampoline_idx
.align 1
.globl _gld_stub_353
.thumb_func _gld_stub_353
_gld_stub_353:
    movw r12, #353
    b.w _gld_trampoline_idx
.align 1
.globl _gld_stub_354
.thumb_func _gld_stub_354
_gld_stub_354:
    movw r12, #354
    b.w _gld_trampoline_idx
.align 1
.globl _gld_stub_355
.thumb_func _gld_stub_355
_gld_stub_355:
    movw r12, #355
    b.w _gld_trampoline_idx
.align 1
.globl _gld_stub_356
.thumb_func _gld_stub_356
_gld_stub_356:
    movw r12, #356
    b.w _gld_trampoline_idx
.align 1
.globl _gld_stub_357
.thumb_func _gld_stub_357
_gld_stub_357:
    movw r12, #357
    b.w _gld_trampoline_idx
.align 1
.globl _gld_stub_358
.thumb_func _gld_stub_358
_gld_stub_358:
    movw r12, #358
    b.w _gld_trampoline_idx
.align 1
.globl _gld_stub_359
.thumb_func _gld_stub_359
_gld_stub_359:
    movw r12, #359
    b.w _gld_trampoline_idx
.align 1
.globl _gld_stub_360
.thumb_func _gld_stub_360
_gld_stub_360:
    movw r12, #360
    b.w _gld_trampoline_idx
.align 1
.globl _gld_stub_361
.thumb_func _gld_stub_361
_gld_stub_361:
    movw r12, #361
    b.w _gld_trampoline_idx
.align 1
.globl _gld_stub_362
.thumb_func _gld_stub_362
_gld_stub_362:
    movw r12, #362
    b.w _gld_trampoline_idx
.align 1
.globl _gld_stub_363
.thumb_func _gld_stub_363
_gld_stub_363:
    movw r12, #363
    b.w _gld_trampoline_idx
.align 1
.globl _gld_stub_364
.thumb_func _gld_stub_364
_gld_stub_364:
    movw r12, #364
    b.w _gld_trampoline_idx
.align 1
.globl _gld_stub_365
.thumb_func _gld_stub_365
_gld_stub_365:
    movw r12, #365
    b.w _gld_trampoline_idx
.align 1
.globl _gld_stub_366
.thumb_func _gld_stub_366
_gld_stub_366:
    movw r12, #366
    b.w _gld_trampoline_idx
.align 1
.globl _gld_stub_367
.thumb_func _gld_stub_367
_gld_stub_367:
    movw r12, #367
    b.w _gld_trampoline_idx
.align 1
.globl _gld_stub_368
.thumb_func _gld_stub_368
_gld_stub_368:
    movw r12, #368
    b.w _gld_trampoline_idx
.align 1
.globl _gld_stub_369
.thumb_func _gld_stub_369
_gld_stub_369:
    movw r12, #369
    b.w _gld_trampoline_idx
.align 1
.globl _gld_stub_370
.thumb_func _gld_stub_370
_gld_stub_370:
    movw r12, #370
    b.w _gld_trampoline_idx
.align 1
.globl _gld_stub_371
.thumb_func _gld_stub_371
_gld_stub_371:
    movw r12, #371
    b.w _gld_trampoline_idx
.align 1
.globl _gld_stub_372
.thumb_func _gld_stub_372
_gld_stub_372:
    movw r12, #372
    b.w _gld_trampoline_idx
.align 1
.globl _gld_stub_373
.thumb_func _gld_stub_373
_gld_stub_373:
    movw r12, #373
    b.w _gld_trampoline_idx
.align 1
.globl _gld_stub_374
.thumb_func _gld_stub_374
_gld_stub_374:
    movw r12, #374
    b.w _gld_trampoline_idx
.align 1
.globl _gld_stub_375
.thumb_func _gld_stub_375
_gld_stub_375:
    movw r12, #375
    b.w _gld_trampoline_idx
.align 1
.globl _gld_stub_376
.thumb_func _gld_stub_376
_gld_stub_376:
    movw r12, #376
    b.w _gld_trampoline_idx
.align 1
.globl _gld_stub_377
.thumb_func _gld_stub_377
_gld_stub_377:
    movw r12, #377
    b.w _gld_trampoline_idx
.align 1
.globl _gld_stub_378
.thumb_func _gld_stub_378
_gld_stub_378:
    movw r12, #378
    b.w _gld_trampoline_idx
.align 1
.globl _gld_stub_379
.thumb_func _gld_stub_379
_gld_stub_379:
    movw r12, #379
    b.w _gld_trampoline_idx
.align 1
.globl _gld_stub_380
.thumb_func _gld_stub_380
_gld_stub_380:
    movw r12, #380
    b.w _gld_trampoline_idx
.align 1
.globl _gld_stub_381
.thumb_func _gld_stub_381
_gld_stub_381:
    movw r12, #381
    b.w _gld_trampoline_idx
.align 1
.globl _gld_stub_382
.thumb_func _gld_stub_382
_gld_stub_382:
    movw r12, #382
    b.w _gld_trampoline_idx
.align 1
.globl _gld_stub_383
.thumb_func _gld_stub_383
_gld_stub_383:
    movw r12, #383
    b.w _gld_trampoline_idx
.align 1
.globl _gld_stub_384
.thumb_func _gld_stub_384
_gld_stub_384:
    movw r12, #384
    b.w _gld_trampoline_idx
.align 1
.globl _gld_stub_385
.thumb_func _gld_stub_385
_gld_stub_385:
    movw r12, #385
    b.w _gld_trampoline_idx
.align 1
.globl _gld_stub_386
.thumb_func _gld_stub_386
_gld_stub_386:
    movw r12, #386
    b.w _gld_trampoline_idx
.align 1
.globl _gld_stub_387
.thumb_func _gld_stub_387
_gld_stub_387:
    movw r12, #387
    b.w _gld_fast4_idx
.align 1
.globl _gld_stub_388
.thumb_func _gld_stub_388
_gld_stub_388:
    movw r12, #388
    b.w _gld_fast4_idx
.align 1
.globl _gld_stub_389
.thumb_func _gld_stub_389
_gld_stub_389:
    movw r12, #389
    b.w _gld_trampoline_idx
.align 1
.globl _gld_stub_390
.thumb_func _gld_stub_390
_gld_stub_390:
    movw r12, #390
    b.w _gld_trampoline_idx
.align 1
.globl _gld_stub_391
.thumb_func _gld_stub_391
_gld_stub_391:
    movw r12, #391
    b.w _gld_trampoline_idx
.align 1
.globl _gld_stub_392
.thumb_func _gld_stub_392
_gld_stub_392:
    movw r12, #392
    b.w _gld_trampoline_idx
