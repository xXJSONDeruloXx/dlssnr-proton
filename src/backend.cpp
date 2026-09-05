// Single-client loopback HIP transport. Device pointers stay opaque
// in the PE client; only host staging data crosses the socket.
#define __HIP_PLATFORM_AMD__
#include <hip/hip_runtime_api.h>
#include <arpa/inet.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <unistd.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <unordered_set>
#include "protocol.h"

static bool transfer(int fd, void *p, size_t n, bool send_data) {
    auto b = static_cast<char *>(p);
    while (n) {
        ssize_t r = send_data ? send(fd,b,n,MSG_NOSIGNAL) : recv(fd,b,n,0);
        if (r <= 0) return false;
        b += r; n -= r;
    }
    return true;
}
static void require(hipError_t e, const char *what) {
    if (e != hipSuccess) { fprintf(stderr,"%s: %s\n",what,hipGetErrorString(e)); exit(2); }
}
int main(int argc, char **argv) {
    const char *token=getenv("DLSSNR_HIP_TOKEN");
    if ((argc!=3 && argc!=4) || !token || strlen(token)<32 || strlen(token)>255) {
        fprintf(stderr,"usage: DLSSNR_HIP_TOKEN=<secret> server code-object port\n"); return 64;
    }
    int port=atoi(argv[2]); if(port<0 || port>65535 || (port && port<1024)) return 64;
    require(hipInit(0),"hipInit");
    require(hipSetDevice(0),"hipSetDevice");
    hipModule_t module; require(hipModuleLoad(&module,argv[1]),"hipModuleLoad");
    if(argc==4) {
        if(strcmp(argv[3],"--check"))return 64;
        char name[256];require(hipDeviceGetName(name,sizeof(name),0),"hipDeviceGetName");
        printf("GPU: %s; pinned code object loaded successfully\n",name);
        require(hipModuleUnload(module),"hipModuleUnload");return 0;
    }
    int listener=socket(AF_INET,SOCK_STREAM,0), yes=1;
    setsockopt(listener,SOL_SOCKET,SO_REUSEADDR,&yes,sizeof(yes));
    sockaddr_in addr{}; addr.sin_family=AF_INET; addr.sin_port=htons(port);
    addr.sin_addr.s_addr=htonl(INADDR_LOOPBACK);
    if(bind(listener,(sockaddr*)&addr,sizeof(addr)) || listen(listener,1)) { perror("listen"); return 2; }
    socklen_t address_size=sizeof(addr);getsockname(listener,(sockaddr*)&addr,&address_size);
    fprintf(stderr,"ready: loopback port=%d module_loaded=true\n",ntohs(addr.sin_port)); fflush(stderr);
    int fd=accept(listener,nullptr,nullptr); close(listener);
    if(fd<0) return 2;
    setsockopt(fd,IPPROTO_TCP,TCP_NODELAY,&yes,sizeof(yes));
    bool authenticated=false; uint64_t launches=0, copies=0;
    std::unordered_set<void *> allocations;
    std::unordered_set<hipEvent_t> events;
    NrPacket p;
    while(transfer(fd,&p,sizeof(p),false)) {
        if(p.magic!=NR_MAGIC || p.size>NR_MAX_DATA) break;
        p.name[255]=0;
        if(!authenticated && (p.op!=NR_HELLO || strcmp(p.name,token))) break;
        std::vector<unsigned char> data(p.size);
        if(p.size && !transfer(fd,data.data(),p.size,false)) break;
        size_t incoming=p.size; p.size=0; hipError_t e=hipSuccess;
        switch(p.op) {
        case NR_HELLO: authenticated=true; break;
        case NR_COUNT: { int n=0; e=hipGetDeviceCount(&n); p.a[0]=n; break; }
        case NR_DEVICE: e=hipSetDevice((int)p.a[0]); break;
        case NR_VERSION: {int v=0;e=p.a[0]?hipRuntimeGetVersion(&v):hipDriverGetVersion(&v);p.a[0]=v;break;}
        case NR_PROPS: {
            hipDeviceProp_t prop{}; e=hipGetDeviceProperties(&prop,(int)p.a[0]);
            data.resize(sizeof(prop)); memcpy(data.data(),&prop,sizeof(prop)); p.size=data.size(); break;
        }
        case NR_ALLOC: {
            size_t n=p.a[0];void *ptr=nullptr; e=hipMalloc(&ptr,n); p.a[0]=(uintptr_t)ptr;
            if(e==hipSuccess) {allocations.insert(ptr);}
            break;
        }
        case NR_FREE: {
            void *ptr=(void*)p.a[0];
            if(!allocations.count(ptr)) e=hipErrorInvalidValue;
            else { e=hipFree(ptr); if(e==hipSuccess) {allocations.erase(ptr);} } break;
        }
        case NR_COPY: {
            size_t n=p.a[2]; int kind=p.a[3];
            if(n>NR_MAX_DATA || (kind==1 && incoming!=n)) {e=hipErrorInvalidValue;break;}
            if(kind==1) e=hipMemcpy((void*)p.a[0],data.data(),n,hipMemcpyHostToDevice);
            else if(kind==2) {data.resize(n);e=hipMemcpy(data.data(),(void*)p.a[1],n,hipMemcpyDeviceToHost);if(e==hipSuccess)p.size=n;}
            else if(kind==3) e=hipMemcpy((void*)p.a[0],(void*)p.a[1],n,hipMemcpyDeviceToDevice);
            else e=hipErrorInvalidValue;
            ++copies; break;
        }
        case NR_SET: e=hipMemset((void*)p.a[0],p.a[1],p.a[2]); break;
        case NR_GLOBAL: {
            hipDeviceptr_t ptr; size_t size;
            e=hipModuleGetGlobal(&ptr,&size,module,p.name);
            if(e==hipSuccess) {p.a[0]=(uintptr_t)ptr;p.a[1]=size;} break;
        }
        case NR_LAUNCH: {
            hipFunction_t fn; e=hipModuleGetFunction(&fn,module,p.name);
            if(e!=hipSuccess) break;
            size_t argsize=incoming;
            void *extra[]={HIP_LAUNCH_PARAM_BUFFER_POINTER,data.data(),HIP_LAUNCH_PARAM_BUFFER_SIZE,&argsize,HIP_LAUNCH_PARAM_END};
            e=hipModuleLaunchKernel(fn,p.a[0],p.a[1],p.a[2],p.a[3],p.a[4],p.a[5],p.a[6],nullptr,nullptr,extra);
            if(e==hipSuccess) ++launches;
            break;
        }
        case NR_SYNC: e=hipDeviceSynchronize(); break;
        case NR_EVENT_CREATE: {hipEvent_t ev; e=hipEventCreate(&ev);if(e==hipSuccess){events.insert(ev);p.a[0]=(uintptr_t)ev;}break;}
        case NR_EVENT_RECORD: if(events.count((hipEvent_t)p.a[0])) e=hipEventRecord((hipEvent_t)p.a[0],nullptr);else e=hipErrorInvalidValue;break;
        case NR_EVENT_SYNC: if(events.count((hipEvent_t)p.a[0]))e=hipEventSynchronize((hipEvent_t)p.a[0]);else e=hipErrorInvalidValue;break;
        case NR_EVENT_TIME: {
            float ms=0; if(!events.count((hipEvent_t)p.a[0]) || !events.count((hipEvent_t)p.a[1])){e=hipErrorInvalidValue;break;}
            e=hipEventElapsedTime(&ms,(hipEvent_t)p.a[0],(hipEvent_t)p.a[1]); memcpy(&p.a[0],&ms,4);break;
        }
        default: e=hipErrorNotSupported;break;
        }
        p.status=e;
        if(e!=hipSuccess) fprintf(stderr,"op=%u error=%s\n",p.op,hipGetErrorString(e));
        if(!transfer(fd,&p,sizeof(p),true) || (p.size && !transfer(fd,data.data(),p.size,true))) break;
    }
    fprintf(stderr,"closed: launches=%llu copies=%llu\n",(unsigned long long)launches,(unsigned long long)copies);
    close(fd);
    // Process teardown owns remaining device allocations, including on disconnect.
    return 0;
}
