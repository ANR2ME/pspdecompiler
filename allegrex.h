/**
 * Author: Humberto Naves (hsnaves@gmail.com)
 */

#ifndef __ALLEGREX_H
#define __ALLEGREX_H

#define INSN_READ_GPR_S      0x00000001
#define INSN_READ_GPR_T      0x00000002
#define INSN_READ_GPR_D      0x00000004
#define INSN_READ_FPR_S      0x00000008
#define INSN_READ_FPR_T      0x00000010
#define INSN_READ_COND_CODE  0x00000020
#define INSN_READ_HI         0x00000040
#define INSN_READ_LO         0x00000080
#define INSN_WRITE_GPR_D     0x00000100
#define INSN_WRITE_GPR_T     0x00000200
#define INSN_WRITE_FPR_D     0x00000400
#define INSN_WRITE_FPR_T     0x00000800
#define INSN_WRITE_COND_CODE 0x00001000
#define INSN_WRITE_HI        0x00002000
#define INSN_WRITE_LO        0x00004000
#define INSN_JUMP            0x00008000
#define INSN_BRANCH          0x00010000
#define INSN_BRANCHLIKELY    0x00020000
#define INSN_LINK            0x00040000
#define INSN_LOAD            0x00080000
#define INSN_STORE           0x00100000

#define INSN_ALLEGREX        0x00000000
#define INSN_SPECIAL         0x01000000
#define INSN_DEBUG           0x02000000
#define INSN_COP0            0x03000000
#define INSN_FPU             0x04000000
#define INSN_VFPU            0x05000000

#define INSN_TYPE(flags)  ((flags) & 0x0F000000)

#define INSN_ALIAS           0x80000000

#define COMMAND_INDEX   1
#define COMMAND_BSEARCH 2
#define COMMAND_TEST    3
#define COMMAND_END     4

struct disasm_command {
	int type;
	unsigned int param1;
	unsigned int param2;
	unsigned int param3;
};

struct bsearch_index {
	int index;
	unsigned int opcode;
};

