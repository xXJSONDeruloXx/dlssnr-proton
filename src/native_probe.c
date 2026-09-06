#include "native_api.h"
#include <stdio.h>

static const char *source_name(int source) {
    if (source==2) return "dll";
    if (source==1) return "package";
    return "unknown";
}

int main(int argc, char **argv) {
    if (argc!=3) {
        fprintf(stderr,"usage: %s LIBDLSSNR MODEL\n",argv[0]);
        return 64;
    }
    NrNativeApi api;
    char error[512];
    if (nr_native_api_open(argv[1],&api,error,sizeof(error))) {
        fprintf(stderr,"%s\n",error);
        return 2;
    }
    dlssnr_model *model=NULL;
    int result=api.model_open(argv[2],&model);
    if (result) {
        fprintf(stderr,"model_open=%d\n",result);
        nr_native_api_close(&api);
        return 3;
    }
    printf("abi=%u source=%s tensors=%u\n",api.abi_version(),
           source_name(api.model_source_kind(model)),api.model_tensor_count(model));
    printf("dll_sha256=%s\nweights_sha256=%s\n",
           api.model_dll_sha256(model),api.model_weights_sha256(model));

    dlssnr_runtime *runtime=NULL;
    result=api.runtime_create(&runtime);
    if (result) {
        fprintf(stderr,"runtime_create=%d\n",result);
        api.model_close(model);nr_native_api_close(&api);
        return 4;
    }
    result=api.runtime_set_model(runtime,model);
    if (result) {
        fprintf(stderr,"runtime_set_model=%d\n",result);
        api.runtime_destroy(runtime);api.model_close(model);nr_native_api_close(&api);
        return 5;
    }
    printf("device=%s\n",api.runtime_device_name(runtime));
    api.runtime_destroy(runtime);
    api.model_close(model);
    nr_native_api_close(&api);
    return 0;
}
