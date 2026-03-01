/*
 * pool_bench.c — Pool allocator comparison: fixed 128KB vs smart-sized
 *
 * Compares:
 *   OLD: ason_pool_new_sized(128 * 1024)  — always 128KB
 *   NEW: ason_pool_new_sized(input_len)   — smart estimation from input
 *
 * Scenarios:
 *   1) Tiny single struct          (~30 bytes)
 *   2) Small vec × 3               (~60 bytes)
 *   3) Medium vec × 100            (~2 KB)
 *   4) Large vec × 10000           (~200 KB)
 *   5) Binary single struct
 *   6) Binary vec × 100
 *   7) Binary vec × 10000
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <time.h>
#include "ason.h"

/* ===========================================================================
 * Timing
 * =========================================================================== */
#ifdef __APPLE__
#include <mach/mach_time.h>
static double now_ns(void) {
    static mach_timebase_info_data_t info = {0,0};
    if (info.denom == 0) mach_timebase_info(&info);
    return (double)mach_absolute_time() * info.numer / info.denom;
}
#else
static double now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1e9 + ts.tv_nsec;
}
#endif

/* ===========================================================================
 * Struct
 * =========================================================================== */

typedef struct {
    int64_t id;
    ason_string_t name;
    ason_string_t email;
    int64_t age;
    double score;
    bool active;
} PUser;

ASON_FIELDS(PUser, 6,
    ASON_FIELD(PUser, id,     "id",     i64),
    ASON_FIELD(PUser, name,   "name",   str),
    ASON_FIELD(PUser, email,  "email",  str),
    ASON_FIELD(PUser, age,    "age",    i64),
    ASON_FIELD(PUser, score,  "score",  f64),
    ASON_FIELD(PUser, active, "active", bool))

ASON_FIELDS_BIN(PUser, 6)

static void free_puser(PUser* u) {
    ason_string_free(&u->name);
    ason_string_free(&u->email);
}

/* ===========================================================================
 * Data generator
 * =========================================================================== */

static PUser* generate_users(size_t n) {
    const char* names[] = {"Alice","Bob","Carol","David","Eve","Frank","Grace","Hank"};
    PUser* users = (PUser*)calloc(n, sizeof(PUser));
    for (size_t i = 0; i < n; i++) {
        users[i].id = (int64_t)i;
        users[i].name = ason_string_from(names[i % 8]);
        char email[64]; snprintf(email, 64, "%s@example.com", names[i % 8]);
        users[i].email = ason_string_from(email);
        users[i].age = 25 + (int64_t)(i % 40);
        users[i].score = 50.0 + (double)(i % 50) + 0.5;
        users[i].active = (i % 3 != 0);
    }
    return users;
}

/* ===========================================================================
 * Benchmark helpers — manually do what decode_pooled macros do,
 * but allow switching between old (128KB) and new (smart) sizing.
 * =========================================================================== */

#define OLD_POOL_SIZE  (128u * 1024u)

/* Decode single struct with a given pool size override */
static ason_err_t decode_single_with_pool(const char* input, size_t len,
                                           PUser* out, ason_pool_t** pool_out,
                                           size_t pool_size) {
    ason_pool_t* p = ason_pool_new_sized(pool_size);
    if (!p) return ASON_ERR_ALLOC;
    ason__set_pool(p);
    ason_err_t e = ason_decode_PUser(input, len, out);
    ason__set_pool(NULL);
    if (e != ASON_OK) { ason_pool_destroy(p); *pool_out = NULL; return e; }
    *pool_out = p;
    return ASON_OK;
}

/* Decode vec with a given pool size override */
static ason_err_t decode_vec_with_pool(const char* input, size_t len,
                                        PUser** out, size_t* out_count,
                                        ason_pool_t** pool_out,
                                        size_t pool_size) {
    ason_pool_t* p = ason_pool_new_sized(pool_size);
    if (!p) return ASON_ERR_ALLOC;
    ason__set_pool(p);
    ason_err_t e = ason_decode_vec_PUser(input, len, out, out_count);
    ason__set_pool(NULL);
    if (e != ASON_OK) { ason_pool_destroy(p); *pool_out = NULL; return e; }
    *pool_out = p;
    return ASON_OK;
}

