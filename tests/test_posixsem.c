/* POSIX semaphores (src/runtime_sema.c) against the contract shadPS4 follows. */
#include "../src/runtime_sema.c"
#include <assert.h>

void *runtime_low_alloc(size_t size) { return calloc(1, size); }
void runtime_low_free(void *pointer) { free(pointer); }
uint64_t runtime_wait_begin(void) { return 0; }
int runtime_verbose(void) { return 0; }
void runtime_wait_end(const char *what, uint64_t start, const char *detail, long long arg) { (void)what; (void)start; (void)detail; (void)arg; }
static int32_t guest_errno;
int32_t *runtime_errno(void) { return &guest_errno; }

static PosixSem *shared;
static int32_t posted;
static void *poster(void *arg) {
    (void)arg;
    posted = posix_sem_post(&shared);
    return NULL;
}

int main(void) {
    assert(posix_sem_init(&shared, 0, 0x80000000u) == -1 && guest_errno == 22);
    assert(posix_sem_init(&shared, 0, 0) == 0);
    /* A wait returns once another thread posts. */
    pthread_t thread;
    assert(!pthread_create(&thread, NULL, poster, NULL));
    assert(posix_sem_wait(&shared) == 0);
    pthread_join(thread, NULL);
    assert(posted == 0);
    /* The count is kept: two posts allow two waits. */
    assert(posix_sem_post(&shared) == 0 && posix_sem_post(&shared) == 0);
    assert(posix_sem_wait(&shared) == 0 && posix_sem_wait(&shared) == 0 && shared->value == 0);
    PosixSem *full = NULL;
    assert(posix_sem_init(&full, 0, 0x7fffffff) == 0);
    assert(posix_sem_post(&full) == -1 && guest_errno == 84);
    assert(posix_sem_destroy(&full) == 0 && full == NULL);
    assert(posix_sem_post(&full) == -1 && guest_errno == 22);
    assert(posix_sem_wait(NULL) == -1 && posix_sem_destroy(NULL) == -1);
    assert(posix_sem_destroy(&shared) == 0);
    puts("posix semaphore test: ok");
    return 0;
}
