#include <stdio.h>
#include <ctype.h>

#include "allegrex.h"

/* Format codes
 * %d - Rd
 * %t - Rt
 * %s - Rs
 * %i - 16bit signed immediate
 * %I - 16bit unsigned immediate (always printed in hex)
 * %o - 16bit signed offset (rt base)
 * %O - 16bit signed offset (PC relative)
 * %j - 26bit absolute offset
 * %J - Register jump
 * %a - SA
 * %0 - Cop0 register
 * %1 - Cop1 register
 * %2? - Cop2 register (? is (s, d))
 * %p - General cop (i.e. numbered) register
 * %n? - ins/ext size, ? (e, i)
 * %r - Debug register
 * %k - Cache function
 * %D - Fd
 * %T - Ft
 * %S - Fs
 * %x? - Vt (? is (s/scalar, p/pair, t/triple, q/quad, m/matrix pair, n/matrix triple, o/matrix quad)
 * %y? - Vs
 * %z? - Vd
 * %X? - Vo (? is (s, q))
 * %Y - VFPU offset
 * %Z? - VFPU condition code/name (? is (c, n))
 * %v? - VFPU immediate, ? (3, 5, 8, k, i, h, r, p? (? is (0, 1, 2, 3, 4, 5, 6, 7)))
 * %c - code (for break)
 * %C - code (for syscall)
 * %? - Indicates vmmul special exception
 */

#define RT(op) ((op >> 16) & 0x1F)
#define RS(op) ((op >> 21) & 0x1F)
#define RD(op) ((op >> 11) & 0x1F)
#define FT(op) ((op >> 16) & 0x1F)
#define FS(op) ((op >> 11) & 0x1F)
#define FD(op) ((op >> 6) & 0x1F)
#define SA(op) ((op >> 6)  & 0x1F)
#define IMM(op) ((signed short) (op & 0xFFFF))
#define IMMU(op) ((unsigned short) (op & 0xFFFF))
#define JUMP(op, pc) ((pc & 0xF0000000) | ((op & 0x3FFFFFF) << 2))
#define CODE(op) ((op >> 6) & 0xFFFFF)
#define SIZE(op) ((op >> 11) & 0x1F)
#define POS(op)  ((op >> 6) & 0x1F)
#define VO(op)   (((op & 3) << 5) | ((op >> 16) & 0x1F))
#define VCC(op)  ((op >> 18) & 7)
#define VD(op)   (op & 0x7F)
#define VS(op)   ((op >> 8) & 0x7F)
#define VT(op)   ((op >> 16) & 0x7F)

/* [hlide] new #defines */
#define VED(op)  (op & 0xFF)
#define VES(op)  ((op >> 8) & 0xFF)
#define VCN(op)  (op & 0x0F)
#define VI3(op)  ((op >> 16) & 0x07)
#define VI5(op)  ((op >> 16) & 0x1F)
#define VI8(op)  ((op >> 16) & 0xFF)

/* VFPU 16-bit floating-point format. */
#define VFPU_FLOAT16_EXP_MAX    0x1f
#define VFPU_SH_FLOAT16_SIGN    15
#define VFPU_MASK_FLOAT16_SIGN  0x1
#define VFPU_SH_FLOAT16_EXP     10
#define VFPU_MASK_FLOAT16_EXP   0x1f
#define VFPU_SH_FLOAT16_FRAC    0
#define VFPU_MASK_FLOAT16_FRAC  0x3ff

/* VFPU prefix instruction operands.  The *_SH_* values really specify where
   the bitfield begins, as VFPU prefix instructions have four operands
   encoded within the immediate field. */
#define VFPU_SH_PFX_NEG         16
#define VFPU_MASK_PFX_NEG       0x1     /* Negation. */
#define VFPU_SH_PFX_CST         12
#define VFPU_MASK_PFX_CST       0x1     /* Constant. */
#define VFPU_SH_PFX_ABS_CSTHI   8
#define VFPU_MASK_PFX_ABS_CSTHI 0x1     /* Abs/Constant (bit 2). */
#define VFPU_SH_PFX_SWZ_CSTLO   0
#define VFPU_MASK_PFX_SWZ_CSTLO 0x3     /* Swizzle/Constant (bits 0-1). */
#define VFPU_SH_PFX_MASK        8
#define VFPU_MASK_PFX_MASK      0x1     /* Mask. */
#define VFPU_SH_PFX_SAT         0
#define VFPU_MASK_PFX_SAT       0x3     /* Saturation. */

/* Special handling of the vrot instructions. */
#define VFPU_MASK_OP_SIZE       0x8080  /* Masks the operand size (pair, triple, quad). */
#define VFPU_OP_SIZE_PAIR       0x80
#define VFPU_OP_SIZE_TRIPLE     0x8000
#define VFPU_OP_SIZE_QUAD       0x8080
/* Note that these are within the rotators field, and not the full opcode. */
#define VFPU_SH_ROT_HI          2
#define VFPU_MASK_ROT_HI        0x3
#define VFPU_SH_ROT_LO          0
#define VFPU_MASK_ROT_LO        0x3
#define VFPU_SH_ROT_NEG         4       /* Negation. */
#define VFPU_MASK_ROT_NEG       0x1


struct allegrex_instruction
{
  enum insn_type itype;
  const char *name;
  unsigned int opcode;
  unsigned int mask;
  const char *fmt;
};

static struct allegrex_instruction instructions[] =
{
  /* Macro instructions */
  { I_SLL,             "nop",        0x00000000, 0xFFFFFFFF, ""        },
  { I_ADDIU,           "li",         0x24000000, 0xFFE00000, "%t, %i"  },
  { I_ORI,             "li",         0x34000000, 0xFFE00000, "%t, %I"  },
  { I_ADDU,            "move",       0x00000021, 0xFC1F07FF, "%d, %s"  },
  { I_OR,              "move",       0x00000025, 0xFC1F07FF, "%d, %s"  },
  { I_BEQ,             "b",          0x10000000, 0xFFFF0000, "%O"      },
  { I_BGEZ,            "b",          0x04010000, 0xFFFF0000, "%O"      },
  { I_BGEZAL,          "bal",        0x04110000, 0xFFFF0000, "%O"      },
  { I_BNE,             "bnez",       0x14000000, 0xFC1F0000, "%s, %O"  },
  { I_BNEL,            "bnezl",      0x54000000, 0xFC1F0000, "%s, %O"  },
  { I_BEQ,             "beqz",       0x10000000, 0xFC1F0000, "%s, %O"  },
  { I_BEQL,            "beqzl",      0x50000000, 0xFC1F0000, "%s, %O"  },
  { I_SUB,             "neg",        0x00000022, 0xFFE007FF, "%d, %t"  },
  { I_SUBU,            "negu",       0x00000023, 0xFFE007FF, "%d, %t"  },
  { I_NOR,             "not",        0x00000027, 0xFC1F07FF, "%d, %s"  },
  { I_JALR,            "jalr",       0x0000F809, 0xFC1FFFFF, "%J"      },

