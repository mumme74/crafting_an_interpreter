#ifndef COMMON_H
#define COMMON_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

//#define DEBUG_STRESS_GC
//#define DEBUG_STRESS_GC_OLDER
//#define DEBUG_LOG_GC
#define DEBUG_LOG_GC_MARK 0
//#define DEBUG_LOG_GC_FREE 0
#define DEBUG_LOG_GC_ALLOC 0

//#define DEBUG_TRACE_EXECUTION
//#define DEBUG_PRINT_CODE


#define NAN_BOXING

#define COMPUTED_GOTO

#define UINT8_COUNT (UINT8_MAX + 1)

#define LOX_VERSION "0.1"

typedef uint8_t ObjFlags;

typedef struct PathInfo {
  const char *path;
  int pathLen;
  const char *dirname;
  int dirnameLen;
  const char *basename;
  int basenameLen;
  const char *ext;
  int extLen;
  const char *filename;
  int filenameLen;
} PathInfo;

// read contents of file at path, reciever tkaes overship over
// ALLOCATE chars.
char *readFile(const char *path);
// checks if path exists as a file
bool fileExists(const char *path);

PathInfo parsePath(const char *path);

// initalize things before initVM
void loxInit(int argc, char * const argv[]);

// initalize type system
void initTypes();
// free typesystem
void freeTypes();

#endif // COMMON_H
