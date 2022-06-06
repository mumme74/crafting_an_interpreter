#ifndef COMMON_H
#define COMMON_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

//#define DEBUG_STRESS_GC
//#define DEBUG_LOG_GC
//#define DEBUG_LOG_GC_MARK 0
//#define DEBUG_LOG_GC_FREE 0
//#define DEBUG_LOG_GC_ALLOC 0
//#define DEBUG_STRESS_GC_OLDER

//#define DEBUG_TRACE_EXECUTION
//#define DEBUG_PRINT_CODE

//#define NAN_BOXING

#define COMPUTED_GOTO

#define UINT8_COUNT (UINT8_MAX + 1)


typedef uint8_t ObjFlags;

#endif // COMMON_H