  /* MIPS instructions */
  { I_ADD,             "add",         0x00000020, 0xFC0007FF, "%d, %s, %t"  },
  { I_ADDI,            "addi",        0x20000000, 0xFC000000, "%t, %s, %i"  },
  { I_ADDIU,           "addiu",       0x24000000, 0xFC000000, "%t, %s, %i"  },
  { I_ADDU,            "addu",        0x00000021, 0xFC0007FF, "%d, %s, %t"  },
  { I_AND,             "and",         0x00000024, 0xFC0007FF, "%d, %s, %t"  },
  { I_ANDI,            "andi",        0x30000000, 0xFC000000, "%t, %s, %I"  },
  { I_BEQ,             "beq",         0x10000000, 0xFC000000, "%s, %t, %O"  },
  { I_BEQL,            "beql",        0x50000000, 0xFC000000, "%s, %t, %O"  },
  { I_BGEZ,            "bgez",        0x04010000, 0xFC1F0000, "%s, %O"      },
  { I_BGEZAL,          "bgezal",      0x04110000, 0xFC1F0000, "%s, %O"      },
  { I_BGEZL,           "bgezl",       0x04030000, 0xFC1F0000, "%s, %O"      },
  { I_BGTZ,            "bgtz",        0x1C000000, 0xFC1F0000, "%s, %O"      },
  { I_BGTZL,           "bgtzl",       0x5C000000, 0xFC1F0000, "%s, %O"      },
  { I_BITREV,          "bitrev",      0x7C000520, 0xFFE007FF, "%d, %t"      },
  { I_BLEZ,            "blez",        0x18000000, 0xFC1F0000, "%s, %O"      },
  { I_BLEZL,           "blezl",       0x58000000, 0xFC1F0000, "%s, %O"      },
  { I_BLTZ,            "bltz",        0x04000000, 0xFC1F0000, "%s, %O"      },
  { I_BLTZL,           "bltzl",       0x04020000, 0xFC1F0000, "%s, %O"      },
  { I_BLTZAL,          "bltzal",      0x04100000, 0xFC1F0000, "%s, %O"      },
  { I_BLTZALL,         "bltzall",     0x04120000, 0xFC1F0000, "%s, %O"      },
  { I_BNE,             "bne",         0x14000000, 0xFC000000, "%s, %t, %O"  },
  { I_BNEL,            "bnel",        0x54000000, 0xFC000000, "%s, %t, %O"  },
  { I_BREAK,           "break",       0x0000000D, 0xFC00003F, "%c"          },
  { I_CACHE,           "cache",       0xbc000000, 0xfc000000, "%k, %o"      },
  { I_CFC0,            "cfc0",        0x40400000, 0xFFE007FF, "%t, %p"      },
  { I_CLO,             "clo",         0x00000017, 0xFC1F07FF, "%d, %s"      },
  { I_CLZ,             "clz",         0x00000016, 0xFC1F07FF, "%d, %s"      },
  { I_CTC0,            "ctc0",        0x40C00000, 0xFFE007FF, "%t, %p"      },
  { I_MAX,             "max",         0x0000002C, 0xFC0007FF, "%d, %s, %t"  },
  { I_MIN,             "min",         0x0000002D, 0xFC0007FF, "%d, %s, %t"  },
  { I_DBREAK,          "dbreak",      0x7000003F, 0xFFFFFFFF, ""            },
  { I_DIV,             "div",         0x0000001A, 0xFC00FFFF, "%s, %t"      },
  { I_DIVU,            "divu",        0x0000001B, 0xFC00FFFF, "%s, %t"      },
  { I_DRET,            "dret",        0x7000003E, 0xFFFFFFFF, ""            },
  { I_ERET,            "eret",        0x42000018, 0xFFFFFFFF, ""            },
  { I_EXT,             "ext",         0x7C000000, 0xFC00003F, "%t, %s, %a, %ne" },
  { I_INS,             "ins",         0x7C000004, 0xFC00003F, "%t, %s, %a, %ni" },
  { I_J,               "j",           0x08000000, 0xFC000000, "%j"          },
  { I_JR,              "jr",          0x00000008, 0xFC1FFFFF, "%J"          },
  { I_JALR,            "jalr",        0x00000009, 0xFC1F07FF, "%J, %d"      },
  { I_JAL,             "jal",         0x0C000000, 0xFC000000, "%j"          },
  { I_LB,              "lb",          0x80000000, 0xFC000000, "%t, %o"      },
  { I_LBU,             "lbu",         0x90000000, 0xFC000000, "%t, %o"      },
  { I_LH,              "lh",          0x84000000, 0xFC000000, "%t, %o"      },
  { I_LHU,             "lhu",         0x94000000, 0xFC000000, "%t, %o"      },
  { I_LL,              "ll",          0xC0000000, 0xFC000000, "%t, %O"      },
  { I_LUI,             "lui",         0x3C000000, 0xFFE00000, "%t, %I"      },
  { I_LW,              "lw",          0x8C000000, 0xFC000000, "%t, %o"      },
  { I_LWL,             "lwl",         0x88000000, 0xFC000000, "%t, %o"      },
  { I_LWR,             "lwr",         0x98000000, 0xFC000000, "%t, %o"      },
  { I_MADD,            "madd",        0x0000001C, 0xFC00FFFF, "%s, %t"      },
  { I_MADDU,           "maddu",       0x0000001D, 0xFC00FFFF, "%s, %t"      },
  { I_MFC0,            "mfc0",        0x40000000, 0xFFE007FF, "%t, %0"      },
  { I_MFDR,            "mfdr",        0x7000003D, 0xFFE007FF, "%t, %r"      },
  { I_MFHI,            "mfhi",        0x00000010, 0xFFFF07FF, "%d"          },
  { I_MFIC,            "mfic",        0x70000024, 0xFFE007FF, "%t, %p"      },
  { I_MFLO,            "mflo",        0x00000012, 0xFFFF07FF, "%d"          },
  { I_MOVN,            "movn",        0x0000000B, 0xFC0007FF, "%d, %s, %t"  },
  { I_MOVZ,            "movz",        0x0000000A, 0xFC0007FF, "%d, %s, %t"  },
  { I_MSUB,            "msub",        0x0000002e, 0xfc00ffff, "%d, %t"      },
  { I_MSUBU,           "msubu",       0x0000002f, 0xfc00ffff, "%d, %t"      },
  { I_MTC0,            "mtc0",        0x40800000, 0xFFE007FF, "%t, %0"      },
  { I_MTDR,            "mtdr",        0x7080003D, 0xFFE007FF, "%t, %r"      },
  { I_MTIC,            "mtic",        0x70000026, 0xFFE007FF, "%t, %p"      },
  { I_HALT,            "halt",        0x70000000, 0xFFFFFFFF, ""            },
  { I_MTHI,            "mthi",        0x00000011, 0xFC1FFFFF, "%s"          },
  { I_MTLO,            "mtlo",        0x00000013, 0xFC1FFFFF, "%s"          },
  { I_MULT,            "mult",        0x00000018, 0xFC00FFFF, "%s, %t"      },
  { I_MULTU,           "multu",       0x00000019, 0xFC0007FF, "%s, %t"      },
  { I_NOR,             "nor",         0x00000027, 0xFC0007FF, "%d, %s, %t"  },
  { I_OR,              "or",          0x00000025, 0xFC0007FF, "%d, %s, %t"  },
  { I_ORI,             "ori",         0x34000000, 0xFC000000, "%t, %s, %I"  },
  { I_ROTR,            "rotr",        0x00200002, 0xFFE0003F, "%d, %t, %a"  },
  { I_ROTV,            "rotv",        0x00000046, 0xFC0007FF, "%d, %t, %s"  },
  { I_SEB,             "seb",         0x7C000420, 0xFFE007FF, "%d, %t"      },
  { I_SEH,             "seh",         0x7C000620, 0xFFE007FF, "%d, %t"      },
  { I_SB,              "sb",          0xA0000000, 0xFC000000, "%t, %o"      },
  { I_SH,              "sh",          0xA4000000, 0xFC000000, "%t, %o"      },
  { I_SLLV,            "sllv",        0x00000004, 0xFC0007FF, "%d, %t, %s"  },
  { I_SLL,             "sll",         0x00000000, 0xFFE0003F, "%d, %t, %a"  },
  { I_SLT,             "slt",         0x0000002A, 0xFC0007FF, "%d, %s, %t"  },
  { I_SLTI,            "slti",        0x28000000, 0xFC000000, "%t, %s, %i"  },
  { I_SLTIU,           "sltiu",       0x2C000000, 0xFC000000, "%t, %s, %i"  },
  { I_SLTU,            "sltu",        0x0000002B, 0xFC0007FF, "%d, %s, %t"  },
  { I_SRA,             "sra",         0x00000003, 0xFFE0003F, "%d, %t, %a"  },
  { I_SRAV,            "srav",        0x00000007, 0xFC0007FF, "%d, %t, %s"  },
  { I_SRLV,            "srlv",        0x00000006, 0xFC0007FF, "%d, %t, %s"  },
  { I_SRL,             "srl",         0x00000002, 0xFFE0003F, "%d, %t, %a"  },
  { I_SW,              "sw",          0xAC000000, 0xFC000000, "%t, %o"      },
  { I_SWL,             "swl",         0xA8000000, 0xFC000000, "%t, %o"      },
  { I_SWR,             "swr",         0xB8000000, 0xFC000000, "%t, %o"      },
  { I_SUB,             "sub",         0x00000022, 0xFC0007FF, "%d, %s, %t"  },
  { I_SUBU,            "subu",        0x00000023, 0xFC0007FF, "%d, %s, %t"  },
  { I_SYNC,            "sync",        0x0000000F, 0xFFFFFFFF, ""            },
  { I_SYSCALL,         "syscall",     0x0000000C, 0xFC00003F, "%C"          },
  { I_XOR,             "xor",         0x00000026, 0xFC0007FF, "%d, %s, %t"  },
  { I_XORI,            "xori",        0x38000000, 0xFC000000, "%t, %s, %I"  },
  { I_WSBH,            "wsbh",        0x7C0000A0, 0xFFE007FF, "%d, %t"      },
  { I_WSBW,            "wsbw",        0x7C0000E0, 0xFFE007FF, "%d, %t"      },

  /* FPU instructions */
  { I_ABS_S,           "abs.s",        0x46000005, 0xFFFF003F, "%D, %S"      },
  { I_ADD_S,           "add.s",        0x46000000, 0xFFE0003F, "%D, %S, %T"  },
  { I_BC1F,            "bc1f",         0x45000000, 0xFFFF0000, "%O"          },
  { I_BC1FL,           "bc1fl",        0x45020000, 0xFFFF0000, "%O"          },
  { I_BC1T,            "bc1t",         0x45010000, 0xFFFF0000, "%O"          },
  { I_BC1TL,           "bc1tl",        0x45030000, 0xFFFF0000, "%O"          },
  { I_C_F_S,           "c.f.s",        0x46000030, 0xFFE007FF, "%S, %T"      },
  { I_C_UN_S,          "c.un.s",       0x46000031, 0xFFE007FF, "%S, %T"      },
  { I_C_EQ_S,          "c.eq.s",       0x46000032, 0xFFE007FF, "%S, %T"      },
  { I_C_UEQ_S,         "c.ueq.s",      0x46000033, 0xFFE007FF, "%S, %T"      },
  { I_C_OLT_S,         "c.olt.s",      0x46000034, 0xFFE007FF, "%S, %T"      },
  { I_C_ULT_S,         "c.ult.s",      0x46000035, 0xFFE007FF, "%S, %T"      },
  { I_C_OLE_S,         "c.ole.s",      0x46000036, 0xFFE007FF, "%S, %T"      },
  { I_C_ULE_S,         "c.ule.s",      0x46000037, 0xFFE007FF, "%S, %T"      },
  { I_C_SF_S,          "c.sf.s",       0x46000038, 0xFFE007FF, "%S, %T"      },
  { I_C_NGLE_S,        "c.ngle.s",     0x46000039, 0xFFE007FF, "%S, %T"      },
  { I_C_SEQ_S,         "c.seq.s",      0x4600003A, 0xFFE007FF, "%S, %T"      },
  { I_C_NGL_S,         "c.ngl.s",      0x4600003B, 0xFFE007FF, "%S, %T"      },
  { I_C_LT_S,          "c.lt.s",       0x4600003C, 0xFFE007FF, "%S, %T"      },
  { I_C_NGE_S,         "c.nge.s",      0x4600003D, 0xFFE007FF, "%S, %T"      },
  { I_C_LE_S,          "c.le.s",       0x4600003E, 0xFFE007FF, "%S, %T"      },
  { I_C_NGT_S,         "c.ngt.s",      0x4600003F, 0xFFE007FF, "%S, %T"      },
  { I_CEIL_W_S,        "ceil.w.s",     0x4600000E, 0xFFFF003F, "%D, %S"      },
  { I_CFC1,            "cfc1",         0x44400000, 0xFFE007FF, "%t, %p"      },
  { I_CTC1,            "ctc1",         0x44c00000, 0xFFE007FF, "%t, %p"      },
  { I_CVT_S_W,         "cvt.s.w",      0x46800020, 0xFFFF003F, "%D, %S"      },
  { I_CVT_W_S,         "cvt.w.s",      0x46000024, 0xFFFF003F, "%D, %S"      },
  { I_DIV_S,           "div.s",        0x46000003, 0xFFE0003F, "%D, %S, %T"  },
  { I_FLOOR_W_S,       "floor.w.s",    0x4600000F, 0xFFFF003F, "%D, %S"      },
  { I_LWC1,            "lwc1",         0xc4000000, 0xFC000000, "%T, %o"      },
  { I_MFC1,            "mfc1",         0x44000000, 0xFFE007FF, "%t, %1"      },
  { I_MOV_S,           "mov.s",        0x46000006, 0xFFFF003F, "%D, %S"      },
  { I_MTC1,            "mtc1",         0x44800000, 0xFFE007FF, "%t, %1"      },
  { I_MUL_S,           "mul.s",        0x46000002, 0xFFE0003F, "%D, %S, %T"  },
  { I_NEG_S,           "neg.s",        0x46000007, 0xFFFF003F, "%D, %S"      },
  { I_ROUND_W_S,       "round.w.s",    0x4600000C, 0xFFFF003F, "%D, %S"      },
  { I_SQRT_S,          "sqrt.s",       0x46000004, 0xFFFF003F, "%D, %S"      },
  { I_SUB_S,           "sub.s",        0x46000001, 0xFFE0003F, "%D, %S, %T"  },
  { I_SWC1,            "swc1",         0xe4000000, 0xFC000000, "%T, %o"      },
  { I_TRUNC_W_S,       "trunc.w.s",    0x4600000D, 0xFFFF003F, "%D, %S"      },

