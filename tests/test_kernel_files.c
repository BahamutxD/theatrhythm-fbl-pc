/* File attributes, time-zone conversions and other libkernel extras (src/runtime_kernel.c),
 * against the contracts shadPS4 follows. Guest paths map to a scratch directory. */
#define _GNU_SOURCE
#undef NDEBUG /* the checks are asserts; release builds define NDEBUG */
#include "../src/runtime_kernel.c"
#include <assert.h>
#include <sys/stat.h>

int32_t *runtime_errno(void) { static int32_t value; return &value; }
void runtime_report(void) {}
void runtime_thread_count_yield(int zero_sleep) { (void)zero_sleep; }
uintptr_t runtime_lookup(const RuntimeExport *table, size_t count, const char *name) { (void)table; (void)count; (void)name; return 0; }
/* /data maps to out/kernel-files-scratch; /app0 to the same place, read-only through game_path. */
static const char *root="out/kernel-files-scratch";
int runtime_file_translate(const char *guest, char *out, size_t size) {
    const char *rest=!strncmp(guest,"/data",5) ? guest+5 : !strncmp(guest,"/app0",5) ? guest+5 : NULL;
    if (!rest) return ENOENT;
    snprintf(out,size,"%s%s",root,rest); return 0;
}
int64_t runtime_file_stat(const char *guest, void *st) {
    (void)st; char path[512]; struct stat s;
    if (runtime_file_translate(guest,path,sizeof(path))) return -ENOENT;
    return stat(path,&s) ? -errno : 0;
}
int64_t runtime_file_fstat(int fd, void *st) { (void)st; return fd==3 ? 0 : -EBADF; }
static int64_t last_getdents_fd=-1;
int64_t runtime_file_getdents(int fd, char *buffer, uint64_t size, int64_t *base) {
    (void)buffer; assert(!base); last_getdents_fd=fd;
    return fd==3 ? (int64_t)size/2 : -EBADF;
}

int main(void) {
    char path[512];
    mkdir("out",0755); mkdir(root,0755);
    snprintf(path,sizeof(path),"%s/file.bin",root);
    FILE *f=fopen(path,"wb"); assert(f); fputs("x",f); fclose(f);

    /* chmod / sceKernelChmod: the file must exist and be writable. */
    assert(posix_chmod("/data/file.bin",0644)==0);
    assert(posix_chmod("/data/missing",0644)==-1 && *runtime_errno()==2);
    assert(kernel_chmod("/app0/file.bin",0644)==ERR(30) && kernel_chmod(NULL,0)==ERR(14));
    assert(posix_fchmod(3,0644)==0 && posix_fchmod(4,0644)==-1 && *runtime_errno()==9);

    /* utimes sets the host times; bad microseconds are EINVAL. */
    GuestTimeval times[2]={{1000000000,0},{1200000000,5}};
    assert(posix_utimes("/data/file.bin",times)==0);
    struct stat s; assert(stat(path,&s)==0 && s.st_mtime==1200000000);
    assert(kernel_utimes("/data/file.bin",NULL)==0 && stat(path,&s)==0 && s.st_mtime>1200000000);
    times[1].usec=1000000;
    assert(posix_utimes("/data/file.bin",times)==-1 && *runtime_errno()==22);
    assert(kernel_utimes("/app0/file.bin",NULL)==ERR(30));
    assert(posix_futimes(3,NULL)==0 && posix_futimes(5,NULL)==-1);

    /* fcntl on a file. */
    assert(posix_fcntl(3,1,0)==0 && posix_fcntl(3,2,1)==0 && posix_fcntl(3,3,0)==2);
    assert(posix_fcntl(3,4,2|4)==0 && posix_fcntl(3,4,8)==-1 && *runtime_errno()==22);
    unsigned char lock[24]; memset(lock,0,sizeof(lock));
    assert(posix_fcntl(3,7,(uint64_t)(uintptr_t)lock)==0 && lock[20]==2);
    assert(posix_fcntl(3,8,(uint64_t)(uintptr_t)lock)==0 && posix_fcntl(9,1,0)==-1 && *runtime_errno()==9);

    /* sceKernelGetdents: getdirentries without a base; errors as SCE codes. */
    char buffer[1024];
    assert(kernel_getdents(3,buffer,sizeof(buffer))==512 && last_getdents_fd==3);
    assert(kernel_getdents(6,buffer,sizeof(buffer))==ERR(9));

    /* UTC <-> local: the two conversions are inverse and agree with sceKernelGettimezone. */
    const int64_t utc=1700000000;
    int64_t local=0, back=0; GuestTimesec st; uint64_t dst=7;
    assert(kernel_utc_to_local(utc,NULL,NULL,NULL)==ERR(22));
    assert(kernel_utc_to_local(utc,&local,&st,&dst)==0 && st.t==utc && dst==0 && st.dst_sec==0);
    assert(local-utc==(int64_t)(int32_t)st.west_sec);
    GuestTimezone tz; int32_t dst_seconds=-1;
    assert(kernel_local_to_utc(local,0,&back,NULL,NULL)==ERR(22));
    assert(kernel_local_to_utc(local,0,&back,&tz,&dst_seconds)==0 && back==utc && dst_seconds==0);
    assert(tz.minuteswest==(int32_t)(-(local-utc)/60));

    /* Development-kit LEDs and libc's internal segment. */
    uint64_t info[2]={1,2};
    assert(kernel_set_gpo(0xff)==0 && internal_segment_info(info)==0 && !info[0] && !info[1]);
    assert(internal_segment_info(NULL)==ERR(14) && kernel_sleep(0)==0);

    remove(path); rmdir(root);
    puts("kernel file tests passed");
    return 0;
}
