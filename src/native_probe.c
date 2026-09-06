#define _POSIX_C_SOURCE 200809L
#include <dlfcn.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

typedef struct dlssnr_model dlssnr_model;
typedef struct dlssnr_runtime dlssnr_runtime;

typedef uint32_t (*abi_version_fn)(void);
typedef int (*model_open_fn)(const char *, dlssnr_model **);
typedef void (*model_close_fn)(dlssnr_model *);
typedef int (*model_source_fn)(const dlssnr_model *);
typedef uint32_t (*model_count_fn)(const dlssnr_model *);
typedef const char *(*model_hash_fn)(const dlssnr_model *);
typedef int (*runtime_create_fn)(dlssnr_runtime **);
typedef void (*runtime_destroy_fn)(dlssnr_runtime *);
typedef const char *(*runtime_name_fn)(const dlssnr_runtime *);
typedef int (*runtime_set_model_fn)(dlssnr_runtime *, dlssnr_model *);

static void *symbol(void *library, const char *name) {
    void *result=dlsym(library,name);
    if (!result) {
        fprintf(stderr,"missing symbol: %s\n",name);
        exit(3);
    }
    return result;
}

int main(int argc, char **argv) {
    if (argc!=3) {
        fprintf(stderr,"usage: %s LIBDLSSNR MODEL\n",argv[0]);
        return 64;
    }
    void *library=dlopen(argv[1],RTLD_NOW|RTLD_LOCAL);
    if (!library) {
        fprintf(stderr,"dlopen: %s\n",dlerror());
        return 2;
    }
    abi_version_fn abi=(abi_version_fn)symbol(library,"dlssnr_abi_version");
    model_open_fn open_model=(model_open_fn)symbol(library,"dlssnr_model_open");
    model_close_fn close_model=(model_close_fn)symbol(library,"dlssnr_model_close");
    model_source_fn source=(model_source_fn)symbol(library,"dlssnr_model_source_kind");
    model_count_fn count=(model_count_fn)symbol(library,"dlssnr_model_tensor_count");
    model_hash_fn dll_hash=(model_hash_fn)symbol(library,"dlssnr_model_dll_sha256");
    model_hash_fn weights_hash=(model_hash_fn)symbol(library,"dlssnr_model_weights_sha256");
    runtime_create_fn create_runtime=(runtime_create_fn)symbol(library,"dlssnr_runtime_create");
    runtime_destroy_fn destroy_runtime=(runtime_destroy_fn)symbol(library,"dlssnr_runtime_destroy");
    runtime_name_fn device_name=(runtime_name_fn)symbol(library,"dlssnr_runtime_device_name");
    runtime_set_model_fn set_model=(runtime_set_model_fn)symbol(library,"dlssnr_runtime_set_model");

    if (abi()!=1) {
        fprintf(stderr,"unsupported ABI: %u\n",abi());
        dlclose(library);
        return 4;
    }
    dlssnr_model *model=NULL;
    int result=open_model(argv[2],&model);
    if (result) {
        fprintf(stderr,"model_open=%d\n",result);
        dlclose(library);
        return 5;
    }
    printf("abi=%u source=%d tensors=%u\n",abi(),source(model),count(model));
    printf("dll_sha256=%s\nweights_sha256=%s\n",dll_hash(model),weights_hash(model));

    dlssnr_runtime *runtime=NULL;
    result=create_runtime(&runtime);
    if (result) {
        fprintf(stderr,"runtime_create=%d\n",result);
        close_model(model);dlclose(library);
        return 6;
    }
    result=set_model(runtime,model);
    if (result) {
        fprintf(stderr,"runtime_set_model=%d\n",result);
        destroy_runtime(runtime);close_model(model);dlclose(library);
        return 7;
    }
    printf("device=%s\n",device_name(runtime));
    destroy_runtime(runtime);
    close_model(model);
    dlclose(library);
    return 0;
}
