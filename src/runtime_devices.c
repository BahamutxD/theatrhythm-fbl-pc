/* Hardware and system extras the game links but the PC does not have:
 *   - no PlayStation Camera is attached (shadPS4 camera.cpp with no camera configured);
 *   - Audio3d keeps its ports and objects, but the game imports no PortPush/PortAdvance, so
 *     nothing it queues is ever mixed (shadPS4 audio3d.cpp validation);
 *   - no PS VR: the social screen mirrors the game on the TV and its dialog has nothing to share;
 *   - Razor CPU markers and the job manager's object registry (ulobjmgr) keep nothing;
 *   - sceSystemServiceLoadExec can only start the game itself again. */
#include "runtime.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>

/* ---- Camera ---- */
#define CAMERA_PARAM ((int32_t)0x802E0000)
#define CAMERA_NOT_OPEN ((int32_t)0x802E0004)
#define CAMERA_FORMAT_UNKNOWN ((int32_t)0x802E0007)
#define CAMERA_NOT_CONNECTED ((int32_t)0x802E0010)
#define USER_SYSTEM 0xFF
#define FRAME_DATA_MAX 584
#define FRAME_STATUS 232 /* OrbisCameraFrameData.status[2] */
static int camera_handles;
static pthread_mutex_t camera_lock=PTHREAD_MUTEX_INITIALIZER;
static int camera_open_count(void) { pthread_mutex_lock(&camera_lock); int n=camera_handles; pthread_mutex_unlock(&camera_lock); return n; }
/* sceCameraIsAttached(index): only index 0 exists; 0 = not attached. */
static ABI int32_t camera_attached(int32_t index) { return index ? CAMERA_PARAM : 0; }
/* sceCameraOpen(user, type, index, param): the system user, type 0, index 0; a handle (the
 * library opens without a device, as shadPS4). */
static ABI int32_t camera_open(int32_t user,int32_t type,int32_t index,const void *param) {
    (void)param;
    if (user!=USER_SYSTEM || type || index) return CAMERA_PARAM;
    pthread_mutex_lock(&camera_lock); int handle=++camera_handles; pthread_mutex_unlock(&camera_lock);
    return handle;
}
/* sceCameraClose(handle): the library closes with its last handle. */
static ABI int32_t camera_close(int32_t handle) {
    if (handle<1) return CAMERA_PARAM;
    pthread_mutex_lock(&camera_lock);
    int32_t r=camera_handles ? (--camera_handles,0) : CAMERA_NOT_OPEN;
    pthread_mutex_unlock(&camera_lock);
    return r;
}
/* sceCameraIsValidFrameData(handle, frame): 1 when both status words are 0. */
static ABI int32_t camera_valid_frame(int32_t handle,const uint8_t *frame) {
    uint32_t size, status[2];
    if (handle<1 || !frame || (memcpy(&size,frame,4),size>FRAME_DATA_MAX)) return CAMERA_PARAM;
    if (!camera_open_count()) return CAMERA_NOT_OPEN;
    memcpy(status,frame+FRAME_STATUS,8);
    return !status[0] && !status[1];
}
/* sceCameraSetConfig(handle, config): no device to configure. */
static ABI int32_t camera_config(int32_t handle,const void *config) {
    if (handle<1 || !config) return CAMERA_PARAM;
    return camera_open_count() ? CAMERA_NOT_CONNECTED : CAMERA_NOT_OPEN;
}
/* sceCameraStart(handle, param{sizeThis=24, formatLevel[2], option}): format levels 1..0xf. */
static ABI int32_t camera_start(int32_t handle,const uint32_t *param) {
    if (handle<1 || !param || param[0]!=24) return CAMERA_PARAM;
    if (!camera_open_count()) return CAMERA_NOT_OPEN;
    if (param[1]>0xf || param[2]>0xf || !(param[1]|param[2])) return CAMERA_FORMAT_UNKNOWN;
    return CAMERA_NOT_CONNECTED;
}
/* sceCameraSetVideoSync(handle, param{sizeThis=16, mode 0/1, option NULL}). */
static ABI int32_t camera_video_sync(int32_t handle,const uint8_t *param) {
    uint32_t size, mode; void *option;
    if (handle<1 || !param) return CAMERA_PARAM;
    memcpy(&size,param,4); memcpy(&mode,param+4,4); memcpy(&option,param+8,8);
    if (size!=16 || mode>1 || option) return CAMERA_PARAM;
    return camera_open_count() ? 0 : CAMERA_NOT_OPEN;
}
/* sceCameraGetFrameData(handle, frame): marks both frames invalid (-1); there is no frame. */
static ABI int32_t camera_frame(int32_t handle,uint8_t *frame) {
    if (!frame) return CAMERA_PARAM;
    const int32_t invalid[2]={-1,-1};
    memcpy(frame+FRAME_STATUS,invalid,8);
    uint32_t size; memcpy(&size,frame,4);
    if (handle<1 || size>FRAME_DATA_MAX) return CAMERA_PARAM;
    return camera_open_count() ? CAMERA_NOT_CONNECTED : CAMERA_NOT_OPEN;
}

