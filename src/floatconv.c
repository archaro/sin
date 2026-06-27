#define _GNU_SOURCE
#include "floatconv.h"

#include <ctype.h>
#include <errno.h>
#include <locale.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static bool set_error(char **errdetail, const char *msg) {
  if (errdetail != NULL) {
    *errdetail = strdup(msg);
  }
  return false;
}

static bool is_decimal_literal_syntax(const char *text) {
  const unsigned char *p = (const unsigned char *)text;
  if (*p == '+' || *p == '-') p++;

  bool before = false;
  while (isdigit(*p)) {
    before = true;
    p++;
  }

  bool after = false;
  if (*p == '.') {
    p++;
    while (isdigit(*p)) {
      after = true;
      p++;
    }
  }

  if (!before || !after) return false;

  if (*p == 'e' || *p == 'E') {
    p++;
    if (*p == '+' || *p == '-') p++;
    bool exp = false;
    while (isdigit(*p)) {
      exp = true;
      p++;
    }
    if (!exp) return false;
  }

  return *p == '\0';
}

bool sin_parse_binary64(const char *text, double *out, char **errdetail) {
  if (errdetail != NULL) *errdetail = NULL;
  if (text == NULL) return set_error(errdetail, "binary64 literal is NULL");
  if (out == NULL) return set_error(errdetail, "binary64 output pointer is NULL");
  if (!is_decimal_literal_syntax(text)) {
    return set_error(errdetail, "invalid decimal binary64 literal syntax");
  }

  errno = 0;
  char *end = NULL;
  double parsed = 0.0;

#if defined(__GLIBC__) || defined(__APPLE__) || defined(__FreeBSD__) || defined(__OpenBSD__) || defined(__NetBSD__)
  locale_t c_locale = newlocale(LC_NUMERIC_MASK, "C", (locale_t)0);
  if (c_locale == (locale_t)0) {
    return set_error(errdetail, "could not create C numeric locale for binary64 parsing");
  }
  parsed = strtod_l(text, &end, c_locale);
  freelocale(c_locale);
#else
  struct lconv *lc = localeconv();
  if (lc == NULL || lc->decimal_point == NULL || strcmp(lc->decimal_point, ".") != 0) {
    return set_error(errdetail, "platform lacks locale-independent binary64 parsing support");
  }
  parsed = strtod(text, &end);
#endif

  if (end == NULL || *end != '\0') {
    return set_error(errdetail, "binary64 parser stopped before end of literal");
  }
  if (errno == ERANGE) {
    /* Correctly rounded underflow/subnormal results and overflow to infinity
       are valid IEEE-754 outcomes for decimal literals. */
  } else if (errno != 0) {
    return set_error(errdetail, "binary64 literal conversion failed");
  }

  *out = parsed;
  return true;
}

bool sin_parse_binary64_bits(const char *text, uint64_t *out_bits, char **errdetail) {
  if (errdetail != NULL) *errdetail = NULL;
  if (out_bits == NULL) return set_error(errdetail, "binary64 bits output pointer is NULL");

  double parsed = 0.0;
  if (!sin_parse_binary64(text, &parsed, errdetail)) return false;

  uint64_t bits = 0;
  memcpy(&bits, &parsed, sizeof(bits));
  *out_bits = bits;
  return true;
}