/* Decode binary single with a given pool size override */
static ason_err_t decode_bin_single_with_pool(const char* data, size_t len,
                                               PUser* out, ason_pool_t** pool_out,
                                               size_t pool_size) {
    ason_pool_t* p = ason_pool_new_sized(pool_size);
    if (!p) return ASON_ERR_ALLOC;
    ason__set_pool(p);
    ason_err_t e = ason_decode_bin_PUser(data, len, out);
    ason__set_pool(NULL);
    if (e != ASON_OK) { ason_pool_destroy(p); *pool_out = NULL; return e; }
    *pool_out = p;
    return ASON_OK;
}

/* Decode binary vec with a given pool size override */
static ason_err_t decode_bin_vec_with_pool(const char* data, size_t len,
                                            PUser** out, size_t* out_count,
                                            ason_pool_t** pool_out,
                                            size_t pool_size) {
    ason_pool_t* p = ason_pool_new_sized(pool_size);
    if (!p) return ASON_ERR_ALLOC;
    ason__set_pool(p);
    ason_err_t e = ason_decode_bin_vec_PUser(data, len, out, out_count);
    ason__set_pool(NULL);
    if (e != ASON_OK) { ason_pool_destroy(p); *pool_out = NULL; return e; }
    *pool_out = p;
    return ASON_OK;
}

/* ===========================================================================
 * Report: computes smart pool estimate like the macro does
 * =========================================================================== */

static size_t smart_est_single(size_t input_len) {
    return input_len < ASON_POOL_MIN_SIZE ? ASON_POOL_MIN_SIZE : input_len;
}

static size_t smart_est_vec(const char* input, size_t len) {
    size_t tc = ason_count_tuples(input, len);
    size_t est = tc * sizeof(PUser) + len;
    return est < ASON_POOL_MIN_SIZE ? ASON_POOL_MIN_SIZE : est;
}

static size_t smart_est_bin_single(size_t data_len) {
    return data_len < ASON_POOL_MIN_SIZE ? ASON_POOL_MIN_SIZE : data_len;
}

static size_t smart_est_bin_vec(const char* data, size_t len) {
    uint32_t count = 0;
    if (len >= 4) memcpy(&count, data, 4);
    size_t est = (size_t)count * sizeof(PUser) + len;
    return est < ASON_POOL_MIN_SIZE ? ASON_POOL_MIN_SIZE : est;
}

/* ===========================================================================
 * Result printing
 * =========================================================================== */

typedef struct {
    const char* scenario;
    size_t input_bytes;
    size_t old_pool_initial;
    size_t new_pool_initial;
    int    old_blocks;
    int    new_blocks;
    double old_ns_per_op;
    double new_ns_per_op;
    size_t old_total_alloc;
    size_t new_total_alloc;
} PoolBenchResult;

static void print_header(void) {
    printf("%-38s %9s | %10s %6s %7s | %10s %6s %7s | %7s %8s\n",
           "Scenario", "Input",
           "Old Pool", "Blks", "ns/op",
           "New Pool", "Blks", "ns/op",
           "Speedup", "MemSave");
    printf("%-38s %9s | %10s %6s %7s | %10s %6s %7s | %7s %8s\n",
           "--------------------------------------", "---------",
           "----------", "------", "-------",
           "----------", "------", "-------",
           "-------", "--------");
}

static size_t pool_total_alloc(ason_pool_t* p) {
    size_t total = 0;
    for (size_t i = 0; i < p->block_count; i++) total += p->block_sizes[i];
    return total;
}

static void print_result(const PoolBenchResult* r) {
    double speedup = r->old_ns_per_op / r->new_ns_per_op;
    double mem_save = (1.0 - (double)r->new_total_alloc / (double)r->old_total_alloc) * 100.0;
    printf("%-38s %7zuB | %8zuB %4d %7.0fns | %8zuB %4d %7.0fns | %6.2fx %6.0f%%\n",
           r->scenario, r->input_bytes,
           r->old_pool_initial, r->old_blocks, r->old_ns_per_op,
           r->new_pool_initial, r->new_blocks, r->new_ns_per_op,
           speedup, mem_save);
}

