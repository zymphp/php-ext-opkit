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
   | OpKit is heavily based on Zend OPcache                               |
   +----------------------------------------------------------------------+
   | This product includes PHP software, freely available from            |
   | <http://www.php.net/software/>                                       |
   +----------------------------------------------------------------------+
*/


#ifndef OPKIT_WRAPPER_H
#define OPKIT_WRAPPER_H

#include "php.h"
#include "Optimizer/zend_optimizer.h"
#include <sys/param.h>

#ifndef MAXPATHLEN
# define MAXPATHLEN 4096
#endif

#ifndef CONST_OWNED_BY_PERSISTENT_SCRIPT
# define CONST_OWNED_BY_PERSISTENT_SCRIPT (1<<4)
#endif

/* --- PHP 8.5 Compiler Flag Compatibility --- */
#if PHP_VERSION_ID >= 80500
#ifndef ZEND_ACC_PTR_OPS
# define ZEND_ACC_PTR_OPS (1 << 28)
#endif
#ifndef ZEND_ACC_NODISCARD
# define ZEND_ACC_NODISCARD (1 << 29)
#endif
#endif

/* --- PHP 8.4 Property Hooks Compatibility --- */
#if PHP_VERSION_ID >= 80400
#ifndef ZEND_PROPERTY_HOOK_COUNT
# define ZEND_PROPERTY_HOOK_COUNT 2
#endif
#ifndef ZEND_PROPERTY_HOOK_STRUCT_SIZE
# define ZEND_PROPERTY_HOOK_STRUCT_SIZE (sizeof(zend_function*) * ZEND_PROPERTY_HOOK_COUNT)
#endif
#endif

/* --- PHP 8.4 doc_comment Compatibility --- */
/* In PHP 8.4, doc_comment was removed from several structures */
#if PHP_VERSION_ID >= 80400
# define OPKIT_HAS_DOC_COMMENT(ptr) 0
# define OPKIT_DOC_COMMENT(ptr) NULL
#else
# define OPKIT_HAS_DOC_COMMENT(ptr) ((ptr) != NULL)
# define OPKIT_DOC_COMMENT(ptr) (ptr)
#endif


/* --- From zend_shared_alloc.h --- */
#ifndef ZEND_SHARED_ALLOC_H
#define ZEND_SHARED_ALLOC_H

#include "zend.h"
/* #include "ZendAccelerator.h" */

#if defined(__APPLE__) && defined(__MACH__) /* darwin */
# ifdef HAVE_SHM_MMAP_POSIX
#  define USE_SHM_OPEN  1
# endif
# ifdef HAVE_SHM_MMAP_ANON
#  define USE_MMAP      1
# endif
#elif defined(__linux__) || defined(_AIX)
# ifdef HAVE_SHM_MMAP_POSIX
#  define USE_SHM_OPEN  1
# endif
# ifdef HAVE_SHM_IPC
#  define USE_SHM       1
# endif
# ifdef HAVE_SHM_MMAP_ANON
#  define USE_MMAP      1
# endif
#elif defined(__sparc) || defined(__sun)
# ifdef HAVE_SHM_MMAP_POSIX
#  define USE_SHM_OPEN  1
# endif
# ifdef HAVE_SHM_IPC
#  define USE_SHM       1
# endif
# if defined(__i386)
#  ifdef HAVE_SHM_MMAP_ANON
#   define USE_MMAP     1
#  endif
# endif
#else
# ifdef HAVE_SHM_MMAP_POSIX
#  define USE_SHM_OPEN  1
# endif
# ifdef HAVE_SHM_MMAP_ANON
#  define USE_MMAP      1
# endif
# ifdef HAVE_SHM_IPC
#  define USE_SHM       1
# endif
#endif

#define ALLOC_FAILURE           0
#define ALLOC_SUCCESS           1
#define FAILED_REATTACHED       2
#define SUCCESSFULLY_REATTACHED 4
#define ALLOC_FAIL_MAPPING      8
#define ALLOC_FALLBACK          9
#if PHP_VERSION_ID >= 80500
#define NO_SHM_BACKEND          10
#endif

