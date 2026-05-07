/*
   +----------------------------------------------------------------------+
   | OpKit                                                                |
   +----------------------------------------------------------------------+
   | Copyright (c) The PHP Group                                          |
   +----------------------------------------------------------------------+
   | This source file is subject to version 3.01 of the PHP license,      |
   | that is bundled with this package in the file LICENSE, and is        |
   | available through the world-wide-web at the following url:           |
   | https://www.php.net/license/3_01.txt                                 |
   | If you did not receive a copy of the PHP license and are unable to   |
   | obtain it through the world-wide-web, please send a note to          |
   | license@php.net so we can mail you a copy immediately.               |
   +----------------------------------------------------------------------+
   | Author: Eno-CN <Eno_CN@qq.com>                                       |
   +----------------------------------------------------------------------+
   | Shared Memory Allocator for OpKit (based on OPcache mmap)            |
   +----------------------------------------------------------------------+
*/

#include "opkit_shared_alloc.h"

#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#if defined(__linux__) && defined(HAVE_MEMFD_CREATE)
# ifndef _GNU_SOURCE
#  define _GNU_SOURCE
# endif
#endif

static opkit_shared_segment *opkit_shm_segment = NULL;
static int opkit_lock_fd = -1;

#ifndef ZEND_WIN32
static void opkit_shared_alloc_create_lock(void)
{
#if defined(__linux__) && defined(HAVE_MEMFD_CREATE)
    opkit_lock_fd = memfd_create("opkit_lock", MFD_CLOEXEC);
    if (opkit_lock_fd >= 0)
        return;
#endif
#ifdef O_TMPFILE
    opkit_lock_fd = open("/tmp", O_RDWR | O_TMPFILE | O_EXCL | O_CLOEXEC, 0666);
    if (opkit_lock_fd >= 0)
        return;
#endif
    char tmpfile[MAXPATHLEN];
    snprintf(tmpfile, sizeof(tmpfile), "/tmp/opkit.lock.XXXXXX");
    opkit_lock_fd = mkstemp(tmpfile);
    if (opkit_lock_fd >= 0) {
        unlink(tmpfile);
        int val = fcntl(opkit_lock_fd, F_GETFD, 0);
        val |= FD_CLOEXEC;
        fcntl(opkit_lock_fd, F_SETFD, val);
    }
}
#endif

int opkit_shared_alloc_startup(size_t requested_size)
{
    if (requested_size == 0) {
        return SUCCESS;
    }

    void *p = mmap(NULL, requested_size, PROT_READ | PROT_WRITE,
                   MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    if (p == MAP_FAILED) {
        return FAILURE;
    }

    opkit_shm_segment = (opkit_shared_segment *)malloc(sizeof(opkit_shared_segment));
    if (!opkit_shm_segment) {
        munmap(p, requested_size);
        return FAILURE;
    }

    opkit_shm_segment->p = p;
    opkit_shm_segment->size = requested_size;
    opkit_shm_segment->pos = 0;
    opkit_shm_segment->end = requested_size;

#ifndef ZEND_WIN32
    opkit_shared_alloc_create_lock();
#endif

    return SUCCESS;
}

void opkit_shared_alloc_shutdown(void)
{
    if (opkit_shm_segment) {
        if (opkit_shm_segment->p && opkit_shm_segment->p != MAP_FAILED) {
            munmap(opkit_shm_segment->p, opkit_shm_segment->size);
        }
        free(opkit_shm_segment);
        opkit_shm_segment = NULL;
    }

#ifndef ZEND_WIN32
    if (opkit_lock_fd >= 0) {
        close(opkit_lock_fd);
        opkit_lock_fd = -1;
    }
#endif
}

void *opkit_shared_alloc(size_t size)
{
    size_t block_size = ZEND_MM_ALIGNED_SIZE(size);
    if (!opkit_shm_segment) {
        return NULL;
    }
    if (block_size > opkit_shm_segment->end - opkit_shm_segment->pos) {
        return NULL;
    }
    void *retval = (char *)opkit_shm_segment->p + opkit_shm_segment->pos;
    opkit_shm_segment->pos += block_size;
    return retval;
}

size_t opkit_shared_alloc_get_free_memory(void)
{
    if (!opkit_shm_segment) {
        return 0;
    }
    return opkit_shm_segment->end - opkit_shm_segment->pos;
}

bool opkit_accel_in_shm(void *ptr)
{
    if (!opkit_shm_segment || !opkit_shm_segment->p) {
        return 0;
    }
    return (char*)ptr >= (char*)opkit_shm_segment->p &&
           (char*)ptr < (char*)opkit_shm_segment->p + opkit_shm_segment->size;
}

bool opkit_shm_reset(void)
{
    if (!opkit_shm_segment) {
        return false;
    }
    opkit_shared_alloc_lock();
    opkit_shm_segment->pos = 0;
    opkit_shared_alloc_unlock();
    return true;
}

void opkit_shared_alloc_lock(void)
{
#ifndef ZEND_WIN32
    if (opkit_lock_fd < 0) return;
    struct flock mem_write_lock;
    mem_write_lock.l_type = F_WRLCK;
    mem_write_lock.l_whence = SEEK_SET;
    mem_write_lock.l_start = 0;
    mem_write_lock.l_len = 1;
    while (1) {
        if (fcntl(opkit_lock_fd, F_SETLKW, &mem_write_lock) == -1) {
            if (errno == EINTR) continue;
            break;
        }
        break;
    }
#endif
}

void opkit_shared_alloc_unlock(void)
{
#ifndef ZEND_WIN32
    if (opkit_lock_fd < 0) return;
    struct flock mem_write_unlock;
    mem_write_unlock.l_type = F_UNLCK;
    mem_write_unlock.l_whence = SEEK_SET;
    mem_write_unlock.l_start = 0;
    mem_write_unlock.l_len = 1;
    fcntl(opkit_lock_fd, F_SETLK, &mem_write_unlock);
#endif
}

void opkit_shared_alloc_safe_unlock(void)
{
    opkit_shared_alloc_unlock();
}