/* ===========================================================================
 * Benchmark runners
 * =========================================================================== */

#define WARMUP 100
#define ITERS  10000

static PoolBenchResult bench_text_single(const char* input, size_t len) {
    size_t new_est = smart_est_single(len);
    PoolBenchResult r = {0};
    r.scenario = "Text single struct";
    r.input_bytes = len;
    r.old_pool_initial = OLD_POOL_SIZE;
    r.new_pool_initial = new_est;

    /* Warmup + capture block info */
    for (int i = 0; i < WARMUP; i++) {
        PUser out = {0};
        ason_pool_t* pool = NULL;
        decode_single_with_pool(input, len, &out, &pool, OLD_POOL_SIZE);
        if (i == WARMUP - 1) { r.old_blocks = (int)pool->block_count; r.old_total_alloc = pool_total_alloc(pool); }
        ason_pool_destroy(pool);
    }

    /* Old: timed */
    double t0 = now_ns();
    for (int i = 0; i < ITERS; i++) {
        PUser out = {0};
        ason_pool_t* pool = NULL;
        decode_single_with_pool(input, len, &out, &pool, OLD_POOL_SIZE);
        ason_pool_destroy(pool);
    }
    r.old_ns_per_op = (now_ns() - t0) / ITERS;

    /* New: warmup + capture */
    for (int i = 0; i < WARMUP; i++) {
        PUser out = {0};
        ason_pool_t* pool = NULL;
        decode_single_with_pool(input, len, &out, &pool, new_est);
        if (i == WARMUP - 1) { r.new_blocks = (int)pool->block_count; r.new_total_alloc = pool_total_alloc(pool); }
        ason_pool_destroy(pool);
    }

    /* New: timed */
    t0 = now_ns();
    for (int i = 0; i < ITERS; i++) {
        PUser out = {0};
        ason_pool_t* pool = NULL;
        decode_single_with_pool(input, len, &out, &pool, new_est);
        ason_pool_destroy(pool);
    }
    r.new_ns_per_op = (now_ns() - t0) / ITERS;

    return r;
}

static PoolBenchResult bench_text_vec(const char* label, const char* input, size_t len) {
    size_t new_est = smart_est_vec(input, len);
    PoolBenchResult r = {0};
    r.scenario = label;
    r.input_bytes = len;
    r.old_pool_initial = OLD_POOL_SIZE;
    r.new_pool_initial = new_est;

    for (int i = 0; i < WARMUP; i++) {
        PUser* arr = NULL; size_t n = 0;
        ason_pool_t* pool = NULL;
        decode_vec_with_pool(input, len, &arr, &n, &pool, OLD_POOL_SIZE);
        if (i == WARMUP - 1) { r.old_blocks = (int)pool->block_count; r.old_total_alloc = pool_total_alloc(pool); }
        ason_pool_destroy(pool);
    }

    double t0 = now_ns();
    for (int i = 0; i < ITERS; i++) {
        PUser* arr = NULL; size_t n = 0;
        ason_pool_t* pool = NULL;
        decode_vec_with_pool(input, len, &arr, &n, &pool, OLD_POOL_SIZE);
        ason_pool_destroy(pool);
    }
    r.old_ns_per_op = (now_ns() - t0) / ITERS;

    for (int i = 0; i < WARMUP; i++) {
        PUser* arr = NULL; size_t n = 0;
        ason_pool_t* pool = NULL;
        decode_vec_with_pool(input, len, &arr, &n, &pool, new_est);
        if (i == WARMUP - 1) { r.new_blocks = (int)pool->block_count; r.new_total_alloc = pool_total_alloc(pool); }
        ason_pool_destroy(pool);
    }

    t0 = now_ns();
    for (int i = 0; i < ITERS; i++) {
        PUser* arr = NULL; size_t n = 0;
        ason_pool_t* pool = NULL;
        decode_vec_with_pool(input, len, &arr, &n, &pool, new_est);
        ason_pool_destroy(pool);
    }
    r.new_ns_per_op = (now_ns() - t0) / ITERS;

    return r;
}

