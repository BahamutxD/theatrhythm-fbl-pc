/* Event flags (src/runtime_eventflag.c) against the PS4 contract shadPS4 follows. */
#include "../src/runtime_eventflag.c"
#include <assert.h>

uintptr_t runtime_lookup(const RuntimeExport *table, size_t count, const char *nid) { (void)table; (void)count; (void)nid; return 0; }
void *runtime_low_alloc(size_t size) { return calloc(1, size); }
void runtime_low_free(void *pointer) { free(pointer); }
uint64_t runtime_wait_begin(void) { return 0; }
void runtime_wait_end(const char *what, uint64_t start, const char *detail, long long arg) { (void)what; (void)start; (void)detail; (void)arg; }

static Flag *shared;
static uint64_t seen;
static int32_t waited;
static void *waiter_thread(void *arg) {
    (void)arg;
    waited = evf_wait(shared, 0x6, WAIT_AND | CLEAR_PAT, &seen, NULL);
    return NULL;
}
static void *cancelled_thread(void *arg) {
    (void)arg;
    waited = evf_wait(shared, 0x100, WAIT_OR, &seen, NULL);
    return NULL;
}
static void wait_for_waiter(void) {
    for (;;) {
        pthread_mutex_lock(&lock);
        int queued = shared->first != NULL;
        pthread_mutex_unlock(&lock);
        if (queued) return;
        sched_yield();
    }
}

int main(void) {
    Flag *f = NULL;
    uint64_t result = 0;
    uint32_t timeout;
    assert(evf_create(&f, "test", 0x40, 0, NULL) == ERR(22));          /* unknown attribute */
    assert(evf_create(&f, "a name that is thirty-two chars!", 0, 0, NULL) == ERR(63));
    assert(evf_create(&f, "test", ATTR_MULTI | ATTR_TH_FIFO, 0x1, NULL) == 0);

    /* Poll: OR and AND modes, clears, EBUSY when unmet. */
    assert(evf_poll(f, 0x3, WAIT_AND, &result) == ERR(16) && result == 0x1);
    assert(evf_poll(f, 0x3, WAIT_OR, &result) == 0 && result == 0x1);
    assert(evf_set(f, 0x2) == 0);
    assert(evf_poll(f, 0x3, WAIT_AND | CLEAR_PAT, &result) == 0 && result == 0x3);
    assert(evf_poll(f, 0x3, WAIT_OR, &result) == ERR(16) && result == 0);
    assert(evf_poll(f, 0, WAIT_OR, &result) == ERR(22));
    assert(evf_poll(f, 1, 0x3, &result) == ERR(22));                    /* bad wait mode */

    /* Clear keeps the pattern's bits; CLEAR_ALL zeroes. */
    assert(evf_set(f, 0xf0) == 0 && evf_clear(f, 0x30) == 0);
    assert(evf_poll(f, 0xff, WAIT_OR | CLEAR_ALL, &result) == 0 && result == 0x30);
    assert(evf_poll(f, 0xff, WAIT_OR, &result) == ERR(16));

    /* A timed wait that expires: ETIMEDOUT, no time left; a zero timeout fails at once. */
    timeout = 2000;
    assert(evf_wait(f, 0x1, WAIT_OR, &result, &timeout) == ERR(60) && timeout == 0);
    timeout = 0;
    assert(evf_wait(f, 0x1, WAIT_OR, &result, &timeout) == ERR(60));

    /* A blocked waiter wakes when its pattern is complete, with the bits from before its clear. */
    shared = f;
    pthread_t thread;
    assert(!pthread_create(&thread, NULL, waiter_thread, NULL));
    wait_for_waiter();
    assert(evf_set(f, 0x2) == 0);                                       /* not yet: needs 0x4 too */
    assert(evf_set(f, 0x5) == 0);
    pthread_join(thread, NULL);
    assert(waited == 0 && seen == 0x7);
    assert(evf_poll(f, 0x7, WAIT_OR, &result) == 0 && result == 0x1);   /* 0x6 cleared by the waiter */

    /* Cancel sets the pattern and returns ECANCELED to waiters, with the count. */
    assert(!pthread_create(&thread, NULL, cancelled_thread, NULL));
    wait_for_waiter();
    int32_t count = -1;
    assert(evf_cancel(f, 0x42, &count) == 0 && count == 1);
    pthread_join(thread, NULL);
    assert(waited == ERR(85) && seen == 0x42);

    /* SINGLE flags take one waiter at a time. */
    Flag *single = NULL;
    assert(evf_create(&single, "single", ATTR_SINGLE, 0, NULL) == 0);
    shared = single;
    assert(!pthread_create(&thread, NULL, cancelled_thread, NULL));
    wait_for_waiter();
    assert(evf_poll(single, 0x1, WAIT_OR, &result) == ERR(1));
    /* Deleting wakes the waiter with EACCES; the handle is then unknown. */
    assert(evf_delete(single) == 0);
    pthread_join(thread, NULL);
    assert(waited == ERR(13));
    assert(evf_set(single, 1) == ERR(3) && evf_delete(single) == ERR(3));

    assert(evf_delete(f) == 0);
    puts("eventflag test: ok");
    return 0;
}
