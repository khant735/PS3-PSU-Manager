/* PS3 PSL1GHT does not expose POSIX creat().
   The 7-Zip SDK's 7zFile.c references it for optional output streams.
   This application only uses FileInStream; reject all create requests. */
#include <errno.h>
int creat(const char *path, unsigned int mode) {
 (void)path;
 (void)mode;
 errno=ENOSYS;
 return -1;
}
