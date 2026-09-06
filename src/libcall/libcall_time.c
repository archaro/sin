// Time libcall support.

// Licensed under the MIT License - see LICENSE file for details.

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include "libcall_common.h"
#include "libcall_handlers.h"
#include "libcall_time.h"
#include "stack.h"

static bool time_to_utc(const time_t *seconds, struct tm *result) {
#if defined(_WIN32)
  return gmtime_s(result, seconds) == 0;
#else
  return gmtime_r(seconds, result) != NULL;
#endif
}

static bool int64_to_time_t(int64_t seconds, time_t *result) {
  time_t converted = (time_t)seconds;
  if ((time_t)-1 > (time_t)0) {
    if (seconds < 0 || (uintmax_t)converted != (uintmax_t)seconds) {
      return false;
    }
  } else if ((intmax_t)converted != (intmax_t)seconds) {
    return false;
  }
  *result = converted;
  return true;
}

typedef enum TimeCalendarPart {
  TIME_YEAR,
  TIME_MONTH,
  TIME_DAY,
  TIME_HOUR,
  TIME_MINUTE,
  TIME_SECOND,
} TimeCalendarPart;

typedef enum TimeFormat {
  TIME_FORMAT_TIMESTAMP,
  TIME_FORMAT_TIME,
  TIME_FORMAT_DATE,
  TIME_FORMAT_FULLDATE,
} TimeFormat;

static bool time_format_value(const struct tm *utc, TimeFormat format,
                              char **result) {
  static const char *const months[] = {
      "January", "February", "March", "April", "May", "June",
      "July", "August", "September", "October", "November", "December",
  };
  int64_t year = (int64_t)utc->tm_year + 1900;
  int month = utc->tm_mon + 1;
  if (format != TIME_FORMAT_TIME && (year < 0 || year > 9999)) {
    return false;
  }
  const char *suffix = "th";
  if (utc->tm_mday % 100 < 11 || utc->tm_mday % 100 > 13) {
    switch (utc->tm_mday % 10) {
      case 1:
        suffix = "st";
        break;
      case 2:
        suffix = "nd";
        break;
      case 3:
        suffix = "rd";
        break;
      default:
        break;
    }
  }

  int needed;
  switch (format) {
    case TIME_FORMAT_TIMESTAMP:
      needed = snprintf(NULL, 0, "%04lld-%02d-%02d %02d:%02d:%02d",
                        (long long)year, month, utc->tm_mday, utc->tm_hour,
                        utc->tm_min, utc->tm_sec);
      break;
    case TIME_FORMAT_TIME:
      needed = snprintf(NULL, 0, "%02d:%02d:%02d", utc->tm_hour,
                        utc->tm_min, utc->tm_sec);
      break;
    case TIME_FORMAT_DATE:
      needed = snprintf(NULL, 0, "%04lld-%02d-%02d", (long long)year,
                        month, utc->tm_mday);
      break;
    case TIME_FORMAT_FULLDATE:
      if (month < 1 || month > 12) return false;
      needed = snprintf(NULL, 0, "%d%s %s %04lld", utc->tm_mday, suffix,
                        months[month - 1], (long long)year);
      break;
    default:
      return false;
  }
  if (needed < 0) return false;
  char *text = malloc((size_t)needed + 1);
  if (!text) return false;

  int written;
  switch (format) {
    case TIME_FORMAT_TIMESTAMP:
      written = snprintf(text, (size_t)needed + 1,
                         "%04lld-%02d-%02d %02d:%02d:%02d",
                         (long long)year, month, utc->tm_mday, utc->tm_hour,
                         utc->tm_min, utc->tm_sec);
      break;
    case TIME_FORMAT_TIME:
      written = snprintf(text, (size_t)needed + 1, "%02d:%02d:%02d",
                         utc->tm_hour, utc->tm_min, utc->tm_sec);
      break;
    case TIME_FORMAT_DATE:
      written = snprintf(text, (size_t)needed + 1, "%04lld-%02d-%02d",
                         (long long)year, month, utc->tm_mday);
      break;
    case TIME_FORMAT_FULLDATE:
      written = snprintf(text, (size_t)needed + 1, "%d%s %s %04lld",
                         utc->tm_mday, suffix, months[month - 1],
                         (long long)year);
      break;
    default:
      free(text);
      return false;
  }
  if (written < 0 || written != needed) {
    free(text);
    return false;
  }
  *result = text;
  return true;
}