typedef struct _zend_shared_segment {
    size_t  size;
    size_t  end;
    size_t  pos;  /* position for simple stack allocator */
    void   *p;
} zend_shared_segment;

typedef int (*create_segments_t)(size_t requested_size, zend_shared_segment ***shared_segments, int *shared_segment_count, const char **error_in);
typedef int (*detach_segment_t)(zend_shared_segment *shared_segment);

typedef struct {
	create_segments_t create_segments;
	detach_segment_t detach_segment;
	size_t (*segment_type_size)(void);
} zend_shared_memory_handlers;

typedef struct _handler_entry {
	const char                  *name;
	const zend_shared_memory_handlers *handler;
} zend_shared_memory_handler_entry;

typedef struct _zend_shared_memory_state {
	size_t *positions;  /* current positions for each segment */
	size_t shared_free; /* amount of free shared memory */
} zend_shared_memory_state;

typedef struct _zend_smm_shared_globals {
    /* Shared Memory Manager */
    zend_shared_segment      **shared_segments;
    /* Number of allocated shared segments */
    int                        shared_segments_count;
    /* Amount of free shared memory */
    size_t                     shared_free;
    /* Amount of shared memory allocated by garbage */
    size_t                     wasted_shared_memory;
    /* No more shared memory flag */
    bool                  memory_exhausted;
    /* Saved Shared Allocator State */
    zend_shared_memory_state   shared_memory_state;
	/* Pointer to the application's shared data structures */
	void                      *app_shared_globals;
	/* Reserved shared memory */
	void                      *reserved;
	size_t                     reserved_size;
} zend_smm_shared_globals;

extern zend_smm_shared_globals *smm_shared_globals;

#define ZSMMG(element)		(smm_shared_globals->element)

#define SHARED_ALLOC_REATTACHED		(SUCCESS+1)

BEGIN_EXTERN_C()

int zend_shared_alloc_startup(size_t requested_size, size_t reserved_size);
void zend_shared_alloc_shutdown(void);

/* allocate shared memory block */
void *zend_shared_alloc(size_t size);

/**
 * Wrapper for zend_shared_alloc() which aligns at 64-byte boundary if
 * AVX or SSE2 are used.
 */
static inline void *zend_shared_alloc_aligned(size_t size) {
#if defined(__AVX__) || defined(__SSE2__)
	/* Align to 64-byte boundary */
	void *p = zend_shared_alloc(size + 64);
	return (void *)(((uintptr_t)p + 63L) & ~63L);
#else
	return zend_shared_alloc(size);
#endif
}

/* copy into shared memory */
void *zend_shared_memdup_get_put_free(void *source, size_t size);
void *zend_shared_memdup_put_free(void *source, size_t size);
void *zend_shared_memdup_free(void *source, size_t size);
void *zend_shared_memdup_get_put(void *source, size_t size);
void *zend_shared_memdup_put(void *source, size_t size);
void *zend_shared_memdup(void *source, size_t size);

int  zend_shared_memdup_size(void *p, size_t size);

bool zend_accel_in_shm(void *ptr);

typedef union _align_test {
	void   *ptr;
	double  dbl;
	zend_long  lng;
} align_test;

#if ZEND_GCC_VERSION >= 2000
# define PLATFORM_ALIGNMENT (__alignof__(align_test) < 8 ? 8 : __alignof__(align_test))
#else
# define PLATFORM_ALIGNMENT (sizeof(align_test))
#endif

#define ZEND_ALIGNED_SIZE(size) \
	ZEND_MM_ALIGNED_SIZE_EX(size, PLATFORM_ALIGNMENT)

/* exclusive locking */
void zend_shared_alloc_lock(void);
void zend_shared_alloc_unlock(void); /* returns the allocated size during lock..unlock */
void zend_shared_alloc_safe_unlock(void);

/* old/new mapping functions */
void zend_shared_alloc_init_xlat_table(void);
void zend_shared_alloc_destroy_xlat_table(void);
void zend_shared_alloc_clear_xlat_table(void);
uint32_t zend_shared_alloc_checkpoint_xlat_table(void);
void zend_shared_alloc_restore_xlat_table(uint32_t checkpoint);
void zend_shared_alloc_register_xlat_entry(const void *key, const void *value);
void *zend_shared_alloc_get_xlat_entry(const void *key);

