/*
 * ASON - A Schema-Oriented Notation (C Implementation)
 *
 * High-performance, zero-copy ASON serializer/deserializer for C.
 * Uses SIMD (NEON/SSE2) for accelerated string scanning.
 *
 * Usage pattern (macro-based reflection):
 *
 *   typedef struct { int64_t id; char* name; bool active; } User;
 *   ASON_FIELDS(User, 3,
 *     ASON_FIELD(User, id,     "id",     ASON_I64),
 *     ASON_FIELD(User, name,   "name",   ASON_STR),
 *     ASON_FIELD(User, active, "active", ASON_BOOL))
 *
 *   ason_buf_t buf = ason_encode_User(&user);
 *   User u2 = {0}; ason_decode_User(buf.data, buf.len, &u2);
 */

#ifndef ASON_H
#define ASON_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <float.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * Platform / SIMD detection
 * ============================================================================ */

#if defined(__aarch64__) || defined(_M_ARM64)
  #define ASON_NEON 1
  #include <arm_neon.h>
#elif defined(__SSE2__) || defined(_M_X64) || defined(_M_AMD64)
  #define ASON_SSE2 1
  #include <emmintrin.h>
#endif

/* ============================================================================
 * Compiler hints
 * ============================================================================ */

#if defined(__GNUC__) || defined(__clang__)
  #define ason_likely(x)   __builtin_expect(!!(x), 1)
  #define ason_unlikely(x) __builtin_expect(!!(x), 0)
  #define ason_inline      static inline __attribute__((always_inline))
  #define ason_noinline    __attribute__((noinline))
#elif defined(_MSC_VER)
  #define ason_likely(x)   (x)
  #define ason_unlikely(x) (x)
  #define ason_inline      static __forceinline
  #define ason_noinline    __declspec(noinline)
#else
  #define ason_likely(x)   (x)
  #define ason_unlikely(x) (x)
  #define ason_inline      static inline
  #define ason_noinline
#endif

/* ============================================================================
 * Error codes
 * ============================================================================ */

typedef enum {
    ASON_OK = 0,
    ASON_ERR_ALLOC,
    ASON_ERR_SYNTAX,
    ASON_ERR_UNEXPECTED_CHAR,
    ASON_ERR_INVALID_NUMBER,
    ASON_ERR_BUFFER_OVERFLOW,
    ASON_ERR_SCHEMA_MISMATCH,
    ASON_ERR_MISSING_FIELD,
} ason_err_t;

/* ============================================================================
 * Resizable buffer (serialization output)
 * ============================================================================ */

typedef struct {
    char*  data;
    size_t len;
    size_t cap;
} ason_buf_t;

ason_inline ason_buf_t ason_buf_new(size_t init_cap) {
    ason_buf_t b;
    b.cap = init_cap < 64 ? 64 : init_cap;
    b.data = (char*)malloc(b.cap);
    b.len = 0;
    return b;
}

ason_inline void ason_buf_free(ason_buf_t* b) {
    if (b->data) { free(b->data); b->data = NULL; }
    b->len = b->cap = 0;
}

ason_inline void ason_buf_grow(ason_buf_t* b, size_t need) {
    if (ason_likely(b->len + need <= b->cap)) return;
    size_t nc = b->cap;
    while (nc < b->len + need) nc = nc + (nc >> 1);
    char* tmp = (char*)realloc(b->data, nc);
    if (!tmp) return;  /* OOM: keep old buffer */
    b->data = tmp;
    b->cap = nc;
}

ason_inline void ason_buf_push(ason_buf_t* b, char c) {
    ason_buf_grow(b, 1);
    b->data[b->len++] = c;
}

ason_inline void ason_buf_append(ason_buf_t* b, const char* s, size_t n) {
    ason_buf_grow(b, n);
    memcpy(b->data + b->len, s, n);
    b->len += n;
}

ason_inline void ason_buf_appends(ason_buf_t* b, const char* s) {
    ason_buf_append(b, s, strlen(s));
}

/* ============================================================================
 * Dynamic string (heap-allocated, owning)
 * ============================================================================ */

typedef struct {
    char*  data;
    size_t len;
} ason_string_t;

ason_inline ason_string_t ason_string_new(const char* s, size_t n) {
    ason_string_t r;
    r.data = (char*)malloc(n + 1);
    memcpy(r.data, s, n);
    r.data[n] = '\0';
    r.len = n;
    return r;
}

ason_inline ason_string_t ason_string_from(const char* s) {
    return ason_string_new(s, strlen(s));
}

ason_inline ason_string_t ason_string_from_len(const char* s, size_t n) {
    return ason_string_new(s, n);
}

ason_inline void ason_string_free(ason_string_t* s) {
    if (s->data) { free(s->data); s->data = NULL; }
    s->len = 0;
}

/* ============================================================================
 * Dynamic array (generic via macros)
 * ============================================================================ */

#define ASON_VEC_DEFINE(name, T) \
    typedef struct { T* data; size_t len; size_t cap; } name; \
    ason_inline name name##_new(void) { \
        name v; v.data = NULL; v.len = v.cap = 0; return v; \
    } \
    ason_inline void name##_push(name* v, T val) { \
        if (v->len >= v->cap) { \
            v->cap = v->cap < 4 ? 4 : v->cap + (v->cap >> 1); \
            T* tmp = (T*)realloc(v->data, v->cap * sizeof(T)); \
            if (!tmp) return; \
            v->data = tmp; \
        } \
        v->data[v->len++] = val; \
    } \
    ason_inline void name##_free(name* v) { \
        if (v->data) free(v->data); \
        v->data = NULL; v->len = v->cap = 0; \
    }

/* Pre-defined vec types */
ASON_VEC_DEFINE(ason_vec_i64, int64_t)
ASON_VEC_DEFINE(ason_vec_u64, uint64_t)
ASON_VEC_DEFINE(ason_vec_f64, double)
ASON_VEC_DEFINE(ason_vec_str, ason_string_t)
ASON_VEC_DEFINE(ason_vec_bool, bool)

/* Nested vec */
ASON_VEC_DEFINE(ason_vec_vec_i64, ason_vec_i64)

/* ============================================================================
 * Map (string -> int64_t, string -> string)
 * ============================================================================ */

typedef struct { ason_string_t key; int64_t val; } ason_map_si_entry_t;
ASON_VEC_DEFINE(ason_map_si, ason_map_si_entry_t)

typedef struct { ason_string_t key; ason_string_t val; } ason_map_ss_entry_t;
ASON_VEC_DEFINE(ason_map_ss, ason_map_ss_entry_t)

/* ============================================================================
 * Optional (has_value + value)
 * ============================================================================ */

typedef struct { bool has_value; int64_t value; } ason_opt_i64;
typedef struct { bool has_value; ason_string_t value; } ason_opt_str;
typedef struct { bool has_value; double value; } ason_opt_f64;

/* ============================================================================
 * Field type enum
 * ============================================================================ */

typedef enum {
    ASON_BOOL = 0,
    ASON_I8, ASON_I16, ASON_I32, ASON_I64,
    ASON_U8, ASON_U16, ASON_U32, ASON_U64,
    ASON_F32, ASON_F64,
    ASON_CHAR,
    ASON_STR,
    ASON_OPT_I64, ASON_OPT_STR, ASON_OPT_F64,
    ASON_VEC_I64, ASON_VEC_U64, ASON_VEC_F64,
    ASON_VEC_STR, ASON_VEC_BOOL,
    ASON_VEC_VEC_I64,
    ASON_MAP_SI, ASON_MAP_SS,
    ASON_STRUCT,
} ason_type_t;

/* ============================================================================
 * Field descriptor
 * ============================================================================ */

typedef void (*ason_encode_fn)(ason_buf_t* buf, const void* base, size_t offset);
typedef ason_err_t (*ason_decode_fn)(const char** pos, const char* end, void* base, size_t offset);

typedef struct {
    const char*  name;
    size_t       name_len;
    ason_type_t  type;
    size_t       offset;
    const char*  type_str;
    /* For ASON_STRUCT fields: pointer to sub-struct descriptor */
    const void*  sub_desc;
    ason_encode_fn dump_fn;
    ason_decode_fn load_fn;
} ason_field_t;

/* ============================================================================
 * Struct descriptor
 * ============================================================================ */

typedef struct {
    const char*       struct_name;
    const ason_field_t* fields;
    int               field_count;
} ason_desc_t;

/* ============================================================================
 * SIMD-accelerated helpers
 * ============================================================================ */

/* Find first occurrence of '"' or '\\' or control char in [pos, end).
 * Returns pointer to the found char, or end if none. */
