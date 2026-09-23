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

static bool time_is_leap_year(int64_t year) {
  return year % INT64_C(4) == 0 &&
      (year % INT64_C(100) != 0 || year % INT64_C(400) == 0);
}

static int64_t time_days_from_civil(int64_t year, int64_t month,
                                    int64_t day) {
  /* March-based 400-year eras keep every intermediate bounded for the
   * representable calendar range. The adjustment implements floor division
   * for negative astronomical years using C's truncation-toward-zero divide. */
  int64_t adjusted_year = year - (month <= 2 ? 1 : 0);
  int64_t era = adjusted_year >= 0
      ? adjusted_year / INT64_C(400)
      : (adjusted_year - INT64_C(399)) / INT64_C(400);
  int64_t year_of_era = adjusted_year - era * INT64_C(400);
  int64_t month_index = month + (month > 2 ? -3 : 9);
  int64_t day_of_year = (INT64_C(153) * month_index + 2) / 5 + day - 1;
  int64_t day_of_era = year_of_era * INT64_C(365) + year_of_era / 4 -
      year_of_era / 100 + day_of_year;
  return era * INT64_C(146097) + day_of_era - INT64_C(719468);
}

static uint8_t *time_make_invalid(RuntimeContext *ctx, uint8_t *nextop,
                                  VALUE_t *values) {
  lc_cleanup_values(values, 6);
  return lc_invalid_args_nil_return(ctx, nextop,
      "time.make expects six integer calendar components");
}

uint8_t *lc_time_make(RuntimeContext *ctx, uint8_t *nextop, ITEM_t *item) {
  VALUE_t values[6];
  int64_t year;
  int64_t month;
  int64_t day;
  int64_t hour;
  int64_t minute;
  int64_t second;
  int64_t days;
  int64_t seconds;
  (void)item;

  /* Source arguments are pushed left-to-right; pop them right-to-left. */
  values[5] = pop_stack(ctx->vm->stack);
  values[4] = pop_stack(ctx->vm->stack);
  values[3] = pop_stack(ctx->vm->stack);
  values[2] = pop_stack(ctx->vm->stack);
  values[1] = pop_stack(ctx->vm->stack);
  values[0] = pop_stack(ctx->vm->stack);

  for (size_t i = 0; i < 6; ++i) {
    if (values[i].type != VALUE_int) {
      return time_make_invalid(ctx, nextop, values);
    }
  }
  year = values[0].i;
  month = values[1].i;
  day = values[2].i;
  hour = values[3].i;
  minute = values[4].i;
  second = values[5].i;

  if (month < 1 || month > 12 || day < 1 ||
      day > (month == 2 ? (time_is_leap_year(year) ? 29 : 28)
                         : (month == 4 || month == 6 || month == 9 ||
                            month == 11 ? 30 : 31)) ||
      hour < 0 || hour > 23 || minute < 0 || minute > 59 ||
      second < 0 || second > 59) {
    return time_make_invalid(ctx, nextop, values);
  }

  /* A wider year would necessarily place the result outside int64
   * milliseconds; this guard also keeps all civil-date intermediates safe. */
  if (year < -INT64_C(300000000) || year > INT64_C(300000000)) {
    lc_cleanup_values(values, 6);
    return lc_undefined_nil_return(ctx, nextop);
  }

  days = time_days_from_civil(year, month, day);
  seconds = days * INT64_C(86400) + hour * INT64_C(3600) +
      minute * INT64_C(60) + second;
  if (seconds < INT64_MIN / INT64_C(1000) ||
      seconds > INT64_MAX / INT64_C(1000)) {
    lc_cleanup_values(values, 6);
    return lc_undefined_nil_return(ctx, nextop);
  }

  lc_cleanup_values(values, 6);
  push_stack(ctx->vm->stack,
             (VALUE_t){VALUE_int, {.i = seconds * INT64_C(1000)}});
  return nextop;
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
  TIME_WEEKDAY,
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
    case TIME_WEEKDAY:
      value = utc.tm_wday == 0 ? 7 : utc.tm_wday;
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

uint8_t *lc_time_weekday(RuntimeContext *ctx, uint8_t *nextop, ITEM_t *item) {
  (void)item;
  return time_calendar_part(ctx, nextop,
      "time.weekday expects an integer timestamp in milliseconds",
      TIME_WEEKDAY);
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
