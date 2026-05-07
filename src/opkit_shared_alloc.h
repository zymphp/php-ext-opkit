#ifndef OPKIT_SHARED_ALLOC_H
#define OPKIT_SHARED_ALLOC_H

#include "php.h"

BEGIN_EXTERN_C()

typedef struct _opkit_shared_segment {
    void   *p;
    size_t  size;
    size_t  pos;
    size_t  end;
} opkit_shared_segment;

int opkit_shared_alloc_startup(size_t requested_size);
void opkit_shared_alloc_shutdown(void);
void *opkit_shared_alloc(size_t size);
size_t opkit_shared_alloc_get_free_memory(void);
bool opkit_accel_in_shm(void *ptr);
bool opkit_shm_reset(void);

void opkit_shared_alloc_lock(void);
void opkit_shared_alloc_unlock(void);
void opkit_shared_alloc_safe_unlock(void);

END_EXTERN_C()

#endif
