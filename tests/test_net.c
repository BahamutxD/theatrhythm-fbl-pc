/* Offline networking, PSN libraries, absent devices and RTC date parsing (src/runtime_net.c,
 * runtime_np.c, runtime_devices.c, runtime_rtc.c) against the contracts shadPS4 follows. */
#define _GNU_SOURCE
#undef NDEBUG /* the checks are asserts; release builds define NDEBUG */
#define exports net_exports
#include "../src/runtime_net.c"
#undef exports
#define exports np_exports
#include "../src/runtime_np.c"
#undef exports
#define exports devices_exports
#include "../src/runtime_devices.c"
#undef exports
#define exports rtc_exports
#include "../src/runtime_rtc.c"
#undef exports
#include <assert.h>

int32_t *runtime_errno(void) { static int32_t value; return &value; }
/* Descriptor 3 is an open file; nothing else is open. */
int64_t runtime_file_fstat(int fd, void *guest_stat) { (void)guest_stat; return fd==3 ? 0 : -9; }
uintptr_t runtime_lookup(const RuntimeExport *table, size_t count, const char *nid) { (void)table; (void)count; (void)nid; return 0; }
void runtime_restart(void) { abort(); }

static void test_sockets(void) {
    assert(posix_socket(2,1,0)==-1 && *runtime_errno()==G_ENETUNREACH);
    assert(posix_connect(3,NULL,0)==-1 && *runtime_errno()==G_ENOTSOCK);
    assert(posix_send(7,"x",1,0)==-1 && *runtime_errno()==G_EBADF);
    assert(posix_sockopt(-1,0xffff,4,NULL,NULL)==-1 && *runtime_errno()==G_EBADF);
    /* select: the open file is ready to read and write and has no exception; a closed one is EBADF. */
    uint64_t rd[16]={0}, wr[16]={0}, ex[16]={0};
    rd[0]=wr[0]=ex[0]=1u<<3;
    GuestTimeval zero={0,0};
    assert(posix_select(4,rd,wr,ex,&zero)==2 && rd[0]==1u<<3 && wr[0]==1u<<3 && ex[0]==0);
    rd[0]=1u<<5;
    assert(posix_select(6,rd,NULL,NULL,&zero)==-1 && *runtime_errno()==G_EBADF);
    assert(posix_select(0,NULL,NULL,NULL,&zero)==0);
    GuestTimeval bad={0,1000000};
    assert(posix_select(0,NULL,NULL,NULL,&bad)==-1 && *runtime_errno()==G_EINVAL);
    unsigned char addr[16]; char text[64];
    assert(posix_inet_pton(G_AF_INET,"192.168.1.20",addr)==1 && addr[0]==192 && addr[3]==20);
    assert(posix_inet_pton(G_AF_INET,"nope",addr)==0);
    assert(posix_inet_pton(1,"x",addr)==-1 && *runtime_errno()==G_EAFNOSUPPORT);
    assert(posix_inet_ntop(G_AF_INET,addr,text,sizeof(text))==text && !strcmp(text,"192.168.1.20"));
    assert(!posix_inet_ntop(G_AF_INET,addr,text,4) && *runtime_errno()==G_ENOSPC);
    assert(posix_inet_pton(G_AF_INET6,"::1",addr)==1 && addr[15]==1);
}

static void test_http(void) {
    assert(http_connection(1,NULL,"http",80,1)==HTTP_INVALID_VALUE);
    assert(http_connection(1,"example.com","ftp",21,1)==HTTP_UNKNOWN_SCHEME);
    int32_t conn=http_connection(1,"example.com","HTTPS",443,1);
    assert(conn>0 && http_request2(conn,"GET",NULL,0)==HTTP_INVALID_VALUE);
    int32_t req=http_request2(conn,"GET","/",0);
    char *header; uint64_t size;
    assert(req>conn && http_headers(req,&header,&size)==HTTP_NETWORK && http_headers(req,NULL,&size)==HTTP_INVALID_VALUE);
    /* sceHttpUriParse: the size first, then the element in a pool of that size. */
    const char *uri="https://user:pw@example.com:8443/a/b?x=1#top";
    uint64_t need=0;
    assert(http_uri_parse(NULL,uri,NULL,&need,0)==0 && need>0);
    UriElement e; char pool[256];
    assert(http_uri_parse(&e,uri,pool,NULL,need-1)==HTTP_OUT_OF_MEMORY);
    assert(http_uri_parse(&e,uri,pool,NULL,need)==0);
    assert(!e.opaque && !strcmp(e.scheme,"https") && !strcmp(e.username,"user") && !strcmp(e.password,"pw"));
    assert(!strcmp(e.hostname,"example.com") && e.port==8443 && !strcmp(e.path,"/a/b"));
    assert(!strcmp(e.query,"?x=1") && !strcmp(e.fragment,"#top"));
    assert(http_uri_parse(&e,"http://[::1]/",pool,NULL,sizeof(pool))==0 && !strcmp(e.hostname,"::1") && e.port==80);
    assert(http_uri_parse(&e,"mailto:someone",pool,NULL,sizeof(pool))==0 && e.opaque && !strcmp(e.path,"someone"));
    assert(http_uri_parse(&e,"http://host:99x/",pool,NULL,sizeof(pool))==HTTP_INVALID_URL);
    assert(http_uri_parse(&e,NULL,pool,NULL,sizeof(pool))==HTTP_INVALID_URL);
    assert(http_uri_parse(NULL,uri,NULL,NULL,0)==HTTP_INVALID_VALUE);
}

