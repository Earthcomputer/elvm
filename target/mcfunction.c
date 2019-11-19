
#include <ir/ir.h>
#include <target/util.h>

#define SOA "scoreboard objectives add "
#define SPG "scoreboard players get "
#define SPS "scoreboard players set "
#define SPA "scoreboard players add "
#define SPR "scoreboard players remove"
#define SPO "scoreboard players operation "
#define DMS "data modify storage elvm:elvm "
#define DGS "data get storage elvm:elvm "
#define DRS "data modify remove "

#define MEM_PHI 27146105

#define MIN_INT32 (-2147483648)
#define MAX_INT32 2147483647

static const char* MCFUNCTION_REG_NAMES[7] = { "elvm_a", "elvm_b", "elvm_c", "elvm_d", "elvm_bp", "elvm_sp", "elvm_pc" };

static const char* prefix = "";

static void mcf_emit_line(const char* line, ...) {
  va_list ap;
  va_start(ap, line);
  emit_line("%s%s", prefix, line, ap);
  va_end(ap);
}

static void mcf_emit_function_header(const char *name) {
  emit_line("========= %s.mcfunction =========", name);
  emit_line("# Generated by ELVM");
}

static void mcf_emit_set_reg(const char *reg, Value *value) {
  if (value->type == REG)
    mcf_emit_line(SPO "ELVM %s = ELVM %s", reg, reg_names[value->reg]);
  else if (value->type == IMM)
    mcf_emit_line(SPS "ELVM %s %d", reg, value->imm);
  else
    error("invalid value");
}

static void mcf_emit_mem_table_store(Value *addr, Value *value) {
  mcf_emit_set_reg("elvm_mem_val", value);
  mcf_emit_set_reg("elvm_mem_addr", addr);
  if (addr->type == REG) {
    mcf_emit_set_reg("elvm_mem_idx", addr);
    mcf_emit_line(SPO "ELVM elvm_mem_idx *= ELVM elvm_mem_phi");
    mcf_emit_line(SPO "ELVM elvm_mem_idx %%= ELVM elvm_mem_len");
  } else {
    mcf_emit_line(SPS "ELVM elvm_mem_idx %d", (addr->imm * MEM_PHI) & 31);
  }
  mcf_emit_line(DMS "mem_tmp set value []");
  mcf_emit_line("function elvm:storemem");
}

static void mcf_emit_mem_table_load(Value* addr) {
  mcf_emit_set_reg("elvm_mem_addr", addr);
  if (addr->type == REG) {
    mcf_emit_set_reg("elvm_mem_idx", addr);
    mcf_emit_line(SPO "ELVM elvm_mem_idx *= ELVM elvm_mem_phi");
    mcf_emit_line(SPO "ELVM elvm_mem_idx %%= elvm_mem_len");
  } else {
    mcf_emit_line(SPS "ELVM elvm_mem_idx %d", (addr->imm * MEM_PHI) & 31);
  }
  mcf_emit_line(DMS "mem_tmp set value []");
  mcf_emit_line("function elvm:loadmem");
}

static void define_mem_func(const char* name, const char* shift_func, const char* safebin_func) {
  mcf_emit_function_header(format("elvm:%s", name));
  emit_line("execute if score ELVM elvm_mem_idx matches 0 run " DMS "bin_tmp set value []");
  emit_line("execute if score ELVM elvm_mem_idx matches 0 run function elvm:%s", safebin_func);
  emit_line("execute unless score ELVM elvm_mem_idx matches 0 run function elvm:%s", shift_func);
}

static void define_shiftmem_func(const char* name, const char* mem_func) {
  mcf_emit_function_header(format("elvm:%s", name));
  emit_line(DMS "mem_tmp append from storage elvm:elvm mem[0]");
  emit_line(DRS "mem[0]");
  emit_line(SPR "elvm_mem_idx 1");
  emit_line("function elvm:%s", mem_func);
  emit_line(DMS "mem prepend from storage elvm:elvm mem_tmp[-1]");
}

static void define_safebin_func(const char* name, const char* bin_func, const char* notfound_operation) {
  mcf_emit_function_header(format("elvm:%s", name));
  emit_line("execute if data storage elvm:elvm mem[0][0] run function %s", bin_func);
  emit_line("execute unless data storage elvm:elvm mem[0][0] ", notfound_operation);
}

static void define_bin_func(const char* name, const char* shift_func, const char* operation) {
  mcf_emit_function_header(format("elvm:%s", name));
  emit_line("execute store result score ELVM elvm_test_addr run " DGS "mem[0][0].a");
  emit_line("execute if score ELVM elvm_mem_addr = ELVM elvm_test_addr %s", operation);
  emit_line("execute unless score ELVM elvm_mem_addr = ELVM elvm_test_addr run function elvm:%s", shift_func);
}

