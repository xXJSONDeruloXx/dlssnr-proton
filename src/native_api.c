#define _POSIX_C_SOURCE 200809L
#include "native_api.h"
#include <dlfcn.h>
#include <stdio.h>
#include <string.h>

static int resolve(void *library, const char *name, void **out, char *error, size_t error_size) {
    dlerror();
    *out=dlsym(library,name);
    const char *message=dlerror();
    if (message || !*out) {
        if (error && error_size) snprintf(error,error_size,"missing %s: %s",name,message?message:"unknown");
        return -1;
    }
    return 0;
}
#define RESOLVE(field,name) do {     void *symbol=NULL;     if (resolve(api->library,name,&symbol,error,error_size)) goto fail;     memcpy(&api->field,&symbol,sizeof(symbol)); } while(0)

int nr_native_api_open(const char *path, NrNativeApi *api, char *error, size_t error_size) {
    if (!path || !api) return -1;
    memset(api,0,sizeof(*api));
    api->library=dlopen(path,RTLD_NOW|RTLD_LOCAL);
    if (!api->library) {
        if (error && error_size) snprintf(error,error_size,"dlopen: %s",dlerror());
        return -2;
    }
    RESOLVE(abi_version,"dlssnr_abi_version");
    RESOLVE(model_open,"dlssnr_model_open");
    RESOLVE(model_close,"dlssnr_model_close");
    RESOLVE(model_source_kind,"dlssnr_model_source_kind");
    RESOLVE(model_tensor_count,"dlssnr_model_tensor_count");
    RESOLVE(model_dll_sha256,"dlssnr_model_dll_sha256");
    RESOLVE(model_weights_sha256,"dlssnr_model_weights_sha256");
    RESOLVE(runtime_create,"dlssnr_runtime_create");
    RESOLVE(runtime_destroy,"dlssnr_runtime_destroy");
    RESOLVE(runtime_device_name,"dlssnr_runtime_device_name");
    RESOLVE(runtime_set_model,"dlssnr_runtime_set_model");
    if (api->abi_version()!=1) {
        if (error && error_size) snprintf(error,error_size,"unsupported ABI: %u",api->abi_version());
        goto fail;
    }
    return 0;
fail:
    nr_native_api_close(api);
    return -3;
}
void nr_native_api_close(NrNativeApi *api) {
    if (!api) return;
    if (api->library) dlclose(api->library);
    memset(api,0,sizeof(*api));
}
