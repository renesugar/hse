/* SPDX-License-Identifier: Apache-2.0 */
/*
 * Copyright (C) 2015-2020 Micron Technology, Inc.  All rights reserved.
 */

#include <hse_util/platform.h>
#include <hse_util/slab.h>
#include <hse_test_support/mwc_rand.h>

#include <hse_ikvdb/omf_kmd.h>
#include <hse_ikvdb/limits.h>

#include <byteswap.h>
#include <getopt.h>

#ifdef NDEBUG
const size_t        mem_size = 1ULL * 1024 * 1024 * 1024;
const unsigned long randc = 16ULL * 1024 * 1024;
#else
const size_t        mem_size = 1ULL * 4 * 1024 * 1024;
const unsigned long randc = 1ULL * 1024 * 1024;
#endif
void *mem;
u64 * randv;
u32   seed;

#define ENCODER(NAME) \
    static inline __attribute__((always_inline)) void NAME(void *base, size_t *off, u64 val)

#define DECODER(NAME) \
    static inline __attribute__((always_inline)) u64 NAME(const void *base, size_t *off)

DECODER(decode_n8)
{
    const u8 *p = base + *off;
    *off += sizeof(*p);
    return *p;
}
DECODER(decode_n16)
{
    const u16 *p = base + *off;
    *off += sizeof(*p);
    return *p;
}
DECODER(decode_n32)
{
    const u32 *p = base + *off;
    *off += sizeof(*p);
    return *p;
}
DECODER(decode_n64)
{
    const u64 *p = base + *off;
    *off += sizeof(*p);
    return *p;
}

ENCODER(encode_n8)
{
    u8 *p = base + *off;
    assert(val <= U8_MAX);
    *off += sizeof(*p);
    *p = (u8)val;
}
ENCODER(encode_n16)
{
    u16 *p = base + *off;
    assert(val <= U16_MAX);
    *off += sizeof(*p);
    *p = (u16)val;
}
ENCODER(encode_n32)
{
    u32 *p = base + *off;
    assert(val <= U32_MAX);
    *off += sizeof(*p);
    *p = (u32)val;
}
ENCODER(encode_n64)
{
    u64 *p = base + *off;
    assert(val <= U64_MAX);
    *off += sizeof(*p);
    *p = (u64)val;
}

DECODER(decode_be16)
{
    const u16 *p = base + *off;
    *off += sizeof(*p);
    return be16toh(*p);
}
DECODER(decode_le16)
{
    const u16 *p = base + *off;
    *off += sizeof(*p);
    return le16toh(*p);
}

DECODER(decode_be32)
{
    const u32 *p = base + *off;
    *off += sizeof(*p);
    return be32toh(*p);
}
DECODER(decode_le32)
{
    const u32 *p = base + *off;
    *off += sizeof(*p);
    return le32toh(*p);
}

DECODER(decode_be64)
{
    const u64 *p = base + *off;
    *off += sizeof(*p);
    return be64toh(*p);
}
DECODER(decode_le64)
{
    const u64 *p = base + *off;
    *off += sizeof(*p);
    return le64toh(*p);
}

ENCODER(encode_be16)
{
    u16 *p = base + *off;
    assert(val <= U16_MAX);
    *off += sizeof(*p);
    *p = htobe16((u16)val);
}
ENCODER(encode_le16)
{
    u16 *p = base + *off;
    assert(val <= U16_MAX);
    *off += sizeof(*p);
    *p = htole16((u16)val);
}

ENCODER(encode_be32)
{
    u32 *p = base + *off;
    assert(val <= U32_MAX);
    *off += sizeof(*p);
    *p = htobe32((u32)val);
}
ENCODER(encode_le32)
{
    u32 *p = base + *off;
    assert(val <= U32_MAX);
    *off += sizeof(*p);
    *p = htole32((u32)val);
}

ENCODER(encode_be64)
{
    u64 *p = base + *off;
    assert(val <= U64_MAX);
    *off += sizeof(*p);
    *p = htobe64((u64)val);
}
ENCODER(encode_le64)
{
    u64 *p = base + *off;
    assert(val <= U64_MAX);
    *off += sizeof(*p);
    *p = htole64((u64)val);
}

