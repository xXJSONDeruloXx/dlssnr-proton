#define _POSIX_C_SOURCE 200809L
#include "native_api.h"
#include "native_protocol.h"
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int transfer(int fd, void *buffer, size_t size, int writing) {
    unsigned char *p=(unsigned char *)buffer;
    while (size) {
        ssize_t n=writing?send(fd,p,size,MSG_NOSIGNAL):recv(fd,p,size,0);
        if (n<=0) return -1;
        p+=n;size-=(size_t)n;
    }
    return 0;
}

int main(int argc, char **argv) {
    if (argc!=3) {
        fprintf(stderr,"usage: %s LIBDLSSNR MODEL\n",argv[0]);
        return 64;
    }
    const char *token=getenv("DLSSNR_NATIVE_TOKEN");
    if (!token || strlen(token)<32 || strlen(token)>=NR_NATIVE_TOKEN_MAX) {
        fprintf(stderr,"DLSSNR_NATIVE_TOKEN must contain 32-%u characters\n",NR_NATIVE_TOKEN_MAX-1);
        return 64;
    }
    NrNativeApi api;char error[512];
    if (nr_native_api_open(argv[1],&api,error,sizeof(error))) {
        fprintf(stderr,"%s\n",error);return 2;
    }
    dlssnr_model *model=NULL;
    if (api.model_open(argv[2],&model)) {
        fprintf(stderr,"model open failed\n");nr_native_api_close(&api);return 3;
    }
    dlssnr_runtime *runtime=NULL;
    if (api.runtime_create(&runtime) || api.runtime_set_model(runtime,model)) {
        fprintf(stderr,"runtime setup failed\n");
        if (runtime) api.runtime_destroy(runtime);
        api.model_close(model);nr_native_api_close(&api);return 4;
    }

    int listener=socket(AF_INET,SOCK_STREAM,0),yes=1;
    if (listener<0) goto socket_fail;
    setsockopt(listener,SOL_SOCKET,SO_REUSEADDR,&yes,sizeof(yes));
    struct sockaddr_in address;
    memset(&address,0,sizeof(address));
    address.sin_family=AF_INET;
    address.sin_addr.s_addr=htonl(INADDR_LOOPBACK);
    if (bind(listener,(struct sockaddr *)&address,sizeof(address)) || listen(listener,1)) goto socket_fail;
    socklen_t address_size=sizeof(address);
    if (getsockname(listener,(struct sockaddr *)&address,&address_size)) goto socket_fail;
    printf("ready: native port=%u abi=%u source=%d tensors=%u device=%s\n",
           (unsigned)ntohs(address.sin_port),api.abi_version(),
           api.model_source_kind(model),api.model_tensor_count(model),
           api.runtime_device_name(runtime));
    fflush(stdout);

    int client=accept(listener,NULL,NULL);
    close(listener);listener=-1;
    if (client<0) goto socket_fail;
    setsockopt(client,IPPROTO_TCP,TCP_NODELAY,&yes,sizeof(yes));
    int authenticated=0;
    for (;;) {
        NrNativePacket packet;
        if (transfer(client,&packet,sizeof(packet),0)) break;
        int close_after=0;
        if (packet.magic!=NR_NATIVE_MAGIC || packet.version!=NR_NATIVE_PROTOCOL_VERSION) break;
        if (!authenticated) {
            if (packet.op!=NR_NATIVE_HELLO ||
                strnlen(packet.token,NR_NATIVE_TOKEN_MAX)!=strlen(token) ||
                memcmp(packet.token,token,strlen(token))) break;
            authenticated=1;
        } else if (packet.op==NR_NATIVE_INFO) {
            packet.a[0]=api.abi_version();
            packet.a[1]=(uint64_t)api.model_source_kind(model);
            packet.a[2]=api.model_tensor_count(model);
            packet.a[3]=0;
            snprintf(packet.text,sizeof(packet.text),
                     "device=%s\ndll_sha256=%s\nweights_sha256=%s",
                     api.runtime_device_name(runtime),api.model_dll_sha256(model),
                     api.model_weights_sha256(model));
        } else if (packet.op==NR_NATIVE_PING) {
            packet.a[0]++;
        } else if (packet.op==NR_NATIVE_CLOSE) {
            close_after=1;
        } else {
            packet.status=-1;
        }
        memset(packet.token,0,sizeof(packet.token));
        if (transfer(client,&packet,sizeof(packet),1) || close_after) break;
    }
    close(client);
    api.runtime_destroy(runtime);api.model_close(model);nr_native_api_close(&api);
    return 0;

socket_fail:
    perror("native host socket");
    if (listener>=0) close(listener);
    api.runtime_destroy(runtime);api.model_close(model);nr_native_api_close(&api);
    return 5;
}