size_t zend_shared_alloc_get_free_memory(void);
void zend_shared_alloc_save_state(void);
void zend_shared_alloc_restore_state(void);
const char *zend_accel_get_shared_model(void);

/**
 * Memory write protection
 *
 * @param protected true to protect shared memory (read-only), false
 * to unprotect shared memory (writable)
 */
void zend_accel_shared_protect(bool protected);

#ifdef USE_MMAP
extern const zend_shared_memory_handlers zend_alloc_mmap_handlers;
#endif

#ifdef USE_SHM
extern const zend_shared_memory_handlers zend_alloc_shm_handlers;
#endif

#ifdef USE_SHM_OPEN
extern const zend_shared_memory_handlers zend_alloc_posix_handlers;
#endif

#ifdef ZEND_WIN32
extern const zend_shared_memory_handlers zend_alloc_win32_handlers;
void zend_shared_alloc_create_lock(void);
void zend_shared_alloc_lock_win32(void);
void zend_shared_alloc_unlock_win32(void);
#endif

END_EXTERN_C()

#endif /* ZEND_SHARED_ALLOC_H */

/* --- From zend_accelerator_hash.h --- */
#ifndef ZEND_ACCELERATOR_HASH_H
#define ZEND_ACCELERATOR_HASH_H

#include "zend.h"

/*
	zend_accel_hash - is a hash table allocated in shared memory and
	distributed across simultaneously running processes. The hash tables have
	fixed sizen selected during construction by zend_accel_hash_init(). All the
	hash entries are preallocated in the 'hash_entries' array. 'num_entries' is
	initialized by zero and grows when new data is added.
	zend_accel_hash_update() just takes the next entry from 'hash_entries'
	array and puts it into appropriate place of 'hash_table'.
	Hash collisions are resolved by separate chaining with linked lists,
	however, entries are still taken from the same 'hash_entries' array.
	'key' and 'data' passed to zend_accel_hash_update() must be already
	allocated in shared memory. Few keys may be resolved to the same data.
	using 'indirect' entries, that point to other entries ('data' is actually
	a pointer to another zend_accel_hash_entry).
	zend_accel_hash_update() requires exclusive lock, however,
	zend_accel_hash_find() does not.
*/

typedef struct _zend_accel_hash_entry zend_accel_hash_entry;

struct _zend_accel_hash_entry {
	zend_ulong             hash_value;
	zend_string           *key;
	zend_accel_hash_entry *next;
	void                  *data;
	bool                   indirect;
};

typedef struct _zend_accel_hash {
	zend_accel_hash_entry **hash_table;
	zend_accel_hash_entry  *hash_entries;
	uint32_t               num_entries;
	uint32_t               max_num_entries;
	uint32_t               num_direct_entries;
} zend_accel_hash;

BEGIN_EXTERN_C()

void zend_accel_hash_init(zend_accel_hash *accel_hash, uint32_t hash_size);
void zend_accel_hash_clean(zend_accel_hash *accel_hash);

zend_accel_hash_entry* zend_accel_hash_update(
		zend_accel_hash        *accel_hash,
		zend_string            *key,
		bool                   indirect,
		void                   *data);

void* zend_accel_hash_find(
		zend_accel_hash        *accel_hash,
		zend_string            *key);

zend_accel_hash_entry* zend_accel_hash_find_entry(
		zend_accel_hash        *accel_hash,
		zend_string            *key);

int zend_accel_hash_unlink(
		zend_accel_hash        *accel_hash,
		zend_string            *key);

static inline bool zend_accel_hash_is_full(zend_accel_hash *accel_hash)
{
	if (accel_hash->num_entries == accel_hash->max_num_entries) {
		return 1;
	} else {
		return 0;
	}
}

END_EXTERN_C()

#endif /* ZEND_ACCELERATOR_HASH_H */

/* --- From ZendAccelerator.h --- */
#ifndef ZEND_ACCELERATOR_H
#define ZEND_ACCELERATOR_H

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif

#define ACCELERATOR_PRODUCT_NAME	"Zend OPcache"
/* 2 - added Profiler support, on 20010712 */
/* 3 - added support for Optimizer's encoded-only-files mode */
/* 4 - works with the new Optimizer, that supports the file format with licenses */
/* 5 - API 4 didn't really work with the license-enabled file format.  v5 does. */
/* 6 - Monitor was removed from ZendPlatform.so, to a module of its own */
/* 7 - Optimizer was embedded into Accelerator */
/* 8 - Standalone Open Source Zend OPcache */
#define ACCELERATOR_API_NO 8

#if ZEND_WIN32
# include "zend_config.w32.h"
#else
#include "zend_config.h"
# include <sys/time.h>
# include <sys/resource.h>
#endif

#if HAVE_UNISTD_H
# include "unistd.h"
#endif

#include "zend_extensions.h"
#include "zend_compile.h"

#include "Optimizer/zend_optimizer.h"
/* #include "zend_accelerator_hash.h" */
/* #include "zend_accelerator_debug.h" */

#ifndef PHPAPI
# ifdef ZEND_WIN32
#  define PHPAPI __declspec(dllimport)
# else
#  define PHPAPI
# endif
#endif

#ifndef ZEND_EXT_API
# ifdef ZEND_WIN32
#  define ZEND_EXT_API __declspec(dllexport)
# elif defined(__GNUC__) && __GNUC__ >= 4
#  define ZEND_EXT_API __attribute__ ((visibility("default")))
# else
#  define ZEND_EXT_API
# endif
#endif

#ifdef ZEND_WIN32
# ifndef MAXPATHLEN
#  include "win32/ioutil.h"
#  define MAXPATHLEN PHP_WIN32_IOUTIL_MAXPATHLEN
# endif
# include <direct.h>
#else
# ifndef MAXPATHLEN
#  define MAXPATHLEN     4096
# endif
# include <sys/param.h>
#endif

/*** file locking ***/
#ifndef ZEND_WIN32
extern int lock_file;
#endif

#if defined(ZEND_WIN32)
# define ENABLE_FILE_CACHE_FALLBACK 1
#else
# define ENABLE_FILE_CACHE_FALLBACK 0
#endif

#if ZEND_WIN32
typedef unsigned __int64 accel_time_t;
#else
typedef time_t accel_time_t;
#endif

typedef enum _zend_accel_restart_reason {
	ACCEL_RESTART_OOM,    /* restart because of out of memory */
	ACCEL_RESTART_HASH,   /* restart because of hash overflow */
	ACCEL_RESTART_USER    /* restart scheduled by opcache_reset() */
} zend_accel_restart_reason;

typedef struct _zend_early_binding {
	zend_string *lcname;
	zend_string *rtd_key;
	zend_string *lc_parent_name;
	uint32_t cache_slot;
} zend_early_binding;

typedef struct _zend_persistent_script {
	zend_script    script;
	zend_long      compiler_halt_offset;   /* position of __HALT_COMPILER or -1 */
	int            ping_auto_globals_mask; /* which autoglobals are used by the script */
	accel_time_t   timestamp;              /* the script modification time */
	bool      corrupted;
	bool      is_phar;
	bool      empty;
	uint32_t       num_warnings;
	uint32_t       num_early_bindings;
	zend_error_info **warnings;
	zend_early_binding *early_bindings;

	void          *mem;                    /* shared memory area used by script structures */
	size_t         size;                   /* size of used shared memory */

	struct zend_persistent_script_dynamic_members {
		time_t       last_used;
		zend_ulong   hits;
		unsigned int memory_consumption;
		time_t       revalidate;
	} dynamic_members;
} zend_persistent_script;

