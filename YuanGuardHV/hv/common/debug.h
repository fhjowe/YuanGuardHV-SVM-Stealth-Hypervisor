#ifndef YGHV_DEBUG_H
#define YGHV_DEBUG_H

#include <ntddk.h>

#define YGHV_TAG 'vhGY'  /* pool tag for YuanGuardHV allocations */

#ifdef YGHV_DEBUG_LOG
#define LOG_INFO(fmt, ...)  DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_INFO_LEVEL, "[YGHV] " fmt "\n", ##__VA_ARGS__)
#define LOG_ERROR(fmt, ...) DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL, "[YGHV][E] " fmt "\n", ##__VA_ARGS__)
#define LOG_WARN(fmt, ...)  DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_WARNING_LEVEL, "[YGHV][W] " fmt "\n", ##__VA_ARGS__)
#else
#define LOG_INFO(fmt, ...)  ((void)0)
#define LOG_ERROR(fmt, ...) ((void)0)
#define LOG_WARN(fmt, ...)  ((void)0)
#endif

void yghv_trace(const char *msg);
void yghv_trace_u64(const char *label, uint64_t v);

#endif