ason_inline const char* ason_find_quote_or_special(const char* pos, const char* end) {
#if defined(ASON_NEON)
    uint8x16_t q_dq  = vdupq_n_u8('"');
    uint8x16_t q_bs  = vdupq_n_u8('\\');
    uint8x16_t q_lim = vdupq_n_u8(0x20);
    while (pos + 16 <= end) {
        uint8x16_t chunk = vld1q_u8((const uint8_t*)pos);
        uint8x16_t m = vorrq_u8(
            vorrq_u8(vceqq_u8(chunk, q_dq), vceqq_u8(chunk, q_bs)),
            vcltq_u8(chunk, q_lim));
        /* Reduce to scalar: any lane set? */
        if (vmaxvq_u8(m)) {
            for (int i = 0; i < 16; i++)
                if (pos[i] == '"' || pos[i] == '\\' || (uint8_t)pos[i] < 0x20) return pos + i;
        }
        pos += 16;
    }
#elif defined(ASON_SSE2)
    __m128i q_dq  = _mm_set1_epi8('"');
    __m128i q_bs  = _mm_set1_epi8('\\');
    __m128i q_lim = _mm_set1_epi8(0x1F);
    while (pos + 16 <= end) {
        __m128i chunk = _mm_loadu_si128((const __m128i*)pos);
        __m128i m = _mm_or_si128(
            _mm_or_si128(_mm_cmpeq_epi8(chunk, q_dq), _mm_cmpeq_epi8(chunk, q_bs)),
            _mm_cmpeq_epi8(_mm_min_epu8(chunk, q_lim), chunk));
        int mask = _mm_movemask_epi8(m);
        if (mask) {
            int idx = __builtin_ctz(mask);
            return pos + idx;
        }
        pos += 16;
    }
#endif
    /* Scalar fallback */
    while (pos < end) {
        if (*pos == '"' || *pos == '\\' || (uint8_t)*pos < 0x20) return pos;
        pos++;
    }
    return end;
}

/* Check if string needs quoting (contains structural chars) */
ason_inline bool ason_needs_quoting(const char* s, size_t len) {
    if (len == 0) return true;
    /* Leading/trailing whitespace */
    if (s[0] == ' ' || s[0] == '\t' || s[len-1] == ' ' || s[len-1] == '\t') return true;
    /* Bool/number-like */
    if (len >= 4 && (memcmp(s, "true", 4) == 0 || memcmp(s, "null", 4) == 0)) return true;
    if (len == 5 && memcmp(s, "false", 5) == 0) return true;
    if ((s[0] >= '0' && s[0] <= '9') || s[0] == '-' || s[0] == '+') return true;

#if defined(ASON_NEON)
    /* SIMD: check for structural chars: ,()[]{}:"\\<ctrl> */
    uint8x16_t q_comma = vdupq_n_u8(',');
    uint8x16_t q_lp    = vdupq_n_u8('(');
    uint8x16_t q_rp    = vdupq_n_u8(')');
    uint8x16_t q_lb    = vdupq_n_u8('[');
    uint8x16_t q_rb    = vdupq_n_u8(']');
    uint8x16_t q_lc    = vdupq_n_u8('{');
    uint8x16_t q_rc    = vdupq_n_u8('}');
    uint8x16_t q_dq    = vdupq_n_u8('"');
    uint8x16_t q_colon = vdupq_n_u8(':');
    uint8x16_t q_bs    = vdupq_n_u8('\\');
    uint8x16_t q_lim   = vdupq_n_u8(0x20);
    const char* p = s;
    const char* e = s + len;
    while (p + 16 <= e) {
        uint8x16_t chunk = vld1q_u8((const uint8_t*)p);
        uint8x16_t m = vorrq_u8(
            vorrq_u8(vorrq_u8(vceqq_u8(chunk, q_comma), vceqq_u8(chunk, q_lp)),
                     vorrq_u8(vceqq_u8(chunk, q_rp), vceqq_u8(chunk, q_lb))),
            vorrq_u8(vorrq_u8(vceqq_u8(chunk, q_rb), vceqq_u8(chunk, q_lc)),
                     vorrq_u8(vorrq_u8(vceqq_u8(chunk, q_rc), vceqq_u8(chunk, q_dq)),
                              vorrq_u8(vorrq_u8(vceqq_u8(chunk, q_colon), vceqq_u8(chunk, q_bs)),
                                       vcltq_u8(chunk, q_lim)))));
        if (vmaxvq_u8(m)) return true;
        p += 16;
    }
    while (p < e) {
        char c = *p;
        if (c == ',' || c == '(' || c == ')' || c == '[' || c == ']' ||
            c == '{' || c == '}' || c == '"' || c == ':' || c == '\\' ||
            (uint8_t)c < 0x20) return true;
        p++;
    }
#elif defined(ASON_SSE2)
    __m128i q_comma = _mm_set1_epi8(',');
    __m128i q_lp    = _mm_set1_epi8('(');
    __m128i q_rp    = _mm_set1_epi8(')');
    __m128i q_lb    = _mm_set1_epi8('[');
    __m128i q_rb    = _mm_set1_epi8(']');
    __m128i q_lc    = _mm_set1_epi8('{');
    __m128i q_rc    = _mm_set1_epi8('}');
    __m128i q_dq    = _mm_set1_epi8('"');
    __m128i q_colon = _mm_set1_epi8(':');
    __m128i q_bs    = _mm_set1_epi8('\\');
    __m128i q_lim   = _mm_set1_epi8(0x1F);
    const char* p = s;
    const char* e = s + len;
    while (p + 16 <= e) {
        __m128i chunk = _mm_loadu_si128((const __m128i*)p);
        __m128i m = _mm_or_si128(
            _mm_or_si128(_mm_or_si128(_mm_cmpeq_epi8(chunk, q_comma), _mm_cmpeq_epi8(chunk, q_lp)),
                         _mm_or_si128(_mm_cmpeq_epi8(chunk, q_rp), _mm_cmpeq_epi8(chunk, q_lb))),
            _mm_or_si128(_mm_or_si128(_mm_cmpeq_epi8(chunk, q_rb), _mm_cmpeq_epi8(chunk, q_lc)),
                         _mm_or_si128(_mm_or_si128(_mm_cmpeq_epi8(chunk, q_rc), _mm_cmpeq_epi8(chunk, q_dq)),
                                      _mm_or_si128(_mm_or_si128(_mm_cmpeq_epi8(chunk, q_colon), _mm_cmpeq_epi8(chunk, q_bs)),
                                                   _mm_cmpeq_epi8(_mm_min_epu8(chunk, q_lim), chunk)))));
        if (_mm_movemask_epi8(m)) return true;
        p += 16;
    }
    while (p < e) {
        char c = *p;
        if (c == ',' || c == '(' || c == ')' || c == '[' || c == ']' ||
            c == '{' || c == '}' || c == '"' || c == ':' || c == '\\' ||
            (uint8_t)c < 0x20) return true;
        p++;
    }
#else
    const char* p = s;
    const char* e = s + len;
    while (p < e) {
        char c = *p;
        if (c == ',' || c == '(' || c == ')' || c == '[' || c == ']' ||
            c == '{' || c == '}' || c == '"' || c == ':' || c == '\\' ||
            (uint8_t)c < 0x20) return true;
        p++;
    }
#endif
    return false;
}

/* ============================================================================
 * Fast number formatting (DEC_DIGITS lookup, same as C++ version)
 * ============================================================================ */

static const char ASON_DEC_DIGITS[201] =
    "00010203040506070809"
    "10111213141516171819"
    "20212223242526272829"
    "30313233343536373839"
    "40414243444546474849"
    "50515253545556575859"
    "60616263646566676869"
    "70717273747576777879"
    "80818283848586878889"
    "90919293949596979899";

ason_inline void ason_buf_append_u64(ason_buf_t* b, uint64_t v) {
    char tmp[20];
    int i = 20;
    if (v == 0) { ason_buf_push(b, '0'); return; }
    while (v >= 100) {
        int idx = (int)(v % 100) * 2;
        v /= 100;
        tmp[--i] = ASON_DEC_DIGITS[idx + 1];
        tmp[--i] = ASON_DEC_DIGITS[idx];
    }
    if (v >= 10) {
        int idx = (int)v * 2;
        tmp[--i] = ASON_DEC_DIGITS[idx + 1];
        tmp[--i] = ASON_DEC_DIGITS[idx];
    } else {
        tmp[--i] = (char)('0' + v);
    }
    ason_buf_append(b, tmp + i, 20 - i);
}

ason_inline void ason_buf_append_i64(ason_buf_t* b, int64_t v) {
    if (v < 0) {
        ason_buf_push(b, '-');
        ason_buf_append_u64(b, (uint64_t)(-(v + 1)) + 1);
    } else {
        ason_buf_append_u64(b, (uint64_t)v);
    }
}