enum allegrex_insn {
  I_INVALID,
  I_ADD,
  I_ADDI,
  I_ADDIU,
  I_ADDU,
  I_AND,
  I_ANDI,
  I_BEQ,
  I_BEQL,
  I_BGEZ,
  I_BGEZAL,
  I_BGEZL,
  I_BGTZ,
  I_BGTZL,
  I_BITREV,
  I_BLEZ,
  I_BLEZL,
  I_BLTZ,
  I_BLTZL,
  I_BLTZAL,
  I_BLTZALL,
  I_BNE,
  I_BNEL,
  I_BREAK,
  I_CACHE,
  I_CFC0,
  I_CLO,
  I_CLZ,
  I_CTC0,
  I_MAX,
  I_MIN,
  I_DBREAK,
  I_DIV,
  I_DIVU,
  I_DRET,
  I_ERET,
  I_EXT,
  I_INS,
  I_J,
  I_JR,
  I_JALR,
  I_JAL,
  I_LB,
  I_LBU,
  I_LH,
  I_LHU,
  I_LL,
  I_LUI,
  I_LW,
  I_LWL,
  I_LWR,
  I_MADD,
  I_MADDU,
  I_MFC0,
  I_MFDR,
  I_MFHI,
  I_MFIC,
  I_MFLO,
  I_MOVN,
  I_MOVZ,
  I_MSUB,
  I_MSUBU,
  I_MTC0,
  I_MTDR,
  I_MTIC,
  I_HALT,
  I_MTHI,
  I_MTLO,
  I_MULT,
  I_MULTU,
  I_NOR,
  I_OR,
  I_ORI,
  I_ROTR,
  I_ROTV,
  I_SEB,
  I_SEH,
  I_SB,
  I_SC,
  I_SH,
  I_SLLV,
  I_SLL,
  I_SLT,
  I_SLTI,
  I_SLTIU,
  I_SLTU,
  I_SRA,
  I_SRAV,
  I_SRLV,
  I_SRL,
  I_SW,
  I_SWL,
  I_SWR,
  I_SUB,
  I_SUBU,
  I_SYNC,
  I_SYSCALL,
  I_XOR,
  I_XORI,
  I_WSBH,
  I_WSBW,
  I_ABS_S,
  I_ADD_S,
  I_BC1F,
  I_BC1FL,
  I_BC1T,
  I_BC1TL,
  I_C_F_S,
  I_C_UN_S,
  I_C_EQ_S,
  I_C_UEQ_S,
  I_C_OLT_S,
  I_C_ULT_S,
  I_C_OLE_S,
  I_C_ULE_S,
  I_C_SF_S,
  I_C_NGLE_S,
  I_C_SEQ_S,
  I_C_NGL_S,
  I_C_LT_S,
  I_C_NGE_S,
  I_C_LE_S,
  I_C_NGT_S,
  I_CEIL_W_S,
  I_CFC1,
  I_CTC1,
  I_CVT_S_W,
  I_CVT_W_S,
  I_DIV_S,
  I_FLOOR_W_S,
  I_LWC1,
  I_MFC1,
  I_MOV_S,
  I_MTC1,
  I_MUL_S,
  I_NEG_S,
  I_ROUND_W_S,
  I_SQRT_S,
  I_SUB_S,
  I_SWC1,
  I_TRUNC_W_S,
  I_BVF,
  I_BVFL,
  I_BVT,
  I_BVTL,
  I_LV_Q,
  I_LV_S,
  I_LVL_Q,
  I_LVR_Q,
  I_MFV,
  I_MFVC,
  I_MTV,
  I_MTVC,
  I_SV_Q,
  I_SV_S,
  I_SVL_Q,
  I_SVR_Q,
  I_VABS_P,
  I_VABS_Q,
  I_VABS_S,
  I_VABS_T,
  I_VADD_P,
  I_VADD_Q,
  I_VADD_S,
  I_VADD_T,
  I_VASIN_P,
  I_VASIN_Q,
  I_VASIN_S,
  I_VASIN_T,
  I_VAVG_P,
  I_VAVG_Q,
  I_VAVG_T,
  I_VBFY1_P,
  I_VBFY1_Q,
  I_VBFY2_Q,
  I_VCMOVF_P,
  I_VCMOVF_Q,
  I_VCMOVF_S,
  I_VCMOVF_T,
  I_VCMOVT_P,
  I_VCMOVT_Q,
  I_VCMOVT_S,
  I_VCMOVT_T,
  I_VCMP_P,
  I_VCMP_Q,
  I_VCMP_S,
  I_VCMP_T,
  I_VCOS_P,
  I_VCOS_Q,
  I_VCOS_S,
  I_VCOS_T,
  I_VCRS_T,
  I_VCRSP_T,
  I_VCST_P,
  I_VCST_Q,
  I_VCST_S,
  I_VCST_T,
  I_VDET_P,
  I_VDIV_P,
  I_VDIV_Q,
  I_VDIV_S,
  I_VDIV_T,
  I_VDOT_P,
  I_VDOT_Q,
  I_VDOT_T,
  I_VEXP2_P,
  I_VEXP2_Q,
  I_VEXP2_S,
  I_VEXP2_T,
  I_VF2H_P,
  I_VF2H_Q,
  I_VF2ID_P,
  I_VF2ID_Q,
  I_VF2ID_S,
  I_VF2ID_T,
  I_VF2IN_P,
  I_VF2IN_Q,
  I_VF2IN_S,
  I_VF2IN_T,
  I_VF2IU_P,
  I_VF2IU_Q,
  I_VF2IU_S,
  I_VF2IU_T,
  I_VF2IZ_P,
  I_VF2IZ_Q,
  I_VF2IZ_S,
  I_VF2IZ_T,
  I_VFAD_P,
  I_VFAD_Q,
  I_VFAD_T,
  I_VFIM_S,
  I_VFLUSH,
  I_VH2F_P,
  I_VH2F_S,
  I_VHDP_P,
  I_VHDP_Q,
  I_VHDP_T,
  I_VHTFM2_P,
  I_VHTFM3_T,
  I_VHTFM4_Q,
  I_VI2C_Q,
  I_VI2F_P,
  I_VI2F_Q,
  I_VI2F_S,
  I_VI2F_T,
  I_VI2S_P,
  I_VI2S_Q,
  I_VI2UC_Q,
  I_VI2US_P,
  I_VI2US_Q,
  I_VIDT_P,
  I_VIDT_Q,
  I_VIIM_S,
  I_VLGB_S,
  I_VLOG2_P,
  I_VLOG2_Q,
  I_VLOG2_S,
  I_VLOG2_T,
  I_VMAX_P,
  I_VMAX_Q,
  I_VMAX_S,
  I_VMAX_T,
  I_VMFVC,
  I_VMIDT_P,
  I_VMIDT_Q,
  I_VMIDT_T,
  I_VMIN_P,
  I_VMIN_Q,
  I_VMIN_S,
  I_VMIN_T,
  I_VMMOV_P,
  I_VMMOV_Q,
  I_VMMOV_T,
  I_VMMUL_P,
  I_VMMUL_Q,
  I_VMMUL_T,
  I_VMONE_P,
  I_VMONE_Q,
  I_VMONE_T,
  I_VMOV_P,
  I_VMOV_Q,
  I_VMOV_S,
  I_VMOV_T,
  I_VMSCL_P,
  I_VMSCL_Q,
  I_VMSCL_T,
  I_VMTVC,
  I_VMUL_P,
  I_VMUL_Q,
  I_VMUL_S,
  I_VMUL_T,
  I_VMZERO_P,
  I_VMZERO_Q,
  I_VMZERO_T,
  I_VNEG_P,
  I_VNEG_Q,
  I_VNEG_S,
  I_VNEG_T,
  I_VNOP,
  I_VNRCP_P,
  I_VNRCP_Q,
  I_VNRCP_S,
  I_VNRCP_T,
  I_VNSIN_P,
  I_VNSIN_Q,
  I_VNSIN_S,
  I_VNSIN_T,
  I_VOCP_P,
  I_VOCP_Q,
  I_VOCP_S,
  I_VOCP_T,
  I_VONE_P,
  I_VONE_Q,
  I_VONE_S,
  I_VONE_T,
  I_VPFXD,
  I_VPFXS,
  I_VPFXT,
  I_VQMUL_Q,
  I_VRCP_P,
  I_VRCP_Q,
  I_VRCP_S,
  I_VRCP_T,
  I_VREXP2_P,
  I_VREXP2_Q,
  I_VREXP2_S,
  I_VREXP2_T,
  I_VRNDF1_P,
  I_VRNDF1_Q,
  I_VRNDF1_S,
  I_VRNDF1_T,
  I_VRNDF2_P,
  I_VRNDF2_Q,
  I_VRNDF2_S,
  I_VRNDF2_T,
  I_VRNDI_P,
  I_VRNDI_Q,
  I_VRNDI_S,
  I_VRNDI_T,
  I_VRNDS_S,
  I_VROT_P,
  I_VROT_Q,
  I_VROT_T,
  I_VRSQ_P,
  I_VRSQ_Q,
  I_VRSQ_S,
  I_VRSQ_T,
  I_VS2I_P,
  I_VS2I_S,
  I_VSAT0_P,
  I_VSAT0_Q,
  I_VSAT0_S,
  I_VSAT0_T,
  I_VSAT1_P,
  I_VSAT1_Q,
  I_VSAT1_S,
  I_VSAT1_T,
  I_VSBN_S,
  I_VSBZ_S,
  I_VSCL_P,
  I_VSCL_Q,
  I_VSCL_T,
  I_VSCMP_P,
  I_VSCMP_Q,
  I_VSCMP_S,
  I_VSCMP_T,
  I_VSGE_P,
  I_VSGE_Q,
  I_VSGE_S,
  I_VSGE_T,
  I_VSGN_P,
  I_VSGN_Q,
  I_VSGN_S,
  I_VSGN_T,
  I_VSIN_P,
  I_VSIN_Q,
  I_VSIN_S,
  I_VSIN_T,
  I_VSLT_P,
  I_VSLT_Q,
  I_VSLT_S,
  I_VSLT_T,
  I_VSOCP_P,
  I_VSOCP_S,
  I_VSQRT_P,
  I_VSQRT_Q,
  I_VSQRT_S,
  I_VSQRT_T,
  I_VSRT1_Q,
  I_VSRT2_Q,
  I_VSRT3_Q,
  I_VSRT4_Q,
  I_VSUB_P,
  I_VSUB_Q,
  I_VSUB_S,
  I_VSUB_T,
  I_VSYNC,
  I_VT4444_Q,
  I_VT5551_Q,
  I_VT5650_Q,
  I_VTFM2_P,
  I_VTFM3_T,
  I_VTFM4_Q,
  I_VUS2I_P,
  I_VUS2I_S,
  I_VWB_Q,
  I_VWBN_S,
  I_VZERO_P,
  I_VZERO_Q,
  I_VZERO_S,
  I_VZERO_T,
  I_MFVME,
  I_MTVME
};

struct allegrex_instruction
{
  enum allegrex_insn insn;
  const char *name;
  unsigned int opcode;
  unsigned int mask;
  const char *fmt;
  unsigned int flags;
};

extern const char *gpr_names[];

char *allegrex_disassemble (unsigned int opcode, unsigned int PC, int prtall);
const struct allegrex_instruction *allegrex_decode (unsigned int opcode, int allowalias);

#endif /* __ALLEGREX_H */

