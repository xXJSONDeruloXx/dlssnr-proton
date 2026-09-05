// Windows HIP ABI shim for the pinned private runtime. Uses synchronous host
// staging and a single native default stream; external memory fails explicitly.
#include <winsock2.h>
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include "protocol.h"
#include "signatures.h"
#define API __declspec(dllexport)
typedef struct { unsigned x,y,z; } Dim3;
typedef struct { Dim3 grid,block; size_t shared; void *stream; } Config;
static __thread Config config_stack[16];
static __thread unsigned config_depth;
static __thread int last_error;
static SRWLOCK lock=SRWLOCK_INIT;
static SOCKET sock=INVALID_SOCKET;
static int broken;
static struct { const void *host; char name[256]; } functions[128], variables[128];
static unsigned nfunctions,nvariables;

static int io(void *buffer,size_t n,int writing) {
    char *p=buffer;
    while(n){int amount=n>1048576?1048576:(int)n; int r=writing?send(sock,p,amount,0):recv(sock,p,amount,0);if(r<=0)return 0;p+=r;n-=r;}
    return 1;
}
static int connect_server(void) {
    if(sock!=INVALID_SOCKET)return 1;
    if(broken)return 0;
    char port[16]={0},token[256]={0};
    if(!GetEnvironmentVariableA("DLSSNR_HIP_PORT",port,sizeof(port)) ||
       !GetEnvironmentVariableA("DLSSNR_HIP_TOKEN",token,sizeof(token))) {
        HMODULE self=0;char path[MAX_PATH],settings[300]={0};DWORD n=0;
        if(!GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,(const char*)&connect_server,&self))return 0;
        if(!GetModuleFileNameA(self,path,sizeof(path)))return 0;
        char *slash=strrchr(path,'\\');if(!slash||slash-path>MAX_PATH-30)return 0;
        strcpy(slash+1,".dlssnr-hip-session");
        HANDLE f=CreateFileA(path,GENERIC_READ,FILE_SHARE_READ,0,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,0);
        if(f==INVALID_HANDLE_VALUE)return 0;
        BOOL ok=ReadFile(f,settings,sizeof(settings)-1,&n,0);CloseHandle(f);
        if(!ok||sscanf(settings,"%15s %255s",port,token)!=2||strlen(token)<32)return 0;
    }
    WSADATA w; if(WSAStartup(MAKEWORD(2,2),&w))return 0;
    sock=socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);
    struct sockaddr_in a={0};a.sin_family=AF_INET;a.sin_port=htons((unsigned short)atoi(port));a.sin_addr.s_addr=htonl(INADDR_LOOPBACK);
    int yes=1;setsockopt(sock,IPPROTO_TCP,TCP_NODELAY,(char*)&yes,sizeof(yes));
    if(connect(sock,(struct sockaddr*)&a,sizeof(a)))goto fail;
    NrPacket p={.magic=NR_MAGIC,.op=NR_HELLO};memcpy(p.name,token,strlen(token)+1);
    if(!io(&p,sizeof(p),1)||!io(&p,sizeof(p),0)||p.magic!=NR_MAGIC||p.status||p.size)goto fail;
    return 1;
