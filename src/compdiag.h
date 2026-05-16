#pragma once

#include <stdbool.h>
#include <stdint.h>

bool compdiag_set_once(int8_t *current_errnum, char **errdetail,
                       int8_t new_errnum, const char *phase,
                       const char *detail);
bool compdiag_setf_once(int8_t *current_errnum, char **errdetail,
                        int8_t new_errnum, const char *phase,
                        const char *fmt, ...);
void compdiag_reset_detail(char **errdetail);