static void define_shiftbin_func(const char* name, const char* safebin_func) {
  mcf_emit_function_header(format("elvm:%s", name));
  emit_line(DMS "bin_tmp append from storage elvm:elvm mem[0][0]");
  emit_line(DRS "mem[0][0]");
  emit_line("function elvm:%s", safebin_func);
  emit_line(DMS "mem[0] prepend from storage elvm:elvm bin_tmp[-1]");
}

static void define_storeval_func() {
  mcf_emit_function_header("elvm:storeval");
  emit_line(DMS "mem[0] append value {}");
  emit_line("execute store result storage elvm:elvm mem[0][0].a run " SPG "ELVM elvm_mem_addr");
  emit_line("execute store result storage elvm:elvm mem[0][0].v run " SPG "ELVM elvm_mem_val");
}


static void mcf_emit_test(Inst* inst, const char* cmd_when_false, const char* cmd_when_true) {
  bool inverted = 0;
  Op op = normalize_cond(inst->op, 0);
  if (op == JNE || op == JLT || op == JGT) {
    inverted = 1;
    op = normalize_cond(op, 1);
  }

  const char* if_ = inverted ? "unless" : "if";
  const char* unless = inverted ? "if" : "unless";
  if (inst->src.type == IMM) {
    const char* left_dots = op == JLE ? ".." : "";
    const char* right_dots = op == JGE ? ".." : "";
    mcf_emit_line("execute %s score ELVM %s matches %s%d%s run %s",
                  unless, reg_names[inst->dst.reg], left_dots, inst->src.imm, right_dots, cmd_when_false);
    mcf_emit_line("execute %s score ELVM %s matches %s%d%s run %s",
                  if_, reg_names[inst->dst.reg], left_dots, inst->src.imm, right_dots, cmd_when_true);
  } else {
    const char* op_str = op == JEQ ? "=" : op == JLE ? "<=" : ">=";
    mcf_emit_line("execute %s score ELVM %s %s ELVM %s run %s",
        unless, reg_names[inst->dst.reg], op_str, reg_names[inst->src.reg], cmd_when_false);
    mcf_emit_line("execute %s score ELVM %s %s ELVM %s run %s",
        if_, reg_names[inst->dst.reg], op_str, reg_names[inst->src.reg], cmd_when_true);
  }
}

static void mcf_emit_func_prologue(int func_id) {
  mcf_emit_function_header(format("elvm:func%d", func_id));
}

static void mcf_emit_func_epilogue() {
}

static void mcf_emit_pc_change(int pc) {
  prefix = format("execute if score ELVM elvm_pc matches %d run ", pc);
}

static void mcf_emit_inst(Inst* inst) {
  switch (inst->op) {
    case MOV: {
      mcf_emit_set_reg(reg_names[inst->dst.reg], &inst->src);
      break;
    }

    case ADD: {
      const char* dst = reg_names[inst->dst.reg];
      if (inst->src.type == IMM) {
        if (inst->src.imm & UINT_MAX) {
          if (inst->src.imm < 0)
            mcf_emit_line(SPR "ELVM %s %d", dst, -inst->src.imm);
          else
            mcf_emit_line(SPA "ELVM %s %d", dst, inst->src.imm);
        }
      } else {
        mcf_emit_line(SPO "ELVM %s += ELVM %s", dst, reg_names[inst->src.reg]);
      }
      mcf_emit_line(SPO "ELVM %s %%= ELVM elvm_uint_max", dst);
      mcf_emit_line(SPA "ELVM elvm_pc 1");
      break;
    }

    case SUB: {
      const char* dst = reg_names[inst->dst.reg];
      if (inst->src.type == IMM) {
        if (inst->src.imm & UINT_MAX) {
          if (inst->src.imm < 0)
            mcf_emit_line(SPA "ELVM %s %d", dst, -inst->src.imm);
          else
            mcf_emit_line(SPR "ELVM %s %d", dst, inst->src.imm);
        }
      } else {
        mcf_emit_line(SPO "ELVM %s -= ELVM %s", dst, reg_names[inst->src.reg]);
      }
      mcf_emit_line(SPO "ELVM %s %%= ELVM elvm_uint_max");
      mcf_emit_line(SPA "ELVM elvm_pc 1");
      break;
    }

    case LOAD: {
      mcf_emit_mem_table_load(&inst->src);
      mcf_emit_line(SPO "ELVM %s = ELVM elvm_mem_res", reg_names[inst->dst.reg]);
      mcf_emit_line(SPA "ELVM elvm_pc 1");
      break;
    }

    case STORE: {
      mcf_emit_mem_table_store(&inst->src, &inst->dst);
      mcf_emit_line(SPA "ELVM elvm_pc 1");
      break;
    }

    case EXIT: {
      mcf_emit_line(SPS "ELVM elvm_pc -1");
      break;
    }

    case PUTC: {
      /* TODO: implement */
      mcf_emit_line(SPA "ELVM elvm_pc 1");
      break;
    }

    case GETC: {
      /* TODO: implement */
      mcf_emit_line(SPA "ELVM elvm_pc 1");
      break;
    }

    case DUMP: {
      mcf_emit_line(SPA "ELVM elvm_pc 1");
      break;
    }

    case EQ:
    case NE:
    case LT:
    case LE:
    case GT:
    case GE: {
      mcf_emit_test(inst,
                    SPS "ELVM elvm_tmp 0",
                    SPS "ELVM elvm_tmp 1");
      mcf_emit_line(SPO "ELVM %s = ELVM elvm_tmp", reg_names[inst->dst.reg]);
      mcf_emit_line(SPA "ELVM elvm_pc 1");
      break;
    }

    case JMP: {
      mcf_emit_set_reg("elvm_pc", &inst->jmp);
      break;
    }

    case JEQ:
    case JNE:
    case JLT:
    case JLE:
    case JGT:
    case JGE: {
      if (inst->jmp.type == IMM)
        mcf_emit_test(inst, SPA "ELVM elvm_pc 1", format(SPS "ELVM elvm_pc %d", inst->jmp.imm));
      else
        mcf_emit_test(inst, SPA "ELVM elvm_pc 1", format(SPO "ELVM elvm_pc = ELVM %s", reg_names[inst->jmp.reg]));
      break;
    }

    default:
      error("oops");
  }
}