static uint8_t *time_formatted(RuntimeContext *ctx, uint8_t *nextop,
                               const char *name, TimeFormat format) {
  VALUE_t milliseconds = pop_stack(ctx->vm->stack);
  if (milliseconds.type != VALUE_int) {
    value_free(&milliseconds);
    return lc_invalid_args_nil_return(ctx, nextop, name);
  }

  int64_t seconds = milliseconds.i / INT64_C(1000);
  if (milliseconds.i % INT64_C(1000) < 0) seconds--;
  value_free(&milliseconds);

  time_t epoch_seconds;
  struct tm utc;
  if (!int64_to_time_t(seconds, &epoch_seconds) ||
      !time_to_utc(&epoch_seconds, &utc)) {
    return lc_undefined_nil_return(ctx, nextop);
  }

  char *text = NULL;
  if (!time_format_value(&utc, format, &text)) {
    return lc_undefined_nil_return(ctx, nextop);
  }
  push_stack(ctx->vm->stack, (VALUE_t){VALUE_str, {.s = text}});
  return nextop;
}

static uint8_t *time_calendar_part(RuntimeContext *ctx, uint8_t *nextop,
                                   const char *name, TimeCalendarPart part) {
  VALUE_t milliseconds = pop_stack(ctx->vm->stack);
  if (milliseconds.type != VALUE_int) {
    value_free(&milliseconds);
    return lc_invalid_args_nil_return(ctx, nextop, name);
  }

  int64_t seconds = milliseconds.i / INT64_C(1000);
  if (milliseconds.i % INT64_C(1000) < 0) seconds--;
  value_free(&milliseconds);

  time_t epoch_seconds;
  struct tm utc;
  if (!int64_to_time_t(seconds, &epoch_seconds) ||
      !time_to_utc(&epoch_seconds, &utc)) {
    return lc_undefined_nil_return(ctx, nextop);
  }

  int64_t value;
  switch (part) {
    case TIME_YEAR:
      value = (int64_t)utc.tm_year + 1900;
      break;
    case TIME_MONTH:
      value = (int64_t)utc.tm_mon + 1;
      break;
    case TIME_DAY:
      value = utc.tm_mday;
      break;
    case TIME_HOUR:
      value = utc.tm_hour;
      break;
    case TIME_MINUTE:
      value = utc.tm_min;
      break;
    case TIME_SECOND:
      value = utc.tm_sec;
      break;
    default:
      return lc_undefined_nil_return(ctx, nextop);
  }
  push_stack(ctx->vm->stack, (VALUE_t){VALUE_int, {.i = value}});
  return nextop;
}

uint8_t *lc_time_year(RuntimeContext *ctx, uint8_t *nextop, ITEM_t *item) {
  (void)item;
  return time_calendar_part(ctx, nextop,
      "time.year expects an integer timestamp in milliseconds", TIME_YEAR);
}

uint8_t *lc_time_month(RuntimeContext *ctx, uint8_t *nextop, ITEM_t *item) {
  (void)item;
  return time_calendar_part(ctx, nextop,
      "time.month expects an integer timestamp in milliseconds", TIME_MONTH);
}

uint8_t *lc_time_day(RuntimeContext *ctx, uint8_t *nextop, ITEM_t *item) {
  (void)item;
  return time_calendar_part(ctx, nextop,
      "time.day expects an integer timestamp in milliseconds", TIME_DAY);
}

uint8_t *lc_time_hour(RuntimeContext *ctx, uint8_t *nextop, ITEM_t *item) {
  (void)item;
  return time_calendar_part(ctx, nextop,
      "time.hour expects an integer timestamp in milliseconds", TIME_HOUR);
}

uint8_t *lc_time_minute(RuntimeContext *ctx, uint8_t *nextop, ITEM_t *item) {
  (void)item;
  return time_calendar_part(ctx, nextop,
      "time.minute expects an integer timestamp in milliseconds", TIME_MINUTE);
}

uint8_t *lc_time_second(RuntimeContext *ctx, uint8_t *nextop, ITEM_t *item) {
  (void)item;
  return time_calendar_part(ctx, nextop,
      "time.second expects an integer timestamp in milliseconds", TIME_SECOND);
}

uint8_t *lc_time_timestamp(RuntimeContext *ctx, uint8_t *nextop, ITEM_t *item) {
  (void)item;
  return time_formatted(ctx, nextop,
      "time.timestamp expects an integer timestamp in milliseconds",
      TIME_FORMAT_TIMESTAMP);
}

uint8_t *lc_time_time(RuntimeContext *ctx, uint8_t *nextop, ITEM_t *item) {
  (void)item;
  return time_formatted(ctx, nextop,
      "time.time expects an integer timestamp in milliseconds",
      TIME_FORMAT_TIME);
}

uint8_t *lc_time_date(RuntimeContext *ctx, uint8_t *nextop, ITEM_t *item) {
  (void)item;
  return time_formatted(ctx, nextop,
      "time.date expects an integer timestamp in milliseconds",
      TIME_FORMAT_DATE);
}

uint8_t *lc_time_fulldate(RuntimeContext *ctx, uint8_t *nextop, ITEM_t *item) {
  (void)item;
  return time_formatted(ctx, nextop,
      "time.fulldate expects an integer timestamp in milliseconds",
      TIME_FORMAT_FULLDATE);
}