/* ---- Audio3d ---- */
#define A3D_INVALID_PORT ((int32_t)0x80EA0002)
#define A3D_INVALID_PARAMETER ((int32_t)0x80EA0004)
#define A3D_OUT_OF_RESOURCES ((int32_t)0x80EA0006)
#define A3D_NOT_READY ((int32_t)0x80EA0007)
#define A3D_PORTS 4
#define A3D_INVALID 0xFFFFFFFFu
static struct { int used; uint32_t max_objects, objects, next_object; } a3d_ports[A3D_PORTS];
static int a3d_ready;
static pthread_mutex_t a3d_lock=PTHREAD_MUTEX_INITIALIZER;
/* sceAudio3dInitialize(reserved): reserved must be 0; NOT_READY when already initialized. */
static ABI int32_t a3d_initialize(int64_t reserved) {
    if (reserved) return A3D_INVALID_PARAMETER;
    pthread_mutex_lock(&a3d_lock);
    int32_t r=a3d_ready ? A3D_NOT_READY : 0;
    a3d_ready=1;
    pthread_mutex_unlock(&a3d_lock);
    return r;
}
/* sceAudio3dTerminate: closes every port. */
static ABI int32_t a3d_terminate(void) {
    pthread_mutex_lock(&a3d_lock);
    int32_t r=a3d_ready ? 0 : A3D_NOT_READY;
    a3d_ready=0; memset(a3d_ports,0,sizeof(a3d_ports));
    pthread_mutex_unlock(&a3d_lock);
    return r;
}
/* sceAudio3dPortOpen(user, params, port): the system user; OrbisAudio3dOpenParameters of size
 * 0x10/0x18/0x20/0x28 {size, granularity, rate, max objects, queue depth, buffer mode, pad,
 * beds}; 48 kHz, granularity a multiple of 256 with the queue depth limits, 2 beds (or 3). */
static ABI int32_t a3d_port_open(int32_t user,const uint8_t *params,uint32_t *port) {
    if (port) *port=A3D_INVALID;
    if (user!=USER_SYSTEM || !params || !port) return A3D_INVALID_PARAMETER;
    uint64_t size; uint32_t f[6]={0,0,512,2,0,2};
    memcpy(&size,params,8); memcpy(f,params+8,8);
    switch (size&~UINT64_C(7)) {
    case 0x10: break;
    case 0x18: memcpy(f+2,params+16,8); break;
    case 0x20: memcpy(f+2,params+16,12); break;
    case 0x28: memcpy(f+2,params+16,12); memcpy(f+5,params+32,4); break;
    default: return A3D_INVALID_PARAMETER;
    }
    uint32_t granularity=f[0], rate=f[1], max_objects=f[2], depth=f[3], mode=f[4], beds=f[5];
    if (rate || granularity<0x100 || (granularity&0xff) || !max_objects || !depth ||
        (granularity==0x100 && depth>0x40) || (granularity==0x200 && depth>0x1f) ||
        (granularity==0x300 && depth>0x14) || (depth>0xf && granularity>0x3ff) || mode>2 ||
        (beds&~1u)!=2) return A3D_INVALID_PARAMETER;
    pthread_mutex_lock(&a3d_lock);
    int32_t r=a3d_ready ? A3D_OUT_OF_RESOURCES : A3D_NOT_READY;
    for (uint32_t i=0;a3d_ready && i<A3D_PORTS;++i) if (!a3d_ports[i].used) {
        a3d_ports[i].used=1; a3d_ports[i].max_objects=max_objects>0x200 ? 0x200 : max_objects;
        a3d_ports[i].objects=a3d_ports[i].next_object=0;
        *port=i; r=0; break;
    }
    pthread_mutex_unlock(&a3d_lock);
    return r;
}
static int a3d_port(uint32_t port) { return port<A3D_PORTS && a3d_ports[port].used; }
static ABI int32_t a3d_port_close(uint32_t port) {
    pthread_mutex_lock(&a3d_lock);
    int32_t r=a3d_port(port) ? 0 : A3D_INVALID_PORT;
    if (!r) a3d_ports[port].used=0;
    pthread_mutex_unlock(&a3d_lock);
    return r;
}
/* sceAudio3dPortFlush(port): nothing is queued for output. */
static ABI int32_t a3d_port_flush(uint32_t port) {
    pthread_mutex_lock(&a3d_lock); int ok=a3d_port(port); pthread_mutex_unlock(&a3d_lock);
    return ok ? 0 : A3D_INVALID_PORT;
}
/* sceAudio3dObjectReserve(port, object): ids from 1, at most the port's max objects. */
static ABI int32_t a3d_object_reserve(uint32_t port,uint32_t *object) {
    if (!object) return A3D_INVALID_PARAMETER;
    *object=A3D_INVALID;
    pthread_mutex_lock(&a3d_lock);
    int32_t r=!a3d_port(port) ? A3D_INVALID_PORT : a3d_ports[port].objects>=a3d_ports[port].max_objects ? A3D_OUT_OF_RESOURCES : 0;
    if (!r) { ++a3d_ports[port].objects; *object=++a3d_ports[port].next_object; }
    pthread_mutex_unlock(&a3d_lock);
    return r;
}
/* sceAudio3dObjectSetAttributes(port, object, count, attributes): accepted and dropped. */
static ABI int32_t a3d_object_attributes(uint32_t port,uint32_t object,uint64_t count,const void *attributes) {
    (void)object;
    pthread_mutex_lock(&a3d_lock); int ok=a3d_port(port); pthread_mutex_unlock(&a3d_lock);
    if (!ok) return A3D_INVALID_PORT;
    return !count || !attributes ? A3D_INVALID_PARAMETER : 0;
}

