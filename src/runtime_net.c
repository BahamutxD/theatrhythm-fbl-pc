/* Networking for an offline console (the cable is unplugged, as in runtime_services.c):
 *   - POSIX sockets (libScePosix / libkernel, shadPS4 kernel.cpp sys_*): socket() fails with
 *     ENETUNREACH, so no socket ever exists. A call on a descriptor fails with ENOTSOCK for an
 *     open file and EBADF otherwise. select() sees only files, which are always ready.
 *   - libSceHttp calls runtime_services.c does not have: objects are created, no request ever
 *     gets a response (HTTP NETWORK), and sceHttpUriParse is the plain string contract.
 * POSIX functions return -1 and set the guest errno (FreeBSD numbers). */
#define _GNU_SOURCE
#include "runtime.h"
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <time.h>
#include <stdatomic.h>
#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#endif

#define G_EBADF 9
#define G_EINVAL 22
#define G_ENOSPC 28
#define G_ENOTSOCK 38
#define G_EAFNOSUPPORT 47
#define G_ENETUNREACH 51
#define G_AF_INET 2
#define G_AF_INET6 28
#define FD_SET_BITS 1024

static int32_t fail(int32_t e) { *runtime_errno()=e; return -1; }
static int is_open(int fd) { unsigned char st[256]; return fd>=0 && runtime_file_fstat(fd,st)==0; }
/* Any socket call on `fd`: never a socket here. */
static int32_t not_socket(int fd) { return fail(is_open(fd) ? G_ENOTSOCK : G_EBADF); }

/* socket(domain, type, protocol): no network interface is up. */
static ABI int32_t posix_socket(int domain,int type,int protocol) {
    (void)domain; (void)type; (void)protocol;
    return fail(G_ENETUNREACH);
}
/* bind/connect/listen/accept/shutdown/getsockname/getpeername/setsockopt/getsockopt and the
 * send/recv family take the socket first. */
static ABI int32_t posix_bind(int fd,const void *addr,uint32_t len) { (void)addr; (void)len; return not_socket(fd); }
static ABI int32_t posix_connect(int fd,const void *addr,uint32_t len) { (void)addr; (void)len; return not_socket(fd); }
static ABI int32_t posix_listen(int fd,int backlog) { (void)backlog; return not_socket(fd); }
static ABI int32_t posix_accept(int fd,void *addr,uint32_t *len) { (void)addr; (void)len; return not_socket(fd); }
static ABI int32_t posix_shutdown(int fd,int how) { (void)how; return not_socket(fd); }
static ABI int32_t posix_sockname(int fd,void *addr,uint32_t *len) { (void)addr; (void)len; return not_socket(fd); }
static ABI int32_t posix_sockopt(int fd,int level,int name,void *value,void *len) {
    (void)level; (void)name; (void)value; (void)len; return not_socket(fd);
}
static ABI int64_t posix_send(int fd,const void *buf,uint64_t len,int flags) { (void)buf; (void)len; (void)flags; return not_socket(fd); }
static ABI int64_t posix_sendto(int fd,const void *buf,uint64_t len,int flags,const void *to,uint32_t tolen) {
    (void)buf; (void)len; (void)flags; (void)to; (void)tolen; return not_socket(fd);
}
static ABI int64_t posix_recvfrom(int fd,void *buf,uint64_t len,int flags,void *from,uint32_t *fromlen) {
    (void)buf; (void)len; (void)flags; (void)from; (void)fromlen; return not_socket(fd);
}
static ABI int64_t posix_msg(int fd,void *msg,int flags) { (void)msg; (void)flags; return not_socket(fd); }

static void sleep_us(uint64_t us) {
#ifdef _WIN32
    compat_sleep_ns(us*1000);
#else
    struct timespec t={(time_t)(us/1000000),(long)(us%1000000)*1000};
    while (nanosleep(&t,&t)) {}
#endif
}
/* select(nfds, read, write, except, timeout): FreeBSD fd_set bitmaps of 1024 bits. Files are
 * always ready to read and write and have no exceptional condition; a bit for a descriptor that
 * is not open fails with EBADF. With nothing ready it sleeps the timeout (forever with none). */