static void emit_main_function(Data* data) {

  mcf_emit_function_header("elvm:main");
  for (int i = 0; i < 7; i++)
    emit_line(SOA "%s dummy", reg_names[i]);

  emit_line(SOA "elvm_tmp dummy");

  emit_line(SOA "elvm_mem_phi dummy");
  emit_line(SPS "elvm_mem_phi %d", MEM_PHI);

  emit_line(SOA "elvm_mem_len dummy");
  emit_line(SPS "elvm_mem_len 32");
  emit_line(DMS "mem set value [[],[],[],[],[],[],[],[],[],[],[],[],[],[],[],[],[],[],[],[],[],[],[],[],[],[],[],[],[],[],[],[]]");

  emit_line(SOA "elvm_mem_addr dummy");
  emit_line(SOA "elvm_mem_idx dummy");
  emit_line(SOA "elvm_mem_val dummy");
  emit_line(SOA "elvm_mem_res dummy");
  emit_line(SOA "elvm_test_addr dummy");

  emit_line(SOA "elvm_uint_max dummy");
  emit_line(SPS "ELVM elvm_uint_max %d", UINT_MAX + 1);

  for (int mp = 0; data; data = data->next, mp++) {
    if (data->v) {
      Value addr;
      addr.type = IMM;
      addr.imm = mp;
      Value val;
      val.type = IMM;
      val.imm = data->v;
      mcf_emit_mem_table_store(&addr, &val);
    }
  }

  emit_line("function elvm:loop");
}

static void define_utility_functions() {
  define_mem_func("storemem", "storememsh", "safestorebin");
  define_shiftmem_func("storememsh", "storemem");
  define_safebin_func("safestorebin", "storebin", "run function elvm:storeval");
  define_bin_func("storebin", "storebinsh", "store result storage elvm:elvm mem[0][0].v int 1 run " SPG "ELVM elvm_mem_val");
  define_shiftbin_func("storebinsh", "safestorebin");
  define_storeval_func();

  define_mem_func("loadmem", "loadmemsh", "safeloadbin");
  define_shiftmem_func("loadmemsh", "loadmem");
  define_bin_func("safeloadbin", "loadbin", "run " SPS "ELVM elvm_mem_res 0");
  define_bin_func("loadbin", "loadbinsh", "store result score ELVM elvm_mem_res run " DGS "mem[0][0].v");
  define_shiftbin_func("loadbinsh", "safeloadbin");
}

void target_mcfunction(Module* module) {
  reg_names = MCFUNCTION_REG_NAMES;

  int num_functions = emit_chunked_main_loop(module->text,
                                             mcf_emit_func_prologue,
                                             mcf_emit_func_epilogue,
                                             mcf_emit_pc_change,
                                             mcf_emit_inst);

  mcf_emit_function_header("elvm:loop");
  for (int i = 0; i < num_functions; i++) {
    emit_line("execute if score ELVM elvm_pc matches %d..%d run function elvm:func%d", i * CHUNKED_FUNC_SIZE, (i + 1) * CHUNKED_FUNC_SIZE - 1, i);
  }
  emit_line("execute if score ELVM elvm_pc matches 0.. run function elvm:loop");

  emit_main_function(module->data);
  define_utility_functions();

}
