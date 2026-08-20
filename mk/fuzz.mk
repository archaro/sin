# LibFuzzer build, corpus seeding, and smoke campaign.  Every generated
# corpus, log, and crash artifact is kept under the active variant's obj path
# unless FUZZ_ARTIFACT_DIR explicitly requests an external archive location.
FUZZ_CC ?= clang
FUZZ_TIME ?= 30
FUZZ_RUNS ?= 10000
FUZZ_SEED ?= 1
FUZZ_ARTIFACT_DIR ?=
XXD ?= xxd
FUZZ_DIR := $(TEST_DIR)/fuzz
FUZZ_VARIANT := fuzz-$(BUILD)-$(notdir $(FUZZ_CC))
FUZZ_OBJ_DIR := obj/$(FUZZ_VARIANT)/$(FUZZ_DIR)
FUZZ_LIB_DIR := lib/$(FUZZ_VARIANT)
FUZZ_CORPUS_DIR := $(FUZZ_OBJ_DIR)/corpus
FUZZ_LOCAL_ARTIFACT_DIR := $(FUZZ_OBJ_DIR)/artifacts
FUZZ_TMP_ROOT := obj/$(FUZZ_VARIANT)/tmp
FUZZ_TMP_ROOT_ABS := $(abspath $(FUZZ_TMP_ROOT))
FUZZ_BIN := $(FUZZ_OBJ_DIR)/fuzz_scomp
FUZZ_SDISS_BIN := $(FUZZ_OBJ_DIR)/fuzz_sdiss
FUZZ_SIN_OBJECT_BIN := $(FUZZ_OBJ_DIR)/fuzz_sin_object
FUZZ_BINS := $(FUZZ_BIN) $(FUZZ_SDISS_BIN) $(FUZZ_SIN_OBJECT_BIN)
FUZZ_SANITIZE_FLAGS := -fsanitize=fuzzer-no-link,address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=undefined
FUZZ_LINK_FLAGS := -fsanitize=fuzzer,address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=undefined
FUZZ_CFLAGS := $(BASE_CFLAGS) $(STRICT_WARNING_FLAGS) $(LIBUV_CFLAGS) -I$(SRC_DIR) -I$(TEST_DIR) -I$(GENERATED_DIR) $(FUZZ_SANITIZE_FLAGS)
FUZZ_PROD_SANITIZE_FLAGS := -fsanitize=fuzzer-no-link,address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=undefined
FUZZ_MAKE = $(MAKE) --no-print-directory CC="$(FUZZ_CC)" OBJ_DIR="obj/$(FUZZ_VARIANT)" LIB_DIR="$(FUZZ_LIB_DIR)" CFLAGS="$(CFLAGS) $(FUZZ_PROD_SANITIZE_FLAGS)" LDFLAGS="$(LDFLAGS) $(FUZZ_PROD_SANITIZE_FLAGS)"

$(FUZZ_OBJ_DIR)/%.o: $(FUZZ_DIR)/%.c $(PARSER_GENERATED)
	@mkdir -p $(@D)
	$(FUZZ_CC) -c $(CPPFLAGS) $(FUZZ_CFLAGS) -MMD -MP -MF $(@:.o=.d) $< -o $@
$(FUZZ_BIN): $(FUZZ_OBJ_DIR)/fuzz_scomp.o $(LIB)
	@mkdir -p $(@D)
	$(FUZZ_CC) -o $@ $^ $(FUZZ_LINK_FLAGS) $(LIBS)
$(FUZZ_SDISS_BIN): $(FUZZ_OBJ_DIR)/fuzz_sdiss.o $(LIB)
	@mkdir -p $(@D)
	$(FUZZ_CC) -o $@ $^ $(FUZZ_LINK_FLAGS) $(LIBS)
$(FUZZ_SIN_OBJECT_BIN): $(FUZZ_OBJ_DIR)/fuzz_sin_object.o $(LIB)
	@mkdir -p $(@D)
	$(FUZZ_CC) -o $@ $^ $(FUZZ_LINK_FLAGS) $(LIBS)