static void test_np(void) {
    /* Signed out: contexts are refused, so their requests never exist. */
    assert(signed_out()==NP_SIGNED_OUT && community_no_id()==COMMUNITY_INVALID_ID);
    assert(score_ctx_a(1,1)==NP_SIGNED_OUT);
    uint8_t m2[16]={0}; uint16_t ctx;
    assert(matching2_context_a(m2,&ctx)==MATCHING2_INVALID_ARGUMENT);
    m2[8]=0x10;
    assert(matching2_context_a(m2,&ctx)==NP_SIGNED_OUT && matching2_context_a(m2,NULL)==MATCHING2_INVALID_ARGUMENT);
    assert(matching2_no_context()==MATCHING2_INVALID_CONTEXT_ID);
    assert(webapi_context_a(1,USER_INVALID)==WEBAPI_INVALID_ARGUMENT && webapi_context_a(1,1)==NP_SIGNED_OUT);
    assert(webapi_context_a(0x8000,1)==WEBAPI_INVALID_LIB_CONTEXT_ID && webapi_handle(1)>0);
    assert(webapi_header(1,NULL,"v")==WEBAPI_INVALID_ARGUMENT && webapi_header(1,"k","v")==NP_SIGNED_OUT);
    char filter[16];
    assert(webapi_filter(1,1,"svc",INVALID_SERVICE_LABEL,filter,1)==WEBAPI_INVALID_ARGUMENT);
    assert(webapi_filter(1,1,NULL,0,NULL,1)==WEBAPI_INVALID_ARGUMENT && webapi_filter(1,1,"svc",0,filter,1)>0);
    assert(webapi_callback_a(1,1,NULL,NULL)==WEBAPI_INVALID_ARGUMENT && webapi_callback_a(1,1,filter,NULL)==NP_SIGNED_OUT);
    int32_t r1=auth_request(), r2=auth_request();
    assert(r1==0x10000001 && r2==r1+1);
    char code[32]; int32_t issuer;
    assert(auth_code_a(r1,NULL,code,&issuer)==AUTH_INVALID_ARGUMENT && auth_code_a(r1,code,code,&issuer)==NP_SIGNED_OUT);
    assert(auth_token_a(r1,code,NULL)==AUTH_INVALID_ARGUMENT && auth_token_a(r1,code,code)==NP_SIGNED_OUT);
}

static void test_dialogs(void) {
    int32_t result=-1;
    assert(signin_open(NULL)==DIALOG_NOT_INITIALIZED && signin_status()==0);
    assert(signin_init()==0 && signin_init()==DIALOG_ALREADY_INITIALIZED && signin_status()==1);
    assert(signin_result(&result)==DIALOG_NOT_FINISHED);
    assert(signin_open(NULL)==0 && signin_status()==3);
    assert(signin_result(NULL)==DIALOG_ARG_NULL && signin_result(&result)==0 && result==RESULT_USER_CANCELED);
    assert(signin_term()==0 && signin_term()==DIALOG_NOT_INITIALIZED);
    uint8_t inv[56]; memset(inv,0xcc,sizeof(inv));
    assert(invitation_init()==0 && invitation_open(NULL)==0 && invitation_result(inv)==0);
    int32_t codes[2]; memcpy(codes,inv+8,8);
    assert(codes[0]==NP_SIGNED_OUT && codes[1]==RESULT_USER_CANCELED && inv[0]==0xcc && inv[16]==0);
    assert(invitation_term()==0);
    int32_t error_param[4]={16,(int32_t)0x80550006,1,0};
    assert(error_init()==0 && error_open(NULL)==DIALOG_ARG_NULL && error_open(error_param)==0 && error_status()==3);
    assert(error_term()==0);
    assert(social_close()==DIALOG_NOT_INITIALIZED && social_init()==0 && social_open(NULL)==0);
    assert(social_result(&result)==0 && result==RESULT_USER_CANCELED && social_close()==0 && social_term()==0);
}

static void test_camera(void) {
    assert(camera_attached(0)==0 && camera_attached(1)==CAMERA_PARAM);
    assert(camera_open(1,0,0,NULL)==CAMERA_PARAM && camera_close(1)==CAMERA_NOT_OPEN);
    int32_t h=camera_open(USER_SYSTEM,0,0,NULL);
    assert(h==1);
    uint32_t start[6]={24,1,0,0,0,0};
    assert(camera_start(h,start)==CAMERA_NOT_CONNECTED && camera_config(h,start)==CAMERA_NOT_CONNECTED);
    start[1]=0; assert(camera_start(h,start)==CAMERA_FORMAT_UNKNOWN);
    start[0]=16; assert(camera_start(h,start)==CAMERA_PARAM);
    uint8_t sync[16]={16};
    assert(camera_video_sync(h,sync)==0);
    sync[4]=2; assert(camera_video_sync(h,sync)==CAMERA_PARAM);
    uint8_t frame[FRAME_DATA_MAX]={0}; uint32_t size=FRAME_DATA_MAX; memcpy(frame,&size,4);
    assert(camera_valid_frame(h,frame)==1);
    assert(camera_frame(h,frame)==CAMERA_NOT_CONNECTED && camera_valid_frame(h,frame)==0);
    assert(camera_close(h)==0 && camera_frame(h,frame)==CAMERA_NOT_OPEN && camera_close(h)==CAMERA_NOT_OPEN);
}

