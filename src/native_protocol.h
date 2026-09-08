#pragma once
#include <stdint.h>

#define NR_NATIVE_MAGIC 0x544e524eu
#define NR_NATIVE_PROTOCOL_VERSION 1u
#define NR_NATIVE_TOKEN_MAX 128u
#define NR_NATIVE_TEXT_MAX 256u

enum {
    NR_NATIVE_HELLO=1,
    NR_NATIVE_INFO=2,
    NR_NATIVE_PING=3,
    NR_NATIVE_CLOSE=4
};

typedef struct {
    uint32_t magic;
    uint32_t version;
    uint32_t op;
    int32_t status;
    uint64_t a[8];
    char token[NR_NATIVE_TOKEN_MAX];
    char text[NR_NATIVE_TEXT_MAX];
} NrNativePacket;

#ifdef __cplusplus
static_assert(sizeof(NrNativePacket)==464,"native wire ABI");
#else
_Static_assert(sizeof(NrNativePacket)==464,"native wire ABI");
#endif