/* ---- SocialScreen (PS VR): no headset, the TV already shows the game ----
 * sceSocialScreenInitialize / Terminate / SetMode / ConfigureSeparateMode / OpenSeparateMode /
 * CloseSeparateMode: OK with nothing to switch. InitializeSeparateModeParameter(param) clears the
 * 32-byte parameter the game then fills (the eboot keeps it in 32 bytes of its frame). */
static ABI int32_t social_ok(void) { return 0; }
static ABI int32_t social_param_init(void *param) { if (param) memset(param,0,32); return 0; }

/* sceVideoOutConfigureOptionsInitialize_(options, size): clears the options record (the eboot
 * passes its 16-byte record and its size). */
#define VIDEO_OUT_INVALID_VALUE ((int32_t)0x80290001)
#define VIDEO_OUT_INVALID_ADDRESS ((int32_t)0x80290002)
static ABI int32_t video_options_init(void *options,uint64_t size) {
    if (!options) return VIDEO_OUT_INVALID_ADDRESS;
    if (!size || size>0x100) return VIDEO_OUT_INVALID_VALUE;
    memset(options,0,size); return 0;
}

/* sceRazorCpuJobManagerDispatch / Job / Sequence: profiler markers of libSceJobManager; no
 * profiler is attached (OK, as shadPS4). */
static ABI int32_t razor_marker(void) { return 0; }
/* _sceUlobjmgrRegisterObject(object, kind, id) / _sceUlobjmgrUnregisterObject(id) (shadPS4
 * ulobjmgr.cpp): user-level object registry for the debugger; POSIX EINVAL for a NULL object,
 * kind 0 or no id slot, else id 0; ids from 0x4000 are invalid to unregister. */
static ABI int32_t ulobj_register(uint64_t object,int32_t kind,uint32_t *id) {
    if (!object || !kind || !id) return 22;
    *id=0; return 0;
}
static ABI int32_t ulobj_unregister(uint32_t id) { return id>=0x4000 ? 22 : 0; }

/* sceSystemServiceLoadExec(path, argv): ends this process and starts `path`. Only the game's own
 * eboot can be started (runtime_restart); anything else stops. */
static ABI int32_t load_exec(const char *path,const char *const *argv) {
    (void)argv;
    if (path && (!strcmp(path,"/app0/eboot.bin") || !strcmp(path,"app0/eboot.bin"))) {
        puts("Runtime: sceSystemServiceLoadExec of the game itself: restarting");
        runtime_restart();
    }
    fprintf(stderr,"STOP: sceSystemServiceLoadExec(%s): only the game itself can be started\n",path ? path : "(null)");
    exit(21);
}

static const RuntimeExport exports[]={
    {"sceCameraIsAttached",camera_attached}, {"sceCameraOpen",camera_open}, {"sceCameraClose",camera_close},
    {"sceCameraIsValidFrameData",camera_valid_frame}, {"sceCameraSetConfig",camera_config},
    {"sceCameraStart",camera_start}, {"sceCameraSetVideoSync",camera_video_sync},
    {"sceCameraGetFrameData",camera_frame},
    {"sceAudio3dInitialize",a3d_initialize}, {"sceAudio3dTerminate",a3d_terminate},
    {"sceAudio3dPortOpen",a3d_port_open}, {"sceAudio3dPortClose",a3d_port_close},
    {"sceAudio3dPortFlush",a3d_port_flush}, {"sceAudio3dObjectReserve",a3d_object_reserve},
    {"sceAudio3dObjectSetAttributes",a3d_object_attributes},
    {"sceSocialScreenInitialize",social_ok}, {"sceSocialScreenTerminate",social_ok},
    {"sceSocialScreenSetMode",social_ok}, {"sceSocialScreenConfigureSeparateMode",social_ok},
    {"sceSocialScreenOpenSeparateMode",social_ok}, {"sceSocialScreenCloseSeparateMode",social_ok},
    {"sceSocialScreenInitializeSeparateModeParameter",social_param_init},
    {"sceVideoOutConfigureOptionsInitialize_",video_options_init},
    {"sceRazorCpuJobManagerDispatch",razor_marker}, {"sceRazorCpuJobManagerJob",razor_marker},
    {"sceRazorCpuJobManagerSequence",razor_marker},
    {"_sceUlobjmgrRegisterObject",ulobj_register}, {"_sceUlobjmgrUnregisterObject",ulobj_unregister},
    {"sceSystemServiceLoadExec",load_exec},
};
uintptr_t runtime_devices_resolve(const char *name) { return RUNTIME_LOOKUP(exports,name); }
