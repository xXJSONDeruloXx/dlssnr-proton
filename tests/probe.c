#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <stdarg.h>
static HANDLE log_handle;
static void report(const char *format,...) {
    char buffer[1024];va_list args;va_start(args,format);
    int n=vsnprintf(buffer,sizeof(buffer),format,args);va_end(args);
    if(n>0){DWORD written;WriteFile(log_handle,buffer,n<1024?(DWORD)n:1023,&written,0);FlushFileBuffers(log_handle);}
}
typedef struct {unsigned x,y,z;} Dim3;
#define LOAD(name,ret,...) ret (*name)(__VA_ARGS__)=(void*)GetProcAddress(dll,#name);if(!name)return 3
#define CHECK(expr) do{report("calling %s\n",#expr);int e=(expr);if(e){report("%s failed %d\n",#expr,e);return 2;}}while(0)
int main(void) {
    log_handle=CreateFileA("bridge-probe-result.log",GENERIC_WRITE,FILE_SHARE_READ,0,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,0);
    HMODULE dll=LoadLibraryA("amdhip64_7.dll");if(!dll){report("load failed %lu\n",GetLastError());return 2;}
    LOAD(hipGetDeviceCount,int,int*);
    LOAD(hipDriverGetVersion,int,int*);
    LOAD(hipRuntimeGetVersion,int,int*);
    LOAD(hipMalloc,int,void**,size_t);
    LOAD(hipMemset,int,void*,int,size_t);
    LOAD(hipMemcpy,int,void*,const void*,size_t,int);
    LOAD(hipFree,int,void*);
    LOAD(hipDeviceSynchronize,int,void);
    LOAD(__hipRegisterFunction,void,void**,const void*,char*,const char*,int,void*,void*,void*,void*,void*);
    LOAD(hipLaunchKernel,int,const void*,Dim3,Dim3,void**,size_t,void*);
    int count=0;CHECK(hipGetDeviceCount(&count));if(count<1)return 2;
    int driver=0,runtime=0;CHECK(hipDriverGetVersion(&driver));CHECK(hipRuntimeGetVersion(&runtime));
    report("driver=%d runtime=%d\n",driver,runtime);
    void *device=0;CHECK(hipMalloc(&device,8));CHECK(hipMemset(device,0,8));
    const void *stub=(void*)(uintptr_t)0x1234;
    __hipRegisterFunction(0,stub,0,"_Z10k_flag_setPjj",0,0,0,0,0,0);
    unsigned value=0x12345678;void *args[]={&device,&value};
    CHECK(hipLaunchKernel(stub,(Dim3){1,1,1},(Dim3){32,1,1},args,0,0));
    CHECK(hipDeviceSynchronize());
    unsigned result[2]={0};CHECK(hipMemcpy(result,device,8,2));CHECK(hipFree(device));
    report("{\"device_count\":%d,\"result\":[%u,%u],\"expected\":305419896,\"pass\":%s}\n",count,result[0],result[1],result[1]==value?"true":"false");
    return result[1]!=value;
}
