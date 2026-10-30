/* Host stand-in for the RP2350's second core: runs a demo frame plan on
 * one or two threads. The calling thread is worker 0 and takes the even
 * parts; a helper thread is worker 1 and takes the odd parts. Each pass ends
 * at a barrier, as it would with the inter-core FIFO on a device. Host
 * timings from this pool measure host threads, not RP2350 cores. */
#ifndef EH_HOST_POOL_H
#define EH_HOST_POOL_H

#include <stdint.h>

#include "eh/eh_demo.h"

typedef struct eh_pool eh_pool;

/* workers is 1 or EH_WORKERS; returns NULL on failure. */
eh_pool *eh_pool_create(int workers);
void eh_pool_destroy(eh_pool *pool);
int eh_pool_workers(const eh_pool *pool);

/* Same contract and output as eh_demo_frame(). */
void eh_pool_frame(eh_pool *pool, eh_demo *d, uint32_t t_ms, uint32_t buttons, eh_rgb565 *fb);

#endif
