// Libcall registry allocation and lookup.

// Licensed under the MIT License - see LICENSE file for details.

#include <stdlib.h>
#include <string.h>

#include "libcall_registry.h"
#include "log.h"
#include "memory.h"



static int libcall_name_entry_cmp(const void *a, const void *b) {
  const LIBCALL_NAME_ENTRY_t *ea = (const LIBCALL_NAME_ENTRY_t *)a;
  const LIBCALL_NAME_ENTRY_t *eb = (const LIBCALL_NAME_ENTRY_t *)b;
  return strcmp(ea->lookup_key, eb->lookup_key);
}

static int libcall_lookup_key_cmp(const char *libname, const char *callname,
                                  const char *lookup_key) {
  while (*libname && *lookup_key != '\0' && *lookup_key != '\x1f') {
    unsigned char left = (unsigned char)*libname;
    unsigned char right = (unsigned char)*lookup_key;
    if (left != right) return (int)left - (int)right;
    libname++;
    lookup_key++;
  }

  if (*libname != '\0') {
    return (int)(unsigned char)*libname - (int)(unsigned char)'\x1f';
  }
  if (*lookup_key != '\x1f') {
    return (int)(unsigned char)'\x1f' - (int)(unsigned char)*lookup_key;
  }
  lookup_key++;

  while (*callname && *lookup_key) {
    unsigned char left = (unsigned char)*callname;
    unsigned char right = (unsigned char)*lookup_key;
    if (left != right) return (int)left - (int)right;
    callname++;
    lookup_key++;
  }
  return (int)(unsigned char)*callname - (int)(unsigned char)*lookup_key;
}

static bool libcall_make_key(const char *libname, const char *callname,
                             char **out_key) {
  size_t liblen = strlen(libname);
  size_t calllen = strlen(callname);
  size_t keylen;
  size_t allocation_size;
  if (alloc_add_overflow(liblen, 1, &keylen) ||
      alloc_add_overflow(keylen, calllen, &keylen) ||
      alloc_add_overflow(keylen, 1, &allocation_size)) {
    return false;
  }
  char *key = alloc_malloc(allocation_size);
  if (!key) {
    return false;
  }
  memcpy(key, libname, liblen);
  key[liblen] = '\x1f';
  memcpy(key + liblen + 1, callname, calllen);
  key[keylen] = '\0';
  *out_key = key;
  return true;
}

static LibcallRegistry default_libcall_registry = {0};


static bool libcall_args_in_range(uint8_t args) {
  return args <= 32;
}

typedef struct LIBCALL_KEY_NODE {
  char *key;
  struct LIBCALL_KEY_NODE *next;
} LIBCALL_KEY_NODE_t;

static uint32_t libcall_key_hash(const char *key) {
  // djb2 hash
  uint32_t hash = 5381U;
  for (unsigned char c = (unsigned char)*key; c != '\0'; c = (unsigned char)*++key) {
    hash = ((hash << 5) + hash) + c;
  }
  return hash;
}

static void libcall_key_set_free(LIBCALL_KEY_NODE_t **buckets, size_t bucket_count) {
  if (!buckets) return;
  for (size_t i = 0; i < bucket_count; i++) {
    LIBCALL_KEY_NODE_t *node = buckets[i];
    while (node) {
      LIBCALL_KEY_NODE_t *next = node->next;
      if (node->key) {
        free(node->key);
      }
      free(node);
      node = next;
    }
  }
  free(buckets);
}

static bool libcall_registry_fail(const char *msg, const LIBCALL_t *entry, size_t idx, bool fail_fast) {
  logerr("FATAL: libcall registry self-check failed: %s (entry %zu: %s.%s lib=%d call=%d args=%u)\n",
         msg, idx,
         entry && entry->libname ? entry->libname : "<null-lib>",
         entry && entry->callname ? entry->callname : "<null-call>",
         entry ? (int)entry->lib_index : -1,
         entry ? (int)entry->call_index : -1,
         entry ? (unsigned)entry->args : 0U);
  if (fail_fast) {
    abort();
  }
  return false;
}