        /* VPU instructions */
  { I_BVF,             "bvf",         0x49000000, 0xFFE30000, "%Zc, %O"         }, /* [hlide] %Z -> %Zc */
  { I_BVFL,            "bvfl",        0x49020000, 0xFFE30000, "%Zc, %O"         }, /* [hlide] %Z -> %Zc */
  { I_BVT,             "bvt",         0x49010000, 0xFFE30000, "%Zc, %O"         }, /* [hlide] %Z -> %Zc */
  { I_BVTL,            "bvtl",        0x49030000, 0xFFE30000, "%Zc, %O"         }, /* [hlide] %Z -> %Zc */
  { I_LV_Q,            "lv.q",        0xD8000000, 0xFC000002, "%Xq, %Y"         },
  { I_LV_S,            "lv.s",        0xC8000000, 0xFC000000, "%Xs, %Y"         },
  { I_LVL_Q,           "lvl.q",       0xD4000000, 0xFC000002, "%Xq, %Y"         },
  { I_LVR_Q,           "lvr.q",       0xD4000002, 0xFC000002, "%Xq, %Y"         },
  { I_MFV,             "mfv",         0x48600000, 0xFFE0FF80, "%t, %zs"         }, /* [hlide] added "%t, %zs" */
  { I_MFVC,            "mfvc",        0x48600000, 0xFFE0FF00, "%t, %2d"         }, /* [hlide] added "%t, %2d" */
  { I_MTV,             "mtv",         0x48E00000, 0xFFE0FF80, "%t, %zs"         }, /* [hlide] added "%t, %zs" */
  { I_MTVC,            "mtvc",        0x48E00000, 0xFFE0FF00, "%t, %2d"         }, /* [hlide] added "%t, %2d" */
  { I_SV_Q,            "sv.q",        0xF8000000, 0xFC000002, "%Xq, %Y"         },
  { I_SV_S,            "sv.s",        0xE8000000, 0xFC000000, "%Xs, %Y"         },
  { I_SVL_Q,           "svl.q",       0xF4000000, 0xFC000002, "%Xq, %Y"         },
  { I_SVR_Q,           "svr.q",       0xF4000002, 0xFC000002, "%Xq, %Y"         },
  { I_VABS_P,          "vabs.p",      0xD0010080, 0xFFFF8080, "%zp, %yp"        },
  { I_VABS_Q,          "vabs.q",      0xD0018080, 0xFFFF8080, "%zq, %yq"        },
  { I_VABS_S,          "vabs.s",      0xD0010000, 0xFFFF8080, "%zs, %ys"        },
  { I_VABS_T,          "vabs.t",      0xD0018000, 0xFFFF8080, "%zt, %yt"        },
  { I_VADD_P,          "vadd.p",      0x60000080, 0xFF808080, "%zp, %yp, %xp"   },
  { I_VADD_Q,          "vadd.q",      0x60008080, 0xFF808080, "%zq, %yq, %xq"   },
  { I_VADD_S,          "vadd.s",      0x60000000, 0xFF808080, "%zs, %ys, %xs"   }, /* [hlide] %yz -> %ys */
  { I_VADD_T,          "vadd.t",      0x60008000, 0xFF808080, "%zt, %yt, %xt"   },
  { I_VASIN_P,         "vasin.p",     0xD0170080, 0xFFFF8080, "%zp, %yp"        },
  { I_VASIN_Q,         "vasin.q",     0xD0178080, 0xFFFF8080, "%zq, %yq"        },
  { I_VASIN_S,         "vasin.s",     0xD0170000, 0xFFFF8080, "%zs, %ys"        },
  { I_VASIN_T,         "vasin.t",     0xD0178000, 0xFFFF8080, "%zt, %yt"        },
  { I_VAVG_P,          "vavg.p",      0xD0470080, 0xFFFF8080, "%zp, %yp"        },
  { I_VAVG_Q,          "vavg.q",      0xD0478080, 0xFFFF8080, "%zq, %yq"        },
  { I_VAVG_T,          "vavg.t",      0xD0478000, 0xFFFF8080, "%zt, %yt"        },
  { I_VBFY1_P,         "vbfy1.p",     0xD0420080, 0xFFFF8080, "%zp, %yp"        },
  { I_VBFY1_Q,         "vbfy1.q",     0xD0428080, 0xFFFF8080, "%zq, %yq"        },
  { I_VBFY2_Q,         "vbfy2.q",     0xD0438080, 0xFFFF8080, "%zq, %yq"        },
  { I_VCMOVF_P,        "vcmovf.p",    0xD2A80080, 0xFFF88080, "%zp, %yp, %v3"   }, /* [hlide] added "%zp, %yp, %v3" */
  { I_VCMOVF_Q,        "vcmovf.q",    0xD2A88080, 0xFFF88080, "%zq, %yq, %v3"   }, /* [hlide] added "%zq, %yq, %v3" */
  { I_VCMOVF_S,        "vcmovf.s",    0xD2A80000, 0xFFF88080, "%zs, %ys, %v3"   }, /* [hlide] added "%zs, %ys, %v3" */
  { I_VCMOVF_T,        "vcmovf.t",    0xD2A88000, 0xFFF88080, "%zt, %yt, %v3"   }, /* [hlide] added "%zt, %yt, %v3" */
  { I_VCMOVT_P,        "vcmovt.p",    0xD2A00080, 0xFFF88080, "%zp, %yp, %v3"   }, /* [hlide] added "%zp, %yp, %v3" */
  { I_VCMOVT_Q,        "vcmovt.q",    0xD2A08080, 0xFFF88080, "%zq, %yq, %v3"   }, /* [hlide] added "%zq, %yq, %v3" */
  { I_VCMOVT_S,        "vcmovt.s",    0xD2A00000, 0xFFF88080, "%zs, %ys, %v3"   }, /* [hlide] added "%zs, %ys, %v3" */
  { I_VCMOVT_T,        "vcmovt.t",    0xD2A08000, 0xFFF88080, "%zt, %yt, %v3"   }, /* [hlide] added "%zt, %yt, %v3" */
  { I_VCMP_P,          "vcmp.p",      0x6C000080, 0xFFFFFFF0, "%Zn"             }, /* [hlide] added "%Zn" */
  { I_VCMP_P,          "vcmp.p",      0x6C000080, 0xFFFF80F0, "%Zn, %yp"        }, /* [hlide] added "%Zn, %xp" */
  { I_VCMP_P,          "vcmp.p",      0x6C000080, 0xFF8080F0, "%Zn, %yp, %xp"   }, /* [hlide] added "%Zn, %zp, %xp" */
  { I_VCMP_Q,          "vcmp.q",      0x6C008080, 0xFFFFFFF0, "%Zn"             }, /* [hlide] added "%Zn" */
  { I_VCMP_Q,          "vcmp.q",      0x6C008080, 0xFFFF80F0, "%Zn, %yq"        }, /* [hlide] added "%Zn, %yq" */
  { I_VCMP_Q,          "vcmp.q",      0x6C008080, 0xFF8080F0, "%Zn, %yq, %xq"   }, /* [hlide] added "%Zn, %yq, %xq" */
  { I_VCMP_S,          "vcmp.s",      0x6C000000, 0xFFFFFFF0, "%Zn"             }, /* [hlide] added "%Zn" */
  { I_VCMP_S,          "vcmp.s",      0x6C000000, 0xFFFF80F0, "%Zn, %ys"        }, /* [hlide] added "%Zn, %ys" */
  { I_VCMP_S,          "vcmp.s",      0x6C000000, 0xFF8080F0, "%Zn, %ys, %xs"   }, /* [hlide] added "%Zn, %ys, %xs" */
  { I_VCMP_T,          "vcmp.t",      0x6C008000, 0xFFFFFFF0, "%Zn"             }, /* [hlide] added "%zp" */
  { I_VCMP_T,          "vcmp.t",      0x6C008000, 0xFFFF80F0, "%Zn, %yt"        }, /* [hlide] added "%Zn, %yt" */
  { I_VCMP_T,          "vcmp.t",      0x6C008000, 0xFF8080F0, "%Zn, %yt, %xt"   }, /* [hlide] added "%Zn, %yt, %xt" */
  { I_VCOS_P,          "vcos.p",      0xD0130080, 0xFFFF8080, "%zp, %yp"        },
  { I_VCOS_Q,          "vcos.q",      0xD0138080, 0xFFFF8080, "%zq, %yq"        },
  { I_VCOS_S,          "vcos.s",      0xD0130000, 0xFFFF8080, "%zs, %ys"        },
  { I_VCOS_T,          "vcos.t",      0xD0138000, 0xFFFF8080, "%zt, %yt"        },
  { I_VCRS_T,          "vcrs.t",      0x66808000, 0xFF808080, "%zt, %yt, %xt"   },
  { I_VCRSP_T,         "vcrsp.t",     0xF2808000, 0xFF808080, "%zt, %yt, %xt"   },
  { I_VCST_P,          "vcst.p",      0xD0600080, 0xFFE0FF80, "%zp, %vk"        }, /* [hlide] "%zp, %yp, %xp" -> "%zp, %vk" */
  { I_VCST_Q,          "vcst.q",      0xD0608080, 0xFFE0FF80, "%zq, %vk"        }, /* [hlide] "%zq, %yq, %xq" -> "%zq, %vk" */
  { I_VCST_S,          "vcst.s",      0xD0600000, 0xFFE0FF80, "%zs, %vk"        }, /* [hlide] "%zs, %ys, %xs" -> "%zs, %vk" */
  { I_VCST_T,          "vcst.t",      0xD0608000, 0xFFE0FF80, "%zt, %vk"        }, /* [hlide] "%zt, %yt, %xt" -> "%zt, %vk" */
  { I_VDET_P,          "vdet.p",      0x67000080, 0xFF808080, "%zs, %yp, %xp"   },
  { I_VDIV_P,          "vdiv.p",      0x63800080, 0xFF808080, "%zp, %yp, %xp"   },
  { I_VDIV_Q,          "vdiv.q",      0x63808080, 0xFF808080, "%zq, %yq, %xq"   },
  { I_VDIV_S,          "vdiv.s",      0x63800000, 0xFF808080, "%zs, %ys, %xs"   }, /* [hlide] %yz -> %ys */
  { I_VDIV_T,          "vdiv.t",      0x63808000, 0xFF808080, "%zt, %yt, %xt"   },
  { I_VDOT_P,          "vdot.p",      0x64800080, 0xFF808080, "%zs, %yp, %xp"   },
  { I_VDOT_Q,          "vdot.q",      0x64808080, 0xFF808080, "%zs, %yq, %xq"   },
  { I_VDOT_T,          "vdot.t",      0x64808000, 0xFF808080, "%zs, %yt, %xt"   },
  { I_VEXP2_P,         "vexp2.p",     0xD0140080, 0xFFFF8080, "%zp, %yp"        },
  { I_VEXP2_Q,         "vexp2.q",     0xD0148080, 0xFFFF8080, "%zq, %yq"        },
  { I_VEXP2_S,         "vexp2.s",     0xD0140000, 0xFFFF8080, "%zs, %ys"        },
  { I_VEXP2_T,         "vexp2.t",     0xD0148000, 0xFFFF8080, "%zt, %yt"        },
  { I_VF2H_P,          "vf2h.p",      0xD0320080, 0xFFFF8080, "%zs, %yp"        }, /* [hlide] %zp -> %zs */
  { I_VF2H_Q,          "vf2h.q",      0xD0328080, 0xFFFF8080, "%zp, %yq"        }, /* [hlide] %zq -> %zp */
  { I_VF2ID_P,         "vf2id.p",     0xD2600080, 0xFFE08080, "%zp, %yp, %v5"   }, /* [hlide] added "%zp, %yp, %v5" */
  { I_VF2ID_Q,         "vf2id.q",     0xD2608080, 0xFFE08080, "%zq, %yq, %v5"   }, /* [hlide] added "%zq, %yq, %v5" */
  { I_VF2ID_S,         "vf2id.s",     0xD2600000, 0xFFE08080, "%zs, %ys, %v5"   }, /* [hlide] added "%zs, %ys, %v5" */
  { I_VF2ID_T,         "vf2id.t",     0xD2608000, 0xFFE08080, "%zt, %yt, %v5"   }, /* [hlide] added "%zt, %yt, %v5" */
  { I_VF2IN_P,         "vf2in.p",     0xD2000080, 0xFFE08080, "%zp, %yp, %v5"   }, /* [hlide] added "%zp, %yp, %v5" */
  { I_VF2IN_Q,         "vf2in.q",     0xD2008080, 0xFFE08080, "%zq, %yq, %v5"   }, /* [hlide] added "%zq, %yq, %v5" */
  { I_VF2IN_S,         "vf2in.s",     0xD2000000, 0xFFE08080, "%zs, %ys, %v5"   }, /* [hlide] added "%zs, %ys, %v5" */
  { I_VF2IN_T,         "vf2in.t",     0xD2008000, 0xFFE08080, "%zt, %yt, %v5"   }, /* [hlide] added "%zt, %yt, %v5" */
  { I_VF2IU_P,         "vf2iu.p",     0xD2400080, 0xFFE08080, "%zp, %yp, %v5"   }, /* [hlide] added "%zp, %yp, %v5" */
  { I_VF2IU_Q,         "vf2iu.q",     0xD2408080, 0xFFE08080, "%zq, %yq, %v5"   }, /* [hlide] added "%zq, %yq, %v5" */
  { I_VF2IU_S,         "vf2iu.s",     0xD2400000, 0xFFE08080, "%zs, %ys, %v5"   }, /* [hlide] added "%zs, %ys, %v5" */
  { I_VF2IU_T,         "vf2iu.t",     0xD2408000, 0xFFE08080, "%zt, %yt, %v5"   }, /* [hlide] added "%zt, %yt, %v5" */
  { I_VF2IZ_P,         "vf2iz.p",     0xD2200080, 0xFFE08080, "%zp, %yp, %v5"   }, /* [hlide] added "%zp, %yp, %v5" */
  { I_VF2IZ_Q,         "vf2iz.q",     0xD2208080, 0xFFE08080, "%zq, %yq, %v5"   }, /* [hlide] added "%zq, %yq, %v5" */
  { I_VF2IZ_S,         "vf2iz.s",     0xD2200000, 0xFFE08080, "%zs, %ys, %v5"   }, /* [hlide] added "%zs, %ys, %v5" */
  { I_VF2IZ_T,         "vf2iz.t",     0xD2208000, 0xFFE08080, "%zt, %yt, %v5"   }, /* [hlide] added "%zt, %yt, %v5" */
  { I_VFAD_P,          "vfad.p",      0xD0460080, 0xFFFF8080, "%zp, %yp"        },
  { I_VFAD_Q,          "vfad.q",      0xD0468080, 0xFFFF8080, "%zq, %yq"        },
  { I_VFAD_T,          "vfad.t",      0xD0468000, 0xFFFF8080, "%zt, %yt"        },
  { I_VFIM_S,          "vfim.s",      0xDF800000, 0xFF800000, "%xs, %vh"        }, /* [hlide] added "%xs, %vh" */
  { I_VFLUSH,          "vflush",      0xFFFF040D, 0xFFFFFFFF, ""                },
  { I_VH2F_P,          "vh2f.p",      0xD0330080, 0xFFFF8080, "%zq, %yp"        }, /* [hlide] %zp -> %zq */
  { I_VH2F_S,          "vh2f.s",      0xD0330000, 0xFFFF8080, "%zp, %ys"        }, /* [hlide] %zs -> %zp */
  { I_VHDP_P,          "vhdp.p",      0x66000080, 0xFF808080, "%zs, %yp, %xp"   }, /* [hlide] added "%zs, %yp, %xp" */
  { I_VHDP_Q,          "vhdp.q",      0x66008080, 0xFF808080, "%zs, %yq, %xq"   }, /* [hlide] added "%zs, %yq, %xq" */
  { I_VHDP_T,          "vhdp.t",      0x66008000, 0xFF808080, "%zs, %yt, %xt"   }, /* [hlide] added "%zs, %yt, %xt" */
  { I_VHTFM2_P,        "vhtfm2.p",    0xF0800000, 0xFF808080, "%zp, %ym, %xp"   }, /* [hlide] added "%zp, %ym, %xp" */
  { I_VHTFM3_T,        "vhtfm3.t",    0xF1000080, 0xFF808080, "%zt, %yn, %xt"   }, /* [hlide] added "%zt, %yn, %xt" */
  { I_VHTFM4_Q,        "vhtfm4.q",    0xF1808000, 0xFF808080, "%zq, %yo, %xq"   }, /* [hlide] added "%zq, %yo, %xq" */
  { I_VI2C_Q,          "vi2c.q",      0xD03D8080, 0xFFFF8080, "%zs, %yq"        }, /* [hlide] added "%zs, %yq" */
  { I_VI2F_P,          "vi2f.p",      0xD2800080, 0xFFE08080, "%zp, %yp, %v5"   }, /* [hlide] added "%zp, %yp, %v5" */
  { I_VI2F_Q,          "vi2f.q",      0xD2808080, 0xFFE08080, "%zq, %yq, %v5"   }, /* [hlide] added "%zq, %yq, %v5" */
  { I_VI2F_S,          "vi2f.s",      0xD2800000, 0xFFE08080, "%zs, %ys, %v5"   }, /* [hlide] added "%zs, %ys, %v5" */
  { I_VI2F_T,          "vi2f.t",      0xD2808000, 0xFFE08080, "%zt, %yt, %v5"   }, /* [hlide] added "%zt, %yt, %v5" */
  { I_VI2S_P,          "vi2s.p",      0xD03F0080, 0xFFFF8080, "%zs, %yp"        }, /* [hlide] added "%zs, %yp" */
  { I_VI2S_Q,          "vi2s.q",      0xD03F8080, 0xFFFF8080, "%zp, %yq"        }, /* [hlide] added "%zp, %yq" */
  { I_VI2UC_Q,         "vi2uc.q",     0xD03C8080, 0xFFFF8080, "%zq, %yq"        }, /* [hlide] %zp -> %zq */
  { I_VI2US_P,         "vi2us.p",     0xD03E0080, 0xFFFF8080, "%zq, %yq"        }, /* [hlide] %zp -> %zq */
  { I_VI2US_Q,         "vi2us.q",     0xD03E8080, 0xFFFF8080, "%zq, %yq"        }, /* [hlide] %zp -> %zq */
  { I_VIDT_P,          "vidt.p",      0xD0030080, 0xFFFFFF80, "%zp"             },
  { I_VIDT_Q,          "vidt.q",      0xD0038080, 0xFFFFFF80, "%zq"             },
  { I_VIIM_S,          "viim.s",      0xDF000000, 0xFF800000, "%xs, %vi"        }, /* [hlide] added "%xs, %vi" */
  { I_VLGB_S,          "vlgb.s",      0xD0370000, 0xFFFF8080, "%zs, %ys"        },
  { I_VLOG2_P,         "vlog2.p",     0xD0150080, 0xFFFF8080, "%zp, %yp"        },
  { I_VLOG2_Q,         "vlog2.q",     0xD0158080, 0xFFFF8080, "%zq, %yq"        },
  { I_VLOG2_S,         "vlog2.s",     0xD0150000, 0xFFFF8080, "%zs, %ys"        },
  { I_VLOG2_T,         "vlog2.t",     0xD0158000, 0xFFFF8080, "%zt, %yt"        },
  { I_VMAX_P,          "vmax.p",      0x6D800080, 0xFF808080, "%zp, %yp, %xp"   },
  { I_VMAX_Q,          "vmax.q",      0x6D808080, 0xFF808080, "%zq, %yq, %xq"   },
  { I_VMAX_S,          "vmax.s",      0x6D800000, 0xFF808080, "%zs, %ys, %xs"   },
  { I_VMAX_T,          "vmax.t",      0x6D808000, 0xFF808080, "%zt, %yt, %xt"   },
  { I_VMFVC,           "vmfvc",       0xD0500000, 0xFFFF0080, "%zs, %2s"        }, /* [hlide] added "%zs, %2s" */
  { I_VMIDT_P,         "vmidt.p",     0xF3830080, 0xFFFFFF80, "%zm"             }, /* [hlide] %zp -> %zm */
  { I_VMIDT_Q,         "vmidt.q",     0xF3838080, 0xFFFFFF80, "%zo"             }, /* [hlide] %zq -> %zo */
  { I_VMIDT_T,         "vmidt.t",     0xF3838000, 0xFFFFFF80, "%zn"             }, /* [hlide] %zt -> %zn */
  { I_VMIN_P,          "vmin.p",      0x6D000080, 0xFF808080, "%zp, %yp, %xp"   },
  { I_VMIN_Q,          "vmin.q",      0x6D008080, 0xFF808080, "%zq, %yq, %xq"   },
  { I_VMIN_S,          "vmin.s",      0x6D000000, 0xFF808080, "%zs, %ys, %xs"   },
  { I_VMIN_T,          "vmin.t",      0x6D008000, 0xFF808080, "%zt, %yt, %xt"   },
  { I_VMMOV_P,         "vmmov.p",     0xF3800080, 0xFFFF8080, "%zm, %ym"        }, /* [hlide] added "%zm, %ym" */
  { I_VMMOV_Q,         "vmmov.q",     0xF3808080, 0xFFFF8080, "%zo, %yo"        },
  { I_VMMOV_T,         "vmmov.t",     0xF3808000, 0xFFFF8080, "%zn, %yn"        }, /* [hlide] added "%zn, %yn" */
  { I_VMMUL_P,         "vmmul.p",     0xF0000080, 0xFF808080, "%?%zm, %ym, %xm"   }, /* [hlide] added "%?%zm, %ym, %xm" */
  { I_VMMUL_Q,         "vmmul.q",     0xF0008080, 0xFF808080, "%?%zo, %yo, %xo"   },
  { I_VMMUL_T,         "vmmul.t",     0xF0008000, 0xFF808080, "%?%zn, %yn, %xn"   }, /* [hlide] added "%?%zn, %yn, %xn" */
  { I_VMONE_P,         "vmone.p",     0xF3870080, 0xFFFFFF80, "%zp"             },
  { I_VMONE_Q,         "vmone.q",     0xF3878080, 0xFFFFFF80, "%zq"             },
  { I_VMONE_T,         "vmone.t",     0xF3878000, 0xFFFFFF80, "%zt"             },
  { I_VMOV_P,          "vmov.p",      0xD0000080, 0xFFFF8080, "%zp, %yp"        },
  { I_VMOV_Q,          "vmov.q",      0xD0008080, 0xFFFF8080, "%zq, %yq"        },
  { I_VMOV_S,          "vmov.s",      0xD0000000, 0xFFFF8080, "%zs, %ys"        },
  { I_VMOV_T,          "vmov.t",      0xD0008000, 0xFFFF8080, "%zt, %yt"        },
  { I_VMSCL_P,         "vmscl.p",     0xF2000080, 0xFF808080, "%zm, %ym, %xs"   }, /* [hlide] %zp, %yp, %xp -> %zm, %ym, %xs */
  { I_VMSCL_Q,         "vmscl.q",     0xF2008080, 0xFF808080, "%zo, %yo, %xs"   }, /* [hlide] %zq, %yq, %xp -> %zo, %yo, %xs */
  { I_VMSCL_T,         "vmscl.t",     0xF2008000, 0xFF808080, "%zn, %yn, %xs"   }, /* [hlide] %zt, %yt, %xp -> %zn, %yn, %xs */
  { I_VMTVC,           "vmtvc",       0xD0510000, 0xFFFF8000, "%2d, %ys"        }, /* [hlide] added "%2d, %ys" */
  { I_VMUL_P,          "vmul.p",      0x64000080, 0xFF808080, "%zp, %yp, %xp"   },
  { I_VMUL_Q,          "vmul.q",      0x64008080, 0xFF808080, "%zq, %yq, %xq"   },
  { I_VMUL_S,          "vmul.s",      0x64000000, 0xFF808080, "%zs, %ys, %xs"   },
  { I_VMUL_T,          "vmul.t",      0x64008000, 0xFF808080, "%zt, %yt, %xt"   },
  { I_VMZERO_P,        "vmzero.p",    0xF3860080, 0xFFFFFF80, "%zm"             }, /* [hlide] %zp -> %zm */
  { I_VMZERO_Q,        "vmzero.q",    0xF3868080, 0xFFFFFF80, "%zo"             }, /* [hlide] %zq -> %zo */
  { I_VMZERO_T,        "vmzero.t",    0xF3868000, 0xFFFFFF80, "%zn"             }, /* [hlide] %zt -> %zn */
  { I_VNEG_P,          "vneg.p",      0xD0020080, 0xFFFF8080, "%zp, %yp"        },
  { I_VNEG_Q,          "vneg.q",      0xD0028080, 0xFFFF8080, "%zq, %yq"        },
  { I_VNEG_S,          "vneg.s",      0xD0020000, 0xFFFF8080, "%zs, %ys"        },
  { I_VNEG_T,          "vneg.t",      0xD0028000, 0xFFFF8080, "%zt, %yt"        },
  { I_VNOP,            "vnop",        0xFFFF0000, 0xFFFFFFFF, ""                },
  { I_VNRCP_P,         "vnrcp.p",     0xD0180080, 0xFFFF8080, "%zp, %yp"        },
  { I_VNRCP_Q,         "vnrcp.q",     0xD0188080, 0xFFFF8080, "%zq, %yq"        },
  { I_VNRCP_S,         "vnrcp.s",     0xD0180000, 0xFFFF8080, "%zs, %ys"        },
  { I_VNRCP_T,         "vnrcp.t",     0xD0188000, 0xFFFF8080, "%zt, %yt"        },
  { I_VNSIN_P,         "vnsin.p",     0xD01A0080, 0xFFFF8080, "%zp, %yp"        },
  { I_VNSIN_Q,         "vnsin.q",     0xD01A8080, 0xFFFF8080, "%zq, %yq"        },
  { I_VNSIN_S,         "vnsin.s",     0xD01A0000, 0xFFFF8080, "%zs, %ys"        },
  { I_VNSIN_T,         "vnsin.t",     0xD01A8000, 0xFFFF8080, "%zt, %yt"        },
  { I_VOCP_P,          "vocp.p",      0xD0440080, 0xFFFF8080, "%zp, %yp"        },
  { I_VOCP_Q,          "vocp.q",      0xD0448080, 0xFFFF8080, "%zq, %yq"        },
  { I_VOCP_S,          "vocp.s",      0xD0440000, 0xFFFF8080, "%zs, %ys"        },
  { I_VOCP_T,          "vocp.t",      0xD0448000, 0xFFFF8080, "%zt, %yt"        },
  { I_VONE_P,          "vone.p",      0xD0070080, 0xFFFFFF80, "%zp"             },
  { I_VONE_Q,          "vone.q",      0xD0078080, 0xFFFFFF80, "%zq"             },
  { I_VONE_S,          "vone.s",      0xD0070000, 0xFFFFFF80, "%zs"             },
  { I_VONE_T,          "vone.t",      0xD0078000, 0xFFFFFF80, "%zt"             },
  { I_VPFXD,           "vpfxd",       0xDE000000, 0xFF000000, "[%vp4, %vp5, %vp6, %vp7]"   }, /* [hlide] added "[%vp4, %vp5, %vp6, %vp7]" */
  { I_VPFXS,           "vpfxs",       0xDC000000, 0xFF000000, "[%vp0, %vp1, %vp2, %vp3]"   }, /* [hlide] added "[%vp0, %vp1, %vp2, %vp3]" */
  { I_VPFXT,           "vpfxt",       0xDD000000, 0xFF000000, "[%vp0, %vp1, %vp2, %vp3]"   }, /* [hlide] added "[%vp0, %vp1, %vp2, %vp3]" */
  { I_VQMUL_Q,         "vqmul.q",     0xF2808080, 0xFF808080, "%zq, %yq, %xq"   }, /* [hlide] added "%zq, %yq, %xq" */
  { I_VRCP_P,          "vrcp.p",      0xD0100080, 0xFFFF8080, "%zp, %yp"        },
  { I_VRCP_Q,          "vrcp.q",      0xD0108080, 0xFFFF8080, "%zq, %yq"        },
  { I_VRCP_S,          "vrcp.s",      0xD0100000, 0xFFFF8080, "%zs, %ys"        },
  { I_VRCP_T,          "vrcp.t",      0xD0108000, 0xFFFF8080, "%zt, %yt"        },
  { I_VREXP2_P,        "vrexp2.p",    0xD01C0080, 0xFFFF8080, "%zp, %yp"        },
  { I_VREXP2_Q,        "vrexp2.q",    0xD01C8080, 0xFFFF8080, "%zq, %yq"        },
  { I_VREXP2_S,        "vrexp2.s",    0xD01C0000, 0xFFFF8080, "%zs, %ys"        },
  { I_VREXP2_T,        "vrexp2.t",    0xD01C8000, 0xFFFF8080, "%zt, %yt"        },
  { I_VRNDF1_P,        "vrndf1.p",    0xD0220080, 0xFFFFFF80, "%zp"             },
  { I_VRNDF1_Q,        "vrndf1.q",    0xD0228080, 0xFFFFFF80, "%zq"             },
  { I_VRNDF1_S,        "vrndf1.s",    0xD0220000, 0xFFFFFF80, "%zs"             },
  { I_VRNDF1_T,        "vrndf1.t",    0xD0228000, 0xFFFFFF80, "%zt"             },
  { I_VRNDF2_P,        "vrndf2.p",    0xD0230080, 0xFFFFFF80, "%zp"             },
  { I_VRNDF2_Q,        "vrndf2.q",    0xD0238080, 0xFFFFFF80, "%zq"             },
  { I_VRNDF2_S,        "vrndf2.s",    0xD0230000, 0xFFFFFF80, "%zs"             },
  { I_VRNDF2_T,        "vrndf2.t",    0xD0238000, 0xFFFFFF80, "%zt"             },
  { I_VRNDI_P,         "vrndi.p",     0xD0210080, 0xFFFFFF80, "%zp"             },
  { I_VRNDI_Q,         "vrndi.q",     0xD0218080, 0xFFFFFF80, "%zq"             },
  { I_VRNDI_S,         "vrndi.s",     0xD0210000, 0xFFFFFF80, "%zs"             },
  { I_VRNDI_T,         "vrndi.t",     0xD0218000, 0xFFFFFF80, "%zt"             },
  { I_VRNDS_S,         "vrnds.s",     0xD0200000, 0xFFFF80FF, "%ys"             },
  { I_VROT_P,          "vrot.p",      0xF3A00080, 0xFFE08080, "%zp, %ys, %vr"   }, /* [hlide] added "%zp, %ys, %vr" */
  { I_VROT_Q,          "vrot.q",      0xF3A08080, 0xFFE08080, "%zq, %ys, %vr"   }, /* [hlide] added "%zq, %ys, %vr" */
  { I_VROT_T,          "vrot.t",      0xF3A08000, 0xFFE08080, "%zt, %ys, %vr"   }, /* [hlide] added "%zt, %ys, %vr" */
  { I_VRSQ_P,          "vrsq.p",      0xD0110080, 0xFFFF8080, "%zp, %yp"        },
  { I_VRSQ_Q,          "vrsq.q",      0xD0118080, 0xFFFF8080, "%zq, %yq"        },
  { I_VRSQ_S,          "vrsq.s",      0xD0110000, 0xFFFF8080, "%zs, %ys"        },
  { I_VRSQ_T,          "vrsq.t",      0xD0118000, 0xFFFF8080, "%zt, %yt"        },
  { I_VS2I_P,          "vs2i.p",      0xD03B0080, 0xFFFF8080, "%zq, %yp"        }, /* [hlide] %zp -> %zq */
  { I_VS2I_S,          "vs2i.s",      0xD03B0000, 0xFFFF8080, "%zp, %ys"        }, /* [hlide] %zs -> %zp */
  { I_VSAT0_P,         "vsat0.p",     0xD0040080, 0xFFFF8080, "%zp, %yp"        },
  { I_VSAT0_Q,         "vsat0.q",     0xD0048080, 0xFFFF8080, "%zq, %yq"        },
  { I_VSAT0_S,         "vsat0.s",     0xD0040000, 0xFFFF8080, "%zs, %ys"        },
  { I_VSAT0_T,         "vsat0.t",     0xD0048000, 0xFFFF8080, "%zt, %yt"        },
  { I_VSAT1_P,         "vsat1.p",     0xD0050080, 0xFFFF8080, "%zp, %yp"        },
  { I_VSAT1_Q,         "vsat1.q",     0xD0058080, 0xFFFF8080, "%zq, %yq"        },
  { I_VSAT1_S,         "vsat1.s",     0xD0050000, 0xFFFF8080, "%zs, %ys"        },
  { I_VSAT1_T,         "vsat1.t",     0xD0058000, 0xFFFF8080, "%zt, %yt"        },
  { I_VSBN_S,          "vsbn.s",      0x61000000, 0xFF808080, "%zs, %ys, %xs"   },
  { I_VSBZ_S,          "vsbz.s",      0xD0360000, 0xFFFF8080, "%zs, %ys"        },
  { I_VSCL_P,          "vscl.p",      0x65000080, 0xFF808080, "%zp, %yp, %xs"   }, /* [hlide] %xp -> %xs */
  { I_VSCL_Q,          "vscl.q",      0x65008080, 0xFF808080, "%zq, %yq, %xs"   }, /* [hlide] %xq -> %xs */
  { I_VSCL_T,          "vscl.t",      0x65008000, 0xFF808080, "%zt, %yt, %xs"   }, /* [hlide] %xt -> %xs */
  { I_VSCMP_P,         "vscmp.p",     0x6E800080, 0xFF808080, "%zp, %yp, %xp"   },
  { I_VSCMP_Q,         "vscmp.q",     0x6E808080, 0xFF808080, "%zq, %yq, %xq"   },
  { I_VSCMP_S,         "vscmp.s",     0x6E800000, 0xFF808080, "%zs, %ys, %xs"   },
  { I_VSCMP_T,         "vscmp.t",     0x6E808000, 0xFF808080, "%zt, %yt, %xt"   },
  { I_VSGE_P,          "vsge.p",      0x6F000080, 0xFF808080, "%zp, %yp, %xp"   },
  { I_VSGE_Q,          "vsge.q",      0x6F008080, 0xFF808080, "%zq, %yq, %xq"   },
  { I_VSGE_S,          "vsge.s",      0x6F000000, 0xFF808080, "%zs, %ys, %xs"   },
  { I_VSGE_T,          "vsge.t",      0x6F008000, 0xFF808080, "%zt, %yt, %xt"   },
  { I_VSGN_P,          "vsgn.p",      0xD04A0080, 0xFFFF8080, "%zp, %yp"        },
  { I_VSGN_Q,          "vsgn.q",      0xD04A8080, 0xFFFF8080, "%zq, %yq"        },
  { I_VSGN_S,          "vsgn.s",      0xD04A0000, 0xFFFF8080, "%zs, %ys"        },
  { I_VSGN_T,          "vsgn.t",      0xD04A8000, 0xFFFF8080, "%zt, %yt"        },
  { I_VSIN_P,          "vsin.p",      0xD0120080, 0xFFFF8080, "%zp, %yp"        },
  { I_VSIN_Q,          "vsin.q",      0xD0128080, 0xFFFF8080, "%zq, %yq"        },
  { I_VSIN_S,          "vsin.s",      0xD0120000, 0xFFFF8080, "%zs, %ys"        },
  { I_VSIN_T,          "vsin.t",      0xD0128000, 0xFFFF8080, "%zt, %yt"        },
  { I_VSLT_P,          "vslt.p",      0x6F800080, 0xFF808080, "%zp, %yp, %xp"   },
  { I_VSLT_Q,          "vslt.q",      0x6F808080, 0xFF808080, "%zq, %yq, %xq"   },
  { I_VSLT_S,          "vslt.s",      0x6F800000, 0xFF808080, "%zs, %ys, %xs"   },
  { I_VSLT_T,          "vslt.t",      0x6F808000, 0xFF808080, "%zt, %yt, %xt"   },
  { I_VSOCP_P,         "vsocp.p",     0xD0450080, 0xFFFF8080, "%zq, %yp"        }, /* [hlide] %zp -> %zq */
  { I_VSOCP_S,         "vsocp.s",     0xD0450000, 0xFFFF8080, "%zp, %ys"        }, /* [hlide] %zs -> %zp */
  { I_VSQRT_P,         "vsqrt.p",     0xD0160080, 0xFFFF8080, "%zp, %yp"        },
  { I_VSQRT_Q,         "vsqrt.q",     0xD0168080, 0xFFFF8080, "%zq, %yq"        },
  { I_VSQRT_S,         "vsqrt.s",     0xD0160000, 0xFFFF8080, "%zs, %ys"        },
  { I_VSQRT_T,         "vsqrt.t",     0xD0168000, 0xFFFF8080, "%zt, %yt"        },
  { I_VSRT1_Q,         "vsrt1.q",     0xD0408080, 0xFFFF8080, "%zq, %yq"        },
  { I_VSRT2_Q,         "vsrt2.q",     0xD0418080, 0xFFFF8080, "%zq, %yq"        },
  { I_VSRT3_Q,         "vsrt3.q",     0xD0488080, 0xFFFF8080, "%zq, %yq"        },
  { I_VSRT4_Q,         "vsrt4.q",     0xD0498080, 0xFFFF8080, "%zq, %yq"        },
  { I_VSUB_P,          "vsub.p",      0x60800080, 0xFF808080, "%zp, %yp, %xp"   },
  { I_VSUB_Q,          "vsub.q",      0x60808080, 0xFF808080, "%zq, %yq, %xq"   },
  { I_VSUB_S,          "vsub.s",      0x60800000, 0xFF808080, "%zs, %ys, %xs"   },
  { I_VSUB_T,          "vsub.t",      0x60808000, 0xFF808080, "%zt, %yt, %xt"   },
  { I_VSYNC,           "vsync",       0xFFFF0320, 0xFFFFFFFF, ""                },
  { I_VSYNC,           "vsync",       0xFFFF0000, 0xFFFF0000, "%I"              },
  { I_VT4444_Q,        "vt4444.q",    0xD0598080, 0xFFFF8080, "%zq, %yq"        }, /* [hlide] %zq -> %zp */
  { I_VT5551_Q,        "vt5551.q",    0xD05A8080, 0xFFFF8080, "%zq, %yq"        }, /* [hlide] %zq -> %zp */
  { I_VT5650_Q,        "vt5650.q",    0xD05B8080, 0xFFFF8080, "%zq, %yq"        }, /* [hlide] %zq -> %zp */
  { I_VTFM2_P,         "vtfm2.p",     0xF0800080, 0xFF808080, "%zp, %ym, %xp"   }, /* [hlide] added "%zp, %ym, %xp" */
  { I_VTFM3_T,         "vtfm3.t",     0xF1008000, 0xFF808080, "%zt, %yn, %xt"   }, /* [hlide] added "%zt, %yn, %xt" */
  { I_VTFM4_Q,         "vtfm4.q",     0xF1808080, 0xFF808080, "%zq, %yo, %xq"   }, /* [hlide] added "%zq, %yo, %xq" */
  { I_VUS2I_P,         "vus2i.p",     0xD03A0080, 0xFFFF8080, "%zq, %yp"        }, /* [hlide] added "%zq, %yp" */
  { I_VUS2I_S,         "vus2i.s",     0xD03A0000, 0xFFFF8080, "%zp, %ys"        }, /* [hlide] added "%zp, %ys" */
  { I_VWB_Q,           "vwb.q",       0xF8000002, 0xFC000002, "%Xq, %Y"         },
  { I_VWBN_S,          "vwbn.s",      0xD3000000, 0xFF008080, "%zs, %xs, %I"    },
  { I_VZERO_P,         "vzero.p",     0xD0060080, 0xFFFFFF80, "%zp"             },
  { I_VZERO_Q,         "vzero.q",     0xD0068080, 0xFFFFFF80, "%zq"             },
  { I_VZERO_S,         "vzero.s",     0xD0060000, 0xFFFFFF80, "%zs"             },
  { I_VZERO_T,         "vzero.t",     0xD0068000, 0xFFFFFF80, "%zt"             },
  { I_MFVME,           "mfvme",       0x68000000, 0xFC000000, "%t, %i"          },
  { I_MTVME,           "mtvme",       0xb0000000, 0xFC000000, "%t, %i"          },
};

