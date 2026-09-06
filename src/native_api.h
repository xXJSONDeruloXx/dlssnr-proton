#pragma once
#include <stddef.h>
#include <stdint.h>

typedef struct dlssnr_model dlssnr_model;
typedef struct dlssnr_runtime dlssnr_runtime;

typedef struct {
    uint32_t model_tensors;
    uint32_t required_tensors;
    uint32_t present_required_tensors;
    uint32_t auxiliary_tensors;
} NrStage1Inventory;

typedef struct {
    void *library;
    uint32_t (*abi_version)(void);
    int (*model_open)(const char *, dlssnr_model **);
    void (*model_close)(dlssnr_model *);
    int (*model_source_kind)(const dlssnr_model *);
    uint32_t (*model_tensor_count)(const dlssnr_model *);
    const char *(*model_dll_sha256)(const dlssnr_model *);
    const char *(*model_weights_sha256)(const dlssnr_model *);
    int (*model_stage1_inventory)(const dlssnr_model *, NrStage1Inventory *);
    int (*runtime_create)(dlssnr_runtime **);
    void (*runtime_destroy)(dlssnr_runtime *);
    const char *(*runtime_device_name)(const dlssnr_runtime *);
    int (*runtime_set_model)(dlssnr_runtime *, dlssnr_model *);
} NrNativeApi;

int nr_native_api_open(const char *path, NrNativeApi *api, char *error, size_t error_size);
void nr_native_api_close(NrNativeApi *api);