typedef struct _zend_accel_directives {
	zend_long           memory_consumption;
	zend_long           max_accelerated_files;
	double         max_wasted_percentage;
	char          *user_blacklist_filename;
	zend_long           force_restart_timeout;
	bool      use_cwd;
	bool      ignore_dups;
	bool      validate_timestamps;
	bool      revalidate_path;
	bool      save_comments;
	bool      record_warnings;
	bool      protect_memory;
	bool      file_override_enabled;
	bool      enable_cli;
	bool      validate_permission;
#ifndef ZEND_WIN32
	bool      validate_root;
#endif
	zend_ulong     revalidate_freq;
	zend_ulong     file_update_protection;
	char          *error_log;
#ifdef ZEND_WIN32
	char          *mmap_base;
#endif
	char          *memory_model;
	zend_long           log_verbosity_level;

	zend_long           optimization_level;
	zend_long           opt_debug_level;
	zend_long           max_file_size;
	zend_long           interned_strings_buffer;
	char          *restrict_api;
#ifndef ZEND_WIN32
	char          *lockfile_path;
#endif
	char          *file_cache;
#if PHP_VERSION_ID >= 80500
	bool      file_cache_read_only;
#endif
	bool      file_cache_only;
	bool      file_cache_consistency_checks;
#if ENABLE_FILE_CACHE_FALLBACK
	bool      file_cache_fallback;
#endif
#ifdef HAVE_HUGE_CODE_PAGES
	bool      huge_code_pages;
#endif
	char *preload;
#ifndef ZEND_WIN32
	char *preload_user;
#endif
#ifdef ZEND_WIN32
	char *cache_id;
#endif
} zend_accel_directives;

typedef struct _zend_accel_globals {
	bool               counted;   /* the process uses shared memory */
	bool               enabled;
	bool               locked;    /* thread obtained exclusive lock */
	bool               accelerator_enabled; /* accelerator enabled for current request */
	bool               pcre_reseted;
	zend_accel_directives   accel_directives;
	zend_string            *cwd;                  /* current working directory or NULL */
	zend_string            *include_path;         /* current value of "include_path" directive */
	char                    include_path_key[32]; /* key of current "include_path" */
	char                    cwd_key[32];          /* key of current working directory */
	int                     include_path_key_len;
	bool                    include_path_check;
	int                     cwd_key_len;
	bool                    cwd_check;
	int                     auto_globals_mask;
	time_t                  request_time;
	time_t                  last_restart_time; /* used to synchronize SHM and in-process caches */
	HashTable               xlat_table;
#ifndef ZEND_WIN32
	zend_ulong              root_hash;
#endif
#if PHP_VERSION_ID >= 80500
	void                   *preloaded_internal_run_time_cache;
	size_t                  preloaded_internal_run_time_cache_size;
	bool                    preloading;
#endif
	/* preallocated shared-memory block to save current script */
	void                   *mem;
	zend_persistent_script *current_persistent_script;
	/* cache to save hash lookup on the same INCLUDE opcode */
	const zend_op          *cache_opline;
	zend_persistent_script *cache_persistent_script;
#if PHP_VERSION_ID >= 80500
	zend_string            *key;
#else
	/* preallocated buffer for keys */
	zend_string             key;
	char                    _key[MAXPATHLEN * 8];
#endif
} zend_accel_globals;

typedef struct _zend_string_table {
	uint32_t     nTableMask;
	uint32_t     nNumOfElements;
	zend_string *start;
	zend_string *top;
	zend_string *end;
	zend_string *saved_top;
} zend_string_table;

#if PHP_VERSION_ID >= 80500
typedef uint32_t zend_string_table_pos_t;
# define ZEND_STRING_TABLE_POS_MAX UINT32_MAX
# define ZEND_STRING_TABLE_POS_ALIGNMENT 8
#endif

typedef struct _zend_accel_shared_globals {
	/* Cache Data Structures */
	zend_ulong   hits;
	zend_ulong   misses;
	zend_ulong   blacklist_misses;
	zend_ulong   oom_restarts;     /* number of restarts because of out of memory */
	zend_ulong   hash_restarts;    /* number of restarts because of hash overflow */
	zend_ulong   manual_restarts;  /* number of restarts scheduled by opcache_reset() */
	zend_accel_hash hash;             /* hash table for cached scripts */

	size_t map_ptr_last;
#if PHP_VERSION_ID >= 80500
	size_t map_ptr_static_last;
#endif

	/* Directives & Maintenance */
	time_t          start_time;
	time_t          last_restart_time;
	time_t          force_restart_time;
	bool       accelerator_enabled;
	bool       restart_pending;
	zend_accel_restart_reason restart_reason;
	bool       cache_status_before_restart;
#ifdef ZEND_WIN32
	LONGLONG   mem_usage;
	LONGLONG   restart_in;
#endif
	bool       restart_in_progress;
	bool       jit_counters_stopped;

	/* Preloading */
	zend_persistent_script *preload_script;
	zend_persistent_script **saved_scripts;

	/* uninitialized HashTable Support */
	uint32_t uninitialized_bucket[-HT_MIN_MASK];

	/* Tracing JIT */
	void *jit_traces;
	const void **jit_exit_groups;

	/* Interned Strings Support (must be the last element) */
	zend_string_table interned_strings;
} zend_accel_shared_globals;