static char buffer[1024];
static const char *gpr_names[] =
{
  "zr", "at", "v0", "v1", "a0", "a1", "a2", "a3",
  "t0", "t1", "t2", "t3", "t4", "t5", "t6", "t7",
  "s0", "s1", "s2", "s3", "s4", "s5", "s6", "s7",
  "t8", "t9", "k0", "k1", "gp", "sp", "fp", "ra"
};

static const char *cop0_regs[] =
{
  "COP0_0", "COP0_1", "COP0_2", "COP0_3", "COP0_4", "COP0_5", "COP0_6", "COP0_7",
  "BadVaddr", "Count", "COP0_10", "Compare", "Status", "Cause", "EPC", "PrID",
  "Config", "COP0_17", "COP0_18", "COP0_19", "COP0_20", "COP0_21", "COP0_22", "COP0_23",
  "COP0_24", "EBase", "COP0_26", "COP0_27", "TagLo", "TagHi", "ErrorPC", "COP0_31"
};


static const char *debug_regs[] =
{
  "DRCNTL", "DEPC", "DDATA0", "DDATA1", "IBC", "DBC", "Debug06", "Debug07",
  "IBA", "IBAM", "Debug10", "Debug11", "DBA", "DBAM", "DBD", "DBDM",
  "Debug16", "Debug17", "Debug18", "Debug19", "Debug20", "Debug21", "Debug22", "Debug23",
  "Debug24", "Debug25", "Debug26", "Debug27", "Debug28", "Debug29", "Debug30", "Debug31"
};