void
report(
    const char *   name,
    unsigned long  op_count,
    unsigned long  byte_count,
    struct timeval enc,
    struct timeval dec)
{
    static int header;

    double M = 1024 * 1024;
    double enc1 = enc.tv_sec + enc.tv_usec * 1e-6;
    double dec1 = dec.tv_sec + dec.tv_usec * 1e-6;
    double ops = op_count / M;
    double bytes = byte_count / M;

    if (!header) {
        int len = strlen(name);

        header = 1;
        printf("\n# Encoder performance (values scaled by 1024*1024)\n");

        printf(
            "%-*s  %8s  %8s  %8s  %8s  %8s  %8s\n",
            len,
            "",
            "",
            "",
            "Encode",
            "Encode",
            "Decode",
            "Decode");

        printf(
            "%-*s  %8s  %8s  %8s  %8s  %8s  %8s\n",
            len,
            "Test",
            "Ops",
            "Bytes",
            "ops/s",
            "bytes/s",
            "ops/s",
            "bytes/s");
        printf(
            "%-*s  %.8s  %.8s  %.8s  %.8s  %.8s  %.8s\n",
            len,
            "-----------------",
            "-----------------",
            "-----------------",
            "-----------------",
            "-----------------",
            "-----------------",
            "-----------------");
    }
    printf(
        "%s  %8.0f  %8.0f  %8.0f  %8.0f  %8.0f  %8.0f\n",
        name,
        ops,
        bytes,
        ops / enc1,
        bytes / enc1,
        ops / dec1,
        bytes / dec1);
}

#define test_macro(NAME, ENCODE, DECODE, MAX_MASK, MODE)                      \
    do {                                                                      \
        struct timeval  prev, now, diff1, diff2;                              \
        struct mwc_rand mwc;                                                  \
                                                                              \
        u64  xor1 = 0;                                                        \
        u64  xor2 = 0;                                                        \
        u64  ops = 0;                                                         \
        u64  i, off;                                                          \
        char tnam[256];                                                       \
        char modestr[32];                                                     \
        u64  mode_mask;                                                       \
        int  mode;                                                            \
                                                                              \
        memset(mem, 0, mem_size);                                             \
        mwc_rand_init(&mwc, seed);                                            \
                                                                              \
        /* mask: 3f, 3fff, 3fffff, etc */                                     \
        mode = (1 <= MODE && MODE <= 8) ? MODE : 8;                           \
        snprintf(modestr, sizeof(modestr), "%dbytes", mode);                  \
                                                                              \
        mode_mask = (1LL << ((mode)*8 - 2)) - 1;                              \
        for (i = 0; i < randc; i++)                                           \
            randv[i] = mwc_rand64(&mwc) & (MAX_MASK)&mode_mask;               \
                                                                              \
        gettimeofday(&prev, NULL);                                            \
        for (off = 0; off + 128 < mem_size;) {                                \
            u64 val = randv[(randc - 1) & ops++];                             \
            xor1 ^= val;                                                      \
            ENCODE(mem, &off, val);                                           \
        }                                                                     \
        gettimeofday(&now, NULL);                                             \
        timersub(&now, &prev, &diff1);                                        \
                                                                              \
        gettimeofday(&prev, NULL);                                            \
        for (off = 0, i = 0; i < ops; i++) {                                  \
            u64 val = DECODE(mem, &off);                                      \
            xor2 ^= val;                                                      \
            assert(randv[(randc - 1) & i] == val);                            \
            assert(off < mem_size);                                           \
        }                                                                     \
        gettimeofday(&now, NULL);                                             \
        timersub(&now, &prev, &diff2);                                        \
                                                                              \
        snprintf(tnam, sizeof(tnam), "%-8s %-8s", NAME, modestr);             \
        report(tnam, ops, off, diff1, diff2);                                 \
                                                                              \
        if (ops == 0)                                                         \
            printf("%s: BUG: no values encoded\n", NAME);                     \
        else if (ops != i)                                                    \
            printf(                                                           \
                "%s: BUG: encode count != decode count"                       \
                ": %lu != %lu\n",                                             \
                NAME,                                                         \
                ops,                                                          \
                i);                                                           \
        else if (xor1 != xor2)                                                \
            printf("%s: BUG: XOR mismatch: %lx ! = %lx\n", NAME, xor1, xor2); \
    } while (0)