typedef struct { int64_t sec, usec; } GuestTimeval;
static ABI int32_t posix_select(int nfds,uint64_t *rd,uint64_t *wr,uint64_t *ex,const GuestTimeval *timeout) {
    if (nfds<0 || nfds>FD_SET_BITS) return fail(G_EINVAL);
    if (timeout && (timeout->sec<0 || timeout->usec<0 || timeout->usec>=1000000)) return fail(G_EINVAL);
    uint64_t *sets[3]={rd,wr,ex};
    for (int s=0;s<3;++s) for (int fd=0;sets[s] && fd<nfds;++fd)
        if (sets[s][fd/64]>>(fd%64)&1 && !is_open(fd)) return fail(G_EBADF);
    int ready=0;
    for (int s=0;s<3;++s) for (int fd=0;sets[s] && fd<nfds;++fd) {
        uint64_t bit=UINT64_C(1)<<(fd%64);
        if (!(sets[s][fd/64]&bit)) continue;
        if (s==2) sets[s][fd/64]&=~bit; else ++ready;
    }
    if (ready) return ready;
    if (!timeout) for (;;) sleep_us(1000000);
    sleep_us((uint64_t)timeout->sec*1000000+(uint64_t)timeout->usec);
    return 0;
}
/* inet_pton(af, src, dst): 1 parsed, 0 not an address, -1 EAFNOSUPPORT. */
static ABI int32_t posix_inet_pton(int af,const char *src,void *dst) {
    if (af!=G_AF_INET && af!=G_AF_INET6) return fail(G_EAFNOSUPPORT);
    return inet_pton(af==G_AF_INET ? AF_INET : AF_INET6,src,dst)==1;
}
/* inet_ntop(af, src, dst, size): dst, or NULL with EAFNOSUPPORT / ENOSPC. */
static ABI const char *posix_inet_ntop(int af,const void *src,char *dst,uint32_t size) {
    if (af!=G_AF_INET && af!=G_AF_INET6) { fail(G_EAFNOSUPPORT); return NULL; }
    char text[64];
    if (!inet_ntop(af==G_AF_INET ? AF_INET : AF_INET6,src,text,sizeof(text))) { fail(G_EINVAL); return NULL; }
    if (strlen(text)>=size) { fail(G_ENOSPC); return NULL; }
    return strcpy(dst,text);
}

/* ---- libSceHttp (shadPS4 http.cpp), offline ---- */
#define HTTP_NETWORK ((int32_t)0x80431063)
#define HTTP_OUT_OF_MEMORY ((int32_t)0x80431022)
#define HTTP_UNKNOWN_SCHEME ((int32_t)0x80431061)
#define HTTP_INVALID_VALUE ((int32_t)0x804311fe)
#define HTTP_INVALID_URL ((int32_t)0x80433060)
static _Atomic int32_t http_next=0x10000;
static int same(const char *a,const char *b) {
    while (*a && tolower((unsigned char)*a)==*b) ++a, ++b;
    return !*a && !*b;
}
/* sceHttpCreateConnection(template, server, scheme, port, keepalive): an id; the server is
 * required and the scheme is http or https. */
static ABI int32_t http_connection(int32_t tmpl,const char *server,const char *scheme,uint16_t port,int32_t keepalive) {
    (void)tmpl; (void)port; (void)keepalive;
    if (!server) return HTTP_INVALID_VALUE;
    if (!scheme || (!same(scheme,"http") && !same(scheme,"https"))) return HTTP_UNKNOWN_SCHEME;
    return atomic_fetch_add(&http_next,1);
}
/* sceHttpCreateRequest2(connection, method, path, content length): an id; the path is required. */
static ABI int32_t http_request2(int32_t conn,const char *method,const char *path,uint64_t length) {
    (void)conn; (void)method; (void)length;
    if (!path) return HTTP_INVALID_VALUE;
    return atomic_fetch_add(&http_next,1);
}
/* sceHttpGetAllResponseHeaders(request, header, size): no request gets a response. */
static ABI int32_t http_headers(int32_t req,char **header,uint64_t *size) {
    (void)req;
    if (!header || !size) return HTTP_INVALID_VALUE;
    return HTTP_NETWORK;
}
/* Settings and callbacks (redirect, SSL, auth, chunked transfer, timeouts), aborting a request,
 * flushing cookies and the memory pool statistics: accepted, as shadPS4 does; nothing is sent. */
static ABI int32_t http_ok(void) { return 0; }

/* sceHttpUriParse(out, uri, pool, require, prepare): splits uri into an OrbisHttpUriElement whose
 * strings live in pool (prepare bytes). With no out/pool only *require (the pool size needed) is
 * set. Strings: scheme, username and password, hostname, path, query ("?..."), fragment ("#...");
 * the port defaults to 80 for http and 443 for https. A URI without "//" is opaque. */