ason_inline void ason_buf_append_f64(ason_buf_t* b, double v) {
    if (v != v) { ason_buf_appends(b, "NaN"); return; }
    if (v == 1.0/0.0) { ason_buf_appends(b, "Inf"); return; }
    if (v == -1.0/0.0) { ason_buf_appends(b, "-Inf"); return; }
    if (v < 0) { ason_buf_push(b, '-'); v = -v; }
    double intpart, fracpart;
    fracpart = modf(v, &intpart);
    if (fracpart == 0.0 && intpart < 1e15) {
        ason_buf_append_u64(b, (uint64_t)intpart);
        ason_buf_appends(b, ".0");
        return;
    }
    /* Check 1-decimal / 2-decimal fast paths */
    double f1 = fracpart * 10.0;
    double r1 = f1 - (int)f1;
    if (r1 < 1e-9 && intpart < 1e15) {
        ason_buf_append_u64(b, (uint64_t)intpart);
        ason_buf_push(b, '.');
        ason_buf_push(b, '0' + (int)f1);
        return;
    }
    double f2 = fracpart * 100.0;
    double r2 = f2 - (int)f2;
    if (r2 < 1e-9 && intpart < 1e15) {
        ason_buf_append_u64(b, (uint64_t)intpart);
        ason_buf_push(b, '.');
        int d = (int)f2;
        ason_buf_push(b, '0' + d / 10);
        ason_buf_push(b, '0' + d % 10);
        return;
    }
    /* Fallback */
    char tmp[64];
    int n = snprintf(tmp, sizeof(tmp), "%.17g", (v < 0 ? -v : v));
    /* Ensure decimal point */
    bool has_dot = false;
    for (int i = 0; i < n; i++) { if (tmp[i] == '.' || tmp[i] == 'e' || tmp[i] == 'E') { has_dot = true; break; } }
    ason_buf_append(b, tmp, n);
    if (!has_dot) ason_buf_appends(b, ".0");
}

/* ============================================================================
 * String escaping (write)
 * ============================================================================ */

static const char ASON_ESCAPE[256] = {
    ['\\'] = '\\', ['"'] = '"', ['\n'] = 'n', ['\r'] = 'r', ['\t'] = 't',
    ['\b'] = 'b', ['\f'] = 'f',
};

ason_inline void ason_buf_append_escaped(ason_buf_t* b, const char* s, size_t len) {
    ason_buf_push(b, '"');
    const char* pos = s;
    const char* end = s + len;
    while (pos < end) {
        const char* next = ason_find_quote_or_special(pos, end);
        if (next > pos) ason_buf_append(b, pos, next - pos);
        if (next >= end) break;
        char c = *next;
        char esc = ASON_ESCAPE[(uint8_t)c];
        if (esc) {
            ason_buf_push(b, '\\');
            ason_buf_push(b, esc);
        } else if ((uint8_t)c < 0x20) {
            char hex[7];
            snprintf(hex, sizeof(hex), "\\u%04x", (uint8_t)c);
            ason_buf_append(b, hex, 6);
        } else {
            ason_buf_push(b, c);
        }
        pos = next + 1;
    }
    ason_buf_push(b, '"');
}

ason_inline void ason_buf_append_str(ason_buf_t* b, const char* s, size_t len) {
    if (ason_needs_quoting(s, len)) {
        ason_buf_append_escaped(b, s, len);
    } else {
        ason_buf_append(b, s, len);
    }
}

/* ============================================================================
 * Parser helpers
 * ============================================================================ */

ason_inline void ason_skip_ws(const char** pos, const char* end) {
    while (*pos < end) {
        char c = **pos;
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') { (*pos)++; continue; }
        /* Comment: /asterisk ... asterisk/ */
        if (c == '/' && *pos + 1 < end && (*pos)[1] == '*') {
            *pos += 2;
            while (*pos + 1 < end) {
                if (**pos == '*' && (*pos)[1] == '/') { *pos += 2; break; }
                (*pos)++;
            }
            continue;
        }
        break;
    }
}

ason_inline bool ason_at_value_end(const char* pos, const char* end) {
    if (pos >= end) return true;
    char c = *pos;
    return c == ',' || c == ')' || c == ']';
}

/* Parse a quoted string. Zero-copy: returns pointers into a decode buffer. */
ason_inline ason_err_t ason_parse_quoted_string(const char** pos, const char* end,
                                                 char** out, size_t* out_len) {
    const char* p = *pos;
    if (p >= end || *p != '"') return ASON_ERR_SYNTAX;
    p++;
    /* Fast path: scan for end quote with SIMD */
    const char* start = p;
    const char* found = ason_find_quote_or_special(p, end);
    if (found < end && *found == '"' && found > start) {
        /* No escapes — zero-copy */
        *out = (char*)start;
        *out_len = found - start;
        *pos = found + 1;
        return ASON_OK;
    }
    /* Slow path: has escapes */
    size_t cap = 64;
    char* buf = (char*)malloc(cap);
    size_t len = 0;
    /* Copy any prefix before the special char */
    if (found > start) {
        len = found - start;
        if (len > cap) { cap = len * 2; char* tmp = (char*)realloc(buf, cap); if (!tmp) { free(buf); return ASON_ERR_SYNTAX; } buf = tmp; }
        memcpy(buf, start, len);
    }
    p = found;
    while (p < end) {
        if (*p == '"') { p++; break; }
        if (*p == '\\') {
            p++;
            if (p >= end) { free(buf); return ASON_ERR_SYNTAX; }
            char c = *p++;
            if (len >= cap) { cap = cap * 2; char* tmp = (char*)realloc(buf, cap); if (!tmp) { free(buf); return ASON_ERR_SYNTAX; } buf = tmp; }
            switch (c) {
                case 'n': buf[len++] = '\n'; break;
                case 'r': buf[len++] = '\r'; break;
                case 't': buf[len++] = '\t'; break;
                case 'b': buf[len++] = '\b'; break;
                case 'f': buf[len++] = '\f'; break;
                case '\\': buf[len++] = '\\'; break;
                case '"': buf[len++] = '"'; break;
                case '/': buf[len++] = '/'; break;
                default: buf[len++] = c; break;
            }
        } else {
            if (len >= cap) { cap = cap * 2; char* tmp = (char*)realloc(buf, cap); if (!tmp) { free(buf); return ASON_ERR_SYNTAX; } buf = tmp; }
            buf[len++] = *p++;
        }
    }
    buf[len] = '\0';
    *out = buf;
    *out_len = len;
    *pos = p;
    return ASON_OK;
}

/* Parse a plain (unquoted) value, trimming whitespace */
ason_inline ason_err_t ason_parse_plain_value(const char** pos, const char* end,
                                               char** out, size_t* out_len) {
    const char* start = *pos;
    const char* p = start;
    /* Scan to delimiter and check for backslash in one pass */
    bool has_esc = false;
    while (p < end) {
        char c = *p;
        if (c == ',' || c == ')' || c == ']') break;
        if (c == '\\') has_esc = true;
        p++;
    }
    /* Trim trailing whitespace */
    const char* vend = p;
    while (vend > start && (vend[-1] == ' ' || vend[-1] == '\t' || vend[-1] == '\n' || vend[-1] == '\r')) vend--;
    size_t len = vend - start;
    if (has_esc) {
        char* buf = (char*)malloc(len + 1);
        size_t j = 0;
        for (const char* c = start; c < vend; c++) {
            if (*c == '\\' && c + 1 < vend) {
                c++;
                switch (*c) {
                    case 'n': buf[j++] = '\n'; break;
                    case 'r': buf[j++] = '\r'; break;
                    case 't': buf[j++] = '\t'; break;
                    case '\\': buf[j++] = '\\'; break;
                    case '"': buf[j++] = '"'; break;
                    default: buf[j++] = *c; break;
                }
            } else {
                buf[j++] = *c;
            }
        }
        buf[j] = '\0';
        *out = buf;
        *out_len = j;
    } else {
        *out = (char*)start; /* zero-copy */
        *out_len = len;
    }
    *pos = p;
    return ASON_OK;
}

/* Parse a string value (quoted or plain) */
ason_inline ason_err_t ason_parse_string_value(const char** pos, const char* end,
                                                char** out, size_t* out_len,
                                                bool* allocated) {
    ason_skip_ws(pos, end);
    *allocated = false;
    if (*pos < end && **pos == '"') {
        const char* before = *pos;
        ason_err_t err = ason_parse_quoted_string(pos, end, out, out_len);
        if (err != ASON_OK) return err;
        /* Detect if result was heap-allocated (slow path with escapes) */
        /* Zero-copy returns pointer into input [before+1..*pos-1) */
        *allocated = (*out < before || *out >= *pos);
        return ASON_OK;
    }
    const char* before = *pos;
    ason_err_t err = ason_parse_plain_value(pos, end, out, out_len);
    if (err != ASON_OK) return err;
    /* Detect if result was heap-allocated (has_esc path) */
    *allocated = (*out < before || *out >= *pos);
    return err;
}

/* Skip a balanced value without decoding */
ason_inline void ason_skip_balanced(const char** pos, const char* end, char open, char close) {
    int depth = 1;
    (*pos)++;
    while (*pos < end && depth > 0) {
        char c = **pos;
        if (c == open) depth++;
        else if (c == close) depth--;
        else if (c == '"') {
            (*pos)++;
            while (*pos < end && **pos != '"') {
                if (**pos == '\\') (*pos)++;
                (*pos)++;
            }
            if (*pos < end) (*pos)++;
            continue;
        }
        (*pos)++;
    }
}