#ifdef NDEBUG
#define test test_macro
#else
void
test(
    const char *NAME,
    void (*ENCODE)(void *, size_t *, u64),
    u64 (*DECODE)(const void *, size_t *),
    u64 MAX_MASK,
    int MODE)
{
    struct timeval  prev, now, diff1, diff2;
    struct mwc_rand mwc;

    u64  xor1 = 0;
    u64  xor2 = 0;
    u64  ops = 0;
    u64  i, off, val;
    char tnam[256];
    char modestr[32];
    u64  mode_mask;
    int  mode;

    mwc_rand_init(&mwc, seed);

    /* mask: 3f, 3fff, 3fffff, etc */
    mode = (1 <= MODE && MODE <= 8) ? MODE : 8;
    snprintf(modestr, sizeof(modestr), "%dbytes", mode);

    mode_mask = (1LL << ((mode)*8 - 2)) - 1;
    for (i = 0; i < randc; i++)
        randv[i] = mwc_rand64(&mwc) & (MAX_MASK)&mode_mask;

    gettimeofday(&prev, NULL);
    for (off = 0; off + 128 < mem_size;) {
        val = randv[(randc - 1) & ops++];
        xor1 ^= val;
        ENCODE(mem, &off, val);
    }
    gettimeofday(&now, NULL);
    timersub(&now, &prev, &diff1);

    gettimeofday(&prev, NULL);
    for (off = 0, i = 0; i < ops; i++) {
        val = DECODE(mem, &off);
        xor2 ^= val;
        assert(randv[(randc - 1) & i] == val);
        assert(off < mem_size);
    }
    gettimeofday(&now, NULL);
    timersub(&now, &prev, &diff2);

    snprintf(tnam, sizeof(tnam), "%-8s %-8s", NAME, modestr);
    report(tnam, ops, off, diff1, diff2);

    if (ops == 0)
        printf("%s: BUG: no values encoded\n", NAME);
    else if (ops != i)
        printf(
            "%s: BUG: encode count != decode count"
            ": %lu != %lu\n",
            NAME,
            ops,
            i);
    else if (xor1 != xor2)
        printf("%s: BUG: XOR mismatch: %lx ! = %lx\n", NAME, xor1, xor2);
}
#endif