static const char *vfpu_cond_names[] =
{
  "FL",  "EQ",  "LT",  "LE",
  "TR",  "NE",  "GE",  "GT",
  "EZ",  "EN",  "EI",  "ES",
  "NZ",  "NN",  "NI",  "NS"
};

static const char *vfpu_constants[] = {
  "VFPU_CONST0",
  "VFPU_HUGE",
  "VFPU_SQRT2",
  "VFPU_SQRT1_2",
  "VFPU_2_SQRTPI",
  "VFPU_2_PI",
  "VFPU_1_PI",
  "VFPU_PI_4",
  "VFPU_PI_2",
  "VFPU_PI",
  "VFPU_E",
  "VFPU_LOG2E",
  "VFPU_LOG10E",
  "VFPU_LN2",
  "VFPU_LN10",
  "VFPU_2PI",
  "VFPU_PI_6",
  "VFPU_LOG10TWO",
  "VFPU_LOG2TEN",
  "VFPU_SQRT3_2",
  "VFPU_CONST20",
  "VFPU_CONST21",
  "VFPU_CONST22",
  "VFPU_CONST23",
  "VFPU_CONST24",
  "VFPU_CONST25",
  "VFPU_CONST26",
  "VFPU_CONST27",
  "VFPU_CONST28",
  "VFPU_CONST29",
  "VFPU_CONST30",
  "VFPU_CONST31",
};