typedef struct {
    uint8_t opaque; char *scheme, *username, *password, *hostname, *path, *query, *fragment;
    uint16_t port; uint8_t reserved[10];
} UriElement;
_Static_assert(sizeof(UriElement)==80,"OrbisHttpUriElement layout");
typedef struct { char *pool; uint64_t used, size; int write; } UriPool;
static char *uri_put(UriPool *p,const char *s,uint64_t n,int32_t *error) {
    char *d=p->pool+p->used;
    if (p->write) {
        if (p->size-p->used<n+1) { *error=HTTP_OUT_OF_MEMORY; return NULL; }
        memcpy(d,s,n); d[n]=0;
    }
    p->used+=n+1;
    return p->write ? d : NULL;
}
static ABI int32_t http_uri_parse(UriElement *out,const char *uri,void *pool,uint64_t *require,uint64_t prepare) {
    if (!uri) return HTTP_INVALID_URL;
    UriPool p={pool,0,prepare,out && pool};
    if (!p.write && !require) return HTTP_INVALID_VALUE;
    UriElement e; memset(&e,0,sizeof(e));
    int32_t error=0;
    uint64_t i=0, n=0;
    while (n<0x20 && uri[n] && (isalnum((unsigned char)uri[n]) || strchr("+-.",uri[n]))) ++n;
    if (n && uri[n]==':' && isalpha((unsigned char)uri[0])) { e.scheme=uri_put(&p,uri,n,&error); i=n+1; }
    else e.scheme=uri_put(&p,"",0,&error);
    if (uri[i]=='/' && uri[i+1]=='/') {
        i+=2;
        uint64_t end=i; while (uri[end] && !strchr("/?#@",uri[end])) ++end;
        if (uri[end]=='@') {
            uint64_t colon=i; while (colon<end && uri[colon]!=':') ++colon;
            e.username=uri_put(&p,uri+i,colon-i,&error);
            e.password=uri_put(&p,uri+colon+(colon<end),end-colon-(colon<end),&error);
            i=end+1;
        } else { e.username=uri_put(&p,"",0,&error); e.password=uri_put(&p,"",0,&error); }
        if (uri[i]=='[') {
            uint64_t close=i+1; while (uri[close] && uri[close]!=']') ++close;
            if (uri[close]!=']') return HTTP_INVALID_URL;
            e.hostname=uri_put(&p,uri+i+1,close-i-1,&error); i=close+1;
        } else {
            uint64_t h=i; while (uri[h] && (isalnum((unsigned char)uri[h]) || strchr("-._",uri[h]))) ++h;
            if (h-i>=0xff) return HTTP_INVALID_URL;
            e.hostname=uri_put(&p,uri+i,h-i,&error); i=h;
        }
        if (uri[i]==':') {
            uint32_t port=0; uint64_t d=0;
            while (d<5 && isdigit((unsigned char)uri[i+1+d])) port=port*10+(uint32_t)(uri[i+1+d++]-'0');
            if (port>0xffff || (uri[i+1+d] && !strchr("/?#",uri[i+1+d]))) return HTTP_INVALID_URL;
            if (d) e.port=(uint16_t)port;
            i+=1+d;
        }
        if (!e.port && e.scheme) e.port=same(e.scheme,"https") ? 443 : same(e.scheme,"http") ? 80 : 0;
    } else {
        e.opaque=1;
        e.username=uri_put(&p,"",0,&error); e.password=uri_put(&p,"",0,&error);
        e.hostname=uri_put(&p,"",0,&error);
    }
    n=0; while (uri[i+n] && uri[i+n]!='?' && uri[i+n]!='#') ++n;
    e.path=uri_put(&p,uri+i,n,&error); i+=n;
    n=0; if (uri[i]=='?') while (uri[i+n] && uri[i+n]!='#') ++n;
    e.query=uri_put(&p,uri+i,n,&error); i+=n;
    e.fragment=uri_put(&p,uri+i,strlen(uri+i),&error);
    if (error) return error;
    if (require) *require=p.used;
    if (p.write) *out=e;
    return 0;
}

static const RuntimeExport exports[]={
    {"socket",posix_socket}, {"bind",posix_bind}, {"connect",posix_connect}, {"listen",posix_listen},
    {"accept",posix_accept}, {"shutdown",posix_shutdown}, {"getsockname",posix_sockname},
    {"getpeername",posix_sockname}, {"setsockopt",posix_sockopt}, {"getsockopt",posix_sockopt},
    {"send",posix_send}, {"sendto",posix_sendto}, {"recv",posix_send}, {"recvfrom",posix_recvfrom},
    {"sendmsg",posix_msg}, {"recvmsg",posix_msg}, {"select",posix_select},
    {"inet_pton",posix_inet_pton}, {"inet_ntop",posix_inet_ntop},
    {"sceHttpCreateConnection",http_connection}, {"sceHttpCreateRequest2",http_request2},
    {"sceHttpGetAllResponseHeaders",http_headers}, {"sceHttpUriParse",http_uri_parse},
    {"sceHttpAbortRequest",http_ok}, {"sceHttpSetRedirectCallback",http_ok}, {"sceHttpsSetSslCallback",http_ok},
    {"sceHttpCookieFlush",http_ok}, {"sceHttpSetAuthEnabled",http_ok}, {"sceHttpSetSendTimeOut",http_ok},
    {"sceHttpSetRecvTimeOut",http_ok}, {"sceHttpSetChunkedTransferEnabled",http_ok},
    {"sceHttpGetMemoryPoolStats",http_ok},
};
uintptr_t runtime_net_resolve(const char *name) { return RUNTIME_LOOKUP(exports,name); }