ason_inline void ason_skip_value(const char** pos, const char* end) {
    ason_skip_ws(pos, end);
    if (*pos >= end) return;
    char c = **pos;
    if (c == '(') { ason_skip_balanced(pos, end, '(', ')'); return; }
    if (c == '[') { ason_skip_balanced(pos, end, '[', ']'); return; }
    if (c == '{') { ason_skip_balanced(pos, end, '{', '}'); return; }
    if (c == '"') {
        (*pos)++;
        while (*pos < end && **pos != '"') {
            if (**pos == '\\') (*pos)++;
            (*pos)++;
        }
        if (*pos < end) (*pos)++;
        return;
    }
    while (*pos < end && **pos != ',' && **pos != ')' && **pos != ']') (*pos)++;
}

/* Skip remaining comma-separated values in a tuple until ')' is found.
 * Used when the target struct has fewer fields than the source data. */
ason_inline void ason_skip_remaining_tuple_values(const char** pos, const char* end) {
    for (;;) {
        ason_skip_ws(pos, end);
        if (*pos >= end || **pos == ')') return;
        if (**pos == ',') { (*pos)++; ason_skip_ws(pos, end); if (*pos >= end || **pos == ')') return; }
        else return;
        ason_skip_value(pos, end);
    }
}

/* Parse the schema: {field1,field2,...} or {field1:type1,...} */
/* Returns field names (just pointers + lengths into the input). */
typedef struct { const char* name; size_t len; } ason_schema_field_t;

ason_inline ason_err_t ason_parse_schema(const char** pos, const char* end,
                                          ason_schema_field_t* fields, int* count, int max_fields) {
    ason_skip_ws(pos, end);
    if (*pos >= end || **pos != '{') return ASON_ERR_SYNTAX;
    (*pos)++;
    int n = 0;
    while (n < max_fields) {
        ason_skip_ws(pos, end);
        if (*pos >= end) return ASON_ERR_SYNTAX;
        if (**pos == '}') { (*pos)++; break; }
        if (n > 0) {
            if (**pos == ',') (*pos)++;
            else return ASON_ERR_SYNTAX;
            ason_skip_ws(pos, end);
        }
        /* Field name */
        const char* name_start = *pos;
        while (*pos < end && **pos != ',' && **pos != ':' && **pos != '}' &&
               **pos != ' ' && **pos != '\t' && **pos != '\n' && **pos != '\r' &&
               **pos != '{') (*pos)++;
        size_t name_len = *pos - name_start;
        fields[n].name = name_start;
        fields[n].len = name_len;
        n++;
        /* Skip type annotation if present: :type  or  :{...} or :[...] */
        ason_skip_ws(pos, end);
        if (*pos < end && **pos == ':') {
            (*pos)++;
            ason_skip_ws(pos, end);
            if (*pos < end) {
                char c = **pos;
                if (c == '{') { ason_skip_balanced(pos, end, '{', '}'); }
                else if (c == '[') { ason_skip_balanced(pos, end, '[', ']'); }
                else {
                    while (*pos < end && **pos != ',' && **pos != '}' &&
                           **pos != ' ' && **pos != '\t') (*pos)++;
                }
            }
        }
        /* Inline sub-schema {}: skip it */
        ason_skip_ws(pos, end);
        if (*pos < end && **pos == '{') {
            ason_skip_balanced(pos, end, '{', '}');
        }
    }
    *count = n;
    return ASON_OK;
}

/* ============================================================================
 * Generic field dump/load functions
 * ============================================================================ */

void ason_encode_bool(ason_buf_t* buf, const void* base, size_t offset);
void ason_encode_i8(ason_buf_t* buf, const void* base, size_t offset);
void ason_encode_i16(ason_buf_t* buf, const void* base, size_t offset);
void ason_encode_i32(ason_buf_t* buf, const void* base, size_t offset);
void ason_encode_i64(ason_buf_t* buf, const void* base, size_t offset);
void ason_encode_u8(ason_buf_t* buf, const void* base, size_t offset);
void ason_encode_u16(ason_buf_t* buf, const void* base, size_t offset);
void ason_encode_u32(ason_buf_t* buf, const void* base, size_t offset);
void ason_encode_u64(ason_buf_t* buf, const void* base, size_t offset);
void ason_encode_f32(ason_buf_t* buf, const void* base, size_t offset);
void ason_encode_f64(ason_buf_t* buf, const void* base, size_t offset);
void ason_encode_char(ason_buf_t* buf, const void* base, size_t offset);
void ason_encode_str(ason_buf_t* buf, const void* base, size_t offset);
void ason_encode_opt_i64(ason_buf_t* buf, const void* base, size_t offset);
void ason_encode_opt_str(ason_buf_t* buf, const void* base, size_t offset);
void ason_encode_opt_f64(ason_buf_t* buf, const void* base, size_t offset);
void ason_encode_vec_i64(ason_buf_t* buf, const void* base, size_t offset);
void ason_encode_vec_u64(ason_buf_t* buf, const void* base, size_t offset);
void ason_encode_vec_f64(ason_buf_t* buf, const void* base, size_t offset);
void ason_encode_vec_str(ason_buf_t* buf, const void* base, size_t offset);
void ason_encode_vec_bool(ason_buf_t* buf, const void* base, size_t offset);
void ason_encode_vec_vec_i64(ason_buf_t* buf, const void* base, size_t offset);
void ason_encode_map_si(ason_buf_t* buf, const void* base, size_t offset);
void ason_encode_map_ss(ason_buf_t* buf, const void* base, size_t offset);

ason_err_t ason_decode_bool(const char** pos, const char* end, void* base, size_t offset);
ason_err_t ason_decode_i8(const char** pos, const char* end, void* base, size_t offset);
ason_err_t ason_decode_i16(const char** pos, const char* end, void* base, size_t offset);
ason_err_t ason_decode_i32(const char** pos, const char* end, void* base, size_t offset);
ason_err_t ason_decode_i64(const char** pos, const char* end, void* base, size_t offset);
ason_err_t ason_decode_u8(const char** pos, const char* end, void* base, size_t offset);
ason_err_t ason_decode_u16(const char** pos, const char* end, void* base, size_t offset);
ason_err_t ason_decode_u32(const char** pos, const char* end, void* base, size_t offset);
ason_err_t ason_decode_u64(const char** pos, const char* end, void* base, size_t offset);
ason_err_t ason_decode_f32(const char** pos, const char* end, void* base, size_t offset);
ason_err_t ason_decode_f64(const char** pos, const char* end, void* base, size_t offset);
ason_err_t ason_decode_char(const char** pos, const char* end, void* base, size_t offset);
ason_err_t ason_decode_str(const char** pos, const char* end, void* base, size_t offset);
ason_err_t ason_decode_opt_i64(const char** pos, const char* end, void* base, size_t offset);
ason_err_t ason_decode_opt_str(const char** pos, const char* end, void* base, size_t offset);
ason_err_t ason_decode_opt_f64(const char** pos, const char* end, void* base, size_t offset);
ason_err_t ason_decode_vec_i64(const char** pos, const char* end, void* base, size_t offset);
ason_err_t ason_decode_vec_u64(const char** pos, const char* end, void* base, size_t offset);
ason_err_t ason_decode_vec_f64(const char** pos, const char* end, void* base, size_t offset);
ason_err_t ason_decode_vec_str(const char** pos, const char* end, void* base, size_t offset);
ason_err_t ason_decode_vec_bool(const char** pos, const char* end, void* base, size_t offset);
ason_err_t ason_decode_vec_vec_i64(const char** pos, const char* end, void* base, size_t offset);
ason_err_t ason_decode_map_si(const char** pos, const char* end, void* base, size_t offset);
ason_err_t ason_decode_map_ss(const char** pos, const char* end, void* base, size_t offset);

/* Generic struct dump/load via descriptor */
void ason_encode_struct(ason_buf_t* buf, const void* obj, const ason_desc_t* desc);
ason_err_t ason_decode_struct(const char** pos, const char* end, void* obj, const ason_desc_t* desc);

/* Recursive schema writers */
void ason_write_schema(ason_buf_t* buf, const ason_desc_t* desc);
void ason_write_schema_typed(ason_buf_t* buf, const ason_desc_t* desc);

/* Pretty-format: reformat compact ASON with smart indentation */
ason_buf_t ason_pretty_format(const char* src, size_t len);

/* ============================================================================
 * ason_free_struct_fields — recursively free all heap-allocated fields
 * ============================================================================
 *
 * Walks a descriptor and frees strings, vecs, maps, and nested structs.
 * Safe to call on zero-initialized structs (NULL pointers are no-ops).
 */

