
#include "code.h"
#include "utils.h"

#define RT(op) ((op >> 16) & 0x1F)
#define RS(op) ((op >> 21) & 0x1F)
#define RD(op) ((op >> 11) & 0x1F)
#define SA(op) ((op >> 6)  & 0x1F)
#define IMM(op) ((signed short) (op & 0xFFFF))
#define IMMU(op) ((unsigned short) (op & 0xFFFF))

#define IS_REG_USED(flags, regno) ((1 << (regno)) & flags)
#define REG_USE(flags, regno) (flags) |= 1 << (regno)

static
struct operation *alloc_operation (struct subroutine *sub)
{
  struct operation *op;
  op = fixedpool_alloc (sub->code->opspool);
  op->operands = list_alloc (sub->code->lstpool);
  op->results = list_alloc (sub->code->lstpool);
  return op;
}

static
struct variable *alloc_variable (struct basicblock *block)
{
  struct variable *var;
  var = fixedpool_alloc (block->sub->code->varspool);
  var->uses = list_alloc (block->sub->code->lstpool);
  return var;
}

static
void reset_operation (struct subroutine *sub, struct operation *op)
{
  struct value *val;

  while (list_size (op->results)) {
    val = list_removehead (op->results);
    fixedpool_free (sub->code->valspool, val);
  }

  while (list_size (op->operands)) {
    val = list_removehead (op->operands);
    fixedpool_free (sub->code->valspool, val);
  }
}

static
void simplify_reg_zero (list l)
{
  struct value *val;
  element el;
  el = list_head (l);
  while (el) {
    val = element_getvalue (el);
    if (val->type == VAL_REGISTER && val->value == 0) {
      val->type = VAL_CONSTANT;
    }
    el = element_next (el);
  }
}

static
void simplify_operation (struct subroutine *sub, struct operation *op)
{
  struct value *val;

  if (op->type != OP_INSTRUCTION) return;

  if (list_size (op->results) == 1 && !(op->begin->insn->flags & (INSN_LOAD | INSN_JUMP))) {
    val = list_headvalue (op->results);
    if (val->value == 0) {
      reset_operation (sub, op);
      op->type = OP_NOP;
      return;
    }
  }
  simplify_reg_zero (op->results);
  simplify_reg_zero (op->operands);

  switch (op->insn) {
  case I_ADDU:
  case I_ADD:
  case I_OR:
  case I_XOR:
    val = list_headvalue (op->operands);
    if (val->value == 0) {
      val = list_removehead (op->operands);
      fixedpool_free (sub->code->valspool, val);
      op->type = OP_MOVE;
    } else {
      val = list_tailvalue (op->operands);
      if (val->value == 0) {
        val = list_removetail (op->operands);
        fixedpool_free (sub->code->valspool, val);
        op->type = OP_MOVE;
      }
    }
    break;
  case I_AND:
    val = list_headvalue (op->operands);
    if (val->value == 0) {
      val = list_removetail (op->operands);
      fixedpool_free (sub->code->valspool, val);
      op->type = OP_MOVE;
    } else {
      val = list_tailvalue (op->operands);
      if (val->value == 0) {
        val = list_removehead (op->operands);
        fixedpool_free (sub->code->valspool, val);
        op->type = OP_MOVE;
      }
    }

    break;
  case I_BITREV:
  case I_SEB:
  case I_SEH:
  case I_WSBH:
  case I_WSBW:
    val = list_headvalue (op->operands);
    if (val->value == 0) {
      op->type = OP_MOVE;
    }
    break;
  case I_SUB:
  case I_SUBU:
  case I_SLLV:
  case I_SRLV:
  case I_SRAV:
  case I_ROTV:
    val = list_tailvalue (op->operands);
    if (val->value == 0) {
      val = list_removetail (op->operands);
      fixedpool_free (sub->code->valspool, val);
      op->type = OP_MOVE;
    }
  case I_MOVN:
    val = element_getvalue (element_next (list_head (op->operands)));
    if (val->value == 0) {
      reset_operation (sub, op);
      op->type = OP_NOP;
    }
    break;
  case I_MOVZ:
    val = element_getvalue (element_next (list_head (op->operands)));
    if (val->value == 0) {
      val = list_removetail (op->operands);
      fixedpool_free (sub->code->valspool, val);
      val = list_removetail (op->operands);
      fixedpool_free (sub->code->valspool, val);
      op->type = OP_MOVE;
    }
    break;
  default:
    break;
  }

  return;
}

