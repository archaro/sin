#define _GNU_SOURCE
#include "floatconv.h"

#include <ctype.h>
#include <errno.h>
#include <float.h>
#include <locale.h>
#include <math.h>
#include <stdio.h>
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

static uint64_t double_bits(double value) {
  uint64_t bits = 0;
  memcpy(&bits, &value, sizeof(bits));
  return bits;
}

static bool parse_binary64_bits_noerr(const char *text, uint64_t *out_bits) {
  double parsed = 0.0;
  if (!sin_parse_binary64(text, &parsed, NULL)) return false;
  *out_bits = double_bits(parsed);
  return true;
}

static bool c_locale_snprintf(char *buf, size_t cap, const char *fmt, double value) {
  if (cap == 0) return false;
#if defined(__GLIBC__) || defined(__APPLE__) || defined(__FreeBSD__) || defined(__OpenBSD__) || defined(__NetBSD__)
  locale_t c_locale = newlocale(LC_NUMERIC_MASK, "C", (locale_t)0);
  if (c_locale == (locale_t)0) return false;
  locale_t old_locale = uselocale(c_locale);
  int n = snprintf(buf, cap, fmt, value);
  uselocale(old_locale);
  freelocale(c_locale);
#else
  struct lconv *lc = localeconv();
  if (lc == NULL || lc->decimal_point == NULL || strcmp(lc->decimal_point, ".") != 0) return false;
  int n = snprintf(buf, cap, fmt, value);
#endif
  return n >= 0 && (size_t)n < cap;
}

static bool add_decimal_marker(char *buf, size_t cap) {
  if (strchr(buf, '.') != NULL) return true;
  char *exp = strpbrk(buf, "eE");
  size_t len = strlen(buf);
  if (exp != NULL) {
    size_t pos = (size_t)(exp - buf);
    if (len + 2 >= cap) return false;
    memmove(buf + pos + 2, buf + pos, len - pos + 1);
    buf[pos] = '.';
    buf[pos + 1] = '0';
    return true;
  }
  if (len + 2 >= cap) return false;
  buf[len] = '.';
  buf[len + 1] = '0';
  buf[len + 2] = '\0';
  return true;
}

bool sin_format_binary64_buf(double value, char *buf, size_t cap) {
  if (buf == NULL || cap == 0) return false;
  buf[0] = '\0';

  if (isnan(value)) {
    const char *s = "nan";
    if (strlen(s) + 1 > cap) return false;
    memcpy(buf, s, strlen(s) + 1);
    return true;
  }
  if (isinf(value)) {
    const char *s = signbit(value) ? "-inf" : "inf";
    if (strlen(s) + 1 > cap) return false;
    memcpy(buf, s, strlen(s) + 1);
    return true;
  }
  if (value == 0.0) {
    const char *s = signbit(value) ? "-0.0" : "0.0";
    if (strlen(s) + 1 > cap) return false;
    memcpy(buf, s, strlen(s) + 1);
    return true;
  }

  char candidate[64];
  uint64_t target = double_bits(value);
  for (int precision = 1; precision <= DBL_DECIMAL_DIG; precision++) {
    char fmt[16];
    snprintf(fmt, sizeof(fmt), "%%.%dg", precision);
    if (!c_locale_snprintf(candidate, sizeof(candidate), fmt, value)) return false;
    if (!add_decimal_marker(candidate, sizeof(candidate))) return false;
    uint64_t parsed = 0;
    if (parse_binary64_bits_noerr(candidate, &parsed) && parsed == target) {
      if (strlen(candidate) + 1 > cap) return false;
      memcpy(buf, candidate, strlen(candidate) + 1);
      return true;
    }
  }

  if (!c_locale_snprintf(candidate, sizeof(candidate), "%.17g", value)) return false;
  if (!add_decimal_marker(candidate, sizeof(candidate))) return false;
  if (strlen(candidate) + 1 > cap) return false;
  memcpy(buf, candidate, strlen(candidate) + 1);
  return true;
}

char *sin_format_binary64(double value) {
  char tmp[64];
  if (!sin_format_binary64_buf(value, tmp, sizeof(tmp))) return NULL;
  return strdup(tmp);
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