static inline void ason_free_struct_fields(void* obj, const ason_desc_t* desc) {
    for (int i = 0; i < desc->field_count; i++) {
        const ason_field_t* f = &desc->fields[i];
        void* fp = (char*)obj + f->offset;
        switch (f->type) {
            case ASON_STR: {
                ason_string_t* s = (ason_string_t*)fp;
                ason_string_free(s);
                break;
            }
            case ASON_OPT_STR: {
                ason_opt_str* o = (ason_opt_str*)fp;
                if (o->has_value) ason_string_free(&o->value);
                break;
            }
            case ASON_VEC_I64: { ason_vec_i64_free((ason_vec_i64*)fp); break; }
            case ASON_VEC_U64: { ason_vec_u64_free((ason_vec_u64*)fp); break; }
            case ASON_VEC_F64: { ason_vec_f64_free((ason_vec_f64*)fp); break; }
            case ASON_VEC_BOOL: { ason_vec_bool_free((ason_vec_bool*)fp); break; }
            case ASON_VEC_STR: {
                ason_vec_str* v = (ason_vec_str*)fp;
                for (size_t j = 0; j < v->len; j++) ason_string_free(&v->data[j]);
                ason_vec_str_free(v);
                break;
            }
            case ASON_VEC_VEC_I64: {
                ason_vec_vec_i64* v = (ason_vec_vec_i64*)fp;
                for (size_t j = 0; j < v->len; j++) ason_vec_i64_free(&v->data[j]);
                ason_vec_vec_i64_free(v);
                break;
            }
            case ASON_MAP_SI: {
                ason_map_si* m = (ason_map_si*)fp;
                for (size_t j = 0; j < m->len; j++) ason_string_free(&m->data[j].key);
                ason_map_si_free(m);
                break;
            }
            case ASON_MAP_SS: {
                ason_map_ss* m = (ason_map_ss*)fp;
                for (size_t j = 0; j < m->len; j++) {
                    ason_string_free(&m->data[j].key);
                    ason_string_free(&m->data[j].val);
                }
                ason_map_ss_free(m);
                break;
            }
            case ASON_STRUCT: {
                if (f->sub_desc)
                    ason_free_struct_fields(fp, (const ason_desc_t*)f->sub_desc);
                break;
            }
            default: break; /* scalars: nothing to free */
        }
    }
}

/* ============================================================================
 * ASON_FIELD macro — build a field descriptor
 * ============================================================================ */

#define ASON_DUMP_FN(t) ason_encode_##t
#define ASON_LOAD_FN(t) ason_decode_##t

/* C _Bool aliases: bool expands to _Bool before ## token pasting */
#define ason_encode__Bool ason_encode_bool
#define ason_decode__Bool ason_decode_bool
#define ASON_TYPE_NAME__Bool "bool"
#define ASON_TYPE_ENUM__Bool ASON_BOOL

#define ASON_TYPE_NAME_bool  "bool"
#define ASON_TYPE_NAME_i8    "int"
#define ASON_TYPE_NAME_i16   "int"
#define ASON_TYPE_NAME_i32   "int"
#define ASON_TYPE_NAME_i64   "int"
#define ASON_TYPE_NAME_u8    "int"
#define ASON_TYPE_NAME_u16   "int"
#define ASON_TYPE_NAME_u32   "int"
#define ASON_TYPE_NAME_u64   "int"
#define ASON_TYPE_NAME_f32   "float"
#define ASON_TYPE_NAME_f64   "float"
#define ASON_TYPE_NAME_char  "char"
#define ASON_TYPE_NAME_str   "str"
#define ASON_TYPE_NAME_opt_i64 "int"
#define ASON_TYPE_NAME_opt_str "str"
#define ASON_TYPE_NAME_opt_f64 "float"
#define ASON_TYPE_NAME_vec_i64 "[int]"
#define ASON_TYPE_NAME_vec_u64 "[int]"
#define ASON_TYPE_NAME_vec_f64 "[float]"
#define ASON_TYPE_NAME_vec_str "[str]"
#define ASON_TYPE_NAME_vec_bool "[bool]"
#define ASON_TYPE_NAME_vec_vec_i64 "[[int]]"
#define ASON_TYPE_NAME_map_si "map[str,int]"
#define ASON_TYPE_NAME_map_ss "map[str,str]"

#define ASON_TYPE_ENUM_bool  ASON_BOOL
#define ASON_TYPE_ENUM_i8    ASON_I8
#define ASON_TYPE_ENUM_i16   ASON_I16
#define ASON_TYPE_ENUM_i32   ASON_I32
#define ASON_TYPE_ENUM_i64   ASON_I64
#define ASON_TYPE_ENUM_u8    ASON_U8
#define ASON_TYPE_ENUM_u16   ASON_U16
#define ASON_TYPE_ENUM_u32   ASON_U32
#define ASON_TYPE_ENUM_u64   ASON_U64
#define ASON_TYPE_ENUM_f32   ASON_F32
#define ASON_TYPE_ENUM_f64   ASON_F64
#define ASON_TYPE_ENUM_char  ASON_CHAR
#define ASON_TYPE_ENUM_str   ASON_STR
#define ASON_TYPE_ENUM_opt_i64 ASON_OPT_I64
#define ASON_TYPE_ENUM_opt_str ASON_OPT_STR
#define ASON_TYPE_ENUM_opt_f64 ASON_OPT_F64
#define ASON_TYPE_ENUM_vec_i64 ASON_VEC_I64
#define ASON_TYPE_ENUM_vec_u64 ASON_VEC_U64
#define ASON_TYPE_ENUM_vec_f64 ASON_VEC_F64
#define ASON_TYPE_ENUM_vec_str ASON_VEC_STR
#define ASON_TYPE_ENUM_vec_bool ASON_VEC_BOOL
#define ASON_TYPE_ENUM_vec_vec_i64 ASON_VEC_VEC_I64
#define ASON_TYPE_ENUM_map_si ASON_MAP_SI
#define ASON_TYPE_ENUM_map_ss ASON_MAP_SS

#define ASON_FIELD(StructType, member, fname, ftype) \
    { fname, sizeof(fname) - 1, ASON_TYPE_ENUM_##ftype, \
      offsetof(StructType, member), ASON_TYPE_NAME_##ftype, NULL, \
      ASON_DUMP_FN(ftype), ASON_LOAD_FN(ftype) }

#define ASON_FIELD_STRUCT(StructType, member, fname, sub_desc_ptr) \
    { fname, sizeof(fname) - 1, ASON_STRUCT, \
      offsetof(StructType, member), NULL, sub_desc_ptr, \
      NULL, NULL }

/* ============================================================================
 * ASON_FIELDS — auto-generates descriptor, dump, and load functions
 * ============================================================================ */