static
void append_value (struct subroutine *sub, list l, enum valuetype type, uint32 value, int inserthead)
{
  struct value *val;
  val = fixedpool_alloc (sub->code->valspool);
  val->type = type;
  val->value = value;
  if (inserthead)
    list_inserthead (l, val);
  else
    list_inserttail (l, val);
}

#define GLOBAL_GPR_DEF() \
  if (!IS_REG_USED (ggpr_defined, regno) && regno != 0) { \
    list_inserttail (wheredefined[regno], block);         \
  }                                                       \
  REG_USE (ggpr_defined, regno)

#define ASM_GPR_DEF() \
  if (!IS_REG_USED (gpr_defined, regno) && regno != 0) {           \
    append_value (sub, op->results, VAL_REGISTER, regno, FALSE);   \
  }                                                                \
  REG_USE (gpr_defined, regno)

#define ASM_GPR_USE() \
  if (!IS_REG_USED (gpr_used, regno) && regno != 0 &&              \
      !IS_REG_USED (gpr_defined, regno)) {                         \
    append_value (sub, op->operands, VAL_REGISTER, regno, FALSE);  \
  }                                                                \
  REG_USE (gpr_used, regno)

#define GLOBAL_HILO_DEF() \
  if (!IS_REG_USED (ghilo_defined, regno)) {                    \
    list_inserttail (wheredefined[REGISTER_LO + regno], block); \
  }                                                             \
  REG_USE (ghilo_defined, regno)

#define ASM_HILO_DEF() \
  if (!IS_REG_USED (hilo_defined, regno)) {                                    \
    append_value (sub, op->results, VAL_REGISTER, REGISTER_LO + regno, FALSE); \
  }                                                                            \
  REG_USE (hilo_defined, regno)

#define ASM_HILO_USE() \
  if (!IS_REG_USED (hilo_used, regno) &&                                        \
      !IS_REG_USED (hilo_defined, regno)) {                                     \
    append_value (sub, op->operands, VAL_REGISTER, REGISTER_LO + regno, FALSE); \
  }                                                                             \
  REG_USE (hilo_used, regno)