-include $(FUZZ_OBJ_DIR)/fuzz_scomp.d $(FUZZ_OBJ_DIR)/fuzz_sdiss.d $(FUZZ_OBJ_DIR)/fuzz_sin_object.d

.PHONY: test-fuzz _test-fuzz _fuzz-build _fuzz-run
test-fuzz: _test-fuzz
_test-fuzz:
	+$(FUZZ_MAKE) _fuzz-run
_fuzz-build: $(VARIANT_BIN_DIR)/scomp $(FUZZ_BINS)
	@set -eu; mkdir -p "$(FUZZ_CORPUS_DIR)/scomp" "$(FUZZ_CORPUS_DIR)/sdiss" "$(FUZZ_CORPUS_DIR)/sin-object" "$(FUZZ_TMP_ROOT)"; \
		for src in $(FUZZ_DIR)/corpus/scomp/*; do test -f "$$src" && cp "$$src" "$(FUZZ_CORPUS_DIR)/scomp/" || true; done; \
		command -v "$(XXD)" >/dev/null 2>&1; \
		for hex in $(TEST_DIR)/fixtures/sdiss/*.hex; do test -f "$$hex" || continue; "$(XXD)" -r -p "$$hex" "$(FUZZ_CORPUS_DIR)/sdiss/$$(basename "$$hex" .hex).obj"; done; \
		for src in examples/*.src; do test -f "$$src" || continue; obj="$(FUZZ_CORPUS_DIR)/sin-object/$$(basename "$$src" .src).obj"; "$(VARIANT_BIN_DIR)/scomp" "$$src" "$$obj" >/dev/null 2>&1 || rm -f "$$obj"; done; \
		for hex in $(TEST_DIR)/fixtures/itemstore/*.hex; do test -f "$$hex" || continue; sed '/^[[:space:]]*#/d' "$$hex" | "$(XXD)" -r -p > "$(FUZZ_CORPUS_DIR)/sin-object/$$(basename "$$hex" .hex).itemstore"; done
_fuzz-run: _fuzz-build
	@set -eu; artifact_dir="$(FUZZ_ARTIFACT_DIR)"; test -n "$$artifact_dir" || artifact_dir="$(FUZZ_LOCAL_ARTIFACT_DIR)"; \
		mkdir -p "$$artifact_dir/scomp" "$$artifact_dir/sdiss" "$$artifact_dir/sin-object" \
			"$(FUZZ_OBJ_DIR)/fuzz-logs/scomp" "$(FUZZ_OBJ_DIR)/fuzz-logs/sdiss" \
			"$(FUZZ_OBJ_DIR)/fuzz-logs/sin-object" "$(FUZZ_TMP_ROOT)"; \
		failed=0; total=0; for spec in "scomp $(FUZZ_BIN) $(FUZZ_CORPUS_DIR)/scomp" "sdiss $(FUZZ_SDISS_BIN) $(FUZZ_CORPUS_DIR)/sdiss" "sin-object $(FUZZ_SIN_OBJECT_BIN) $(FUZZ_CORPUS_DIR)/sin-object"; do \
			set -- $$spec; name=$$1; bin=$$2; corpus=$$3; total=$$((total+1)); \
			if SIN_TEST_TMP_ROOT="$(FUZZ_TMP_ROOT_ABS)" TMPDIR="$(FUZZ_TMP_ROOT_ABS)" ASAN_OPTIONS="$(ASAN_OPTIONS):detect_leaks=0" "$$bin" -runs=$(FUZZ_RUNS) -max_total_time=$(FUZZ_TIME) -seed=$(FUZZ_SEED) -artifact_prefix="$$artifact_dir/$$name/" "$(FUZZ_OBJ_DIR)/fuzz-logs/$$name" "$$corpus" >"$(FUZZ_OBJ_DIR)/fuzz-logs/$$name.log" 2>&1; then \
				printf '[fuzz:%s] status=SUCCESS\n' "$$name"; else cat "$(FUZZ_OBJ_DIR)/fuzz-logs/$$name.log"; failed=$$((failed+1)); fi; \
		done; test "$$failed" -eq 0
