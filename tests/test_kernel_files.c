/* File attributes, time-zone conversions and other libkernel extras (src/runtime_kernel.c),
 * against the contracts shadPS4 follows. Guest paths map to a scratch directory. */
#define _GNU_SOURCE
#undef NDEBUG /* the checks are asserts; release builds define NDEBUG */
#include "../src/runtime_kernel.c"
#include <assert.h>
#include <sys/stat.h>

/* The process time zone, as a POSIX TZ string. */
static void set_zone(const char *zone) {
#ifdef _WIN32
    _putenv_s("TZ",zone); _tzset();
#else
    setenv("TZ",zone,1); tzset();
#endif
}
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
    /* Before anything loads the zone: the Windows CRT keeps the machine zone's summer time bias
     * when TZ changes later. */
    set_zone("EST5EDT");
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

    /* UTC <-> local: the two conversions are inverse; west_sec is the standard offset and dst_sec
     * the summer time in effect, so libc's mktime and localtime see summer time (US Eastern time,
     * where 2026-01-15 is winter and 2026-07-15 summer), and a zone without it. */
    const int64_t winter=1768478400, summer=1784116800; /* 12:00 UTC */
    int64_t local=0, back=0; GuestTimesec st; uint64_t dst=7; int32_t dst_seconds=-1;
    assert(kernel_utc_to_local(winter,NULL,NULL,NULL)==ERR(22));
    assert(kernel_local_to_utc(winter,0,&back,NULL,NULL)==ERR(22));
    assert(kernel_utc_to_local(winter,&local,&st,&dst)==0 && local==winter-5*3600 && st.t==winter);
    assert(st.west_sec==-5*3600 && st.dst_sec==0 && dst==0);
    assert(kernel_local_to_utc(local,0,&back,&st,&dst_seconds)==0 && back==winter && st.t==winter);
    assert(st.west_sec==-5*3600 && st.dst_sec==0 && dst_seconds==3600);
    assert(kernel_utc_to_local(summer,&local,&st,&dst)==0 && local==summer-4*3600);
    assert(st.west_sec==-5*3600 && st.dst_sec==3600 && dst==3600);
    assert(kernel_local_to_utc(local,1,&back,&st,&dst_seconds)==0 && back==summer);
    assert(st.west_sec==-5*3600 && st.dst_sec==3600 && dst_seconds==3600);
    /* 02:30 on 2026-03-08 does not exist there: any answer within the hour, but an answer. */
    const int64_t gap=1772937000;
    assert(kernel_local_to_utc(gap,0,&back,&st,&dst_seconds)==0 && llabs(back-(gap+5*3600))<=3600);
    set_zone("SGT-8");
    assert(kernel_utc_to_local(summer,&local,&st,&dst)==0 && local==summer+8*3600);
    assert(st.west_sec==8*3600 && st.dst_sec==0 && dst==0);
    assert(kernel_local_to_utc(local,0,&back,&st,&dst_seconds)==0 && back==summer && dst_seconds==0);

    /* Development-kit LEDs and libc's internal segment. */
    uint64_t info[2]={1,2};
    assert(kernel_set_gpo(0xff)==0 && internal_segment_info(info)==0 && !info[0] && !info[1]);
    assert(internal_segment_info(NULL)==ERR(14) && kernel_sleep(0)==0);

    remove(path); rmdir(root);
    puts("kernel file tests passed");
    return 0;
}