static
void extract_operations (struct subroutine *sub, list *wheredefined)
{
  struct operation *op;
  uint32 ggpr_defined = 0, ghilo_defined = 0;
  element el;

  el = list_head (sub->blocks);
  while (el) {
    struct basicblock *block = element_getvalue (el);
    block->operations = list_alloc (sub->code->lstpool);

    if (block->type == BLOCK_SIMPLE) {
      uint32 gpr_used, gpr_defined;
      uint32 hilo_used, hilo_defined;
      struct location *loc;
      int lastasm = FALSE;

      for (loc = block->val.simple.begin; ; loc++) {
        if (INSN_TYPE (loc->insn->flags) == INSN_ALLEGREX) {
          enum allegrex_insn insn;

          if (lastasm)
            list_inserttail (block->operations, op);
          lastasm = FALSE;

          op = alloc_operation (sub);
          op->type = OP_INSTRUCTION;
          op->begin = op->end = loc;

          switch (loc->insn->insn) {
          case I_ADDI:
            insn = I_ADD;
            append_value (sub, op->operands, VAL_CONSTANT, IMM (loc->opc), FALSE);
            break;
          case I_ADDIU:
            insn = I_ADDU;
            append_value (sub, op->operands, VAL_CONSTANT, IMM (loc->opc), FALSE);
            break;
          case I_ORI:
            insn = I_OR;
            append_value (sub, op->operands, VAL_CONSTANT, IMMU (loc->opc), FALSE);
            break;
          case I_XORI:
            insn = I_XOR;
            append_value (sub, op->operands, VAL_CONSTANT, IMMU (loc->opc), FALSE);
            break;
          case I_ANDI:
            insn = I_AND;
            append_value (sub, op->operands, VAL_CONSTANT, IMMU (loc->opc), FALSE);
            break;
          case I_LUI:
            op->type = OP_MOVE;
            append_value (sub, op->operands, VAL_CONSTANT, IMMU (loc->opc) << 16, FALSE);
            break;
          case I_SLTI:
            insn = I_SLT;
            append_value (sub, op->operands, VAL_CONSTANT, IMM (loc->opc), FALSE);
            break;
          case I_SLTIU:
            insn = I_SLTU;
            append_value (sub, op->operands, VAL_CONSTANT, IMM (loc->opc), FALSE);
            break;
          case I_EXT:
            insn = I_EXT;
            append_value (sub, op->operands, VAL_CONSTANT, SA (loc->opc), FALSE);
            append_value (sub, op->operands, VAL_CONSTANT, RD (loc->opc) + 1, FALSE);
            break;
          case I_INS:
            insn = I_INS;
            append_value (sub, op->operands, VAL_CONSTANT, SA (loc->opc), FALSE);
            append_value (sub, op->operands, VAL_CONSTANT, RD (loc->opc) - SA (loc->opc) + 1, FALSE);
            break;
          case I_ROTR:
            insn = I_ROTV;
            append_value (sub, op->operands, VAL_CONSTANT, SA (loc->opc), FALSE);
            break;
          case I_SLL:
            insn = I_SLLV;
            append_value (sub, op->operands, VAL_CONSTANT, SA (loc->opc), FALSE);
            break;
          case I_SRA:
            insn = I_SRAV;
            append_value (sub, op->operands, VAL_CONSTANT, SA (loc->opc), FALSE);
            break;
          case I_SRL:
            insn = I_SRLV;
            append_value (sub, op->operands, VAL_CONSTANT, SA (loc->opc), FALSE);
            break;
          case I_BEQL:
            insn = I_BEQ;
            break;
          case I_BGEZL:
            insn = I_BGEZ;
            break;
          case I_BGTZL:
            insn = I_BGTZ;
            break;
          case I_BLEZL:
            insn = I_BLEZ;
            break;
          case I_BLTZL:
            insn = I_BLTZ;
            break;
          case I_BLTZALL:
            insn = I_BLTZAL;
            break;
          case I_BNEL:
            insn = I_BNE;
            break;
          default:
            insn = loc->insn->insn;
          }
          op->insn = insn;

          if (loc->insn->flags & INSN_READ_HI) {
            append_value (sub, op->operands, VAL_REGISTER, REGISTER_HI, TRUE);
          }

          if (loc->insn->flags & INSN_READ_LO) {
            append_value (sub, op->operands, VAL_REGISTER, REGISTER_LO, TRUE);
          }

          if (loc->insn->flags & INSN_READ_GPR_D) {
            int regno = RD (loc->opc);
            append_value (sub, op->operands, VAL_REGISTER, regno, TRUE);
          }

          if (loc->insn->flags & INSN_READ_GPR_T) {
            int regno = RT (loc->opc);
            append_value (sub, op->operands, VAL_REGISTER, regno, TRUE);
          }

          if (loc->insn->flags & INSN_READ_GPR_S) {
            int regno = RS (loc->opc);
            append_value (sub, op->operands, VAL_REGISTER, regno, TRUE);
          }

          if (loc->insn->flags & INSN_WRITE_GPR_T) {
            int regno = RT (loc->opc);
            GLOBAL_GPR_DEF ();
            append_value (sub, op->results, VAL_REGISTER, regno, FALSE);
          }

          if (loc->insn->flags & INSN_WRITE_GPR_D) {
            int regno = RD (loc->opc);
            GLOBAL_GPR_DEF ();
            append_value (sub, op->results, VAL_REGISTER, regno, FALSE);
          }

          if (loc->insn->flags & INSN_WRITE_LO) {
            int regno = 0;
            GLOBAL_HILO_DEF ();
            append_value (sub, op->results, VAL_REGISTER, REGISTER_LO, FALSE);
          }

          if (loc->insn->flags & INSN_WRITE_HI) {
            int regno = 1;
            GLOBAL_HILO_DEF ();
            append_value (sub, op->results, VAL_REGISTER, REGISTER_HI, FALSE);
          }

          if (loc->insn->flags & INSN_LINK) {
            int regno = REGISTER_LINK;
            GLOBAL_GPR_DEF ();
            append_value (sub, op->results, VAL_REGISTER, regno, FALSE);
          }

          simplify_operation (sub, op);
          list_inserttail (block->operations, op);
        } else {
          if (!lastasm) {
            op = alloc_operation (sub);
            op->begin = op->end = loc;
            op->type = OP_ASM;
            hilo_used = hilo_defined = 0;
            gpr_used = gpr_defined = 0;
          } else {
            op->end = loc;
          }
          lastasm = TRUE;

          if (loc->insn->flags & INSN_READ_LO) {
            int regno = 0;
            ASM_HILO_USE ();
          }

          if (loc->insn->flags & INSN_READ_HI) {
            int regno = 1;
            ASM_HILO_USE ();
          }

          if (loc->insn->flags & INSN_READ_GPR_D) {
            int regno = RD (loc->opc);
            ASM_GPR_USE ();
          }

          if (loc->insn->flags & INSN_READ_GPR_T) {
            int regno = RT (loc->opc);
            ASM_GPR_USE ();
          }

          if (loc->insn->flags & INSN_READ_GPR_S) {
            int regno = RS (loc->opc);
            ASM_GPR_USE ();
          }

          if (loc->insn->flags & INSN_WRITE_GPR_T) {
            int regno = RT (loc->opc);
            ASM_GPR_DEF ();
            GLOBAL_GPR_DEF ();
          }

          if (loc->insn->flags & INSN_WRITE_GPR_D) {
            int regno = RD (loc->opc);
            ASM_GPR_DEF ();
            GLOBAL_GPR_DEF ();
          }

          if (loc->insn->flags & INSN_WRITE_LO) {
            int regno = 0;
            ASM_HILO_DEF ();
            GLOBAL_HILO_DEF ();
          }

          if (loc->insn->flags & INSN_WRITE_HI) {
            int regno = 1;
            ASM_HILO_DEF ();
            GLOBAL_HILO_DEF ();
          }

          if (loc->insn->flags & INSN_LINK) {
            int regno = REGISTER_LINK;
            ASM_GPR_DEF ();
            GLOBAL_GPR_DEF ();
          }
        }

        if (loc == block->val.simple.end) {
          if (lastasm)
            list_inserttail (block->operations, op);
          break;
        }
      }
    } else if (block->type == BLOCK_CALL) {
      op = alloc_operation (sub);
      op->type = OP_CALL;
      list_inserttail (block->operations, op);
    }

    el = element_next (el);
  }
}