void
run_encoder_perf(u32 seed)
{
    test("warmup", encode_n8, decode_n8, U8_MAX, 1);

    printf("\n");
    test("native8", encode_n8, decode_n8, U8_MAX, 1);
    test("native16", encode_n16, decode_n16, U16_MAX, 2);
    test("native32", encode_n32, decode_n32, U32_MAX, 4);
    test("native64", encode_n64, decode_n64, U64_MAX, 8);
    printf("\n");

    test("be16", encode_be16, decode_be16, U16_MAX, 2);
    test("be32", encode_be32, decode_be32, U32_MAX, 4);
    test("be64", encode_be64, decode_be64, U64_MAX, 8);
    printf("\n");

    test("le16", encode_le16, decode_le16, U16_MAX, 2);
    test("le32", encode_le32, decode_le32, U32_MAX, 4);
    test("le64", encode_le64, decode_le64, U64_MAX, 8);
    printf("\n");

    test("hg16_32k", encode_hg16_32k, decode_hg16_32k, HG16_32K_MAX, 1);
    test("hg16_32k", encode_hg16_32k, decode_hg16_32k, HG16_32K_MAX, 2);
    printf("\n");

    test("hg24_4m", encode_hg24_4m, decode_hg24_4m, HG24_4M_MAX, 1);
    test("hg24_4m", encode_hg24_4m, decode_hg24_4m, HG24_4M_MAX, 2);
    test("hg24_4m", encode_hg24_4m, decode_hg24_4m, HG24_4M_MAX, 3);
    printf("\n");

    test("hg32_1024m", encode_hg32_1024m, decode_hg32_1024m, HG32_1024M_MAX, 1);
    test("hg32_1024m", encode_hg32_1024m, decode_hg32_1024m, HG32_1024M_MAX, 2);
    test("hg32_1024m", encode_hg32_1024m, decode_hg32_1024m, HG32_1024M_MAX, 3);
    test("hg32_1024m", encode_hg32_1024m, decode_hg32_1024m, HG32_1024M_MAX, 4);
    printf("\n");

    test("hg64", encode_hg64, decode_hg64, HG64_MAX, 2);
    test("hg64", encode_hg64, decode_hg64, HG64_MAX, 4);
    test("hg64", encode_hg64, decode_hg64, HG64_MAX, 6);
    test("hg64", encode_hg64, decode_hg64, HG64_MAX, 8);
    printf("\n");

    test("varint", encode_varint, decode_varint, U64_MAX, 1);
    test("varint", encode_varint, decode_varint, U64_MAX, 2);
    test("varint", encode_varint, decode_varint, U64_MAX, 3);
    test("varint", encode_varint, decode_varint, U64_MAX, 4);
    test("varint", encode_varint, decode_varint, U64_MAX, 5);
    test("varint", encode_varint, decode_varint, U64_MAX, 6);
    test("varint", encode_varint, decode_varint, U64_MAX, 7);
    test("varint", encode_varint, decode_varint, U64_MAX, 8);
    printf("\n");
}

#if 1

struct kmd_test_stats {
    u64 nbytes, nkeys, nseqs, nvals, nivals, nzvals, ntombs, nptombs;
};

enum test_enc { enc_short, enc_med, enc_long };
enum test_mix { tombs_only, vals_only, mixed };

static const char *
test_enc_name(enum test_enc enc)
{
    switch (enc) {
        case enc_short:
            return "short";
        case enc_med:
            return "med";
        case enc_long:
            return "long";
    }
    return "unknown";
}

struct kmd_test_profile {
    struct mwc_rand mwc;
    unsigned        max_keys;
    unsigned        max_ents_per_key;
    enum test_enc   enc;
    enum test_mix   mix;
};

unsigned
tp_next_count(struct kmd_test_profile *tp)
{
    return (mwc_rand32(&tp->mwc) % tp->max_ents_per_key) + 1;
}

void
tp_next_entry(
    struct kmd_test_profile *tp,
    enum kmd_vtype *         vtype,
    u64 *                    seq,
    uint *                   vbidx,
    uint *                   vboff,
    const void **            vdata,
    uint *                   vlen)
{
    static char valbuf[CN_SMALL_VALUE_THRESHOLD] = { 17, 23, 42, 211, 164, 96, 11, 7 };
    u32         rv;

    *seq = mwc_rand64(&tp->mwc) & HG64_MAX;

    if (tp->enc == enc_short)
        *seq &= 0x3f;
    else if (tp->enc == enc_med)
        *seq &= 0x3fff;

    rv = mwc_rand32(&tp->mwc);

    if (tp->mix == vals_only)
        goto value;

    if (rv < 3 * (U32_MAX / 100)) {
        *vtype = vtype_zval;
        *vlen = 0;
        return;
    }

    if (rv < 5 * (U32_MAX / 100)) {
        *vtype = vtype_ival;
        *vdata = valbuf;
        *vlen = 1 + mwc_rand32(&tp->mwc) % CN_SMALL_VALUE_THRESHOLD;
        return;
    }

    if (rv < 7 * (U32_MAX / 100)) {
        *vtype = vtype_tomb;
        return;
    }

    if (rv < 10 * (U32_MAX / 100)) {
        *vtype = vtype_ptomb;
        return;
    }

    if (tp->mix == tombs_only) {
        /* a mix of tombs */
        *vtype = vtype_tomb;
        return;
    }

value:
    *vtype = vtype_val;
    *vboff = mwc_rand32(&tp->mwc);
    *vbidx = mwc_rand32(&tp->mwc) & HG16_32K_MAX;
    *vlen = mwc_rand32(&tp->mwc) & HG32_1024M_MAX;
    if (*vlen <= CN_SMALL_VALUE_THRESHOLD)
        *vlen = CN_SMALL_VALUE_THRESHOLD;
    if (tp->enc == enc_short) {
        *vbidx &= 0x3f;
        *vlen &= 0x7f;
    } else if (tp->enc == enc_med) {
        *vbidx &= 0x3f;
        *vlen &= 0xfff;
    }
}