static const char *pfx_cst_names[] =
{
  "0",  "1",  "2",  "1/2",  "3",  "1/3",  "1/4",  "1/6"
};

static const char *pfx_swz_names[] =
{
  "x",  "y",  "z",  "w"
};

static const char *pfx_sat_names[] =
{
  "",  "[0:1]",  "",  "[-1:1]"
};

/* [hlide] added vfpu_extra_regs */
static const char *vfpu_extra_regs[] =
{
  "VFPU_PFXS",
  "VFPU_PFXT",
  "VFPU_PFXD",
  "VFPU_CC ",
  "VFPU_INF4",
  NULL,
  NULL,
  "VFPU_REV",
  "VFPU_RCX0",
  "VFPU_RCX1",
  "VFPU_RCX2",
  "VFPU_RCX3",
  "VFPU_RCX4",
  "VFPU_RCX5",
  "VFPU_RCX6",
  "VFPU_RCX7"
};

static
int print_vfpu_single (int reg, char *output)
{
  return sprintf (output, "S%d%d%d", (reg >> 2) & 7, reg & 3, (reg >> 5) & 3);
}

static
int print_vfpu_reg (int reg, int offset, char one, char two, char *output)
{
  if ((reg >> 5) & 1) {
    return sprintf (output, "%c%d%d%d", two, (reg >> 2) & 7, offset, reg & 3);
  } else {
    return sprintf (output, "%c%d%d%d", one, (reg >> 2) & 7, reg & 3, offset);
  }
}