#ifdef ZEND_WIN32
extern char accel_uname_id[32];
#endif
extern bool accel_startup_ok;
extern bool file_cache_only;
#if ENABLE_FILE_CACHE_FALLBACK
extern bool fallback_process;
#endif

extern zend_accel_shared_globals *accel_shared_globals;
#define ZCSG(element)   (accel_shared_globals->element)

#ifdef ZTS
# define ZCG(v)	ZEND_TSRMG(accel_globals_id, zend_accel_globals *, v)
extern int accel_globals_id;
# ifdef COMPILE_DL_OPCACHE
ZEND_TSRMLS_CACHE_EXTERN()
# endif
#else
# define ZCG(v) (accel_globals.v)
extern zend_accel_globals accel_globals;
#endif

extern const char *zps_api_failure_reason;

BEGIN_EXTERN_C()

void accel_shutdown(void);
zend_result  accel_activate(INIT_FUNC_ARGS);
zend_result accel_post_deactivate(void);
void zend_accel_schedule_restart(zend_accel_restart_reason reason);
void zend_accel_schedule_restart_if_necessary(zend_accel_restart_reason reason);
accel_time_t zend_get_file_handle_timestamp(zend_file_handle *file_handle, size_t *size);
zend_result validate_timestamp_and_record(zend_persistent_script *persistent_script, zend_file_handle *file_handle);
zend_result validate_timestamp_and_record_ex(zend_persistent_script *persistent_script, zend_file_handle *file_handle);
zend_result zend_accel_invalidate(zend_string *filename, bool force);
zend_result accelerator_shm_read_lock(void);
void accelerator_shm_read_unlock(void);

zend_string *accel_make_persistent_key(zend_string *path);
zend_op_array *persistent_compile_file(zend_file_handle *file_handle, int type);

#define IS_ACCEL_INTERNED(str) \
	((char*)(str) >= (char*)ZCSG(interned_strings).start && (char*)(str) < (char*)ZCSG(interned_strings).top)

zend_string* ZEND_FASTCALL accel_new_interned_string(zend_string *str);

uint32_t zend_accel_get_class_name_map_ptr(zend_string *type_name);

END_EXTERN_C()

/* memory write protection */
#define SHM_PROTECT() \
	do { \
		if (ZCG(accel_directives).protect_memory) { \
			zend_accel_shared_protect(true); \
		} \
	} while (0)

#define SHM_UNPROTECT() \
	do { \
		if (ZCG(accel_directives).protect_memory) { \
			zend_accel_shared_protect(false); \
		} \
	} while (0)

#endif /* ZEND_ACCELERATOR_H */

/* --- From zend_accelerator_debug.h --- */
#ifndef ZEND_ACCELERATOR_DEBUG_H
#define ZEND_ACCELERATOR_DEBUG_H

#define ACCEL_LOG_FATAL					0
#define ACCEL_LOG_ERROR					1
#define ACCEL_LOG_WARNING				2
#define ACCEL_LOG_INFO					3
#define ACCEL_LOG_DEBUG					4

BEGIN_EXTERN_C()

void zend_accel_error(int type, const char *format, ...) ZEND_ATTRIBUTE_FORMAT(printf, 2, 3);
ZEND_NORETURN void zend_accel_error_noreturn(int type, const char *format, ...) ZEND_ATTRIBUTE_FORMAT(printf, 2, 3);

END_EXTERN_C()

#endif /* _ZEND_ACCELERATOR_DEBUG_H */

/* --- OpKit Wrapper Logic --- */

