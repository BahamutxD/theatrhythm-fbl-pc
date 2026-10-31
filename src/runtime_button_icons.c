/* Button prompt icons. The game draws every prompt from one 512x256 BC7 atlas in one bundle;
 * scripts/button_icons.py builds Xbox and keyboard copies of that bundle in <out>/button_icons, with
 * each set's texture data as it lands in guest memory (<set>.texture).
 *
 * A fixed set (settings.ini button_icons=xbox or keyboard) mounts its bundle over the game's at start.
 * "auto" follows the device the player last used (runtime_pad.c reports it): on a change the mount
 * follows, so a later load of the bundle gets that set, and a worker thread finds the atlas already
 * in guest memory by its known bytes, writes the new set over it through the backing view and tells
 * the GPU the range changed, so the texture reloads on its next use. */
#define _GNU_SOURCE
#include "runtime.h"
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TEXTURE_BYTES (512*256)  /* BC7, one byte per texel, no mipmaps */
#define TEXTURE_ALIGN 256        /* GCN texture base addresses are in 256-byte units */
#define MAX_COPIES 8
static const char bundle[]="Media/StreamingAssets/aa/PS4/26f8caa7ece23b9c50dba3f9dd2af58f.bundle";
static const char *const prefixes[]={"/app0/","/archive/mount/point/"}; /* both paths the game may use */
static const char *const set_names[BUTTON_ICONS_SETS]={"ps4","xbox","keyboard"};

static char folder[512];
static unsigned char *textures[BUTTON_ICONS_SETS];
static size_t probe_offset;      /* 8 bytes that differ between every two sets */
static uint64_t probes[BUTTON_ICONS_SETS];
static int automatic;
static pthread_mutex_t lock=PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t wake=PTHREAD_COND_INITIALIZER;
static int wanted=BUTTON_ICONS_PS4, shown=BUTTON_ICONS_PS4; /* nothing mounted: the game's own icons */
static uintptr_t copies[MAX_COPIES]; static size_t copy_count; /* atlases in guest memory (worker only) */

static void bundle_file(int set, char *out, size_t size) { snprintf(out,size,"%s/%s.bundle",folder,set_names[set]); }
static int readable(const char *path) { FILE *f=fopen(path,"rb"); if (f) fclose(f); return f!=NULL; }

/* The game's bundle path serves `set`: its replacement, or the game's file for ps4. */
static int mount_set(int set) {
    char host[600], guest[128];
    bundle_file(set,host,sizeof(host));
    int failed=0;
    for (size_t i=0;i<sizeof(prefixes)/sizeof(*prefixes);++i) {
        snprintf(guest,sizeof(guest),"%s%s",prefixes[i],bundle);
        if (set==BUTTON_ICONS_PS4) runtime_file_unmount(guest);
        else failed|=runtime_file_mount(guest,host);
    }
    return failed;
}

static unsigned char *load_texture(int set) {
    char path[600];
    snprintf(path,sizeof(path),"%s/%s.texture",folder,set_names[set]);
    FILE *f=fopen(path,"rb");
    unsigned char *data=f ? malloc(TEXTURE_BYTES+1) : NULL;
    size_t n=data ? fread(data,1,TEXTURE_BYTES+1,f) : 0;
    if (f) fclose(f);
    if (n==TEXTURE_BYTES) return data;
    free(data);
    printf("Button icons: %s %s (python run.py prepare); PS4 icons\n",path,f ? "is not an icon texture" : "missing");
    return NULL;
}

/* The set an atlas-sized block holds, or -1. */
static int set_of(const unsigned char *p) {
    uint64_t v; memcpy(&v,p+probe_offset,8);
    for (int set=0;set<BUTTON_ICONS_SETS;++set)
        if (v==probes[set] && !memcmp(p,textures[set],TEXTURE_BYTES)) return set;
    return -1;
}

typedef struct { uintptr_t found[MAX_COPIES]; size_t count; } Scan;
static void scan_range(uintptr_t address, const unsigned char *bytes, uint64_t size, void *context) {
    Scan *s=context;
    const uintptr_t end=address+size;
    for (uintptr_t at=(address+TEXTURE_ALIGN-1)&~(uintptr_t)(TEXTURE_ALIGN-1);
         at+TEXTURE_BYTES<=end && s->count<MAX_COPIES; at+=TEXTURE_ALIGN) {
        const unsigned char *p=bytes+(at-address);
        uint64_t v; memcpy(&v,p+probe_offset,8);
        if (v!=probes[0] && v!=probes[1] && v!=probes[2]) continue;
        if (set_of(p)>=0) s->found[s->count++]=at;
    }
}

