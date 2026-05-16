#include "compiler_pipeline.h"

#include "absyn.h"
#include "error.h"
#include "ir.h"
#include "lower.h"
#include "memory.h"
#include "parser.h"
#include "semant.h"

typedef struct {
  AS_NODE *absyn;
  SEM_CTX *sem;
  IR_Unit *ir;
  OUTPUT_t *out;
} CompileObjects;

static void cleanup_compile_objects(CompileObjects *objs) {
  if (objs->absyn) {
    as_delete(objs->absyn);
  }
  if (objs->sem) {
    sem_delete_ctx(objs->sem);
  }
  if (objs->ir) {
    ir_destroy_unit(objs->ir);
  }
  if (objs->out) {
    if (objs->out->bytecode) {
      FREE_ARRAY(unsigned char, objs->out->bytecode, objs->out->maxsize);
    }
    FREE_ARRAY(OUTPUT_t, objs->out, 1);
  }
}

int8_t compile_source_to_bytecode_with_params(const char *source, size_t len,
                                              const char **params, size_t param_count,
                                              OUTPUT_t **out, char **errdetail) {
  CompileObjects objs = {0};
  int8_t rc = ERR_NOERROR;

  if (!source || !out) {
    return ERR_COMP_SYNTAX;
  }

  *out = NULL;

  objs.out = GROW_ARRAY(OUTPUT_t, NULL, 0, 1);
  objs.out->maxsize = 1024;
  objs.out->bytecode = GROW_ARRAY(unsigned char, NULL, 0, objs.out->maxsize);
  objs.out->nextbyte = objs.out->bytecode;

  objs.sem = sem_create_ctx();

  rc = parse_source((char *)source, (int)len, &objs.absyn, errdetail);
  if (rc != ERR_NOERROR) {
    goto fail;
  }

  sem_seed_params(objs.sem, params, param_count);

  rc = sem_check_locals(objs.absyn, errdetail, objs.sem);
  if (rc != ERR_NOERROR) {
    goto fail;
  }

  rc = lower_ast_to_ir(objs.absyn, objs.sem, &objs.ir, errdetail);
  if (rc != ERR_NOERROR) {
    goto fail;
  }

  rc = ir_validate(objs.ir, objs.sem->count, errdetail);
  if (rc != ERR_NOERROR) {
    goto fail;
  }

  uint8_t emitted_param_count = 0;
  for (uint32_t i = 0; i < objs.sem->count; i++) {
    if (objs.sem->locals[i].param) {
      emitted_param_count++;
    }
  }

  rc = emit_bytecode(objs.ir, (uint8_t)objs.sem->count, emitted_param_count, objs.out, errdetail);
  if (rc != ERR_NOERROR) {
    goto fail;
  }

  *out = objs.out;
  objs.out = NULL;
  cleanup_compile_objects(&objs);
  return ERR_NOERROR;

fail:
  cleanup_compile_objects(&objs);
  return rc;
}

int8_t compile_source_to_bytecode(const char *source, size_t len, OUTPUT_t **out, char **errdetail) {
  return compile_source_to_bytecode_with_params(source, len, NULL, 0, out, errdetail);
}
