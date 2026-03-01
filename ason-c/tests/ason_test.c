#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <math.h>
#include "ason.h"

static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name) do { printf("  " #name "... "); fflush(stdout); } while(0)
#define PASS() do { printf("OK\n"); tests_passed++; } while(0)
#define FAIL(msg) do { printf("FAIL: %s\n", msg); tests_failed++; return; } while(0)
#define ASSERT_EQ_I(a, b) do { if ((a) != (b)) { FAIL(#a " != " #b); } } while(0)
#define ASSERT_EQ_U(a, b) do { if ((a) != (b)) { FAIL(#a " != " #b); } } while(0)
#define ASSERT_EQ_S(a, b) do { if (strcmp(a, b) != 0) { FAIL(#a " != " #b); } } while(0)
#define ASSERT_TRUE(x) do { if (!(x)) { FAIL(#x " is false"); } } while(0)
#define ASSERT_FALSE(x) do { if (x) { FAIL(#x " is true"); } } while(0)
#define ASSERT_NEAR(a, b, eps) do { if (fabs((a)-(b)) > (eps)) { FAIL(#a " !~ " #b); } } while(0)

/* ===========================================================================
 * Test structs
 * =========================================================================== */

typedef struct { int64_t id; ason_string_t name; bool active; } TSimple;
ASON_FIELDS(TSimple, 3,
    ASON_FIELD(TSimple, id,     "id",     i64),
    ASON_FIELD(TSimple, name,   "name",   str),
    ASON_FIELD(TSimple, active, "active", bool))

static void free_tsimple(TSimple* s) { ason_string_free(&s->name); }

typedef struct { int64_t id; ason_opt_str label; ason_opt_i64 count; } TWithOptional;
ASON_FIELDS(TWithOptional, 3,
    ASON_FIELD(TWithOptional, id,    "id",    i64),
    ASON_FIELD(TWithOptional, label, "label", opt_str),
    ASON_FIELD(TWithOptional, count, "count", opt_i64))

static void free_twithoptional(TWithOptional* w) {
    if (w->label.has_value) ason_string_free(&w->label.value);
}

typedef struct { ason_string_t name; ason_vec_i64 nums; } TWithVec;
ASON_FIELDS(TWithVec, 2,
    ASON_FIELD(TWithVec, name, "name", str),
    ASON_FIELD(TWithVec, nums, "nums", vec_i64))

static void free_twithvec(TWithVec* w) { ason_string_free(&w->name); ason_vec_i64_free(&w->nums); }

typedef struct { ason_string_t val; int64_t n; } TInner;
ASON_FIELDS(TInner, 2,
    ASON_FIELD(TInner, val, "val", str),
    ASON_FIELD(TInner, n,   "n",   i64))

typedef struct { ason_string_t label; TInner inner; } TOuter;
ASON_FIELDS(TOuter, 2,
    ASON_FIELD(TOuter, label, "label", str),
    ASON_FIELD_STRUCT(TOuter, inner, "inner", &TInner_ason_desc))

static void free_touter(TOuter* o) { ason_string_free(&o->label); ason_string_free(&o->inner.val); }

typedef struct { ason_string_t name; ason_map_si attrs; } TWithMap;
ASON_FIELDS(TWithMap, 2,
    ASON_FIELD(TWithMap, name,  "name",  str),
    ASON_FIELD(TWithMap, attrs, "attrs", map_si))

static void free_twithmap(TWithMap* m) {
    ason_string_free(&m->name);
    for (size_t i = 0; i < m->attrs.len; i++) ason_string_free(&m->attrs.data[i].key);
    ason_map_si_free(&m->attrs);
}

typedef struct { double a; double b; float c; } TFloats;
ASON_FIELDS(TFloats, 3,
    ASON_FIELD(TFloats, a, "a", f64),
    ASON_FIELD(TFloats, b, "b", f64),
    ASON_FIELD(TFloats, c, "c", f32))

typedef struct {
    int8_t i8; int16_t i16; int32_t i32; int64_t i64v;
    uint8_t u8; uint16_t u16; uint32_t u32; uint64_t u64v;
} TAllNums;
ASON_FIELDS(TAllNums, 8,
    ASON_FIELD(TAllNums, i8,  "i8",  i8),
    ASON_FIELD(TAllNums, i16, "i16", i16),
    ASON_FIELD(TAllNums, i32, "i32", i32),
    ASON_FIELD(TAllNums, i64v,"i64", i64),
    ASON_FIELD(TAllNums, u8,  "u8",  u8),
    ASON_FIELD(TAllNums, u16, "u16", u16),
    ASON_FIELD(TAllNums, u32, "u32", u32),
    ASON_FIELD(TAllNums, u64v,"u64", u64))

/* Deep nesting: DeepA -> DeepB -> DeepC */
typedef struct { ason_string_t name; int64_t val; } TDeepA;
ASON_FIELDS(TDeepA, 2, ASON_FIELD(TDeepA, name, "name", str), ASON_FIELD(TDeepA, val, "val", i64))
ASON_VEC_STRUCT_DEFINE(TDeepA)

typedef struct { ason_string_t label; ason_vec_TDeepA items; } TDeepB;
ASON_FIELDS(TDeepB, 2,
    ASON_FIELD(TDeepB, label, "label", str),
    ASON_FIELD_VEC_STRUCT(TDeepB, items, "items", TDeepA))
ASON_VEC_STRUCT_DEFINE(TDeepB)

typedef struct { ason_string_t title; ason_vec_TDeepB groups; } TDeepC;
ASON_FIELDS(TDeepC, 2,
    ASON_FIELD(TDeepC, title, "title", str),
    ASON_FIELD_VEC_STRUCT(TDeepC, groups, "groups", TDeepB))

typedef struct { ason_vec_vec_i64 matrix; } TNestedVec;
ASON_FIELDS(TNestedVec, 1, ASON_FIELD(TNestedVec, matrix, "matrix", vec_vec_i64))

typedef struct { ason_string_t val; } TStringOnly;
ASON_FIELDS(TStringOnly, 1, ASON_FIELD(TStringOnly, val, "val", str))

typedef struct { ason_vec_bool flags; } TWithBoolVec;
ASON_FIELDS(TWithBoolVec, 1, ASON_FIELD(TWithBoolVec, flags, "flags", vec_bool))

typedef struct { ason_vec_i64 nums; } TWithIntVec;
ASON_FIELDS(TWithIntVec, 1, ASON_FIELD(TWithIntVec, nums, "nums", vec_i64))

typedef struct { ason_vec_str tags; } TWithStrVec;
ASON_FIELDS(TWithStrVec, 1, ASON_FIELD(TWithStrVec, tags, "tags", vec_str))

/* ===========================================================================
 * Tests
 * =========================================================================== */

void test_simple_roundtrip(void) {
    TEST(simple_roundtrip);
    TSimple s = {42, ason_string_from("Alice"), true};
    ason_buf_t buf = ason_encode_TSimple(&s);
    TSimple s2 = {0};
    ason_err_t err = ason_decode_TSimple(buf.data, buf.len, &s2);
    ASSERT_TRUE(err == ASON_OK);
    ASSERT_EQ_I(s2.id, 42);
    ASSERT_EQ_S(s2.name.data, "Alice");
    ASSERT_TRUE(s2.active);
    ason_buf_free(&buf); free_tsimple(&s); free_tsimple(&s2);
    PASS();
}

void test_typed_roundtrip(void) {
    TEST(typed_roundtrip);
    TSimple s = {1, ason_string_from("Bob"), false};
    ason_buf_t buf = ason_encode_typed_TSimple(&s);
    ASSERT_TRUE(strstr(buf.data, "id:int") != NULL || strstr(buf.data, "id:i64") != NULL);
    TSimple s2 = {0};
    ason_err_t err = ason_decode_TSimple(buf.data, buf.len, &s2);
    ASSERT_TRUE(err == ASON_OK);
    ASSERT_EQ_I(s2.id, 1);
    ASSERT_EQ_S(s2.name.data, "Bob");
    ASSERT_FALSE(s2.active);
    ason_buf_free(&buf); free_tsimple(&s); free_tsimple(&s2);
    PASS();
}

void test_vec_roundtrip(void) {
    TEST(vec_roundtrip);
    TSimple vec[] = {{1, ason_string_from("A"), true},
                     {2, ason_string_from("B"), false},
                     {3, ason_string_from("C"), true}};
    ason_buf_t buf = ason_encode_vec_TSimple(vec, 3);
    TSimple* vec2 = NULL; size_t n = 0;
    ason_err_t err = ason_decode_vec_TSimple(buf.data, buf.len, &vec2, &n);
    ASSERT_TRUE(err == ASON_OK);
    ASSERT_EQ_U(n, 3u);
    ASSERT_EQ_I(vec2[0].id, 1);
    ASSERT_EQ_S(vec2[1].name.data, "B");
    ASSERT_FALSE(vec2[1].active);
    ason_buf_free(&buf);
    for (size_t i = 0; i < 3; i++) free_tsimple(&vec[i]);
    for (size_t i = 0; i < n; i++) free_tsimple(&vec2[i]);
    free(vec2);
    PASS();
}

void test_vec_typed_roundtrip(void) {
    TEST(vec_typed_roundtrip);
    TSimple vec[] = {{1, ason_string_from("A"), true}};
    ason_buf_t buf = ason_encode_typed_vec_TSimple(vec, 1);
    ASSERT_TRUE(strstr(buf.data, "id:") != NULL);
    TSimple* vec2 = NULL; size_t n = 0;
    ason_err_t err = ason_decode_vec_TSimple(buf.data, buf.len, &vec2, &n);
    ASSERT_TRUE(err == ASON_OK);
    ASSERT_EQ_U(n, 1u);
    ASSERT_EQ_S(vec2[0].name.data, "A");
    ason_buf_free(&buf);
    free_tsimple(&vec[0]);
    free_tsimple(&vec2[0]); free(vec2);
    PASS();
}

void test_optional_present(void) {
    TEST(optional_present);
    const char* input = "{id,label,count}:(1,hello,42)";
    TWithOptional r = {0};
    ason_err_t err = ason_decode_TWithOptional(input, strlen(input), &r);
    ASSERT_TRUE(err == ASON_OK);
    ASSERT_EQ_I(r.id, 1);
    ASSERT_TRUE(r.label.has_value);
    ASSERT_EQ_S(r.label.value.data, "hello");
    ASSERT_TRUE(r.count.has_value);
    ASSERT_EQ_I(r.count.value, 42);
    free_twithoptional(&r);
    PASS();
}

void test_optional_absent(void) {
    TEST(optional_absent);
    const char* input = "{id,label,count}:(1,,)";
    TWithOptional r = {0};
    ason_err_t err = ason_decode_TWithOptional(input, strlen(input), &r);
    ASSERT_TRUE(err == ASON_OK);
    ASSERT_EQ_I(r.id, 1);
    ASSERT_FALSE(r.label.has_value);
    ASSERT_FALSE(r.count.has_value);
    free_twithoptional(&r);
    PASS();
}

void test_optional_dump(void) {
    TEST(optional_dump);
    TWithOptional w = {1, {true, ason_string_from("hi")}, {false, 0}};
    ason_buf_t buf = ason_encode_TWithOptional(&w);
    TWithOptional w2 = {0};
    ason_err_t err = ason_decode_TWithOptional(buf.data, buf.len, &w2);
    ASSERT_TRUE(err == ASON_OK);
    ASSERT_EQ_I(w2.id, 1);
    ASSERT_TRUE(w2.label.has_value);
    ASSERT_EQ_S(w2.label.value.data, "hi");
    ASSERT_FALSE(w2.count.has_value);
    ason_buf_free(&buf); free_twithoptional(&w); free_twithoptional(&w2);
    PASS();
}

void test_vec_field(void) {
    TEST(vec_field);
    const char* input = "{name,nums}:(test,[1,2,3,4,5])";
    TWithVec r = {0};
    ason_err_t err = ason_decode_TWithVec(input, strlen(input), &r);
    ASSERT_TRUE(err == ASON_OK);
    ASSERT_EQ_S(r.name.data, "test");
    ASSERT_EQ_U(r.nums.len, 5u);
    ASSERT_EQ_I(r.nums.data[0], 1);
    ASSERT_EQ_I(r.nums.data[4], 5);
    free_twithvec(&r);
    PASS();
}

void test_empty_vec(void) {
    TEST(empty_vec);
    const char* input = "{name,nums}:(test,[])";
    TWithVec r = {0};
    ason_err_t err = ason_decode_TWithVec(input, strlen(input), &r);
    ASSERT_TRUE(err == ASON_OK);
    ASSERT_EQ_S(r.name.data, "test");
    ASSERT_EQ_U(r.nums.len, 0u);
    ason_buf_t buf = ason_encode_TWithVec(&r);
    TWithVec r2 = {0};
    err = ason_decode_TWithVec(buf.data, buf.len, &r2);
    ASSERT_TRUE(err == ASON_OK);
    ASSERT_EQ_U(r2.nums.len, 0u);
    ason_buf_free(&buf); free_twithvec(&r); free_twithvec(&r2);
    PASS();
}

void test_nested_struct(void) {
    TEST(nested_struct);
    const char* input = "{label,inner:{val,n}}:(hello,(world,42))";
    TOuter r = {0};
    ason_err_t err = ason_decode_TOuter(input, strlen(input), &r);
    ASSERT_TRUE(err == ASON_OK);
    ASSERT_EQ_S(r.label.data, "hello");
    ASSERT_EQ_S(r.inner.val.data, "world");
    ASSERT_EQ_I(r.inner.n, 42);
    free_touter(&r);
    PASS();
}

void test_nested_roundtrip(void) {
    TEST(nested_roundtrip);
    TOuter o = {ason_string_from("test"), {ason_string_from("value"), 99}};
    ason_buf_t buf = ason_encode_TOuter(&o);
    TOuter o2 = {0};
    ason_err_t err = ason_decode_TOuter(buf.data, buf.len, &o2);
    ASSERT_TRUE(err == ASON_OK);
    ASSERT_EQ_S(o2.label.data, "test");
    ASSERT_EQ_S(o2.inner.val.data, "value");
    ASSERT_EQ_I(o2.inner.n, 99);
    ason_buf_free(&buf); free_touter(&o); free_touter(&o2);
    PASS();
}

void test_map_field(void) {
    TEST(map_field);
    const char* input = "{name,attrs}:(Alice,[(age,30),(score,95)])";
    TWithMap r = {0};
    ason_err_t err = ason_decode_TWithMap(input, strlen(input), &r);
    ASSERT_TRUE(err == ASON_OK);
    ASSERT_EQ_S(r.name.data, "Alice");
    ASSERT_EQ_U(r.attrs.len, 2u);
    /* find age */
    bool found_age = false, found_score = false;
    for (size_t i = 0; i < r.attrs.len; i++) {
        if (strcmp(r.attrs.data[i].key.data, "age") == 0) { ASSERT_EQ_I(r.attrs.data[i].val, 30); found_age = true; }
        if (strcmp(r.attrs.data[i].key.data, "score") == 0) { ASSERT_EQ_I(r.attrs.data[i].val, 95); found_score = true; }
    }
    ASSERT_TRUE(found_age); ASSERT_TRUE(found_score);
    free_twithmap(&r);
    PASS();
}

void test_map_roundtrip(void) {
    TEST(map_roundtrip);
    TWithMap m = {ason_string_from("Bob"), ason_map_si_new()};
    ason_map_si_entry_t e1 = {ason_string_from("x"), 1};
    ason_map_si_entry_t e2 = {ason_string_from("y"), 2};
    ason_map_si_push(&m.attrs, e1);
    ason_map_si_push(&m.attrs, e2);
    ason_buf_t buf = ason_encode_TWithMap(&m);
    TWithMap m2 = {0};
    ason_err_t err = ason_decode_TWithMap(buf.data, buf.len, &m2);
    ASSERT_TRUE(err == ASON_OK);
    ASSERT_EQ_S(m2.name.data, "Bob");
    ASSERT_EQ_U(m2.attrs.len, 2u);
    ason_buf_free(&buf); free_twithmap(&m); free_twithmap(&m2);
    PASS();
}

void test_quoted_string(void) {
    TEST(quoted_string);
    const char* input = "{id,name,active}:(1,\"hello world\",true)";
    TSimple r = {0};
    ason_err_t err = ason_decode_TSimple(input, strlen(input), &r);
    ASSERT_TRUE(err == ASON_OK);
    ASSERT_EQ_S(r.name.data, "hello world");
    free_tsimple(&r);
    PASS();
}

void test_escape_sequences(void) {
    TEST(escape_sequences);
    TStringOnly s = {ason_string_from("say \"hi\", then (wave)\tnewline\nend")};
    ason_buf_t buf = ason_encode_TStringOnly(&s);
    TStringOnly s2 = {0};
    ason_err_t err = ason_decode_TStringOnly(buf.data, buf.len, &s2);
    ASSERT_TRUE(err == ASON_OK);
    ASSERT_EQ_S(s.val.data, s2.val.data);
    ason_buf_free(&buf); ason_string_free(&s.val); ason_string_free(&s2.val);
    PASS();
}

void test_string_needs_quoting(void) {
    TEST(string_needs_quoting);
    TStringOnly s1 = {ason_string_from("hello,world")};
    ason_buf_t buf1 = ason_encode_TStringOnly(&s1);
    ASSERT_TRUE(strstr(buf1.data, "\"hello,world\"") != NULL);
    ason_buf_free(&buf1); ason_string_free(&s1.val);

    TStringOnly s2 = {ason_string_from("true")};
    ason_buf_t buf2 = ason_encode_TStringOnly(&s2);
    ASSERT_TRUE(strstr(buf2.data, "\"true\"") != NULL);
    ason_buf_free(&buf2); ason_string_free(&s2.val);

    TStringOnly s3 = {ason_string_from("12345")};
    ason_buf_t buf3 = ason_encode_TStringOnly(&s3);
    ASSERT_TRUE(strstr(buf3.data, "\"12345\"") != NULL);
    ason_buf_free(&buf3); ason_string_free(&s3.val);
    PASS();
}

void test_floats(void) {
    TEST(floats);
    TFloats f = {3.14, -0.5, 100.0f};
    ason_buf_t buf = ason_encode_TFloats(&f);
    TFloats f2 = {0};
    ason_err_t err = ason_decode_TFloats(buf.data, buf.len, &f2);
    ASSERT_TRUE(err == ASON_OK);
    ASSERT_NEAR(f2.a, 3.14, 0.001);
    ASSERT_NEAR(f2.b, -0.5, 0.001);
    ASSERT_NEAR(f2.c, 100.0, 0.001);
    ason_buf_free(&buf);
    PASS();
}

void test_integer_valued_float(void) {
    TEST(integer_valued_float);
    TFloats f = {42.0, 0.0, -7.0f};
    ason_buf_t buf = ason_encode_TFloats(&f);
    ASSERT_TRUE(strstr(buf.data, "42.0") != NULL);
    TFloats f2 = {0};
    ason_err_t err = ason_decode_TFloats(buf.data, buf.len, &f2);
    ASSERT_TRUE(err == ASON_OK);
    ASSERT_NEAR(f2.a, 42.0, 0.001);
    ason_buf_free(&buf);
    PASS();
}

void test_negative_numbers(void) {
    TEST(negative_numbers);
    TAllNums n = {0};
    n.i8 = -128; n.i16 = -32768; n.i32 = -2147483647-1; n.i64v = -9223372036854775807LL;
    ason_buf_t buf = ason_encode_TAllNums(&n);
    TAllNums n2 = {0};
    ason_err_t err = ason_decode_TAllNums(buf.data, buf.len, &n2);
    ASSERT_TRUE(err == ASON_OK);
    ASSERT_EQ_I(n2.i8, -128);
    ASSERT_EQ_I(n2.i16, -32768);
    ASSERT_EQ_I(n2.i64v, -9223372036854775807LL);
    ason_buf_free(&buf);
    PASS();
}

void test_large_unsigned(void) {
    TEST(large_unsigned);
    TAllNums n = {0};
    n.u64v = 18446744073709551615ULL;
    n.u32 = 4294967295U;
    ason_buf_t buf = ason_encode_TAllNums(&n);
    TAllNums n2 = {0};
    ason_err_t err = ason_decode_TAllNums(buf.data, buf.len, &n2);
    ASSERT_TRUE(err == ASON_OK);
    ASSERT_EQ_U(n2.u64v, 18446744073709551615ULL);
    ASSERT_EQ_U(n2.u32, 4294967295U);
    ason_buf_free(&buf);
    PASS();
}

void test_deep_nesting(void) {
    TEST(deep_nesting);
    TDeepA a1 = {ason_string_from("a1"), 1};
    TDeepA a2 = {ason_string_from("a2"), 2};
    TDeepA b1 = {ason_string_from("b1"), 10};

    TDeepB g1 = {ason_string_from("group1"), ason_vec_TDeepA_new()};
    ason_vec_TDeepA_push(&g1.items, a1);
    ason_vec_TDeepA_push(&g1.items, a2);
    TDeepB g2 = {ason_string_from("group2"), ason_vec_TDeepA_new()};
    ason_vec_TDeepA_push(&g2.items, b1);

    TDeepC c = {ason_string_from("top"), ason_vec_TDeepB_new()};
    ason_vec_TDeepB_push(&c.groups, g1);
    ason_vec_TDeepB_push(&c.groups, g2);

    ason_buf_t buf = ason_encode_TDeepC(&c);
    TDeepC c2 = {0};
    ason_err_t err = ason_decode_TDeepC(buf.data, buf.len, &c2);
    ASSERT_TRUE(err == ASON_OK);
    ASSERT_EQ_S(c2.title.data, "top");
    ASSERT_EQ_U(c2.groups.len, 2u);
    ASSERT_EQ_S(c2.groups.data[0].label.data, "group1");
    ASSERT_EQ_U(c2.groups.data[0].items.len, 2u);
    ASSERT_EQ_S(c2.groups.data[0].items.data[1].name.data, "a2");
    ASSERT_EQ_I(c2.groups.data[1].items.data[0].val, 10);

    ason_buf_free(&buf);
    /* free c */
    ason_string_free(&c.title);
    for (size_t i = 0; i < c.groups.len; i++) {
        ason_string_free(&c.groups.data[i].label);
        for (size_t j = 0; j < c.groups.data[i].items.len; j++) ason_string_free(&c.groups.data[i].items.data[j].name);
        ason_vec_TDeepA_free(&c.groups.data[i].items);
    }
    ason_vec_TDeepB_free(&c.groups);
    /* free c2 */
    ason_string_free(&c2.title);
    for (size_t i = 0; i < c2.groups.len; i++) {
        ason_string_free(&c2.groups.data[i].label);
        for (size_t j = 0; j < c2.groups.data[i].items.len; j++) ason_string_free(&c2.groups.data[i].items.data[j].name);
        ason_vec_TDeepA_free(&c2.groups.data[i].items);
    }
    ason_vec_TDeepB_free(&c2.groups);
    PASS();
}

void test_nested_vec(void) {
    TEST(nested_vec);
    TNestedVec nv = {{0}};
    nv.matrix = ason_vec_vec_i64_new();
    ason_vec_i64 r1 = ason_vec_i64_new(); ason_vec_i64_push(&r1, 1); ason_vec_i64_push(&r1, 2); ason_vec_i64_push(&r1, 3);
    ason_vec_i64 r2 = ason_vec_i64_new(); ason_vec_i64_push(&r2, 4); ason_vec_i64_push(&r2, 5);
    ason_vec_i64 r3 = ason_vec_i64_new(); ason_vec_i64_push(&r3, 6);
    ason_vec_vec_i64_push(&nv.matrix, r1);
    ason_vec_vec_i64_push(&nv.matrix, r2);
    ason_vec_vec_i64_push(&nv.matrix, r3);

    ason_buf_t buf = ason_encode_TNestedVec(&nv);
    TNestedVec nv2 = {0};
    ason_err_t err = ason_decode_TNestedVec(buf.data, buf.len, &nv2);
    ASSERT_TRUE(err == ASON_OK);
    ASSERT_EQ_U(nv2.matrix.len, 3u);
    ASSERT_EQ_I(nv2.matrix.data[0].data[0], 1);
    ASSERT_EQ_I(nv2.matrix.data[0].data[2], 3);
    ASSERT_EQ_U(nv2.matrix.data[1].len, 2u);
    ASSERT_EQ_U(nv2.matrix.data[2].len, 1u);

    ason_buf_free(&buf);
    for (size_t i = 0; i < nv.matrix.len; i++) ason_vec_i64_free(&nv.matrix.data[i]);
    ason_vec_vec_i64_free(&nv.matrix);
    for (size_t i = 0; i < nv2.matrix.len; i++) ason_vec_i64_free(&nv2.matrix.data[i]);
    ason_vec_vec_i64_free(&nv2.matrix);
    PASS();
}

void test_comments(void) {
    TEST(comments);
    const char* input = "/* top-level comment */ {id,name,active}: /* inline */ (1,Alice,true)";
    TSimple r = {0};
    ason_err_t err = ason_decode_TSimple(input, strlen(input), &r);
    ASSERT_TRUE(err == ASON_OK);
    ASSERT_EQ_I(r.id, 1);
    ASSERT_EQ_S(r.name.data, "Alice");
    ASSERT_TRUE(r.active);
    free_tsimple(&r);
    PASS();
}

void test_whitespace(void) {
    TEST(whitespace);
    const char* input = "{ id , name , active } : ( 1 , Alice , true )";
    TSimple r = {0};
    ason_err_t err = ason_decode_TSimple(input, strlen(input), &r);
    ASSERT_TRUE(err == ASON_OK);
    ASSERT_EQ_I(r.id, 1);
    ASSERT_EQ_S(r.name.data, "Alice");
    ASSERT_TRUE(r.active);
    free_tsimple(&r);
    PASS();
}

void test_multiline(void) {
    TEST(multiline);
    const char* input = "[{id, name, active}]:\n  (1, Alice, true),\n  (2, Bob, false)";
    TSimple* vec = NULL; size_t n = 0;
    ason_err_t err = ason_decode_vec_TSimple(input, strlen(input), &vec, &n);
    ASSERT_TRUE(err == ASON_OK);
    ASSERT_EQ_U(n, 2u);
    ASSERT_EQ_S(vec[0].name.data, "Alice");
    ASSERT_FALSE(vec[1].active);
    for (size_t i = 0; i < n; i++) free_tsimple(&vec[i]);
    free(vec);
    PASS();
}

void test_typed_schema_parse(void) {
    TEST(typed_schema_parse);
    const char* input = "{id:int,name:str,active:bool}:(42,Hello,false)";
    TSimple r = {0};
    ason_err_t err = ason_decode_TSimple(input, strlen(input), &r);
    ASSERT_TRUE(err == ASON_OK);
    ASSERT_EQ_I(r.id, 42);
    ASSERT_EQ_S(r.name.data, "Hello");
    ASSERT_FALSE(r.active);
    free_tsimple(&r);
    PASS();
}

void test_schema_field_mismatch(void) {
    TEST(schema_field_mismatch);
    const char* input = "{id,extra_field,name,active}:(42,ignored,Hello,true)";
    TSimple r = {0};
    ason_err_t err = ason_decode_TSimple(input, strlen(input), &r);
    ASSERT_TRUE(err == ASON_OK);
    ASSERT_EQ_I(r.id, 42);
    ASSERT_EQ_S(r.name.data, "Hello");
    ASSERT_TRUE(r.active);
    free_tsimple(&r);
    PASS();
}

void test_unquoted_string_trim(void) {
    TEST(unquoted_string_trim);
    const char* input = "{id,name,active}:(1,  Alice  ,true)";
    TSimple r = {0};
    ason_err_t err = ason_decode_TSimple(input, strlen(input), &r);
    ASSERT_TRUE(err == ASON_OK);
    ASSERT_EQ_S(r.name.data, "Alice");
    free_tsimple(&r);
    PASS();
}

void test_bool_values(void) {
    TEST(bool_values);
    const char* input1 = "{id,name,active}:(1,A,true)";
    TSimple r1 = {0};
    ason_decode_TSimple(input1, strlen(input1), &r1);
    ASSERT_TRUE(r1.active);
    free_tsimple(&r1);

    const char* input2 = "{id,name,active}:(1,A,false)";
    TSimple r2 = {0};
    ason_decode_TSimple(input2, strlen(input2), &r2);
    ASSERT_FALSE(r2.active);
    free_tsimple(&r2);
    PASS();
}

void test_empty_optional_between_commas(void) {
    TEST(empty_optional_between_commas);
    const char* input = "{id,label,count}:(1,,42)";
    TWithOptional r = {0};
    ason_err_t err = ason_decode_TWithOptional(input, strlen(input), &r);
    ASSERT_TRUE(err == ASON_OK);
    ASSERT_EQ_I(r.id, 1);
    ASSERT_FALSE(r.label.has_value);
    ASSERT_TRUE(r.count.has_value);
    ASSERT_EQ_I(r.count.value, 42);
    free_twithoptional(&r);
    PASS();
}

void test_string_with_spaces(void) {
    TEST(string_with_spaces);
    const char* input = "{id,name,active}:(1,\"  spaces  \",true)";
    TSimple r = {0};
    ason_err_t err = ason_decode_TSimple(input, strlen(input), &r);
    ASSERT_TRUE(err == ASON_OK);
    ASSERT_EQ_S(r.name.data, "  spaces  ");
    free_tsimple(&r);
    PASS();
}

void test_error_handling(void) {
    TEST(error_handling);
    TSimple r = {0};
    ason_err_t err = ason_decode_TSimple("not valid ason", 14, &r);
    ASSERT_TRUE(err != ASON_OK);
    PASS();
}

void test_encode_vec_empty(void) {
    TEST(encode_vec_empty);
    ason_buf_t buf = ason_encode_vec_TSimple(NULL, 0);
    ASSERT_TRUE(strstr(buf.data, "[{id,name,active}]:") != NULL);
    ason_buf_free(&buf);
    PASS();
}

void test_leading_trailing_space_quoting(void) {
    TEST(leading_trailing_space_quoting);
    TStringOnly s1 = {ason_string_from(" leading")};
    ason_buf_t buf1 = ason_encode_TStringOnly(&s1);
    ASSERT_TRUE(strstr(buf1.data, "\" leading\"") != NULL);
    TStringOnly s1b = {0};
    ason_decode_TStringOnly(buf1.data, buf1.len, &s1b);
    ASSERT_EQ_S(s1b.val.data, " leading");
    ason_buf_free(&buf1); ason_string_free(&s1.val); ason_string_free(&s1b.val);

    TStringOnly s3 = {ason_string_from("trailing ")};
    ason_buf_t buf3 = ason_encode_TStringOnly(&s3);
    TStringOnly s4 = {0};
    ason_decode_TStringOnly(buf3.data, buf3.len, &s4);
    ASSERT_EQ_S(s4.val.data, "trailing ");
    ason_buf_free(&buf3); ason_string_free(&s3.val); ason_string_free(&s4.val);
    PASS();
}

void test_backslash_escape(void) {
    TEST(backslash_escape);
    TStringOnly s = {ason_string_from("path\\to\\file")};
    ason_buf_t buf = ason_encode_TStringOnly(&s);
    TStringOnly s2 = {0};
    ason_decode_TStringOnly(buf.data, buf.len, &s2);
    ASSERT_EQ_S(s2.val.data, "path\\to\\file");
    ason_buf_free(&buf); ason_string_free(&s.val); ason_string_free(&s2.val);
    PASS();
}

/* ===========================================================================
 * Format validation tests
 * =========================================================================== */

static const char* BAD_FMT  = "{id:int,name:str}:\n  (1,Alice),\n  (2,Bob),\n  (3,Carol)";
static const char* GOOD_FMT = "[{id:int,name:str}]:\n  (1,Alice),\n  (2,Bob),\n  (3,Carol)";

/* TFmtRow shares the same layout as TSimple-minus-active; use TSimple */
typedef struct { int64_t id; ason_string_t name; } TFmtRow;
ASON_FIELDS(TFmtRow, 2,
    ASON_FIELD(TFmtRow, id,   "id",   i64),
    ASON_FIELD(TFmtRow, name, "name", str))
ASON_FIELDS_BIN(TFmtRow, 2)

static void free_tfmtrow(TFmtRow* r) { ason_string_free(&r->name); }

void test_bad_format_as_vec(void) {
    TEST(bad_format_as_vec);
    TFmtRow* rows = NULL;
    size_t count = 0;
    ason_err_t err = ason_decode_vec_TFmtRow(BAD_FMT, strlen(BAD_FMT), &rows, &count);
    if (err == ASON_OK) {
        for (size_t i = 0; i < count; i++) free_tfmtrow(&rows[i]);
        free(rows);
        FAIL("should reject {schema}: format for vec");
    }
    PASS();
}

void test_bad_format_trailing_rows(void) {
    /* {schema}:(row1),(row2),... — single decode should fail on trailing content */
    TEST(bad_format_trailing_rows);
    TFmtRow r = {0};
    ason_err_t err = ason_decode_TFmtRow(BAD_FMT, strlen(BAD_FMT), &r);
    if (r.name.data) ason_string_free(&r.name);
    if (err == ASON_OK) {
        FAIL("should reject trailing tuples after single struct decode");
    }
    PASS();
}

void test_good_format_as_vec(void) {
    TEST(good_format_as_vec);
    TFmtRow* rows = NULL;
    size_t count = 0;
    ason_err_t err = ason_decode_vec_TFmtRow(GOOD_FMT, strlen(GOOD_FMT), &rows, &count);
    if (err != ASON_OK) { FAIL("should accept [{schema}]: format for vec"); }
    if (count != 3) {
        for (size_t i = 0; i < count; i++) free_tfmtrow(&rows[i]);
        free(rows);
        FAIL("expected 3 rows");
    }
    ASSERT_EQ_I(rows[0].id, 1);
    ASSERT_EQ_S(rows[0].name.data, "Alice");
    ASSERT_EQ_S(rows[2].name.data, "Carol");
    for (size_t i = 0; i < count; i++) free_tfmtrow(&rows[i]);
    free(rows);
    PASS();
}

void test_bad_format_extra_tuples(void) {
    /* Two tuples without [] wrapper — single decode should fail on trailing chars */
    TEST(bad_format_extra_tuples);
    const char* bad = "{id:int,name:str}:(10,Dave),(11,Eve)";
    TFmtRow r = {0};
    ason_err_t err = ason_decode_TFmtRow(bad, strlen(bad), &r);
    if (r.name.data) ason_string_free(&r.name);
    if (err == ASON_OK) { FAIL("should reject trailing tuple"); }
    PASS();
}

void test_good_format_single(void) {
    /* {schema}: (1, Alice) — single struct with one tuple: MUST succeed */
    TEST(good_format_single);
    const char* good = "{id:int,name:str}:(1,Alice)";
    TFmtRow r = {0};
    ason_err_t err = ason_decode_TFmtRow(good, strlen(good), &r);
    if (err != ASON_OK) { FAIL("should accept single struct with one tuple"); }
    ASSERT_EQ_I(r.id, 1);
    ASSERT_EQ_S(r.name.data, "Alice");
    free_tfmtrow(&r);
    PASS();
}

void test_good_format_vec_single(void) {
    /* [{schema}]: (1, Alice) — array schema with one tuple: MUST succeed */
    TEST(good_format_vec_single);
    const char* good = "[{id:int,name:str}]:(1,Alice)";
    TFmtRow* rows = NULL;
    size_t count = 0;
    ason_err_t err = ason_decode_vec_TFmtRow(good, strlen(good), &rows, &count);
    if (err != ASON_OK) { FAIL("should accept [{schema}]: with single tuple"); }
    if (count != 1) {
        for (size_t i = 0; i < count; i++) free_tfmtrow(&rows[i]);
        free(rows);
        FAIL("expected 1 row");
    }
    ASSERT_EQ_I(rows[0].id, 1);
    ASSERT_EQ_S(rows[0].name.data, "Alice");
    free_tfmtrow(&rows[0]);
    free(rows);
    PASS();
}

/* ===========================================================================
 * encodePretty -> decode roundtrip tests
 * =========================================================================== */

void test_pretty_simple_roundtrip(void) {
    TEST(pretty_simple_roundtrip);
    TSimple s1 = {42, ason_string_from("Alice"), true};
    ason_buf_t pretty = ason_encode_pretty_TSimple(&s1);
    TSimple s2 = {0};
    ason_err_t err = ason_decode_TSimple(pretty.data, pretty.len, &s2);
    ason_buf_free(&pretty);
    ason_string_free(&s1.name);
    if (err != ASON_OK) { FAIL("pretty output not decodable"); }
    ASSERT_EQ_I(s2.id, 42);
    ASSERT_EQ_S(s2.name.data, "Alice");
    ASSERT_TRUE(s2.active);
    free_tsimple(&s2);
    PASS();
}

void test_pretty_typed_roundtrip(void) {
    TEST(pretty_typed_roundtrip);
    TSimple s1 = {7, ason_string_from("Bob"), false};
    ason_buf_t pretty = ason_encode_pretty_typed_TSimple(&s1);
    /* typed output should contain type annotations */
    ASSERT_TRUE(strstr(pretty.data, "int") != NULL || strstr(pretty.data, "str") != NULL);
    TSimple s2 = {0};
    ason_err_t err = ason_decode_TSimple(pretty.data, pretty.len, &s2);
    ason_buf_free(&pretty);
    ason_string_free(&s1.name);
    if (err != ASON_OK) { FAIL("typed pretty output not decodable"); }
    ASSERT_EQ_I(s2.id, 7);
    ASSERT_EQ_S(s2.name.data, "Bob");
    ASSERT_FALSE(s2.active);
    free_tsimple(&s2);
    PASS();
}

void test_pretty_vec_roundtrip(void) {
    TEST(pretty_vec_roundtrip);
    TSimple arr[3] = {
        {1, ason_string_from("A"), true},
        {2, ason_string_from("B"), false},
        {3, ason_string_from("C"), true},
    };
    ason_buf_t pretty = ason_encode_pretty_vec_TSimple(arr, 3);
    ASSERT_TRUE(strchr(pretty.data, '\n') != NULL);
    TSimple* out = NULL;
    size_t cnt = 0;
    ason_err_t err = ason_decode_vec_TSimple(pretty.data, pretty.len, &out, &cnt);
    ason_buf_free(&pretty);
    for (int i = 0; i < 3; i++) ason_string_free(&arr[i].name);
    if (err != ASON_OK) { FAIL("pretty vec output not decodable"); }
    ASSERT_EQ_I((int64_t)cnt, 3);
    ASSERT_EQ_I(out[0].id, 1);
    ASSERT_EQ_S(out[0].name.data, "A");
    ASSERT_EQ_I(out[2].id, 3);
    for (size_t i = 0; i < cnt; i++) free_tsimple(&out[i]);
    free(out);
    PASS();
}

void test_pretty_nested_roundtrip(void) {
    TEST(pretty_nested_roundtrip);
    TOuter o = {ason_string_from("hello"), {ason_string_from("world"), 42}};
    ason_buf_t pretty = ason_encode_pretty_TOuter(&o);
    TOuter o2 = {0};
    ason_err_t err = ason_decode_TOuter(pretty.data, pretty.len, &o2);
    ason_buf_free(&pretty);
    free_touter(&o);
    if (err != ASON_OK) { FAIL("pretty nested output not decodable"); }
    ASSERT_EQ_S(o2.label.data, "hello");
    ASSERT_EQ_S(o2.inner.val.data, "world");
    ASSERT_EQ_I(o2.inner.n, 42);
    free_touter(&o2);
    PASS();
}

/* ===========================================================================
 * Binary roundtrip tests
 * =========================================================================== */

void test_binary_single_roundtrip(void) {
    TEST(binary_single_roundtrip);
    TFmtRow s1 = {99, ason_string_from("Zara")};
    ason_buf_t bin = ason_encode_bin_TFmtRow(&s1);
    TFmtRow s2 = {0};
    ason_err_t err = ason_decode_bin_TFmtRow(bin.data, bin.len, &s2);
    ason_buf_free(&bin);
    free_tfmtrow(&s1);
    if (err != ASON_OK) { FAIL("binary decode failed"); }
    ASSERT_EQ_I(s2.id, 99);
    ASSERT_EQ_S(s2.name.data, "Zara");
    free_tfmtrow(&s2);
    PASS();
}

void test_binary_vec_roundtrip(void) {
    TEST(binary_vec_roundtrip);
    TFmtRow arr[3] = {
        {1, ason_string_from("Alice")},
        {2, ason_string_from("Bob")},
        {3, ason_string_from("Carol")},
    };
    ason_buf_t bin = ason_encode_bin_vec_TFmtRow(arr, 3);
    TFmtRow* out = NULL;
    size_t cnt = 0;
    ason_err_t err = ason_decode_bin_vec_TFmtRow(bin.data, bin.len, &out, &cnt);
    ason_buf_free(&bin);
    for (int i = 0; i < 3; i++) free_tfmtrow(&arr[i]);
    if (err != ASON_OK) { FAIL("binary vec decode failed"); }
    ASSERT_EQ_I((int64_t)cnt, 3);
    ASSERT_EQ_I(out[0].id, 1);
    ASSERT_EQ_S(out[0].name.data, "Alice");
    ASSERT_EQ_I(out[2].id, 3);
    ASSERT_EQ_S(out[2].name.data, "Carol");
    for (size_t i = 0; i < cnt; i++) free_tfmtrow(&out[i]);
    free(out);
    PASS();
}

/* ===========================================================================
 * Typed encoding: primitive vec fields
 * =========================================================================== */

void test_encode_typed_bool_vec_field(void) {
    TEST(encode_typed_bool_vec_field);
    TWithBoolVec w = {0};
    ason_vec_bool_push(&w.flags, true);
    ason_vec_bool_push(&w.flags, false);
    ason_vec_bool_push(&w.flags, true);
    ason_buf_t buf = ason_encode_typed_TWithBoolVec(&w);
    ASSERT_TRUE(strstr(buf.data, "flags:[bool]") != NULL);
    TWithBoolVec w2 = {0};
    ason_err_t err = ason_decode_TWithBoolVec(buf.data, buf.len, &w2);
    ASSERT_TRUE(err == ASON_OK);
    ASSERT_EQ_U(w2.flags.len, 3u);
    ASSERT_TRUE(w2.flags.data[0]);
    ASSERT_FALSE(w2.flags.data[1]);
    ASSERT_TRUE(w2.flags.data[2]);
    ason_buf_free(&buf);
    ason_vec_bool_free(&w.flags);
    ason_vec_bool_free(&w2.flags);
    PASS();
}

void test_encode_typed_int_vec_field(void) {
    TEST(encode_typed_int_vec_field);
    TWithIntVec w = {0};
    ason_vec_i64_push(&w.nums, 10);
    ason_vec_i64_push(&w.nums, 20);
    ason_vec_i64_push(&w.nums, 30);
    ason_buf_t buf = ason_encode_typed_TWithIntVec(&w);
    ASSERT_TRUE(strstr(buf.data, "nums:[int]") != NULL);
    TWithIntVec w2 = {0};
    ason_err_t err = ason_decode_TWithIntVec(buf.data, buf.len, &w2);
    ASSERT_TRUE(err == ASON_OK);
    ASSERT_EQ_U(w2.nums.len, 3u);
    ASSERT_EQ_I(w2.nums.data[0], 10);
    ASSERT_EQ_I(w2.nums.data[2], 30);
    ason_buf_free(&buf);
    ason_vec_i64_free(&w.nums);
    ason_vec_i64_free(&w2.nums);
    PASS();
}

void test_encode_typed_str_vec_field(void) {
    TEST(encode_typed_str_vec_field);
    TWithStrVec w = {0};
    ason_vec_str_push(&w.tags, ason_string_from("a"));
    ason_vec_str_push(&w.tags, ason_string_from("b"));
    ason_buf_t buf = ason_encode_typed_TWithStrVec(&w);
    ASSERT_TRUE(strstr(buf.data, "tags:[str]") != NULL);
    TWithStrVec w2 = {0};
    ason_err_t err = ason_decode_TWithStrVec(buf.data, buf.len, &w2);
    ASSERT_TRUE(err == ASON_OK);
    ASSERT_EQ_U(w2.tags.len, 2u);
    ASSERT_EQ_S(w2.tags.data[0].data, "a");
    ASSERT_EQ_S(w2.tags.data[1].data, "b");
    ason_buf_free(&buf);
    for (size_t i = 0; i < w.tags.len; i++) ason_string_free(&w.tags.data[i]);
    ason_vec_str_free(&w.tags);
    for (size_t i = 0; i < w2.tags.len; i++) ason_string_free(&w2.tags.data[i]);
    ason_vec_str_free(&w2.tags);
    PASS();
}

void test_encode_typed_empty_bool_vec(void) {
    TEST(encode_typed_empty_bool_vec);
    TWithBoolVec w = {0};
    ason_buf_t buf = ason_encode_typed_TWithBoolVec(&w);
    ASSERT_TRUE(strstr(buf.data, "flags:[bool]") != NULL);
    ASSERT_TRUE(strstr(buf.data, "[]") != NULL);
    ason_buf_free(&buf);
    PASS();
}

void test_encode_pretty_typed_bool_vec_field(void) {
    TEST(encode_pretty_typed_bool_vec_field);
    TWithBoolVec w = {0};
    ason_vec_bool_push(&w.flags, true);
    ason_vec_bool_push(&w.flags, false);
    ason_buf_t buf = ason_encode_pretty_typed_TWithBoolVec(&w);
    ASSERT_TRUE(strstr(buf.data, "bool") != NULL);
    TWithBoolVec w2 = {0};
    ason_err_t err = ason_decode_TWithBoolVec(buf.data, buf.len, &w2);
    ASSERT_TRUE(err == ASON_OK);
    ASSERT_EQ_U(w2.flags.len, 2u);
    ASSERT_TRUE(w2.flags.data[0]);
    ASSERT_FALSE(w2.flags.data[1]);
    ason_buf_free(&buf);
    ason_vec_bool_free(&w.flags);
    ason_vec_bool_free(&w2.flags);
    PASS();
}

/* ===========================================================================
 * Pool allocator tests
 * =========================================================================== */

ASON_FIELDS_BIN(TSimple, 3)

static void test_pool_basic(void) {
    TEST(pool_basic);
    /* Default pool uses MIN_SIZE (4096) */
    ason_pool_t* pool = ason_pool_new();
    ASSERT_TRUE(pool != NULL);
    ASSERT_EQ_U(pool->block_count, 1);
    ASSERT_EQ_U(pool->block_sizes[0], ASON_POOL_MIN_SIZE);
    /* Bump-allocate within the block */
    void* p1 = ason_pool_alloc(pool, 100);
    ASSERT_TRUE(p1 != NULL);
    void* p2 = ason_pool_alloc(pool, 200);
    ASSERT_TRUE(p2 != NULL);
    ASSERT_TRUE(p2 != p1);
    ASSERT_EQ_U(pool->block_count, 1);
    ason_pool_destroy(pool);
    /* Sized pool: request 8000 bytes initial */
    pool = ason_pool_new_sized(8000);
    ASSERT_TRUE(pool != NULL);
    ASSERT_EQ_U(pool->block_count, 1);
    ASSERT_EQ_U(pool->block_sizes[0], 8000);
    ason_pool_destroy(pool);
    /* Sized pool: small request gets clamped to MIN_SIZE */
    pool = ason_pool_new_sized(100);
    ASSERT_TRUE(pool != NULL);
    ASSERT_EQ_U(pool->block_sizes[0], ASON_POOL_MIN_SIZE);
    ason_pool_destroy(pool);
    PASS();
}

static void test_pool_decode_single(void) {
    TEST(pool_decode_single);
    const char* input = "{id,name,active}:(42,hello,true)";
    TSimple out = {0};
    ason_pool_t* pool = NULL;
    ason_err_t err = ason_decode_pooled_TSimple(input, strlen(input), &out, &pool);
    ASSERT_TRUE(err == ASON_OK);
    ASSERT_TRUE(pool != NULL);
    ASSERT_EQ_I(out.id, 42);
    ASSERT_EQ_S(out.name.data, "hello");
    ASSERT_TRUE(out.active == true);
    /* Pool-allocated: don't call free_struct_fields, just destroy pool */
    ason_pool_destroy(pool);
    PASS();
}

static void test_pool_decode_vec(void) {
    TEST(pool_decode_vec);
    const char* input = "[{id,name,active}]:(1,alice,true),(2,bob,false),(3,charlie,true)";
    TSimple* arr = NULL;
    size_t count = 0;
    ason_pool_t* pool = NULL;
    ason_err_t err = ason_decode_pooled_vec_TSimple(input, strlen(input), &arr, &count, &pool);
    ASSERT_TRUE(err == ASON_OK);
    ASSERT_TRUE(pool != NULL);
    ASSERT_EQ_U(count, 3);
    ASSERT_EQ_I(arr[0].id, 1);
    ASSERT_EQ_S(arr[0].name.data, "alice");
    ASSERT_EQ_I(arr[1].id, 2);
    ASSERT_EQ_S(arr[1].name.data, "bob");
    ASSERT_EQ_I(arr[2].id, 3);
    ASSERT_EQ_S(arr[2].name.data, "charlie");
    ason_pool_destroy(pool);
    PASS();
}

static void test_pool_decode_binary(void) {
    TEST(pool_decode_binary);
    /* Encode with standard path */
    TSimple orig = { .id = 99, .name = ason_string_from("binary_test"), .active = true };
    ason_buf_t bin = ason_encode_bin_TSimple(&orig);
    /* Pool-decode binary */
    TSimple out = {0};
    ason_pool_t* pool = NULL;
    ason_err_t err = ason_decode_pooled_bin_TSimple(bin.data, bin.len, &out, &pool);
    ASSERT_TRUE(err == ASON_OK);
    ASSERT_TRUE(pool != NULL);
    ASSERT_EQ_I(out.id, 99);
    ASSERT_EQ_S(out.name.data, "binary_test");
    ASSERT_TRUE(out.active == true);
    ason_pool_destroy(pool);
    ason_buf_free(&bin);
    ason_string_free(&orig.name);
    PASS();
}

static void test_pool_decode_nested(void) {
    TEST(pool_decode_nested);
    const char* input = "{label,inner:{val,n}}:(world,(nested,7))";
    TOuter out = {0};
    ason_pool_t* pool = NULL;
    ason_err_t err = ason_decode_pooled_TOuter(input, strlen(input), &out, &pool);
    ASSERT_TRUE(err == ASON_OK);
    ASSERT_TRUE(pool != NULL);
    ASSERT_EQ_S(out.label.data, "world");
    ASSERT_EQ_S(out.inner.val.data, "nested");
    ASSERT_EQ_I(out.inner.n, 7);
    ason_pool_destroy(pool);
    PASS();
}

static void test_pool_growth(void) {
    TEST(pool_growth);
    /* Start with a small 4096-byte pool */
    ason_pool_t* pool = ason_pool_new_sized(4096);
    ASSERT_TRUE(pool != NULL);
    ASSERT_EQ_U(pool->block_count, 1);
    ASSERT_EQ_U(pool->block_sizes[0], 4096u);
    /* Fill the 4KB block (4 x 1024 = 4KB) */
    for (int i = 0; i < 4; i++) {
        void* p = ason_pool_alloc(pool, 1024);
        ASSERT_TRUE(p != NULL);
    }
    /* Next alloc triggers a second block (doubled: 8KB) */
    void* p = ason_pool_alloc(pool, 1024);
    ASSERT_TRUE(p != NULL);
    ASSERT_EQ_U(pool->block_count, 2);
    ASSERT_EQ_U(pool->block_sizes[1], 8192u);
    ason_pool_destroy(pool);
    PASS();
}

static void test_pool_smart_sizing(void) {
    TEST(pool_smart_sizing);
    /* Single struct: pool sized from input_len */
    const char* input = "{id,name,active}:(42,hello,true)";
    size_t len = strlen(input);
    TSimple out = {0};
    ason_pool_t* pool = NULL;
    ason_err_t err = ason_decode_pooled_TSimple(input, len, &out, &pool);
    ASSERT_TRUE(err == ASON_OK);
    ASSERT_TRUE(pool != NULL);
    /* Pool should be min(input_len, 4096) = 4096, NOT 128KB */
    ASSERT_EQ_U(pool->block_sizes[0], ASON_POOL_MIN_SIZE);
    ASSERT_TRUE(pool->block_sizes[0] < 128u * 1024u);
    ason_pool_destroy(pool);
    /* Vec: pool sized from count * sizeof + input_len */
    const char* vinput = "[{id,name,active}]:(1,alice,true),(2,bob,false),(3,charlie,true)";
    size_t vlen = strlen(vinput);
    TSimple* arr = NULL;
    size_t count = 0;
    pool = NULL;
    err = ason_decode_pooled_vec_TSimple(vinput, vlen, &arr, &count, &pool);
    ASSERT_TRUE(err == ASON_OK);
    ASSERT_EQ_U(count, 3);
    /* Pool should be proportional to input, NOT 128KB */
    ASSERT_TRUE(pool->block_sizes[0] < 128u * 1024u);
    /* Verify tuple counter: 3 tuples * sizeof(TSimple) + input_len */
    size_t expected_est = 3 * sizeof(TSimple) + vlen;
    if (expected_est < ASON_POOL_MIN_SIZE) expected_est = ASON_POOL_MIN_SIZE;
    ASSERT_EQ_U(pool->block_sizes[0], expected_est);
    ason_pool_destroy(pool);
    PASS();
}

/* ===========================================================================
 * Main
 * =========================================================================== */

int main(void) {
    printf("=== ASON C Test Suite ===\n\n");

    printf("--- Serialization/Deserialization ---\n");
    test_simple_roundtrip();
    test_typed_roundtrip();
    test_vec_roundtrip();
    test_vec_typed_roundtrip();

    printf("\n--- Optional fields ---\n");
    test_optional_present();
    test_optional_absent();
    test_optional_dump();
    test_empty_optional_between_commas();

    printf("\n--- Collections ---\n");
    test_vec_field();
    test_empty_vec();
    test_nested_vec();
    test_map_field();
    test_map_roundtrip();

    printf("\n--- Nested structs ---\n");
    test_nested_struct();
    test_nested_roundtrip();
    test_deep_nesting();

    printf("\n--- Strings ---\n");
    test_quoted_string();
    test_escape_sequences();
    test_string_needs_quoting();
    test_unquoted_string_trim();
    test_string_with_spaces();
    test_leading_trailing_space_quoting();
    test_backslash_escape();

    printf("\n--- Numbers ---\n");
    test_floats();
    test_integer_valued_float();
    test_negative_numbers();
    test_large_unsigned();

    printf("\n--- Parsing features ---\n");
    test_bool_values();
    test_comments();
    test_whitespace();
    test_multiline();
    test_typed_schema_parse();
    test_schema_field_mismatch();

    printf("\n--- Edge cases ---\n");
    test_error_handling();
    test_encode_vec_empty();
    test_leading_trailing_space_quoting();

    printf("\n--- Format validation ({schema}: must be rejected for vec) ---\n");
    test_bad_format_as_vec();
    test_bad_format_trailing_rows();
    test_good_format_as_vec();
    test_bad_format_extra_tuples();
    test_good_format_single();
    test_good_format_vec_single();

    printf("\n--- encodePretty -> decode roundtrips ---\n");
    test_pretty_simple_roundtrip();
    test_pretty_typed_roundtrip();
    test_pretty_vec_roundtrip();
    test_pretty_nested_roundtrip();

    printf("\n--- Binary roundtrips ---\n");
    test_binary_single_roundtrip();
    test_binary_vec_roundtrip();

    printf("\n--- Typed encoding: primitive vec fields ---\n");
    test_encode_typed_bool_vec_field();
    test_encode_typed_int_vec_field();
    test_encode_typed_str_vec_field();
    test_encode_typed_empty_bool_vec();
    test_encode_pretty_typed_bool_vec_field();

    printf("\n--- Pool allocator ---\n");
    test_pool_basic();
    test_pool_decode_single();
    test_pool_decode_vec();
    test_pool_decode_binary();
    test_pool_decode_nested();
    test_pool_growth();
    test_pool_smart_sizing();

    printf("\n=== Results: %d passed, %d failed ===\n", tests_passed, tests_failed);
    return tests_failed > 0 ? 1 : 0;
}
