#include "eh_pool.h"

#include <pthread.h>
#include <stdlib.h>

/* macOS has no pthread_barrier_t, so the hand-off is a generation counter
 * under one mutex: the main thread publishes a pass, the helper runs its
 * parts and reports completion. */
struct eh_pool {
    int workers;
    pthread_t helper;
    pthread_mutex_t lock;
    pthread_cond_t wake, done;
    unsigned generation, finished;
    int quit;
    eh_demo *demo;
    eh_rgb565 *fb;
    int pass;
};

static void run_parts(eh_demo *d, eh_rgb565 *fb, int pass, int worker, int workers)
{
    for (int part = worker; part < EH_PARTS; part += workers)
        eh_demo_render_part(d, fb, pass, part, worker);
}

static void *helper_main(void *arg)
{
    eh_pool *p = arg;
    unsigned seen = 0;
    pthread_mutex_lock(&p->lock);
    for (;;) {
        while (p->generation == seen && !p->quit)
            pthread_cond_wait(&p->wake, &p->lock);
        if (p->quit)
            break;
        seen = p->generation;
        eh_demo *d = p->demo;
        eh_rgb565 *fb = p->fb;
        const int pass = p->pass;
        pthread_mutex_unlock(&p->lock);
        run_parts(d, fb, pass, 1, p->workers);
        pthread_mutex_lock(&p->lock);
        p->finished = seen;
        pthread_cond_signal(&p->done);
    }
    pthread_mutex_unlock(&p->lock);
    return NULL;
}

eh_pool *eh_pool_create(int workers)
{
    if (workers != 1 && workers != EH_WORKERS)
        return NULL;
    eh_pool *p = calloc(1, sizeof *p);
    if (!p)
        return NULL;
    p->workers = workers;
    if (workers == 1)
        return p;
    pthread_mutex_init(&p->lock, NULL);
    pthread_cond_init(&p->wake, NULL);
    pthread_cond_init(&p->done, NULL);
    if (pthread_create(&p->helper, NULL, helper_main, p) != 0) {
        pthread_cond_destroy(&p->done);
        pthread_cond_destroy(&p->wake);
        pthread_mutex_destroy(&p->lock);
        free(p);
        return NULL;
    }
    return p;
}

void eh_pool_destroy(eh_pool *p)
{
    if (!p)
        return;
    if (p->workers > 1) {
        pthread_mutex_lock(&p->lock);
        p->quit = 1;
        pthread_cond_signal(&p->wake);
        pthread_mutex_unlock(&p->lock);
        pthread_join(p->helper, NULL);
        pthread_cond_destroy(&p->done);
        pthread_cond_destroy(&p->wake);
        pthread_mutex_destroy(&p->lock);
    }
    free(p);
}

int eh_pool_workers(const eh_pool *p) { return p->workers; }

void eh_pool_frame(eh_pool *p, eh_demo *d, uint32_t t_ms, uint32_t buttons, eh_rgb565 *fb)
{
    if (p->workers == 1) {
        eh_demo_frame(d, t_ms, buttons, fb);
        return;
    }
    eh_demo_prepare(d, t_ms, buttons, fb);
    const int passes = eh_demo_passes(d);
    for (int pass = 0; pass < passes; pass++) {
        pthread_mutex_lock(&p->lock);
        p->demo = d;
        p->fb = fb;
        p->pass = pass;
        const unsigned gen = ++p->generation;
        pthread_cond_signal(&p->wake);
        pthread_mutex_unlock(&p->lock);

        run_parts(d, fb, pass, 0, p->workers);

        pthread_mutex_lock(&p->lock);
        while (p->finished != gen)
            pthread_cond_wait(&p->done, &p->lock);
        pthread_mutex_unlock(&p->lock);
    }
    eh_demo_finish(d, fb);
}
