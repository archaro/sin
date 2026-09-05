# Production objects, generated parser/lexer, and executable links.
LIB_OBJECTS := $(OBJ_DIR)/common/log.o $(OBJ_DIR)/common/memory.o $(OBJ_DIR)/common/cli_io.o \
 $(OBJ_DIR)/bytecode/bytecode_abi.o $(OBJ_DIR)/bytecode/bytecode_wire.o $(OBJ_DIR)/bytecode/bytecode_format.o \
 $(OBJ_DIR)/bytecode/bytecode_verify.o $(OBJ_DIR)/bytecode/bytecode_convert.o $(OBJ_DIR)/bytecode/sdiss_core.o \
 $(OBJ_DIR)/common/floatconv.o $(OBJ_DIR)/parser.o $(OBJ_DIR)/lexer.o \
 $(OBJ_DIR)/compiler/absyn.o $(OBJ_DIR)/compiler/semant.o $(OBJ_DIR)/compiler/ir.o $(OBJ_DIR)/compiler/lower.o \
 $(OBJ_DIR)/compiler/compiler_context.o $(OBJ_DIR)/compiler/compiler_pipeline.o $(OBJ_DIR)/compiler/emitbc.o \
 $(OBJ_DIR)/compiler/compdiag.o $(OBJ_DIR)/common/error.o $(OBJ_DIR)/common/util.o \
 $(OBJ_DIR)/libcall/libcall_sys.o $(OBJ_DIR)/libcall/libcall_task.o $(OBJ_DIR)/libcall/libcall_net.o \
 $(OBJ_DIR)/libcall/libcall_str.o $(OBJ_DIR)/libcall/libcall_list.o $(OBJ_DIR)/libcall/libcall_math.o $(OBJ_DIR)/libcall/libcall_rand.o $(OBJ_DIR)/libcall/libcall_registry.o \
 $(OBJ_DIR)/libcall/libcall_table.o $(OBJ_DIR)/runtime/stack.o $(OBJ_DIR)/runtime/value.o \
 $(OBJ_DIR)/runtime/list.o $(OBJ_DIR)/runtime/itemref.o $(OBJ_DIR)/itemstore/item_hash.o \
 $(OBJ_DIR)/itemstore/item_tree.o $(OBJ_DIR)/itemstore/item_registry.o $(OBJ_DIR)/itemstore/item_persist.o \
 $(OBJ_DIR)/itemstore/item_source_persist.o $(OBJ_DIR)/itemstore/item_error.o $(OBJ_DIR)/itemstore/item_persist_v1.o \
 $(OBJ_DIR)/itemstore/item_persist_v2.o $(OBJ_DIR)/runtime/vm.o $(OBJ_DIR)/runtime/task.o \
 $(OBJ_DIR)/runtime/runtime_decode.o $(OBJ_DIR)/runtime/runtime_value.o $(OBJ_DIR)/runtime/runtime_item_ops.o \
 $(OBJ_DIR)/runtime/runtime_opcode.o $(OBJ_DIR)/runtime/runtime_frame.o $(OBJ_DIR)/runtime/interpret.o \
 $(OBJ_DIR)/net/network.o $(OBJ_DIR)/net/libtelnet.o
PROGRAM_OBJECTS := $(PROGRAMS:%=$(OBJ_DIR)/%.o)
VARIANT_BIN_DIR := $(OBJ_DIR)/bin
VARIANT_PROGRAMS := $(PROGRAMS:%=$(VARIANT_BIN_DIR)/%)
PARSER_C := $(GENERATED_DIR)/parser.c
PARSER_H := $(GENERATED_DIR)/parser.h
LEXER_C := $(GENERATED_DIR)/lexer.c
PARSER_GENERATED := $(PARSER_C) $(PARSER_H)
PARSER_DEPENDENT_OBJECTS := $(OBJ_DIR)/compiler/compiler_context.o $(OBJ_DIR)/compiler/compiler_pipeline.o \
 $(OBJ_DIR)/compiler/emitbc.o $(OBJ_DIR)/libcall/libcall_sys.o $(OBJ_DIR)/runtime/runtime_item_ops.o \
 $(OBJ_DIR)/runtime/runtime_frame.o $(OBJ_DIR)/runtime/interpret.o $(OBJ_DIR)/scomp.o
OBJECTS := $(LIB_OBJECTS) $(PROGRAM_OBJECTS)
DEPS := $(OBJECTS:.o=.d)
.SECONDARY: $(PARSER_GENERATED)

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c
	@mkdir -p $(@D)
	$(CC) -c $(CPPFLAGS) $(CFLAGS) $< -o $@

$(PARSER_GENERATED) &: $(SRC_DIR)/compiler/parser.y
	@mkdir -p $(GENERATED_DIR)
	$(YACC) -o $(PARSER_C) --defines=$(PARSER_H) $<

$(LEXER_C): $(SRC_DIR)/compiler/lexer.l $(PARSER_GENERATED)
	@mkdir -p $(GENERATED_DIR)
	$(LEX) -o $(LEXER_C) $<

$(OBJ_DIR)/parser.o: $(PARSER_C) $(PARSER_H)
	@mkdir -p $(@D)
	$(CC) -c $(CPPFLAGS) $(CFLAGS) $(GENERATED_WARNING_FLAGS) $< -o $@
$(OBJ_DIR)/lexer.o: $(LEXER_C) $(PARSER_H)
	@mkdir -p $(@D)
	$(CC) -c $(CPPFLAGS) $(CFLAGS) $(GENERATED_WARNING_FLAGS) $< -o $@
$(PARSER_DEPENDENT_OBJECTS): $(PARSER_H)

$(LIB): $(LIB_OBJECTS)
	@mkdir -p $(@D)
	ar rcs $@ $^

$(VARIANT_BIN_DIR)/%: $(OBJ_DIR)/%.o $(LIB)
	@mkdir -p $(@D)
	$(CC) -o $@ $^ $(LDFLAGS) $(LIBS)

# The root names remain the concise interactive interface.  Tests use the
# variant-local links above so switching BUILD never reuses a stale program.
$(PROGRAMS): %: $(VARIANT_BIN_DIR)/%
	cp $< $@

-include $(DEPS)