bool libcall_registry_self_check(const LIBCALL_t *calls, bool fail_fast) {
  if (!calls) return libcall_registry_fail("registry pointer is null", NULL, 0, fail_fast);

  bool seen_lib[256] = {0};
  bool seen_pair[256][256] = {{0}};
  size_t key_bucket_count = 257;
  LIBCALL_KEY_NODE_t **seen_keys =
      alloc_calloc(key_bucket_count, sizeof *seen_keys);
  if (!seen_keys) {
    return libcall_registry_fail("failed to allocate textual key set", NULL, 0, fail_fast);
  }
  for (size_t i = 0; calls[i].libname != NULL || calls[i].callname != NULL; i++) {
    const LIBCALL_t *e = &calls[i];
    if (!e->libname || !e->callname || !e->func) {
      libcall_key_set_free(seen_keys, key_bucket_count);
      return libcall_registry_fail("entry requires non-null libname/callname/func", e, i, fail_fast);
    }
    if (e->lib_index < 0 || e->call_index < 0) {
      libcall_key_set_free(seen_keys, key_bucket_count);
      return libcall_registry_fail("negative lib_index/call_index", e, i, fail_fast);
    }
    if (!libcall_args_in_range(e->args)) {
      libcall_key_set_free(seen_keys, key_bucket_count);
      return libcall_registry_fail("args out of acceptable range", e, i, fail_fast);
    }

    char *lookup_key = NULL;
    if (!libcall_make_key(e->libname, e->callname, &lookup_key)) {
      libcall_key_set_free(seen_keys, key_bucket_count);
      return libcall_registry_fail("failed to allocate textual key", e, i, fail_fast);
    }
    size_t bucket = (size_t)(libcall_key_hash(lookup_key) % key_bucket_count);
    for (LIBCALL_KEY_NODE_t *node = seen_keys[bucket]; node; node = node->next) {
      if (strcmp(node->key, lookup_key) == 0) {
        free(lookup_key);
        libcall_key_set_free(seen_keys, key_bucket_count);
        return libcall_registry_fail("duplicate textual key libname.callname", e, i, fail_fast);
      }
    }
    LIBCALL_KEY_NODE_t *new_node = alloc_malloc(sizeof *new_node);
    if (!new_node) {
      free(lookup_key);
      libcall_key_set_free(seen_keys, key_bucket_count);
      return libcall_registry_fail("failed to allocate textual key node", e, i, fail_fast);
    }
    new_node->key = lookup_key;
    new_node->next = seen_keys[bucket];
    seen_keys[bucket] = new_node;

    uint8_t li = (uint8_t)e->lib_index, ci = (uint8_t)e->call_index;
    if (seen_pair[li][ci]) {
      libcall_key_set_free(seen_keys, key_bucket_count);
      return libcall_registry_fail("duplicate numeric key (lib_index,call_index)", e, i, fail_fast);
    }
    seen_pair[li][ci] = true;
    seen_lib[li] = true;
  }

  int max_lib = -1;
  for (int i=0;i<256;i++) if (seen_lib[i]) max_lib = i;
  for (int i=0;i<=max_lib;i++) if (!seen_lib[i] && i!=0) {
    libcall_key_set_free(seen_keys, key_bucket_count);
    return libcall_registry_fail("lib_index values must be contiguous from 1..max", &calls[0], 0, fail_fast);
  }
  libcall_key_set_free(seen_keys, key_bucket_count);
  return true;
}
static size_t libcall_registry_index(const LibcallRegistry *registry, uint8_t lib_index, uint8_t call_index) {
  return ((size_t)lib_index * registry->width) + (size_t)call_index;
}