#undef ZCG
#if PHP_VERSION_ID >= 80500 && defined(ZTS)
# define ZCG(v) ZEND_TSRMG_FAST(accel_globals_offset, zend_accel_globals *, v)
extern size_t accel_globals_offset;
#elif defined(ZTS)
# define ZCG(v) ZEND_TSRMG(accel_globals_id, zend_accel_globals *, v)
#else
# define ZCG(v) (accel_globals.v)
#endif

/* Interned Strings Support (Shadow Partitioning) */
extern zend_string_table opkit_interned_strings;

#undef ZCSG
#define ZCSG(element) opkit_interned_strings

#undef IS_ACCEL_INTERNED
#define IS_ACCEL_INTERNED(str) 	((char*)(str) >= (char*)opkit_interned_strings.start && (char*)(str) < (char*)opkit_interned_strings.top)

#ifndef UNINITIALIZED_BUCKET
#define UNINITIALIZED_BUCKET
static const uint32_t uninitialized_bucket[-HT_MIN_MASK] =
	{HT_INVALID_IDX, HT_INVALID_IDX};
#endif

typedef struct _zend_file_cache_metainfo {
	char         magic[8];
	char         system_id[32];
	size_t       mem_size;
	size_t       str_size;
	size_t       script_offset;
	time_t timestamp;
	uint32_t     checksum;
	size_t       metadata_size;
	size_t       code_size;
	size_t       data_size;
	size_t       misc_size;
} zend_file_cache_metainfo;

static zend_always_inline zend_ulong zend_rotr3(zend_ulong key)
{
	return (key >> 3) | (key << ((sizeof(key) * 8) - 3));
}

static zend_always_inline int _opkit_shared_memdup_size(void *source, size_t size)
{
	void *old_p;
	zend_ulong key = (zend_ulong)source;

	if ((old_p = zend_hash_index_find_ptr(&ZCG(xlat_table), key)) != NULL) {
		/* we already duplicated this pointer */
		return 0;
	}
	zend_hash_index_add_new_ptr(&ZCG(xlat_table), key, source);
	return (int)ZEND_ALIGNED_SIZE(size);
}
#undef zend_shared_memdup_size
#define zend_shared_memdup_size _opkit_shared_memdup_size

static zend_always_inline void _opkit_shared_alloc_init_xlat_table(void)
{
	/* Prepare translation table */
	zend_hash_init(&ZCG(xlat_table), 128, NULL, NULL, 0);
}
#undef zend_shared_alloc_init_xlat_table
#define zend_shared_alloc_init_xlat_table _opkit_shared_alloc_init_xlat_table

static zend_always_inline void _opkit_shared_alloc_destroy_xlat_table(void)
{
	/* Destroy translation table */
	zend_hash_destroy(&ZCG(xlat_table));
}
#undef zend_shared_alloc_destroy_xlat_table
#define zend_shared_alloc_destroy_xlat_table _opkit_shared_alloc_destroy_xlat_table

static zend_always_inline void _opkit_shared_alloc_clear_xlat_table(void)
{
	zend_hash_clean(&ZCG(xlat_table));
}
#undef zend_shared_alloc_clear_xlat_table
#define zend_shared_alloc_clear_xlat_table _opkit_shared_alloc_clear_xlat_table

static zend_always_inline void *_opkit_shared_alloc_get_xlat_entry(const void *key)
{
	return zend_hash_index_find_ptr(&ZCG(xlat_table), (uintptr_t)key);
}
#undef zend_shared_alloc_get_xlat_entry
#define zend_shared_alloc_get_xlat_entry _opkit_shared_alloc_get_xlat_entry

static zend_always_inline void _opkit_shared_alloc_register_xlat_entry(const void *key, const void *value)
{
	zend_hash_index_update_ptr(&ZCG(xlat_table), (uintptr_t)key, (void*)value);
}
#undef zend_shared_alloc_register_xlat_entry
#define zend_shared_alloc_register_xlat_entry _opkit_shared_alloc_register_xlat_entry

extern size_t opkit_metadata_size;
extern size_t opkit_code_size;
extern size_t opkit_data_size;
extern size_t opkit_misc_size;