static
int print_vfpu_quad (int reg, char *output)
{
  return print_vfpu_reg (reg, 0, 'C', 'R', output);
}

static
int print_vfpu_pair (int reg, char *output)
{
  if ((reg >> 6) & 1) {
    return print_vfpu_reg (reg, 2, 'C', 'R', output);
  } else {
    return print_vfpu_reg (reg, 0, 'C', 'R', output);
  }
}

static
int print_vfpu_triple (int reg, char *output)
{
  if ((reg >> 6) & 1) {
    return print_vfpu_reg (reg, 1, 'C', 'R', output);
  } else {
    return print_vfpu_reg (reg, 0, 'C', 'R', output);
  }
}

static
int print_vfpu_mpair (int reg, char *output)
{
  if ((reg >> 6) & 1) {
    return print_vfpu_reg (reg, 2, 'M', 'E', output);
  } else {
    return print_vfpu_reg (reg, 0, 'M', 'E', output);
  }
}

static
int print_vfpu_mtriple (int reg, char *output)
{
  if ((reg >> 6) & 1) {
    return print_vfpu_reg (reg, 1, 'M', 'E', output);
  } else {
    return print_vfpu_reg (reg, 0, 'M', 'E', output);
  }
}

static
int print_vfpu_matrix (int reg, char *output)
{
  return print_vfpu_reg (reg, 0, 'M', 'E', output);
}

static
int print_vfpu_register (int reg, char type, char *output)
{
  switch (type) {
  case 's': return print_vfpu_single (reg, output);
  case 'q': return print_vfpu_quad (reg, output);
  case 'p': return print_vfpu_pair (reg, output);
  case 't': return print_vfpu_triple (reg, output);
  case 'm': return print_vfpu_mpair (reg, output);
  case 'n': return print_vfpu_mtriple (reg, output);
  case 'o': return print_vfpu_matrix (reg, output);
  };

  return 0;
}

static
int print_vfpu_halffloat (int l, char *output)
{
  unsigned short float16 = l & 0xFFFF;
  unsigned int sign = (float16 >> VFPU_SH_FLOAT16_SIGN) & VFPU_MASK_FLOAT16_SIGN;
  int exponent = (float16 >> VFPU_SH_FLOAT16_EXP) & VFPU_MASK_FLOAT16_EXP;
  unsigned int fraction = float16 & VFPU_MASK_FLOAT16_FRAC;
  char signchar = '+' + ((sign == 1) * 2);
  int len;
  /* Convert a VFPU 16-bit floating-point number to IEEE754. */
  union float2int
  {
    unsigned int i;
    float f;
  } float2int;

  if (exponent == VFPU_FLOAT16_EXP_MAX) {
    if (fraction == 0)
      len = sprintf (output, "%cInf", signchar);
    else
      len = sprintf (output, "%cNaN", signchar);
  } else if (exponent == 0 && fraction == 0) {
    len = sprintf (output, "%c0", signchar);
  } else {
    if (exponent == 0) {
      do {
        fraction <<= 1;
        exponent--;
      } while (!(fraction & (VFPU_MASK_FLOAT16_FRAC + 1)));

      fraction &= VFPU_MASK_FLOAT16_FRAC;
    }

    /* Convert to 32-bit single-precision IEEE754. */
    float2int.i = sign << 31;
    float2int.i |= (exponent + 112) << 23;
    float2int.i |= fraction << 13;
    len = sprintf (output, "%g", float2int.f);
  }

  return len;
}