#define ASON_FIELDS(StructType, nfields, ...) \
    static const ason_field_t StructType##_ason_fields[] = { __VA_ARGS__ }; \
    static const ason_desc_t StructType##_ason_desc = { \
        #StructType, StructType##_ason_fields, nfields \
    }; \
    static inline ason_buf_t ason_encode_##StructType(const StructType* obj) { \
        ason_buf_t buf = ason_buf_new(128); \
        ason_write_schema(&buf, &StructType##_ason_desc); \
        ason_buf_push(&buf, ':'); \
        ason_buf_push(&buf, '('); \
        for (int i = 0; i < nfields; i++) { \
            if (i > 0) ason_buf_push(&buf, ','); \
            if (StructType##_ason_fields[i].type == ASON_STRUCT) { \
                if (StructType##_ason_fields[i].dump_fn) { \
                    StructType##_ason_fields[i].dump_fn(&buf, obj, \
                        StructType##_ason_fields[i].offset); \
                } else { \
                    ason_encode_struct(&buf, (const char*)obj + StructType##_ason_fields[i].offset, \
                                     (const ason_desc_t*)StructType##_ason_fields[i].sub_desc); \
                } \
            } else { \
                StructType##_ason_fields[i].dump_fn(&buf, obj, \
                    StructType##_ason_fields[i].offset); \
            } \
        } \
        ason_buf_push(&buf, ')'); \
        return buf; \
    } \
    static inline ason_buf_t ason_encode_typed_##StructType(const StructType* obj) { \
        ason_buf_t buf = ason_buf_new(128); \
        ason_write_schema_typed(&buf, &StructType##_ason_desc); \
        ason_buf_push(&buf, ':'); \
        ason_buf_push(&buf, '('); \
        for (int i = 0; i < nfields; i++) { \
            if (i > 0) ason_buf_push(&buf, ','); \
            if (StructType##_ason_fields[i].type == ASON_STRUCT) { \
                if (StructType##_ason_fields[i].dump_fn) { \
                    StructType##_ason_fields[i].dump_fn(&buf, obj, \
                        StructType##_ason_fields[i].offset); \
                } else { \
                    ason_encode_struct(&buf, (const char*)obj + StructType##_ason_fields[i].offset, \
                                     (const ason_desc_t*)StructType##_ason_fields[i].sub_desc); \
                } \
            } else { \
                StructType##_ason_fields[i].dump_fn(&buf, obj, \
                    StructType##_ason_fields[i].offset); \
            } \
        } \
        ason_buf_push(&buf, ')'); \
        return buf; \
    } \
    static inline ason_err_t ason_decode_##StructType(const char* input, size_t len, StructType* out) { \
        const char* pos = input; \
        const char* end = input + len; \
        ason_skip_ws(&pos, end); \
        if (pos >= end || *pos != '{') return ASON_ERR_SYNTAX; \
        ason_schema_field_t schema[64]; \
        int schema_count = 0; \
        ason_err_t err = ason_parse_schema(&pos, end, schema, &schema_count, 64); \
        if (err != ASON_OK) return err; \
        ason_skip_ws(&pos, end); \
        if (pos >= end || *pos != ':') return ASON_ERR_SYNTAX; \
        pos++; \
        ason_skip_ws(&pos, end); \
        if (pos >= end || *pos != '(') return ASON_ERR_SYNTAX; \
        pos++; \
        int field_map[64]; \
        for (int i = 0; i < schema_count; i++) { \
            field_map[i] = -1; \
            for (int j = 0; j < nfields; j++) { \
                if (schema[i].len == StructType##_ason_fields[j].name_len && \
                    memcmp(schema[i].name, StructType##_ason_fields[j].name, schema[i].len) == 0) { \
                    field_map[i] = j; break; \
                } \
            } \
        } \
        for (int i = 0; i < schema_count; i++) { \
            ason_skip_ws(&pos, end); \
            if (pos < end && *pos == ')') break; \
            if (i > 0) { \
                if (*pos == ',') { pos++; ason_skip_ws(&pos, end); if (pos < end && *pos == ')') break; } \
                else if (*pos == ')') break; \
                else { ason_free_struct_fields(out, &StructType##_ason_desc); return ASON_ERR_SYNTAX; } \
            } \
            if (field_map[i] >= 0) { \
                int fi = field_map[i]; \
                if (StructType##_ason_fields[fi].type == ASON_STRUCT) { \
                    if (StructType##_ason_fields[fi].load_fn) { \
                        err = StructType##_ason_fields[fi].load_fn(&pos, end, out, \
                            StructType##_ason_fields[fi].offset); \
                    } else { \
                        err = ason_decode_struct(&pos, end, \
                            (char*)out + StructType##_ason_fields[fi].offset, \
                            (const ason_desc_t*)StructType##_ason_fields[fi].sub_desc); \
                    } \
                } else { \
                    err = StructType##_ason_fields[fi].load_fn(&pos, end, out, \
                        StructType##_ason_fields[fi].offset); \
                } \
                if (err != ASON_OK) { ason_free_struct_fields(out, &StructType##_ason_desc); return err; } \
            } else { \
                ason_skip_value(&pos, end); \
            } \
        } \
        ason_skip_ws(&pos, end); \
        if (pos < end && *pos == ')') pos++; \
        ason_skip_ws(&pos, end); \
        if (pos < end) return ASON_ERR_SYNTAX; \
        return ASON_OK; \
    } \
    /* encode_typed_vec: [{field:type,...}]:(row1),(row2),... */ \
    static inline ason_buf_t ason_encode_typed_vec_##StructType(const StructType* arr, size_t count) { \
        ason_buf_t buf = ason_buf_new(count * 64 + 128); \
        ason_buf_push(&buf, '['); \
        ason_write_schema_typed(&buf, &StructType##_ason_desc); \
        ason_buf_push(&buf, ']'); \
        ason_buf_push(&buf, ':'); \
        for (size_t r = 0; r < count; r++) { \
            if (r > 0) ason_buf_push(&buf, ','); \
            ason_buf_push(&buf, '('); \
            for (int i = 0; i < nfields; i++) { \
                if (i > 0) ason_buf_push(&buf, ','); \
                if (StructType##_ason_fields[i].type == ASON_STRUCT) { \
                    if (StructType##_ason_fields[i].dump_fn) { \
                        StructType##_ason_fields[i].dump_fn(&buf, &arr[r], \
                            StructType##_ason_fields[i].offset); \
                    } else { \
                        ason_encode_struct(&buf, (const char*)&arr[r] + StructType##_ason_fields[i].offset, \
                                         (const ason_desc_t*)StructType##_ason_fields[i].sub_desc); \
                    } \
                } else { \
                    StructType##_ason_fields[i].dump_fn(&buf, &arr[r], \
                        StructType##_ason_fields[i].offset); \
                } \
            } \
            ason_buf_push(&buf, ')'); \
        } \
        return buf; \
    } \
    /* encode_vec: [{schema}]:(row1),(row2),... */ \
    static inline ason_buf_t ason_encode_vec_##StructType(const StructType* arr, size_t count) { \
        ason_buf_t buf = ason_buf_new(count * 64 + 128); \
        ason_buf_push(&buf, '['); \
        ason_write_schema(&buf, &StructType##_ason_desc); \
        ason_buf_push(&buf, ']'); \
        ason_buf_push(&buf, ':'); \
        for (size_t r = 0; r < count; r++) { \
            if (r > 0) ason_buf_push(&buf, ','); \
            ason_buf_push(&buf, '('); \
            for (int i = 0; i < nfields; i++) { \
                if (i > 0) ason_buf_push(&buf, ','); \
                if (StructType##_ason_fields[i].type == ASON_STRUCT) { \
                    if (StructType##_ason_fields[i].dump_fn) { \
                        StructType##_ason_fields[i].dump_fn(&buf, &arr[r], \
                            StructType##_ason_fields[i].offset); \
                    } else { \
                        ason_encode_struct(&buf, (const char*)&arr[r] + StructType##_ason_fields[i].offset, \
                                         (const ason_desc_t*)StructType##_ason_fields[i].sub_desc); \
                    } \
                } else { \
                    StructType##_ason_fields[i].dump_fn(&buf, &arr[r], \
                        StructType##_ason_fields[i].offset); \
                } \
            } \
            ason_buf_push(&buf, ')'); \
        } \
        return buf; \
    } \
    /* encode_pretty: pretty-formatted single struct */ \
    static inline ason_buf_t ason_encode_pretty_##StructType(const StructType* obj) { \
        ason_buf_t compact = ason_encode_##StructType(obj); \
        ason_buf_t pretty = ason_pretty_format(compact.data, compact.len); \
        ason_buf_free(&compact); \
        return pretty; \
    } \
    /* encode_pretty_typed: pretty-formatted single struct with types */ \
    static inline ason_buf_t ason_encode_pretty_typed_##StructType(const StructType* obj) { \
        ason_buf_t compact = ason_encode_typed_##StructType(obj); \
        ason_buf_t pretty = ason_pretty_format(compact.data, compact.len); \
        ason_buf_free(&compact); \
        return pretty; \
    } \
    /* encode_pretty_vec: pretty-formatted array */ \
    static inline ason_buf_t ason_encode_pretty_vec_##StructType(const StructType* arr, size_t count) { \
        ason_buf_t compact = ason_encode_vec_##StructType(arr, count); \
        ason_buf_t pretty = ason_pretty_format(compact.data, compact.len); \
        ason_buf_free(&compact); \
        return pretty; \
    } \
    /* encode_pretty_typed_vec: pretty-formatted typed array */ \
    static inline ason_buf_t ason_encode_pretty_typed_vec_##StructType(const StructType* arr, size_t count) { \
        ason_buf_t compact = ason_encode_typed_vec_##StructType(arr, count); \
        ason_buf_t pretty = ason_pretty_format(compact.data, compact.len); \
        ason_buf_free(&compact); \
        return pretty; \
    } \
    /* decode_vec: [{schema}]:(row1),(row2),... */ \
    static inline ason_err_t ason_decode_vec_##StructType(const char* input, size_t len, \
                                                         StructType** out, size_t* out_count) { \
        const char* pos = input; \
        const char* end = input + len; \
        ason_skip_ws(&pos, end); \
        if (pos >= end || *pos != '[') return ASON_ERR_SYNTAX; \
        pos++; \
        if (pos >= end || *pos != '{') return ASON_ERR_SYNTAX; \
        ason_schema_field_t schema[64]; \
        int schema_count = 0; \
        ason_err_t err = ason_parse_schema(&pos, end, schema, &schema_count, 64); \
        if (err != ASON_OK) return err; \
        ason_skip_ws(&pos, end); \
        if (pos >= end || *pos != ']') return ASON_ERR_SYNTAX; \
        pos++; \
        ason_skip_ws(&pos, end); \
        if (pos >= end || *pos != ':') return ASON_ERR_SYNTAX; \
        pos++; \
        int field_map[64]; \
        for (int i = 0; i < schema_count; i++) { \
            field_map[i] = -1; \
            for (int j = 0; j < nfields; j++) { \
                if (schema[i].len == StructType##_ason_fields[j].name_len && \
                    memcmp(schema[i].name, StructType##_ason_fields[j].name, schema[i].len) == 0) { \
                    field_map[i] = j; break; \
                } \
            } \
        } \
        size_t cap = 16; \
        size_t cnt = 0; \
        StructType* arr = (StructType*)calloc(cap, sizeof(StructType)); \
        while (1) { \
            ason_skip_ws(&pos, end); \
            if (pos >= end || *pos != '(') break; \
            pos++; \
            if (cnt >= cap) { \
                cap = cap + (cap >> 1); \
                StructType* tmp = (StructType*)realloc(arr, cap * sizeof(StructType)); \
                if (!tmp) { \
                    for (size_t k = 0; k < cnt; k++) ason_free_struct_fields(&arr[k], &StructType##_ason_desc); \
                    free(arr); return ASON_ERR_ALLOC; \
                } \
                arr = tmp; \
                memset(arr + cnt, 0, (cap - cnt) * sizeof(StructType)); \
            } \
            StructType* elem = &arr[cnt]; \
            memset(elem, 0, sizeof(StructType)); \
            for (int i = 0; i < schema_count; i++) { \
                ason_skip_ws(&pos, end); \
                if (pos < end && *pos == ')') break; \
                if (i > 0) { \
                    if (*pos == ',') { pos++; ason_skip_ws(&pos, end); if (pos < end && *pos == ')') break; } \
                    else if (*pos == ')') break; \
                    else { \
                        for (size_t k = 0; k <= cnt; k++) ason_free_struct_fields(&arr[k], &StructType##_ason_desc); \
                        free(arr); return ASON_ERR_SYNTAX; \
                    } \
                } \
                if (field_map[i] >= 0) { \
                    int fi = field_map[i]; \
                    if (StructType##_ason_fields[fi].type == ASON_STRUCT) { \
                        if (StructType##_ason_fields[fi].load_fn) { \
                            err = StructType##_ason_fields[fi].load_fn(&pos, end, elem, \
                                StructType##_ason_fields[fi].offset); \
                        } else { \
                            err = ason_decode_struct(&pos, end, \
                                (char*)elem + StructType##_ason_fields[fi].offset, \
                                (const ason_desc_t*)StructType##_ason_fields[fi].sub_desc); \
                        } \
                    } else { \
                        err = StructType##_ason_fields[fi].load_fn(&pos, end, elem, \
                            StructType##_ason_fields[fi].offset); \
                    } \
                    if (err != ASON_OK) { \
                        for (size_t k = 0; k <= cnt; k++) ason_free_struct_fields(&arr[k], &StructType##_ason_desc); \
                        free(arr); return err; \
                    } \
                } else { \
                    ason_skip_value(&pos, end); \
                } \
            } \
            ason_skip_remaining_tuple_values(&pos, end); \
            ason_skip_ws(&pos, end); \
            if (pos < end && *pos == ')') pos++; \
            cnt++; \
            ason_skip_ws(&pos, end); \
            if (pos < end && *pos == ',') { \
                pos++; \
                ason_skip_ws(&pos, end); \
                if (pos >= end || *pos != '(') break; \
            } \
        } \
        *out = arr; \
        *out_count = cnt; \
        return ASON_OK; \
    }

/* ============================================================================
 * Struct field as dump_fn for vector-of-struct fields
 * ============================================================================ */

/* For struct-typed vector fields, we need a vec type per struct.
 * Use ASON_VEC_STRUCT_DEFINE to create the necessary types. */

#define ASON_VEC_STRUCT_DEFINE(StructType) \
    ASON_VEC_DEFINE(ason_vec_##StructType, StructType) \
    static inline void ason_encode_vec_struct_##StructType(ason_buf_t* buf, const void* base, size_t offset) { \
        const ason_vec_##StructType* v = (const ason_vec_##StructType*)((const char*)base + offset); \
        ason_buf_push(buf, '['); \
        for (size_t i = 0; i < v->len; i++) { \
            if (i > 0) ason_buf_push(buf, ','); \
            ason_encode_struct(buf, &v->data[i], &StructType##_ason_desc); \
        } \
        ason_buf_push(buf, ']'); \
    } \
    static inline ason_err_t ason_decode_vec_struct_##StructType(const char** pos, const char* end, void* base, size_t offset) { \
        ason_skip_ws(pos, end); \
        if (*pos >= end || **pos != '[') return ASON_ERR_SYNTAX; \
        (*pos)++; \
        ason_vec_##StructType* v = (ason_vec_##StructType*)((char*)base + offset); \
        *v = ason_vec_##StructType##_new(); \
        bool first = true; \
        while (1) { \
            ason_skip_ws(pos, end); \
            if (*pos >= end || **pos == ']') { (*pos)++; break; } \
            if (!first) { \
                if (**pos == ',') { (*pos)++; ason_skip_ws(pos, end); if (*pos < end && **pos == ']') { (*pos)++; break; } } \
                else break; \
            } \
            first = false; \
            /* Ensure capacity and load directly into vector slot */ \
            if (v->len >= v->cap) { \
                v->cap = v->cap ? v->cap * 2 : 4; \
                StructType* tmp = (StructType*)realloc(v->data, v->cap * sizeof(StructType)); \
                if (!tmp) { \
                    for (size_t k = 0; k < v->len; k++) ason_free_struct_fields(&v->data[k], &StructType##_ason_desc); \
                    ason_vec_##StructType##_free(v); \
                    return ASON_ERR_ALLOC; \
                } \
                v->data = tmp; \
            } \
            memset(&v->data[v->len], 0, sizeof(StructType)); \
            ason_err_t err = ason_decode_struct(pos, end, &v->data[v->len], &StructType##_ason_desc); \
            if (err != ASON_OK) { \
                ason_free_struct_fields(&v->data[v->len], &StructType##_ason_desc); \
                for (size_t k = 0; k < v->len; k++) ason_free_struct_fields(&v->data[k], &StructType##_ason_desc); \
                ason_vec_##StructType##_free(v); \
                return err; \
            } \
            v->len++; \
        } \
        return ASON_OK; \
    }

/* Field macro for vec-of-struct */
#define ASON_FIELD_VEC_STRUCT(StructType, member, fname, ElemType) \
    { fname, sizeof(fname) - 1, ASON_STRUCT, \
      offsetof(StructType, member), NULL, &ElemType##_ason_desc, \
      (ason_encode_fn)ason_encode_vec_struct_##ElemType, \
      (ason_decode_fn)ason_decode_vec_struct_##ElemType }

/* ============================================================================
 * Binary serialization / deserialization (ASON-BIN)
 * Little-endian fixed-width encoding, zero-copy string_view for reads.
 * ============================================================================
 */

/* Binary write helpers — append into ason_buf_t */
ason_inline void ason_bin_write_u8(ason_buf_t* buf, uint8_t v) {
    ason_buf_push(buf, (char)v);
}
ason_inline void ason_bin_write_u16(ason_buf_t* buf, uint16_t v) {
    uint8_t tmp[2]; memcpy(tmp, &v, 2); ason_buf_append(buf, (char*)tmp, 2);
}
ason_inline void ason_bin_write_u32(ason_buf_t* buf, uint32_t v) {
    uint8_t tmp[4]; memcpy(tmp, &v, 4); ason_buf_append(buf, (char*)tmp, 4);
}
ason_inline void ason_bin_write_u64(ason_buf_t* buf, uint64_t v) {
    uint8_t tmp[8]; memcpy(tmp, &v, 8); ason_buf_append(buf, (char*)tmp, 8);
}
ason_inline void ason_bin_write_f32(ason_buf_t* buf, float v) {
    uint32_t u; memcpy(&u, &v, 4); ason_bin_write_u32(buf, u);
}
ason_inline void ason_bin_write_f64(ason_buf_t* buf, double v) {
    uint64_t u; memcpy(&u, &v, 8); ason_bin_write_u64(buf, u);
}
ason_inline void ason_bin_write_str(ason_buf_t* buf, const char* s, size_t len) {
    ason_bin_write_u32(buf, (uint32_t)len);
    ason_buf_append(buf, s, len);
}
ason_inline void ason_bin_write_ason_string(ason_buf_t* buf, const ason_string_t* s) {
    if (s && s->data) {
        ason_bin_write_str(buf, s->data, s->len);
    } else {
        ason_bin_write_u32(buf, 0);
    }
}

/* Binary read helpers — reads from cursor, advances pos, zero-alloc for strings */
ason_inline ason_err_t ason_bin_read_u8(const char** pos, const char* end, uint8_t* out) {
    if (*pos + 1 > end) return ASON_ERR_BUFFER_OVERFLOW;
    *out = (uint8_t)**pos; (*pos)++;
    return ASON_OK;
}
ason_inline ason_err_t ason_bin_read_u16(const char** pos, const char* end, uint16_t* out) {
    if (*pos + 2 > end) return ASON_ERR_BUFFER_OVERFLOW;
    memcpy(out, *pos, 2); (*pos) += 2;
    return ASON_OK;
}
ason_inline ason_err_t ason_bin_read_u32(const char** pos, const char* end, uint32_t* out) {
    if (*pos + 4 > end) return ASON_ERR_BUFFER_OVERFLOW;
    memcpy(out, *pos, 4); (*pos) += 4;
    return ASON_OK;
}
ason_inline ason_err_t ason_bin_read_u64(const char** pos, const char* end, uint64_t* out) {
    if (*pos + 8 > end) return ASON_ERR_BUFFER_OVERFLOW;
    memcpy(out, *pos, 8); (*pos) += 8;
    return ASON_OK;
}
ason_inline ason_err_t ason_bin_read_f32(const char** pos, const char* end, float* out) {
    uint32_t u;
    if (*pos + 4 > end) return ASON_ERR_BUFFER_OVERFLOW;
    memcpy(&u, *pos, 4); (*pos) += 4;
    memcpy(out, &u, 4);
    return ASON_OK;
}
ason_inline ason_err_t ason_bin_read_f64(const char** pos, const char* end, double* out) {
    uint64_t u;
    if (*pos + 8 > end) return ASON_ERR_BUFFER_OVERFLOW;
    memcpy(&u, *pos, 8); (*pos) += 8;
    memcpy(out, &u, 8);
    return ASON_OK;
}
/* Zero-copy: points directly into the source buffer — caller must keep source alive */
ason_inline ason_err_t ason_bin_read_str_view(const char** pos, const char* end,
                                               const char** out_data, size_t* out_len) {
    uint32_t len;
    if (*pos + 4 > end) return ASON_ERR_BUFFER_OVERFLOW;
    memcpy(&len, *pos, 4); (*pos) += 4;
    if (*pos + len > end) return ASON_ERR_BUFFER_OVERFLOW;
    *out_data = *pos;
    *out_len = len;
    (*pos) += len;
    return ASON_OK;
}
/* Heap-allocating variant — caller must free */
ason_inline ason_err_t ason_bin_read_str_alloc(const char** pos, const char* end,
                                                char** out) {
    const char* d; size_t l;
    ason_err_t e = ason_bin_read_str_view(pos, end, &d, &l);
    if (e != ASON_OK) return e;
    char* s = (char*)malloc(l + 1);
    if (!s) return ASON_ERR_ALLOC;
    memcpy(s, d, l); s[l] = '\0';
    *out = s;
    return ASON_OK;
}
/* Read into ason_string_t — zero-copy (points into source buffer) */
ason_inline ason_err_t ason_bin_read_ason_string(const char** pos, const char* end,
                                                  ason_string_t* out) {
    const char* d; size_t l;
    ason_err_t e = ason_bin_read_str_view(pos, end, &d, &l);
    if (e != ASON_OK) return e;
    out->data = (char*)(uintptr_t)d;   /* cast away const: zero-copy, caller keeps source alive */
    out->len = l;
    return ASON_OK;
}

/* Type-specific binary dump field helpers — same signature as ason_encode_fn */
void ason_bin_encode_bool  (ason_buf_t* buf, const void* base, size_t off);
void ason_bin_encode_i8    (ason_buf_t* buf, const void* base, size_t off);
void ason_bin_encode_i16   (ason_buf_t* buf, const void* base, size_t off);
void ason_bin_encode_i32   (ason_buf_t* buf, const void* base, size_t off);
void ason_bin_encode_i64   (ason_buf_t* buf, const void* base, size_t off);
void ason_bin_encode_u8    (ason_buf_t* buf, const void* base, size_t off);
void ason_bin_encode_u16   (ason_buf_t* buf, const void* base, size_t off);
void ason_bin_encode_u32   (ason_buf_t* buf, const void* base, size_t off);
void ason_bin_encode_u64   (ason_buf_t* buf, const void* base, size_t off);
void ason_bin_encode_f32   (ason_buf_t* buf, const void* base, size_t off);
void ason_bin_encode_f64   (ason_buf_t* buf, const void* base, size_t off);
void ason_bin_encode_str   (ason_buf_t* buf, const void* base, size_t off);
void ason_bin_encode_vec_i64  (ason_buf_t* buf, const void* base, size_t off);
void ason_bin_encode_vec_u64  (ason_buf_t* buf, const void* base, size_t off);
void ason_bin_encode_vec_f64  (ason_buf_t* buf, const void* base, size_t off);
void ason_bin_encode_vec_str  (ason_buf_t* buf, const void* base, size_t off);
void ason_bin_encode_vec_bool (ason_buf_t* buf, const void* base, size_t off);

ason_err_t ason_bin_decode_bool  (const char** pos, const char* end, void* base, size_t off);
ason_err_t ason_bin_decode_i8    (const char** pos, const char* end, void* base, size_t off);
ason_err_t ason_bin_decode_i16   (const char** pos, const char* end, void* base, size_t off);
ason_err_t ason_bin_decode_i32   (const char** pos, const char* end, void* base, size_t off);
ason_err_t ason_bin_decode_i64   (const char** pos, const char* end, void* base, size_t off);
ason_err_t ason_bin_decode_u8    (const char** pos, const char* end, void* base, size_t off);
ason_err_t ason_bin_decode_u16   (const char** pos, const char* end, void* base, size_t off);
ason_err_t ason_bin_decode_u32   (const char** pos, const char* end, void* base, size_t off);
ason_err_t ason_bin_decode_u64   (const char** pos, const char* end, void* base, size_t off);
ason_err_t ason_bin_decode_f32   (const char** pos, const char* end, void* base, size_t off);
ason_err_t ason_bin_decode_f64   (const char** pos, const char* end, void* base, size_t off);
ason_err_t ason_bin_decode_str   (const char** pos, const char* end, void* base, size_t off);
ason_err_t ason_bin_decode_vec_i64  (const char** pos, const char* end, void* base, size_t off);
ason_err_t ason_bin_decode_vec_u64  (const char** pos, const char* end, void* base, size_t off);
ason_err_t ason_bin_decode_vec_f64  (const char** pos, const char* end, void* base, size_t off);
ason_err_t ason_bin_decode_vec_str  (const char** pos, const char* end, void* base, size_t off);
ason_err_t ason_bin_decode_vec_bool (const char** pos, const char* end, void* base, size_t off);

/* Generic struct binary dump/load using the existing descriptor */
void       ason_bin_encode_struct(ason_buf_t* buf, const void* obj, const ason_desc_t* desc);
ason_err_t ason_bin_decode_struct(const char** pos, const char* end, void* obj, const ason_desc_t* desc);

/* Convenience binary-fn selectors (mirrors ASON_DUMP_FN / ASON_LOAD_FN) */
#define ASON_BIN_DUMP_FN(t) ason_bin_encode_##t
#define ASON_BIN_LOAD_FN(t) ason_bin_decode_##t
#define ason_bin_encode__Bool ason_bin_encode_bool
#define ason_bin_decode__Bool ason_bin_decode_bool

/* ASON_FIELDS_BIN adds binary dump/load to an already-declared ASON_FIELDS struct.
 * Use after ASON_FIELDS to inject ason_encode_bin_<T>, ason_decode_bin_<T>, etc. */
#define ASON_FIELDS_BIN(StructType, nfields) \
    static inline ason_buf_t ason_encode_bin_##StructType(const StructType* obj) { \
        ason_buf_t buf = ason_buf_new(nfields * 16); \
        ason_bin_encode_struct(&buf, obj, &StructType##_ason_desc); \
        return buf; \
    } \
    static inline ason_buf_t ason_encode_bin_vec_##StructType(const StructType* arr, size_t count) { \
        ason_buf_t buf = ason_buf_new(count * nfields * 16 + 8); \
        uint32_t n = (uint32_t)count; \
        ason_bin_write_u32(&buf, n); \
        for (size_t i = 0; i < count; i++) { \
            ason_bin_encode_struct(&buf, &arr[i], &StructType##_ason_desc); \
        } \
        return buf; \
    } \
    static inline ason_err_t ason_decode_bin_##StructType(const char* data, size_t len, StructType* out) { \
        const char* pos = data; \
        const char* end = data + len; \
        return ason_bin_decode_struct(&pos, end, out, &StructType##_ason_desc); \
    } \
    static inline ason_err_t ason_decode_bin_vec_##StructType(const char* data, size_t len, \
                                                              StructType** out_arr, size_t* out_count) { \
        const char* pos = data; \
        const char* end = data + len; \
        uint32_t count; \
        ason_err_t err = ason_bin_read_u32(&pos, end, &count); \
        if (err != ASON_OK) return err; \
        StructType* arr = (StructType*)calloc(count, sizeof(StructType)); \
        if (!arr) return ASON_ERR_ALLOC; \
        for (uint32_t i = 0; i < count; i++) { \
            err = ason_bin_decode_struct(&pos, end, &arr[i], &StructType##_ason_desc); \
            if (err != ASON_OK) { \
                for (uint32_t k = 0; k <= i; k++) ason_free_struct_fields(&arr[k], &StructType##_ason_desc); \
                free(arr); return err; \
            } \
        } \
        *out_arr = arr; \
        *out_count = count; \
        return ASON_OK; \
    }

#ifdef __cplusplus
}
#endif

#endif /* ASON_H */