void
run_kmd_write_perf(struct kmd_test_stats *s)
{
    u64      off, seq, rx, rval;
    unsigned count;
    uint     vbidx, vboff, vlen;

    off = 0;
    rx = 0;

    while (off + 1024 <= mem_size) {

        rval = randv[(randc - 1) & rx++];
        count = (rval & 3) + 1;
        seq = rval & 0xfff;

        s->nkeys++;
        s->nseqs += count;

        kmd_set_count(mem, &off, count);

        if (count == 4) {
            kmd_add_tomb(mem, &off, seq++);
            s->ntombs++;
            count = 3;
        }

        vbidx = (rval >> 16) & 0xf;
        vboff = (rval >> 32);
        vlen = (rval >> 24) & 0xff;

        s->nvals += count;
        while (count-- > 0)
            kmd_add_val(mem, &off, seq++, vbidx, vboff, vlen);
    }

    kmd_set_count(mem, &off, 0);
    s->nbytes = off;
}

void
run_kmd_read_perf(struct kmd_test_stats *s)
{
    u64            off, seq, rx, rval, exp_seq;
    unsigned       i, count, exp_count;
    enum kmd_vtype vtype;
    uint           vbidx, vboff, vlen;
    const void *   vdata;

    off = 0;
    rx = 0;

    while (true) {

        rval = randv[(randc - 1) & rx++];
        exp_count = (rval & 3) + 1;
        exp_seq = rval & 0xfff;

        count = kmd_count(mem, &off);
        if (count == 0)
            break;
        assert(count == exp_count);
        exp_count = exp_count;
        exp_seq = exp_seq;

        s->nkeys++;
        s->nseqs += count;

        for (i = 0; i < count; i++) {
            kmd_type_seq(mem, &off, &vtype, &seq);
            assert(exp_seq + i == seq);
            if (vtype == vtype_val) {
                s->nvals++;
                kmd_val(mem, &off, &vbidx, &vboff, &vlen);
            } else if (vtype == vtype_ival) {
                s->nvals++;
                kmd_ival(mem, &off, &vdata, &vlen);
            } else {
                s->ntombs++;
            }
        }
    }
    s->nbytes = off;
}