static PoolBenchResult bench_bin_single(const char* data, size_t len) {
    size_t new_est = smart_est_bin_single(len);
    PoolBenchResult r = {0};
    r.scenario = "Binary single struct";
    r.input_bytes = len;
    r.old_pool_initial = OLD_POOL_SIZE;
    r.new_pool_initial = new_est;

    for (int i = 0; i < WARMUP; i++) {
        PUser out = {0};
        ason_pool_t* pool = NULL;
        decode_bin_single_with_pool(data, len, &out, &pool, OLD_POOL_SIZE);
        if (i == WARMUP - 1) { r.old_blocks = (int)pool->block_count; r.old_total_alloc = pool_total_alloc(pool); }
        ason_pool_destroy(pool);
    }

    double t0 = now_ns();
    for (int i = 0; i < ITERS; i++) {
        PUser out = {0};
        ason_pool_t* pool = NULL;
        decode_bin_single_with_pool(data, len, &out, &pool, OLD_POOL_SIZE);
        ason_pool_destroy(pool);
    }
    r.old_ns_per_op = (now_ns() - t0) / ITERS;

    for (int i = 0; i < WARMUP; i++) {
        PUser out = {0};
        ason_pool_t* pool = NULL;
        decode_bin_single_with_pool(data, len, &out, &pool, new_est);
        if (i == WARMUP - 1) { r.new_blocks = (int)pool->block_count; r.new_total_alloc = pool_total_alloc(pool); }
        ason_pool_destroy(pool);
    }

    t0 = now_ns();
    for (int i = 0; i < ITERS; i++) {
        PUser out = {0};
        ason_pool_t* pool = NULL;
        decode_bin_single_with_pool(data, len, &out, &pool, new_est);
        ason_pool_destroy(pool);
    }
    r.new_ns_per_op = (now_ns() - t0) / ITERS;

    return r;
}

static PoolBenchResult bench_bin_vec(const char* label, const char* data, size_t len) {
    size_t new_est = smart_est_bin_vec(data, len);
    PoolBenchResult r = {0};
    r.scenario = label;
    r.input_bytes = len;
    r.old_pool_initial = OLD_POOL_SIZE;
    r.new_pool_initial = new_est;

    for (int i = 0; i < WARMUP; i++) {
        PUser* arr = NULL; size_t n = 0;
        ason_pool_t* pool = NULL;
        decode_bin_vec_with_pool(data, len, &arr, &n, &pool, OLD_POOL_SIZE);
        if (i == WARMUP - 1) { r.old_blocks = (int)pool->block_count; r.old_total_alloc = pool_total_alloc(pool); }
        ason_pool_destroy(pool);
    }

    double t0 = now_ns();
    for (int i = 0; i < ITERS; i++) {
        PUser* arr = NULL; size_t n = 0;
        ason_pool_t* pool = NULL;
        decode_bin_vec_with_pool(data, len, &arr, &n, &pool, OLD_POOL_SIZE);
        ason_pool_destroy(pool);
    }
    r.old_ns_per_op = (now_ns() - t0) / ITERS;

    for (int i = 0; i < WARMUP; i++) {
        PUser* arr = NULL; size_t n = 0;
        ason_pool_t* pool = NULL;
        decode_bin_vec_with_pool(data, len, &arr, &n, &pool, new_est);
        if (i == WARMUP - 1) { r.new_blocks = (int)pool->block_count; r.new_total_alloc = pool_total_alloc(pool); }
        ason_pool_destroy(pool);
    }

    t0 = now_ns();
    for (int i = 0; i < ITERS; i++) {
        PUser* arr = NULL; size_t n = 0;
        ason_pool_t* pool = NULL;
        decode_bin_vec_with_pool(data, len, &arr, &n, &pool, new_est);
        ason_pool_destroy(pool);
    }
    r.new_ns_per_op = (now_ns() - t0) / ITERS;

    return r;
}

/* ===========================================================================
 * main
 * =========================================================================== */

