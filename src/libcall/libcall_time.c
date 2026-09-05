// Time libcall support.

// Licensed under the MIT License - see LICENSE file for details.

#include <stdint.h>
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
