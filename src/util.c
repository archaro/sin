// Utility functions

#include <sys/stat.h>
#include <sys/types.h>
#include <string.h>
#include <errno.h>

#include "util.h"
#include "log.h"

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