/* Writes `set` over every atlas in guest memory. Known copies are checked again first (the game may
 * have freed one); memory is searched only when none is left. */
static void show(int set) {
    static unsigned char current[TEXTURE_BYTES];
    size_t kept=0;
    for (size_t i=0;i<copy_count;++i) {
        runtime_memory_read_backing(copies[i],current,TEXTURE_BYTES);
        if (set_of(current)>=0) copies[kept++]=copies[i];
    }
    copy_count=kept;
    if (!copy_count) {
        Scan s={{0},0};
        runtime_memory_visit_mapped(scan_range,&s);
        memcpy(copies,s.found,s.count*sizeof(*copies));
        copy_count=s.count;
    }
    size_t written=0;
    for (size_t i=0;i<copy_count;++i) {
        runtime_memory_read_backing(copies[i],current,TEXTURE_BYTES);
        const int had=set_of(current);
        if (had<0 || had==set) continue;
        if (runtime_memory_write_backing(copies[i],textures[set],TEXTURE_BYTES)) {
            runtime_memory_invalidate(copies[i],TEXTURE_BYTES);
            ++written;
        }
    }
    printf("Button icons: %s (%zu of %zu loaded atlases rewritten)\n",set_names[set],written,copy_count);
}

static void *worker(void *unused) {
    (void)unused;
    pthread_mutex_lock(&lock);
    for (;;) {
        while (wanted==shown) pthread_cond_wait(&wake,&lock);
        const int set=wanted;
        pthread_mutex_unlock(&lock);
        if (mount_set(set)) printf("Button icons: cannot mount the %s bundle\n",set_names[set]);
        show(set);
        pthread_mutex_lock(&lock);
        shown=set;
        pthread_cond_broadcast(&wake);
    }
    return NULL;
}

void runtime_button_icons_configure(const char *dir, const char *mode) {
    snprintf(folder,sizeof(folder),"%s",dir);
    int fixed=-1;
    for (int set=0;set<BUTTON_ICONS_SETS;++set) if (!strcmp(mode,set_names[set])) fixed=set;
    if (fixed==BUTTON_ICONS_PS4) return;
    if (fixed>0) {
        char host[600];
        bundle_file(fixed,host,sizeof(host));
        if (!readable(host)) printf("Button icons: %s missing (python run.py prepare); PS4 icons\n",host);
        else if (mount_set(fixed)) printf("Button icons: cannot mount %s; PS4 icons\n",host);
        else printf("Button icons: %s\n",set_names[fixed]);
        return;
    }
    if (strcmp(mode,"auto")) return;
    for (int set=0;set<BUTTON_ICONS_SETS;++set) {
        char host[600];
        bundle_file(set,host,sizeof(host));
        if (set!=BUTTON_ICONS_PS4 && !readable(host)) {
            printf("Button icons: %s missing (python run.py prepare); PS4 icons\n",host);
            return;
        }
        if (!(textures[set]=load_texture(set))) return;
    }
    for (probe_offset=0; probe_offset<TEXTURE_BYTES; probe_offset+=8) {
        for (int set=0;set<BUTTON_ICONS_SETS;++set) memcpy(&probes[set],textures[set]+probe_offset,8);
        if (probes[0]!=probes[1] && probes[0]!=probes[2] && probes[1]!=probes[2]) break;
    }
    if (probe_offset==TEXTURE_BYTES) { puts("Button icons: the icon sets are identical; PS4 icons"); return; }
    pthread_t thread;
    if (pthread_create(&thread,NULL,worker,NULL)) { puts("Button icons: no worker thread; PS4 icons"); return; }
    pthread_detach(thread);
    __atomic_store_n(&automatic,1,__ATOMIC_RELEASE);
    puts("Button icons: auto (follow the device last used)");
}

/* The player used a device whose icons are `set`; called for every sample with input. */
void runtime_button_icons_input(int set) {
    if (!__atomic_load_n(&automatic,__ATOMIC_ACQUIRE) || set<0 || set>=BUTTON_ICONS_SETS) return;
    if (__atomic_load_n(&wanted,__ATOMIC_RELAXED)==set) return;
    pthread_mutex_lock(&lock);
    if (wanted!=set) { __atomic_store_n(&wanted,set,__ATOMIC_RELAXED); pthread_cond_signal(&wake); }
    pthread_mutex_unlock(&lock);
}
