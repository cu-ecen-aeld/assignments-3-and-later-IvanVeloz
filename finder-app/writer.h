#include <sys/stat.h>
#include <fcntl.h>

int main(int argc, char *argv[]);
int validate_args(int argc);
int open_file(char *path);
inline int close_file(int desc);
int touch_file(void);
inline int mk_dir(const char *path);
int mk_dir_r(const char *path);
int print_args(int argc, char *argv[]);

/* Flags for file opening.
 * O_CREAT  Creates file if it doesn't exist yet.
 * O_EXCL   "When given with O_CREAT, this flag will cause the call to open() 
 *          to fail if the file given by name already exists. This is used to 
 *          prevent race conditions on file creation. If O_CREAT is not also 
 *          provided, this flag has no meaning." - Linux System Programming,
 *          Robert Love, published May 2013 by O'Riley.
 * O_TRUNC  "If the file exists, it is a regular file, and the given flags 
 *          allow for writing, the file will be truncated to zero length.[...]"
 *          Linux System Programming, Robert Love, published May 2013 by O'Riley
 */

// For FLAGS_OPEN we don't actually want O_EXCL
// And using O_WRONLY because we want to use as little permissions as necessary.

const int FLAGS_OPEN = O_CREAT | O_TRUNC | O_WRONLY;
const int MODE_OPEN  = S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP; 

const int MODE_MKDIR = S_IRWXU | S_IRWXG;