void libcall_registry_destroy(LibcallRegistry *registry) {
  if (!registry) return;
  if (registry->names) {
    for (size_t i = 0; i < registry->name_count; i++) {
      free(registry->names[i].lookup_key);
      registry->names[i].lookup_key = NULL;
    }
    free(registry->names);
  }

  free(registry->entries);
  memset(registry, 0, sizeof(*registry));
}

void libcall_free_registry(void) {
  libcall_registry_destroy(&default_libcall_registry);
}

void libcall_reset_registry_for_tests(void) {
  libcall_free_registry();
}

bool libcall_registry_init(LibcallRegistry *registry) {
  LIBCALL_REG_ENTRY_t *tmp_registry = NULL;
  LIBCALL_NAME_ENTRY_t *tmp_name_registry = NULL;
  size_t tmp_registry_width = 0;
  size_t tmp_registry_height = 0;
  size_t tmp_count = 0;
  size_t dense_count = 0;
  OP_t tmp_token_funcs[256] = {0};
  uint8_t tmp_token_args[256] = {0};

  if (!registry) return false;
  if (registry->ready) {
    return true;
  }

  if (!libcall_registry_self_check(libcalls, false)) {
    return false;
  }

  int8_t max_lib_index = -1;
  int8_t max_call_index = -1;
  size_t count = 0;
  for (size_t i = 0; libcalls[i].libname != NULL; i++) {
    if (libcalls[i].lib_index < 0 || libcalls[i].call_index < 0) {
      return false;
    }
    if (libcalls[i].lib_index > max_lib_index) {
      max_lib_index = libcalls[i].lib_index;
    }
    if (libcalls[i].call_index > max_call_index) {
      max_call_index = libcalls[i].call_index;
    }
    count++;
  }

  tmp_registry_height = (size_t)max_lib_index + 1;
  tmp_registry_width = (size_t)max_call_index + 1;
  tmp_count = count;
  if (alloc_mul_overflow(tmp_registry_height, tmp_registry_width,
                         &dense_count)) {
    goto fail;
  }

  tmp_registry = alloc_calloc(dense_count, sizeof *tmp_registry);
  tmp_name_registry = alloc_calloc(tmp_count, sizeof *tmp_name_registry);
  if (!tmp_registry || !tmp_name_registry) {
    goto fail;
  }

  for (size_t i = 0; i < tmp_count; i++) {
    uint8_t lib_index = (uint8_t)libcalls[i].lib_index;
    uint8_t call_index = (uint8_t)libcalls[i].call_index;
    size_t dense_index = ((size_t)lib_index * tmp_registry_width) + (size_t)call_index;
    if (tmp_registry[dense_index].func) {
      goto fail;
    }
    tmp_registry[dense_index].func = libcalls[i].func;
    tmp_registry[dense_index].args = libcalls[i].args;

    tmp_name_registry[i].libname = libcalls[i].libname;
    tmp_name_registry[i].callname = libcalls[i].callname;
    tmp_name_registry[i].lib_index = lib_index;
    tmp_name_registry[i].call_index = call_index;
    tmp_name_registry[i].args = libcalls[i].args;
    tmp_name_registry[i].token = (uint8_t)i;
    tmp_name_registry[i].lookup_key = NULL;
    if (!libcall_make_key(libcalls[i].libname, libcalls[i].callname,
                          &tmp_name_registry[i].lookup_key)) {
      goto fail;
    }
    tmp_token_funcs[(uint8_t)i] = libcalls[i].func;
    tmp_token_args[(uint8_t)i] = libcalls[i].args;
  }

  qsort(tmp_name_registry, tmp_count, sizeof(LIBCALL_NAME_ENTRY_t),
        libcall_name_entry_cmp);

  for (size_t i = 1; i < tmp_count; i++) {
    if (strcmp(tmp_name_registry[i - 1].lookup_key,
               tmp_name_registry[i].lookup_key) == 0) {
      goto fail;
    }
  }

  registry->entries = tmp_registry;
  registry->names = tmp_name_registry;
  registry->width = tmp_registry_width;
  registry->height = tmp_registry_height;
  registry->name_count = tmp_count;
  memcpy(registry->token_funcs, tmp_token_funcs, sizeof(registry->token_funcs));
  memcpy(registry->token_args, tmp_token_args, sizeof(registry->token_args));
  registry->ready = true;
  return true;

fail:
  memset(tmp_token_funcs, 0, sizeof(tmp_token_funcs));
  memset(tmp_token_args, 0, sizeof(tmp_token_args));
  for (size_t i = 0; i < tmp_count; i++) {
    if (tmp_name_registry && tmp_name_registry[i].lookup_key) {
      free(tmp_name_registry[i].lookup_key);
      tmp_name_registry[i].lookup_key = NULL;
    }
  }
  if (tmp_name_registry) {
    free(tmp_name_registry);
  }
  if (tmp_registry) {
    free(tmp_registry);
  }
  return false;
}