static
void ssa_step1 (struct subroutine *sub, list *wheredefined)
{
  struct basicblock *block, *bref;
  element el, ref;
  int i, j;

  el = list_head (sub->blocks);
  while (el) {
    block = element_getvalue (el);
    block->mark1 = 0;
    block->mark2 = 0;
    el = element_next (el);
  }

  for (i = 1; i < NUM_REGISTERS; i++) {
    list worklist = wheredefined[i];
    el = list_head (worklist);
    while (el) {
      block = element_getvalue (el);
      block->mark1 = i;
      el = element_next (el);
    }

    while (list_size (worklist) != 0) {
      block = list_removehead (worklist);
      ref = list_head (block->node.frontier);
      while (ref) {
        bref = element_getvalue (ref);
        if (bref->mark2 != i) {
          struct operation *op;

          bref->mark2 = i;
          op = alloc_operation (sub);
          op->type = OP_PHI;
          append_value (sub, op->results, VAL_REGISTER, i, FALSE);
          for (j = 0; j < list_size (bref->inrefs); j++)
            append_value (sub, op->operands, VAL_REGISTER, i, FALSE);
          list_inserthead (bref->operations, op);

          if (bref->mark1 != i) {
            bref->mark1 = i;
            list_inserttail (worklist, bref);
          }
        }
        ref = element_next (ref);
      }
    }
  }
}