int main(void) {
    printf("=== Pool Allocator Benchmark: Fixed 128KB vs Smart-Sized ===\n");
    printf("    Iterations per scenario: %d (warmup: %d)\n\n", ITERS, WARMUP);

    /* ----- Generate test data ----- */
    PUser one_user = {
        .id = 42, .name = ason_string_from("Alice"),
        .email = ason_string_from("alice@example.com"),
        .age = 30, .score = 95.5, .active = true
    };

    PUser* users3   = generate_users(3);
    PUser* users100 = generate_users(100);
    PUser* users10k = generate_users(10000);

    /* Encode text payloads */
    ason_buf_t txt_single = ason_encode_PUser(&one_user);
    ason_buf_t txt_vec3   = ason_encode_vec_PUser(users3, 3);
    ason_buf_t txt_vec100 = ason_encode_vec_PUser(users100, 100);
    ason_buf_t txt_vec10k = ason_encode_vec_PUser(users10k, 10000);

    /* Encode binary payloads */
    ason_buf_t bin_single  = ason_encode_bin_PUser(&one_user);
    ason_buf_t bin_vec100  = ason_encode_bin_vec_PUser(users100, 100);
    ason_buf_t bin_vec10k  = ason_encode_bin_vec_PUser(users10k, 10000);

    printf("Payload sizes:\n");
    printf("  Text single:   %6zu bytes\n", txt_single.len);
    printf("  Text vec x3:   %6zu bytes\n", txt_vec3.len);
    printf("  Text vec x100: %6zu bytes\n", txt_vec100.len);
    printf("  Text vec x10k: %6zu bytes\n", txt_vec10k.len);
    printf("  Bin single:    %6zu bytes\n", bin_single.len);
    printf("  Bin vec x100:  %6zu bytes\n", bin_vec100.len);
    printf("  Bin vec x10k:  %6zu bytes\n\n", bin_vec10k.len);

    /* ----- Run benchmarks ----- */
    print_header();

    PoolBenchResult results[7];
    results[0] = bench_text_single(txt_single.data, txt_single.len);
    results[1] = bench_text_vec("Text vec x3 (tiny)",    txt_vec3.data,   txt_vec3.len);
    results[2] = bench_text_vec("Text vec x100 (medium)", txt_vec100.data, txt_vec100.len);
    results[3] = bench_text_vec("Text vec x10000 (large)", txt_vec10k.data, txt_vec10k.len);
    results[4] = bench_bin_single(bin_single.data, bin_single.len);
    results[5] = bench_bin_vec("Binary vec x100 (medium)", bin_vec100.data, bin_vec100.len);
    results[6] = bench_bin_vec("Binary vec x10000 (large)", bin_vec10k.data, bin_vec10k.len);

    for (int i = 0; i < 7; i++) print_result(&results[i]);

    printf("\n");

    /* Summary */
    printf("Summary:\n");
    for (int i = 0; i < 7; i++) {
        double speedup = results[i].old_ns_per_op / results[i].new_ns_per_op;
        double mem_save = (1.0 - (double)results[i].new_total_alloc / (double)results[i].old_total_alloc) * 100.0;
        printf("  %-38s  speedup: %.2fx  mem saved: %.0f%%  blocks: %d→%d\n",
               results[i].scenario, speedup, mem_save,
               results[i].old_blocks, results[i].new_blocks);
    }

    /* Cleanup */
    ason_buf_free(&txt_single); ason_buf_free(&txt_vec3);
    ason_buf_free(&txt_vec100); ason_buf_free(&txt_vec10k);
    ason_buf_free(&bin_single); ason_buf_free(&bin_vec100);
    ason_buf_free(&bin_vec10k);
    ason_string_free(&one_user.name); ason_string_free(&one_user.email);
    for (size_t i = 0; i < 3; i++) free_puser(&users3[i]);
    for (size_t i = 0; i < 100; i++) free_puser(&users100[i]);
    for (size_t i = 0; i < 10000; i++) free_puser(&users10k[i]);
    free(users3); free(users100); free(users10k);

    return 0;
}
