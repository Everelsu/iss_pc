// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct IssTrophyBackend {
    int32_t (*unlock)(int32_t id,int32_t *platinum);
    int32_t (*state)(uint32_t *flags,uint32_t *count);
    void (*save_open)(int fd,int writing,uint64_t length);
    void (*save_part)(int fd,const void *bytes,int64_t offset,int64_t size,int writing);
    void (*save_close)(int fd);
    void (*progress_reset)(void);
} IssTrophyBackend;
// Installed once at process startup by the optional native trophy backend.
void runtime_trophy_set_backend(const IssTrophyBackend *backend);
void runtime_trophy_save_open(int fd,int writing,uint64_t length);
void runtime_trophy_save_part(int fd,const void *bytes,int64_t offset,int64_t size,int writing);
void runtime_trophy_save_close(int fd);
void runtime_trophy_set_save_mode_ready(int ready);
int runtime_trophy_save_mode_ready(void);
uint8_t __attribute__((sysv_abi)) runtime_trophy_query(void *object,int32_t id);
void runtime_trophy_attach_reset(uintptr_t object,uintptr_t original);
void __attribute__((sysv_abi)) runtime_trophy_progress_reset(void *object);
#ifdef __cplusplus
}
#endif