bool libcall_registry_validate(LibcallRegistry *registry) {
  if (!libcall_registry_init(registry)) {
    return false;
  }

  for (size_t i = 0; libcalls[i].libname != NULL; i++) {
    uint8_t lib_index = (uint8_t)libcalls[i].lib_index;
    uint8_t call_index = (uint8_t)libcalls[i].call_index;
    if (lib_index >= registry->height || call_index >= registry->width) {
      return false;
    }
    size_t dense_index = libcall_registry_index(registry, lib_index, call_index);
    if (!registry->entries[dense_index].func) {
      return false;
    }
    if (registry->entries[dense_index].func != libcalls[i].func ||
        registry->entries[dense_index].args != libcalls[i].args) {
      return false;
    }
  }
  return true;
}


bool libcall_registry_lookup_token(LibcallRegistry *registry, const char *libname, const char *callname, uint8_t *token, uint8_t *args) {
  if (!libcall_registry_init(registry)) return false;
  if (!libname || !callname) return false;

  size_t low = 0;
  size_t high = registry->name_count;
  while (low < high) {
    size_t mid = low + ((high - low) / 2);
    LIBCALL_NAME_ENTRY_t *entry = &registry->names[mid];
    int cmp = libcall_lookup_key_cmp(libname, callname, entry->lookup_key);
    if (cmp < 0) {
      high = mid;
    } else if (cmp > 0) {
      low = mid + 1;
    } else {
      if (token) *token = entry->token;
      if (args) *args = entry->args;
      return true;
    }
  }
  return false;
}

bool libcall_names_unique(const LIBCALL_t *calls) {
  for (size_t i = 0; calls[i].libname != NULL; i++) {
    for (size_t j = i + 1; calls[j].libname != NULL; j++) {
      if (strcmp(calls[i].libname, calls[j].libname) == 0 &&
          strcmp(calls[i].callname, calls[j].callname) == 0) {
        return false;
      }
    }
  }
  return true;
}

OP_t libcall_registry_func_token(LibcallRegistry *registry, uint8_t token) {
  if (!libcall_registry_init(registry)) return NULL;
  return registry->token_funcs[token];
}

bool libcall_registry_token_arg_count(LibcallRegistry *registry, uint8_t token, uint8_t *args) {
  if (!libcall_registry_init(registry)) return false;
  if (!registry->token_funcs[token]) return false;
  if (args) *args = registry->token_args[token];
  return true;
}

bool libcall_init_registry(void) { return libcall_registry_init(&default_libcall_registry); }
bool libcall_validate_registry(void) { return libcall_registry_validate(&default_libcall_registry); }
bool libcall_lookup_token(const char *libname, const char *callname, uint8_t *token, uint8_t *args) {
  return libcall_registry_lookup_token(&default_libcall_registry, libname, callname, token, args);
}
bool libcall_token_arg_count(uint8_t token, uint8_t *args) {
  return libcall_registry_token_arg_count(&default_libcall_registry, token, args);
}
OP_t libcall_func_token(uint8_t token) { return libcall_registry_func_token(&default_libcall_registry, token); }
