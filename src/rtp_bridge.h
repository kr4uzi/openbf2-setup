// [CHOSEN, not recovered] C ABI for the pinned MIT rtptool fork.
#pragma once
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct RtpHandle RtpHandle;
typedef struct { uint8_t* data; size_t size; } RtpBytes;
// Returned errors are UTF-8, thread local, valid until the next bridge call.
const char* obf2_rtp_error(void);
RtpHandle* obf2_rtp_open(const uint8_t* data, size_t size);
typedef int (*RtpRead)(void* context, size_t offset, uint8_t* output, size_t length);
RtpHandle* obf2_rtp_open_reader(size_t size, RtpRead read, void* context);
void obf2_rtp_close(RtpHandle* patch);
size_t obf2_rtp_count(const RtpHandle* patch);
// The copied pathname is NUL terminated; insufficient capacity is an error.
int obf2_rtp_name(const RtpHandle* patch, size_t index, char* name, size_t capacity);
// 0: produced bytes (caller frees); 1: deleted/absent; -1: error. Present empty
// files are distinct from absent files. Size and all source/destination checks stay enabled.
int obf2_rtp_apply(const RtpHandle* patch, size_t index, const uint8_t* source,
                   size_t size, int present, RtpBytes* result);
int obf2_rtp_terminal(const RtpHandle* patch, size_t index);
void obf2_rtp_free(RtpBytes bytes);
#ifdef __cplusplus
}
#endif