void
run_kmd_tp(struct kmd_test_profile *tp, struct kmd_test_stats *s, bool writing)
{
    u64            off, seq;
    unsigned       count, actual_count, may_need;
    enum kmd_vtype vtype;
    uint           i, vbidx, vboff, vlen;
    const void *   vdata;

    off = 0;

    /* If max_keys is 0, stop when buffer is full */
    while (tp->max_keys == 0 || s->nkeys < tp->max_keys) {

        if (writing) {
            may_need = kmd_storage_max(tp->max_ents_per_key);
            if (off + may_need > mem_size)
                break;
        }

        count = tp_next_count(tp);
        if (writing) {
            kmd_set_count(mem, &off, count);
        } else {
            actual_count = kmd_count(mem, &off);
            if (actual_count == 0)
                break;
            assert(actual_count == count);
        }

        s->nkeys++;
        s->nseqs += count;
        for (i = 0; i < count; i++) {
            tp_next_entry(tp, &vtype, &seq, &vbidx, &vboff, &vdata, &vlen);
            switch (vtype) {
                case vtype_tomb:
                    s->ntombs++;
                    break;
                case vtype_ptomb:
                    s->nptombs++;
                    break;
                case vtype_zval:
                    s->nzvals++;
                    break;
                case vtype_ival:
                    s->nivals++;
                    break;
                case vtype_val:
                    s->nvals++;
                    break;
            }
            if (writing) {
                switch (vtype) {
                    case vtype_tomb:
                        kmd_add_tomb(mem, &off, seq);
                        break;
                    case vtype_ptomb:
                        kmd_add_ptomb(mem, &off, seq);
                        break;
                    case vtype_zval:
                        kmd_add_zval(mem, &off, seq);
                        break;
                    case vtype_ival:
                        kmd_add_ival(mem, &off, seq, vdata, vlen);
                        break;
                    case vtype_val:
                        kmd_add_val(mem, &off, seq, vbidx, vboff, vlen);
                }
            } else {
                u64            actual_seq;
                enum kmd_vtype actual_vtype;
                uint           actual_vbidx;
                uint           actual_vboff;
                uint           actual_vlen;
                const void *   actual_vdata;

                kmd_type_seq(mem, &off, &actual_vtype, &actual_seq);
                assert(vtype == actual_vtype);
                assert(seq == actual_seq);
                if (vtype == vtype_val) {
                    kmd_val(mem, &off, &actual_vbidx, &actual_vboff, &actual_vlen);
                    assert(actual_vbidx == vbidx);
                    assert(actual_vboff == vboff);
                    assert(actual_vlen == vlen);
                } else if (vtype == vtype_ival) {
                    kmd_ival(mem, &off, &actual_vdata, &actual_vlen);
                    assert(actual_vlen == vlen);
                }
            }
        }
    }
    if (writing)
        kmd_set_count(mem, &off, 0);

    s->nbytes = off;
}

void
run_kmd_profile(struct kmd_test_profile *tp)
{
    static int headers;

    struct kmd_test_stats  read = {}, write = {};
    struct kmd_test_stats *s;

    memset(mem, 0xab, mem_size);

    mwc_rand_init(&tp->mwc, seed);
    run_kmd_tp(tp, &write, true);

    mwc_rand_init(&tp->mwc, seed);
    run_kmd_tp(tp, &read, true);

    s = &write;
    double ents_per_key = 1.0 * s->nseqs / s->nkeys;
    double bytes_per_seq = 1.0 * s->nbytes / s->nseqs;
    double bytes_per_key = 1.0 * s->nbytes / s->nkeys;

    double tot_ents = s->ntombs + s->nptombs + s->nzvals + s->nvals;
    double tot_tombs = s->ntombs + s->nptombs + s->nzvals;
    double pct_tombs = 100.0 * (tot_tombs / tot_ents);

    if (!headers) {
        headers = 1;

        printf("\n# KMD encoding overhead\n");

        printf(
            "%-5s  %9s  %8s  %10s  %10s\n", "Enc", "%Tombs", "Ents/Key", "Bytes/Key", "Bytes/Ent");
        printf(
            "%-5s  %9s  %8s  %10s  %10s\n", "-----", "------", "--------", "--------", "--------");
    }
    printf(
        "%-5s  %8.0f%%  %8.1f  %10.1f  %10.1f\n",
        test_enc_name(tp->enc),
        pct_tombs,
        ents_per_key,
        bytes_per_key,
        bytes_per_seq);

    if (memcmp(&read, &write, sizeof(read))) {
        printf("ERROR: read stats != write stats\n");
        s = &write;
        printf(
            "write: %lu bytes, %lu keys, %lu seqs, %lu vals, "
            "%lu tombs, %lu ptombs, %lu zvals\n",
            s->nbytes,
            s->nkeys,
            s->nseqs,
            s->nvals,
            s->ntombs,
            s->nptombs,
            s->nzvals);
        s = &read;
        printf(
            "read: %lu bytes, %lu keys, %lu seqs, %lu vals, "
            "%lu tombs, %lu ptombs, %lu zvals\n",
            s->nbytes,
            s->nkeys,
            s->nseqs,
            s->nvals,
            s->ntombs,
            s->nptombs,
            s->nzvals);
    }
}