static
void ssa_search (struct basicblock *block, list *vars, int *count)
{
  element el, vel;
  int i, pushcount[NUM_REGISTERS];

  for (i = 1; i < NUM_REGISTERS; i++)
    pushcount[i] = 0;

  el = list_head (block->operations);
  while (el) {
    struct operation *op;
    struct variable *var;
    struct value *val;

    op = element_getvalue (el);

    if (op->type != OP_PHI) {
      vel = list_head (op->operands);
      while (vel) {
        val = element_getvalue (vel);
        if (val->type == VAL_REGISTER) {
          var = list_headvalue (vars[val->value]);
          val->type = VAL_VARIABLE;
          val->variable = var;
          list_inserttail (var->uses, op);
        }
        vel = element_next (vel);
      }
    }

    vel = list_head (op->results);
    while (vel) {
      val = element_getvalue (vel);
      if (val->type == VAL_REGISTER) {
        val->type = VAL_VARIABLE;
        val->variable = var = alloc_variable (block);
        var->count = ++count[val->value];
        var->name.type = VAL_REGISTER;
        var->name.value = val->value;
        var->def = op;
        list_inserthead (vars[val->value], var);
        list_inserttail (block->sub->variables, var);
        pushcount[val->value]++;
      }
      vel = element_next (vel);
    }

    el = element_next (el);
  }

  el = list_head (block->outrefs);
  while (el) {
    struct basicblock *ref = element_getvalue (el);
    ref->mark1 = 0;
    el = element_next (el);
  }

  el = list_head (block->outrefs);
  while (el) {
    struct basicblock *ref = element_getvalue (el);
    element phiel, bel, lel;
    int j;

    ref->mark1++;
    i = j = 0;
    bel = list_head (ref->inrefs);
    while (bel) {
      if (element_getvalue (bel) == block) {
        if (++j == ref->mark1) {
          break;
        }
      }
      i++;
      bel = element_next (bel);
    }

    phiel = list_head (ref->operations);
    while (phiel) {
      struct operation *op;
      struct value *val;

      op = element_getvalue (phiel);
      if (op->type != OP_PHI) break;

      lel = list_head (op->operands);
      for (j = 0; j < i; j++) {
        lel = element_next (lel);
      }
      val = element_getvalue (lel);
      val->type = VAL_VARIABLE;
      val->variable = list_headvalue (vars[val->value]);
      phiel = element_next (phiel);
    }
    el = element_next (el);
  }

  el = list_head (block->node.children);
  while (el) {
    struct basicblock *child = element_getvalue (el);
    ssa_search (child, vars, count);
    el = element_next (el);
  }

  for (i = 1; i < NUM_REGISTERS; i++) {
    while (pushcount[i]--) {
      list_removehead (vars[i]);
    }
  }

}

static
void ssa_step2 (struct subroutine *sub, list *regs)
{
  struct operation *op;
  int i, count[NUM_REGISTERS];

  op = alloc_operation (sub);
  op->type = OP_START;

  for (i = 1; i < NUM_REGISTERS; i++) {
    count[i] = 0;
    append_value (sub, op->results, VAL_REGISTER, i, FALSE);
  }

  list_inserttail (sub->startblock->operations, op);

  op = alloc_operation (sub);
  op->type = OP_END;
  for (i = 1; i < NUM_REGISTERS; i++) {
    append_value (sub, op->operands, VAL_REGISTER, i, FALSE);
  }

  list_inserttail (sub->endblock->operations, op);
  ssa_search (sub->startblock, regs, count);
}

void build_ssa (struct subroutine *sub)
{
  list reglist[NUM_REGISTERS];
  int i;

  for (i = 0; i < NUM_REGISTERS; i++) {
    reglist[i] = list_alloc (sub->code->lstpool);
  }

  sub->variables = list_alloc (sub->code->lstpool);

  extract_operations (sub, reglist);
  ssa_step1 (sub, reglist);
  ssa_step2 (sub, reglist);

  for (i = 0; i < NUM_REGISTERS; i++) {
    list_free (reglist[i]);
  }
}