/* [hlide] added print_vfpu_prefix */
static
int print_vfpu_prefix (int l, unsigned int pos, char *output)
{
  int len = 0;

  switch (pos)
  {
  case '0':
  case '1':
  case '2':
  case '3':
    {
      unsigned int base = '0';
      unsigned int negation = (l >> (pos - (base - VFPU_SH_PFX_NEG))) & VFPU_MASK_PFX_NEG;
      unsigned int constant = (l >> (pos - (base - VFPU_SH_PFX_CST))) & VFPU_MASK_PFX_CST;
      unsigned int abs_consthi = (l >> (pos - (base - VFPU_SH_PFX_ABS_CSTHI))) & VFPU_MASK_PFX_ABS_CSTHI;
      unsigned int swz_constlo = (l >> ((pos - base) * 2)) & VFPU_MASK_PFX_SWZ_CSTLO;

      if (negation)
        len = sprintf (output, "-");
      if (constant) {
        len += sprintf (output + len, "%s", pfx_cst_names[(abs_consthi << 2) | swz_constlo]);
      } else {
        if (abs_consthi)
          len += sprintf (output + len, "|%s|", pfx_swz_names[swz_constlo]);
        else
          len += sprintf (output + len, "%s", pfx_swz_names[swz_constlo]);
      }
    }
    break;

  case '4':
  case '5':
  case '6':
  case '7':
    {
      unsigned int base = '4';
      unsigned int mask = (l >> (pos - (base - VFPU_SH_PFX_MASK))) & VFPU_MASK_PFX_MASK;
      unsigned int saturation = (l >> ((pos - base) * 2)) & VFPU_MASK_PFX_SAT;

      if (mask)
        len += sprintf (output, "m");
      else
        len += sprintf (output, "%s", pfx_sat_names[saturation]);
    }
    break;
  }

  return len;
}


static
int print_vfpu_rotator (int l, char *output)
{
  int len;

  const char *elements[4];

  unsigned int opcode = l & VFPU_MASK_OP_SIZE;
  unsigned int rotators = (l >> 16) & 0x1f;
  unsigned int opsize, rothi, rotlo, negation, i;

  /* Determine the operand size so we'll know how many elements to output. */
  if (opcode == VFPU_OP_SIZE_PAIR)
    opsize = 2;
  else if (opcode == VFPU_OP_SIZE_TRIPLE)
    opsize = 3;
  else
    opsize = (opcode == VFPU_OP_SIZE_QUAD) * 4;     /* Sanity check. */

  rothi = (rotators >> VFPU_SH_ROT_HI) & VFPU_MASK_ROT_HI;
  rotlo = (rotators >> VFPU_SH_ROT_LO) & VFPU_MASK_ROT_LO;
  negation = (rotators >> VFPU_SH_ROT_NEG) & VFPU_MASK_ROT_NEG;

  if (rothi == rotlo) {
    if (negation) {
      elements[0] = "-s";
      elements[1] = "-s";
      elements[2] = "-s";
      elements[3] = "-s";
    } else {
      elements[0] = "s";
      elements[1] = "s";
      elements[2] = "s";
      elements[3] = "s";
    }
  } else {
    elements[0] = "0";
    elements[1] = "0";
    elements[2] = "0";
    elements[3] = "0";
  }
  if (negation)
    elements[rothi] = "-s";
  else
    elements[rothi] = "s";
  elements[rotlo] = "c";

  len = sprintf (output, "[");
  for (i = 0;;) {
    len += sprintf (output + len, "%s", elements[i++]);
    if (i >= opsize)
      break;
    sprintf (output + len, " ,");
  }

  len += sprintf (output + len, "]");

  return len;
}

/* [hlide] added print_cop2 */
static
int print_cop2 (int reg, char *output)
{
  int len;

  if ((reg >= 128) && (reg < 128+16) && (vfpu_extra_regs[reg - 128])) {
    len = sprintf (output, "%s", vfpu_extra_regs[reg - 128]);
  } else {
    len = sprintf (output, "VFPU_COP2_%d", reg);
  }

  return len;
}


static
void decode_instruction (struct allegrex_instruction *insn, unsigned int opcode, unsigned int PC)
{
  int i = 0, len = 0, vmmul = 0;
  unsigned int data = opcode;
  len += sprintf (buffer, "\t0x%08X: 0x%08X '", PC, opcode);
  for (i = 0; i < 4; i++) {
    char c = (char) (data & 0xFF);
    if (isprint (c)) { len += sprintf (&buffer[len], "%c", c); }
    else { len += sprintf (&buffer[len], "."); }
    data >>= 8;
  }
  sprintf (&buffer[len], "' - %s                  ", insn->name);
  len = 44;

  i = 0;
  while (1) {
    char c = insn->fmt[i++];
    if (c == '\0') break;
    if (c == '%') {
      c = insn->fmt[i++];
      switch (c) {
      case 'd':
        len += sprintf (&buffer[len], "$%s", gpr_names[RD (opcode)]);
        break;
      case 'D':
        len += sprintf (&buffer[len], "$fpr%02d", FD (opcode));
        break;
      case 't':
        len += sprintf (&buffer[len], "$%s", gpr_names[RT (opcode)]);
        break;
      case 'T':
        len += sprintf (&buffer[len], "$fpr%02d", FT (opcode));
        break;
      case 's':
        len += sprintf (&buffer[len], "$%s", gpr_names[RS (opcode)]);
        break;
      case 'S':
        len += sprintf (&buffer[len], "$fpr%02d", FS (opcode));
        break;
      case 'J':
        len += sprintf (&buffer[len], "$%s", gpr_names[RS (opcode)]);
        break;
      case 'i':
        len += sprintf (&buffer[len], "%d", IMM (opcode));
        break;
      case 'I':
        len += sprintf (&buffer[len], "0x%04X", IMMU (opcode));
        break;
      case 'j':
        len += sprintf (&buffer[len], "0x%08X", JUMP (opcode, PC));
        break;
      case 'O':
        len += sprintf (&buffer[len], "0x%08X", PC + 4 + 4 * ((int) IMM (opcode)));
        break;
      case 'o':
        len += sprintf (&buffer[len], "%d($%s)", IMM (opcode), gpr_names[RS (opcode)]);
        break;
      case 'c':
        len += sprintf (&buffer[len], "0x%05X", CODE (opcode));
        break;
      case 'C':
        len += sprintf (&buffer[len], "0x%05X", CODE (opcode));
        break;
      case 'k':
        len += sprintf (&buffer[len], "0x%X", RT (opcode));
        break;
      case 'p':
        len += sprintf (&buffer[len], "$%d", RD (opcode));
        break;
      case 'a':
        len += sprintf (&buffer[len], "%d", SA (opcode));
        break;
      case 'r':
        len += sprintf (&buffer[len], "%s", debug_regs[RD (opcode)]);
        break;
      case '0':
        len += sprintf (&buffer[len], "%s", cop0_regs[RD (opcode)]);
        break;
      case '1':
        len += sprintf (&buffer[len], "$fcr%d", RD (opcode));
        break;
      case '2':
        c = insn->fmt[i++];
        if (c == 'd') {
          len += print_cop2 (VED (opcode), &buffer[len]);
        } else { /* 's'*/
          len += print_cop2 (VES (opcode), &buffer[len]);
        }
        break;
      case 'z':
        c = insn->fmt[i++];
        len += print_vfpu_register (VD (opcode), c, &buffer[len]);
        break;
      case 'Z':
        c = insn->fmt[i++];
        if (c == 'c') {
          len += sprintf (&buffer[len], "%d", VCC (opcode));
        } else { /* 'n' */
          len += sprintf (&buffer[len], "%s", vfpu_cond_names[VCN (opcode)]);
        }
        break;
      case 'n':
        c = insn->fmt[i++];
        if (c == 'e') {
          len += sprintf (&buffer[len], "%d", RD (opcode) + 1);
        } else { /* 'i' */
          len += sprintf (&buffer[len], "%d", RD (opcode) - SA (opcode) + 1);
        }
        break;
      case 'X':
        c = insn->fmt[i++];
        len += print_vfpu_register (VO (opcode), c, &buffer[len]);
        break;
      case 'x':
        c = insn->fmt[i++];
        len += print_vfpu_register (VT (opcode), c, &buffer[len]);
        break;
      case 'Y':
        len += sprintf (&buffer[len], "%d($%s)", IMM (opcode) & ~3, gpr_names[RS (opcode)]);
        break;
      case 'y':
        {
          int reg = VS (opcode);
          if (vmmul) { if (reg & 0x20) { reg &= 0x5F; } else { reg |= 0x20; } }
          c = insn->fmt[i++];
          len += print_vfpu_register (reg, c, &buffer[len]);
        }
        break;
      case 'v':
        c = insn->fmt[i++];
        switch (c) {
        case '3' : len += sprintf (&buffer[len], "%d", VI3 (opcode)); break;
        case '5' : len += sprintf (&buffer[len], "%d", VI5 (opcode)); break;
        case '8' : len += sprintf (&buffer[len], "%d", VI8 (opcode)); break;
        case 'k' : len += sprintf (&buffer[len], "%s", vfpu_constants [VI5 (opcode)]); break;
        case 'i' : len += sprintf (&buffer[len], "%d", IMM (opcode)); break;
        case 'h' : len += print_vfpu_halffloat (opcode, &buffer[len]); break;
        case 'r' : len += print_vfpu_rotator (opcode, &buffer[len]); break;
        case 'p' : c = insn->fmt[i++]; len += print_vfpu_prefix (opcode, c, &buffer[len]); break;
        }
        break;
      case '?':
        vmmul = 1;
        break;
      }
    } else {
      len += sprintf (&buffer[len], "%c", c);
    }
  }
  buffer[len] = '\0';
}

char *allegrex_disassemble (unsigned int opcode, unsigned int PC)
{
  int i;

  for (i = 0; i < sizeof (instructions) / sizeof (struct allegrex_instruction); i++) {
    if ((instructions[i].mask & opcode) == instructions[i].opcode) {
      decode_instruction (&instructions[i], opcode, PC);
      break;
    }
  }
  return (char *) buffer;
}

enum insn_type allegrex_insn_type (unsigned int opcode)
{
  int i;

  for (i = 0; i < sizeof (instructions) / sizeof (struct allegrex_instruction); i++) {
    if ((instructions[i].mask & opcode) == instructions[i].opcode) {
      return instructions[i].itype;
    }
  }
  return I_INVALID;
}

#ifdef TEST_DISASSEMBLE

#include <stdlib.h>

int main (int argc, char **argv)
{
  int i;

  for (i = 0; i < sizeof (instructions) / sizeof (struct allegrex_instruction); i++) {
    unsigned int opcode = rand ();
    opcode = (opcode & (~instructions[i].mask)) | instructions[i].opcode;
    printf ("%s\n", allegrex_disassemble (opcode, 4 * i));
  }

  return 0;
}

#endif /* TEST_DISASSEMBLE */