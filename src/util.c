// Utility functions

#include <limits.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <string.h>
#include <errno.h>

#include "util.h"
#include "log.h"

char* itoa(int value, char* buffer, int base) {
    // Handle negative numbers
    if (value < 0 && base == 10) {
        *buffer++ = '-';  // Add minus sign
        value = -value;   // Make the value positive for conversion
    }

    // Efficiently convert digits using a lookup table
    static const char digits[] = "0123456789abcdefghijklmnopqrstuvwxyz";
    char* p = buffer;
    int temp = value;
    do {
        int digit = temp % base;
        *p++ = digits[digit];
        temp /= base;
    } while (temp);

    // Reverse the string in-place
    *p-- = '\0';
    while (buffer < p) {
        char c = *buffer;
        *buffer++ = *p;
        *p-- = c;
    }

    return buffer;
}

bool make_path(char *path) {
  // Create the intermediate directories first
  for (char *p = strchr(path + 1, '/'); p; p = strchr(p + 1, '/')) {
    *p = '\0'; // Temporarily truncate
    if (mkdir(path, S_IRWXU) != 0) {
      if (errno != EEXIST) {
        *p = '/';
        logerr("Failed to create directories for %s: %s\n", path,
                                                          strerror(errno));
        return false;
      }
    }
    *p = '/';
  }
  // Attempt to create the last directory in the path, if necessary.
  if (mkdir(path, S_IRWXU) != 0 && errno != EEXIST) {
    logerr("Failed to create directory %s: %s\n", path, strerror(errno));
    return false;
  }
  return true;
}