void
run_kmd_perf(void)
{
    struct kmd_test_stats read = {}, write = {};
    struct timeval        t1, t2, tmp;
    struct mwc_rand       mwc;
    double                M = 1024 * 1024;
    double                rtime, wtime;
    u64                   i;

    mwc_rand_init(&mwc, seed);
    for (i = 0; i < randc; i++)
        randv[i] = mwc_rand64(&mwc);

    gettimeofday(&t1, NULL);
    run_kmd_write_perf(&write);
    gettimeofday(&t2, NULL);

    timersub(&t2, &t1, &tmp);
    wtime = tmp.tv_sec + tmp.tv_usec * 1e-6;

    gettimeofday(&t1, NULL);
    run_kmd_read_perf(&read);
    gettimeofday(&t2, NULL);
    timersub(&t2, &t1, &tmp);
    rtime = tmp.tv_sec + tmp.tv_usec * 1e-6;

    printf("\n# KMD encoder performance (values scaled by 1024*1024)\n");

    printf(
        "kmd write %5.0f M keys, %5.0f M ents, %5.0f M bytes"
        ": %5.0f M ents/sec, %5.0f MBytes/sec\n",
        write.nkeys / M,
        write.nseqs / M,
        write.nbytes / M,
        write.nseqs / wtime / M,
        write.nbytes / wtime / M);

    printf(
        "kmd read  %5.0f M keys, %5.0f M ents, %5.0f M bytes"
        ": %5.0f M ents/sec, %5.0f MBytes/sec\n",
        read.nkeys / M,
        read.nseqs / M,
        read.nbytes / M,
        read.nseqs / wtime / M,
        read.nbytes / rtime / M);
}

void
run_kmd_overhead(void)
{
    unsigned      t_epk[] = { 1, 2, 10, 1000 };
    enum test_enc t_enc[] = { enc_short, enc_med, enc_long };
    enum test_mix t_mix[] = { vals_only, mixed, tombs_only };

    int i, j, k;

    for (k = 0; k < NELEM(t_mix); k++) {
        for (i = 0; i < NELEM(t_epk); i++) {
            for (j = 0; j < NELEM(t_enc); j++) {
                struct kmd_test_profile tp = {};

                tp.max_keys = 1000;
                tp.max_ents_per_key = t_epk[i];
                tp.enc = t_enc[j];
                tp.mix = t_mix[k];
                run_kmd_profile(&tp);
            }
        }
    }
}
#endif

void
usage(void)
{
    fprintf(stderr, "Usage: [-s seed] [-g] [-o] [-p]\n");
}

int
main(int argc, char *argv[])
{
    int  opt;
    bool general = false;
    bool kmd_overhead = false;
    bool kmd_perf = false;

    seed = time(NULL);

    while ((opt = getopt(argc, argv, "s:goph")) != -1) {
        switch (opt) {
            case 's':
                seed = atoi(optarg);
                break;
            case 'g':
                general = true;
                break;
            case 'o':
                kmd_overhead = true;
                break;
            case 'p':
                kmd_perf = true;
                break;
            case 'h':
            default:
                usage();
                exit(opt == 'h' ? 0 : -1);
        }
    }

    if (optind > argc) {
        usage();
        exit(-1);
    }

    if (!general && !kmd_overhead && !kmd_perf)
        general = kmd_overhead = kmd_perf = true;

    mem = malloc(mem_size);
    randv = malloc(randc * sizeof(*randv));
    if (!mem || !randv)
        return -1;

    printf("seed = %u\n", seed);

    if (general)
        run_encoder_perf(seed);

    if (kmd_overhead)
        run_kmd_overhead();

    if (kmd_perf)
        run_kmd_perf();

#ifndef NDEBUG
    printf("\nNote: this is a debug build. "
           "Use a release build for better performance.\n\n");
#endif

    free(mem);
    free(randv);
    return 0;
}
