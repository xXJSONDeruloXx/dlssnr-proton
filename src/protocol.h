#pragma once
#include <stdint.h>
#define NR_MAGIC 0x4e524850u
#define NR_MAX_DATA (512u * 1024u * 1024u)
enum { NR_HELLO=1, NR_COUNT, NR_PROPS, NR_ALLOC, NR_FREE, NR_COPY,
       NR_SET, NR_LAUNCH, NR_SYNC, NR_GLOBAL, NR_EVENT_CREATE,
       NR_EVENT_RECORD, NR_EVENT_SYNC, NR_EVENT_TIME, NR_DEVICE, NR_VERSION };
typedef struct {
    uint32_t magic, op, status, size;
    uint64_t a[8];
    char name[256];
} NrPacket;
#ifdef __cplusplus
static_assert(sizeof(NrPacket)==336, "wire ABI");
#else
_Static_assert(sizeof(NrPacket)==336, "wire ABI");
#endif
