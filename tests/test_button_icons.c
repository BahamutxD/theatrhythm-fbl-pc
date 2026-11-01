/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Button prompt icons (src/runtime_button_icons.c): a fixed set mounts its bundle; auto finds the
 * atlas in (fake) guest memory, rewrites it for the device last used and moves the mount with it. */
#define _GNU_SOURCE
#undef NDEBUG /* the checks are asserts; release builds define NDEBUG */
#include "../src/runtime_button_icons.c"
#include <assert.h>
#include <sys/stat.h>
#include <time.h>

/* Guest memory: one mapped range at GUEST, host bytes in `memory`. */
#define GUEST UINT64_C(0x2000000000)
#define MEMORY (1u<<20)
#define ATLAS UINT64_C(0x40100) /* 256-aligned offset of the loaded atlas */
static unsigned char memory[MEMORY];
static uintptr_t invalidated; static uint64_t invalidated_size; static int invalidations;
void runtime_memory_visit_mapped(void (*visit)(uintptr_t, const unsigned char *, uint64_t, void *), void *context) {
    visit(GUEST,memory,MEMORY,context);
}
void runtime_memory_read_backing(uintptr_t address, void *data, uint64_t size) { memcpy(data,memory+(address-GUEST),size); }
int runtime_memory_write_backing(uintptr_t address, const void *data, uint64_t size) {
    memcpy(memory+(address-GUEST),data,size); return 1;
}
void runtime_memory_invalidate(uintptr_t address, uint64_t size) { invalidated=address; invalidated_size=size; ++invalidations; }

/* File mounts: the host file each of the two guest paths serves ("" for the game's own). */
static char mounted[2][600];
static int slot(const char *guest) { return strncmp(guest,"/app0/",6) ? 1 : 0; }
int runtime_file_mount(const char *guest, const char *host) {
    assert(strstr(guest,bundle)); snprintf(mounted[slot(guest)],600,"%s",host); return 0;
}
void runtime_file_unmount(const char *guest) { mounted[slot(guest)][0]=0; }

static const char *dir="out/button-icons-scratch";
static unsigned char sets[BUTTON_ICONS_SETS][TEXTURE_BYTES];
static void write_file(const char *name, const void *data, size_t size) {
    char path[256]; snprintf(path,sizeof(path),"%s/%s",dir,name);
    FILE *f=fopen(path,"wb"); assert(f); assert(fwrite(data,1,size,f)==size); assert(!fclose(f));
}
static void expect_mount(const char *name) {
    char want[600]="";
    if (name) snprintf(want,sizeof(want),"%s/%s.bundle",dir,name);
    assert(!strcmp(mounted[0],want) && !strcmp(mounted[1],want));
}
/* Reports a device and waits for the worker to finish the switch. */
static void use(int set) {
    runtime_button_icons_input(set);
    pthread_mutex_lock(&lock);
    while (shown!=set) pthread_cond_wait(&wake,&lock);
    pthread_mutex_unlock(&lock);
}

int main(void) {
    mkdir("out",0755); mkdir(dir,0755);
    /* Three sets alike except here and there, as icon sheets are (transparent areas match). */
    for (int set=0;set<BUTTON_ICONS_SETS;++set) {
        for (size_t i=0;i<TEXTURE_BYTES;++i) sets[set][i]=(unsigned char)(i*31u>>3);
        for (size_t i=4096;i<TEXTURE_BYTES;i+=4096) sets[set][i]^=(unsigned char)(set*0x55+1);
        static const char *names[]={"ps4.texture","xbox.texture","keyboard.texture"};
        write_file(names[set],sets[set],TEXTURE_BYTES);
    }
    write_file("xbox.bundle","x",1);
    write_file("keyboard.bundle","k",1);

    /* A fixed set only mounts its bundle. */
    runtime_button_icons_configure(dir,"keyboard");
    expect_mount("keyboard");
    assert(!automatic);
    runtime_file_unmount("/app0/x"); runtime_file_unmount("/archive/x");
    runtime_button_icons_configure(dir,"ps4");
    expect_mount(NULL);

    /* auto: the game loaded its own atlas; a copy off the texture alignment is not one. */
    for (size_t i=0;i<MEMORY;++i) memory[i]=(unsigned char)(i*7u);
    memcpy(memory+ATLAS,sets[BUTTON_ICONS_PS4],TEXTURE_BYTES);
    memcpy(memory+ATLAS+TEXTURE_BYTES+64,sets[BUTTON_ICONS_PS4],TEXTURE_BYTES);
    runtime_button_icons_configure(dir,"auto");
    assert(automatic);
    runtime_button_icons_input(BUTTON_ICONS_PS4); /* already shown: nothing to do */
    assert(!invalidations);

    use(BUTTON_ICONS_XBOX);
    assert(!memcmp(memory+ATLAS,sets[BUTTON_ICONS_XBOX],TEXTURE_BYTES));
    assert(!memcmp(memory+ATLAS+TEXTURE_BYTES+64,sets[BUTTON_ICONS_PS4],TEXTURE_BYTES));
    assert(invalidations==1 && invalidated==GUEST+ATLAS && invalidated_size==TEXTURE_BYTES);
    expect_mount("xbox");

    use(BUTTON_ICONS_KEYBOARD);
    assert(!memcmp(memory+ATLAS,sets[BUTTON_ICONS_KEYBOARD],TEXTURE_BYTES));
    assert(invalidations==2);
    expect_mount("keyboard");

    use(BUTTON_ICONS_PS4);
    assert(!memcmp(memory+ATLAS,sets[BUTTON_ICONS_PS4],TEXTURE_BYTES));
    expect_mount(NULL);

    /* The game freed the atlas: nothing is written, only the mount follows (a later load is right). */
    memset(memory+ATLAS,0,TEXTURE_BYTES);
    use(BUTTON_ICONS_XBOX);
    assert(invalidations==3);
    for (size_t i=0;i<TEXTURE_BYTES;++i) assert(!memory[ATLAS+i]);
    expect_mount("xbox");

    /* Loaded again elsewhere (from the mounted bundle): found by a new search. */
    memcpy(memory+0x80000,sets[BUTTON_ICONS_XBOX],TEXTURE_BYTES);
    use(BUTTON_ICONS_KEYBOARD);
    assert(!memcmp(memory+0x80000,sets[BUTTON_ICONS_KEYBOARD],TEXTURE_BYTES));
    assert(invalidations==4 && invalidated==GUEST+0x80000);
    puts("button icons: ok");
    return 0;
}