fail: closesocket(sock);sock=INVALID_SOCKET;broken=1;return 0;
}
static int rpc(NrPacket *p,const void *input,void *output,size_t capacity) {
    AcquireSRWLockExclusive(&lock);int result=999;
    if(!connect_server())goto done;
    unsigned op=p->op;p->magic=NR_MAGIC;
    if(!io(p,sizeof(*p),1)||(p->size&&!io((void*)input,p->size,1))||!io(p,sizeof(*p),0))goto fail;
    if(p->magic!=NR_MAGIC||p->op!=op||p->size>capacity)goto fail;
    if(p->size&&!io(output,p->size,0))goto fail;
    result=p->status;goto done;
fail: closesocket(sock);sock=INVALID_SOCKET;broken=1;
done: ReleaseSRWLockExclusive(&lock);last_error=result;return result;
}
API void **__hipRegisterFatBinary(const void *data) { (void)data; static void *handle;return &handle; }
API void __hipUnregisterFatBinary(void **handle) {(void)handle;}
API void __hipRegisterFunction(void **h,const void *host,char *device,const char *name,int limit,void *a,void *b,void *c,void *d,void *e) {
    (void)h;(void)device;(void)limit;(void)a;(void)b;(void)c;(void)d;(void)e;
    if(nfunctions<128){functions[nfunctions].host=host;strncpy(functions[nfunctions++].name,name,255);}
}
API void __hipRegisterVar(void **h,void *host,char *device,const char *name,int ext,size_t size,int constant,int global) {
    (void)h;(void)device;(void)ext;(void)size;(void)constant;(void)global;
    if(nvariables<128){variables[nvariables].host=host;strncpy(variables[nvariables++].name,name,255);}
}
API int __hipPushCallConfiguration(Dim3 grid,Dim3 block,size_t shared,void *stream) {
    if(config_depth==16||stream)return last_error=801;
    config_stack[config_depth++]=(Config){grid,block,shared,stream};return 0;
}
API int __hipPopCallConfiguration(Dim3 *grid,Dim3 *block,size_t *shared,void **stream) {
    if(!config_depth)return last_error=1;
    Config c=config_stack[--config_depth];*grid=c.grid;*block=c.block;*shared=c.shared;*stream=c.stream;return 0;
}
API int hipGetDeviceCount(int *count) {NrPacket p={.op=NR_COUNT};int e=rpc(&p,0,0,0);if(!e)*count=p.a[0];return e;}
API int hipSetDevice(int device) {NrPacket p={.op=NR_DEVICE,.a={(uint64_t)device}};return rpc(&p,0,0,0);}
// R0600 is the fixed public property ABI shared by HIP 6 and HIP 7.
API int hipGetDevicePropertiesR0600(void *props,int device) {NrPacket p={.op=NR_PROPS,.a={(uint64_t)device}};return rpc(&p,0,props,2048);}
API int hipDriverGetVersion(int *v) {NrPacket p={.op=NR_VERSION,.a={0}};int e=rpc(&p,0,0,0);if(!e)*v=p.a[0];return e;}
API int hipRuntimeGetVersion(int *v) {NrPacket p={.op=NR_VERSION,.a={1}};int e=rpc(&p,0,0,0);if(!e)*v=p.a[0];return e;}
API const char *hipGetErrorString(int e) {switch(e){case 0:return "hipSuccess";case 1:return "hipErrorInvalidValue";case 100:return "hipErrorNoDevice";case 801:return "hipErrorNotSupported (Linux staging bridge)";case 999:return "HIP bridge disconnected/unavailable";default:return "HIP backend error; see native server log";}}
API int hipGetLastError(void) {int e=last_error;last_error=0;return e;}
API int hipMalloc(void **ptr,size_t n) {NrPacket p={.op=NR_ALLOC,.a={n}};int e=rpc(&p,0,0,0);if(!e)*ptr=(void*)p.a[0];return e;}
API int hipFree(void *ptr) {if(!ptr)return 0;NrPacket p={.op=NR_FREE,.a={(uintptr_t)ptr}};return rpc(&p,0,0,0);}
API int hipMemcpy(void *dst,const void *src,size_t n,int kind) {
    if(!n)return 0;
    if(kind==0){memmove(dst,src,n);return 0;}
    if(n>NR_MAX_DATA||kind<1||kind>3)return last_error=801;
    NrPacket p={.op=NR_COPY,.size=kind==1?(uint32_t)n:0,.a={(uintptr_t)dst,(uintptr_t)src,n,kind}};
    return rpc(&p,kind==1?src:0,kind==2?dst:0,kind==2?n:0);
}
API int hipMemcpyAsync(void *dst,const void *src,size_t n,int kind,void *stream) {if(stream)return last_error=801;return hipMemcpy(dst,src,n,kind);}
API int hipMemcpyToSymbol(const void *symbol,const void *src,size_t n,size_t offset,int kind) {
    NrPacket p={.op=NR_GLOBAL};
    for(unsigned i=0;i<nvariables;i++)if(variables[i].host==symbol){strcpy(p.name,variables[i].name);break;}
    if(!p.name[0])return last_error=1;
    int e=rpc(&p,0,0,0);if(e)return e;if(offset>p.a[1]||n>p.a[1]-offset)return last_error=1;
    return hipMemcpy((void*)(p.a[0]+offset),src,n,kind);
}
API int hipMemset(void *ptr,int value,size_t n) {NrPacket p={.op=NR_SET,.a={(uintptr_t)ptr,(unsigned)value,n}};return rpc(&p,0,0,0);}
API int hipMemsetAsync(void *ptr,int value,size_t n,void *stream) {if(stream)return last_error=801;return hipMemset(ptr,value,n);}
API int hipLaunchKernel(const void *host,Dim3 grid,Dim3 block,void **args,size_t shared,void *stream) {
    if(stream)return last_error=801;
    NrPacket p={.op=NR_LAUNCH,.a={grid.x,grid.y,grid.z,block.x,block.y,block.z,shared}};
    for(unsigned i=0;i<nfunctions;i++)if(functions[i].host==host){strcpy(p.name,functions[i].name);break;}
    const NrSignature *sig=0;
    for(unsigned i=0;i<sizeof(nr_signatures)/sizeof(nr_signatures[0]);i++)if(!strcmp(p.name,nr_signatures[i].name)){sig=&nr_signatures[i];break;}
    if(!sig)return last_error=801;
    unsigned char packed[1024]={0};p.size=sig->size;
    for(unsigned i=0;i<sig->count;i++)memcpy(packed+sig->offsets[i],args[i],sig->sizes[i]);
    return rpc(&p,packed,0,0);
}
API int hipDeviceSynchronize(void) {NrPacket p={.op=NR_SYNC};return rpc(&p,0,0,0);}
API int hipEventCreate(void **event) {NrPacket p={.op=NR_EVENT_CREATE};int e=rpc(&p,0,0,0);if(!e)*event=(void*)p.a[0];return e;}
API int hipEventRecord(void *event,void *stream) {if(stream)return last_error=801;NrPacket p={.op=NR_EVENT_RECORD,.a={(uintptr_t)event}};return rpc(&p,0,0,0);}
API int hipEventSynchronize(void *event) {NrPacket p={.op=NR_EVENT_SYNC,.a={(uintptr_t)event}};return rpc(&p,0,0,0);}
API int hipEventElapsedTime(float *ms,void *start,void *end) {NrPacket p={.op=NR_EVENT_TIME,.a={(uintptr_t)start,(uintptr_t)end}};int e=rpc(&p,0,0,0);if(!e)memcpy(ms,&p.a[0],4);return e;}
API int hipImportExternalMemory(void **mem,void *desc) {(void)mem;(void)desc;return last_error=801;}
API int hipExternalMemoryGetMappedBuffer(void **ptr,void *mem,void *desc) {(void)ptr;(void)mem;(void)desc;return last_error=801;}
API int hipDestroyExternalMemory(void *mem) {(void)mem;return last_error=801;}