static void test_audio3d(void) {
    uint32_t port, object;
    uint8_t params[0x28]={0};
    uint64_t size=0x28; uint32_t f[7]={0x100,0,4,2,0,0,2};
    memcpy(params,&size,8); memcpy(params+8,f,sizeof(f));
    assert(a3d_port_open(USER_SYSTEM,params,&port)==A3D_NOT_READY && port==A3D_INVALID);
    assert(a3d_initialize(1)==A3D_INVALID_PARAMETER && a3d_initialize(0)==0 && a3d_initialize(0)==A3D_NOT_READY);
    assert(a3d_port_open(1,params,&port)==A3D_INVALID_PARAMETER);
    assert(a3d_port_open(USER_SYSTEM,params,&port)==0 && port==0);
    for (int i=1;i<=4;++i) assert(a3d_object_reserve(port,&object)==0 && object==(uint32_t)i);
    assert(a3d_object_reserve(port,&object)==A3D_OUT_OF_RESOURCES && a3d_object_reserve(port,NULL)==A3D_INVALID_PARAMETER);
    assert(a3d_object_attributes(port,1,1,params)==0 && a3d_object_attributes(port,1,0,params)==A3D_INVALID_PARAMETER);
    assert(a3d_port_flush(port)==0 && a3d_port_flush(3)==A3D_INVALID_PORT);
    f[0]=0x180; memcpy(params+8,f,4);
    assert(a3d_port_open(USER_SYSTEM,params,&port)==A3D_INVALID_PARAMETER);
    f[0]=0x100; f[6]=4; memcpy(params+8,f,sizeof(f));
    assert(a3d_port_open(USER_SYSTEM,params,&port)==A3D_INVALID_PARAMETER);
    size=0x10; memcpy(params,&size,8);
    assert(a3d_port_open(USER_SYSTEM,params,&port)==0 && port==1);
    assert(a3d_port_close(0)==0 && a3d_port_close(0)==A3D_INVALID_PORT);
    assert(a3d_terminate()==0 && a3d_port_flush(1)==A3D_INVALID_PORT && a3d_terminate()==A3D_NOT_READY);
}

static void test_extras(void) {
    uint8_t opts[16]; memset(opts,0xff,sizeof(opts));
    assert(video_options_init(opts,16)==0 && !opts[0] && !opts[15]);
    assert(video_options_init(NULL,16)==VIDEO_OUT_INVALID_ADDRESS && video_options_init(opts,0)==VIDEO_OUT_INVALID_VALUE);
    uint8_t social[32]; memset(social,0xff,sizeof(social));
    assert(social_param_init(social)==0 && !social[31] && social_ok()==0);
    uint32_t id=7;
    assert(ulobj_register(0,1,&id)==22 && ulobj_register(1,1,&id)==0 && id==0);
    assert(ulobj_unregister(0x4000)==22 && ulobj_unregister(1)==0);
}

static void test_rtc_parse(void) {
    DateTime t={2024,1,2,3,4,5,0}; uint64_t base=to_tick(&t), tick=0;
    assert(rtc_parse_date_time(&tick,"2024-01-02T03:04:05Z")==0 && tick==base);
    assert(rtc_parse_date_time(&tick,"2024-01-02T03:04:05.25Z")==0 && tick==base+250000);
    assert(rtc_parse_date_time(&tick,"2024-01-02T12:04:05+09:00")==0 && tick==base);
    assert(rtc_parse_date_time(&tick,"2024-01-01T22:04:05-05:00")==0 && tick==base);
    assert(rtc_parse_date_time(&tick,"Tue, 02 Jan 2024 04:04:05 +0100")==0 && tick==base);
    assert(rtc_parse_date_time(&tick,"Tue, 02 Jan 2024 03:04:05 GMT")==0 && tick==base);
    assert(rtc_parse_date_time(&tick,"Tue Jan  2 03:04:05 2024")==0 && tick==base);
    assert(rtc_parse_date_time(&tick,"yesterday")==ERR_BAD_PARSE);
    assert(rtc_parse_date_time(&tick,"2024-02-30T00:00:00Z")==ERR_INVALID_DAY);
    assert(rtc_parse_date_time(NULL,"2024-01-02T03:04:05Z")==ERR_INVALID_POINTER);
}

int main(void) {
    test_sockets();
    test_http();
    test_np();
    test_dialogs();
    test_camera();
    test_audio3d();
    test_extras();
    test_rtc_parse();
    puts("net/np/devices tests passed");
    return 0;
}