#undef ADD_SIZE_MD
#define ADD_SIZE_MD(s) opkit_metadata_size += ZEND_ALIGNED_SIZE(s)
#undef ADD_SIZE_CD
#define ADD_SIZE_CD(s) opkit_code_size += ZEND_ALIGNED_SIZE(s)
#undef ADD_SIZE_DT
#define ADD_SIZE_DT(s) opkit_data_size += ZEND_ALIGNED_SIZE(s)
#undef ADD_SIZE_MS
#define ADD_SIZE_MS(s) opkit_misc_size += ZEND_ALIGNED_SIZE(s)

static zend_always_inline void *_opkit_shared_memdup_put(void *source, size_t size)
{
	void *old_p;
	void *new_p;

	if ((old_p = zend_hash_index_find_ptr(&ZCG(xlat_table), (uintptr_t)source)) != NULL) {
		/* we already duplicated this pointer */
		return old_p;
	}
	new_p = ZCG(mem);
	ZCG(mem) = (void*)((char*)ZCG(mem) + ZEND_ALIGNED_SIZE(size));
	memcpy(new_p, source, size);
	zend_hash_index_add_new_ptr(&ZCG(xlat_table), (uintptr_t)source, new_p);
	return new_p;
}
#undef zend_shared_memdup_put
#define zend_shared_memdup_put _opkit_shared_memdup_put

#define _opkit_shared_memdup_put_dt _opkit_shared_memdup_put
#define _opkit_shared_memdup_put_md _opkit_shared_memdup_put
#define _opkit_shared_memdup_put_cd _opkit_shared_memdup_put
#define _opkit_shared_memdup_put_ms _opkit_shared_memdup_put

static zend_always_inline void *_opkit_shared_memdup_put_free(void *source, size_t size)
{
	void *old_p;
	if ((old_p = zend_hash_index_find_ptr(&ZCG(xlat_table), (uintptr_t)source)) != NULL) {
		return old_p;
	}
	void *new_p = _opkit_shared_memdup_put(source, size);
	efree(source);
	return new_p;
}

#undef zend_shared_memdup_put_free
#define zend_shared_memdup_put_free(ptr, size) _opkit_shared_memdup_put_free(ptr, size)
#define _opkit_shared_memdup_put_free_dt(ptr, size) _opkit_shared_memdup_put_free(ptr, size)
#define _opkit_shared_memdup_put_free_cd(ptr, size) _opkit_shared_memdup_put_free(ptr, size)
#define _opkit_shared_memdup_put_free_ms(ptr, size) _opkit_shared_memdup_put_free(ptr, size)

static zend_always_inline void *_opkit_shared_memdup_get(void *source, size_t size)
{
	void *new_p = ZCG(mem);
	ZCG(mem) = (void*)((char*)ZCG(mem) + ZEND_ALIGNED_SIZE(size));
	memcpy(new_p, source, size);
	return new_p;
}
#undef zend_shared_memdup_get
#define zend_shared_memdup_get _opkit_shared_memdup_get

#include "opkit_shared_alloc.h"

#undef zend_shared_alloc_unlock
#define zend_shared_alloc_unlock opkit_shared_alloc_unlock

#undef zend_shared_alloc_lock
#define zend_shared_alloc_lock opkit_shared_alloc_lock

#undef zend_shared_alloc
#define zend_shared_alloc opkit_shared_alloc

#undef zend_accel_in_shm
#define zend_accel_in_shm opkit_accel_in_shm

#undef accel_new_interned_string
#define accel_new_interned_string _opkit_accel_new_interned_string
static zend_always_inline zend_string* ZEND_FASTCALL _opkit_accel_new_interned_string(zend_string *str)
{
	return str;
}

typedef struct _opkit_persistent_script {
	zend_persistent_script script;
	HashTable constants_table;
} opkit_persistent_script;

void opkit_free_persistent_script(zend_persistent_script *script);
void opkit_free_zend_constant(zval *zv);

/* Persist functions */
void zend_persist_warnings_calc(uint32_t num_warnings, zend_error_info **warnings);
zend_error_info **zend_persist_warnings(uint32_t num_warnings, zend_error_info **warnings);
uint32_t zend_accel_script_persist_calc(zend_persistent_script *script, int for_shm);
zend_persistent_script *zend_accel_script_persist(zend_persistent_script *script, int for_shm);

#endif //OPKIT_WRAPPER_H
