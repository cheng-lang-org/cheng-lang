#include <openssl/bn.h>
#include <openssl/ec.h>
#include <openssl/ecdsa.h>
#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <openssl/obj_mac.h>
#include <openssl/sha.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define FIXED32 32
#define HELLO_PAYLOAD_LEN 196
#define FRAME_HEADER_LEN 96
#define FRAME_MAGIC 0x56334631u
#define FRAME_VERSION 1u
#define FRAME_KIND_HELLO 1u

struct v3_lsmr_address {
    uint32_t depth;
    uint8_t digits[32];
};

struct v3_state_root_summary {
    uint8_t state_forest_cid[32];
    uint8_t event_root_cid[32];
    uint8_t account_root_cid[32];
};

struct v3_hello_payload {
    uint8_t peer_id[32];
    struct v3_lsmr_address address;
    uint8_t tip_event_cid[32];
    struct v3_state_root_summary summary;
};

struct v3_frame_header {
    uint32_t magic;
    uint32_t version;
    uint32_t kind;
    uint32_t flags;
    uint32_t payload_len;
    uint32_t epoch;
    uint32_t ganzhi_index;
    uint32_t reserved0;
    uint8_t source_peer_id[32];
    uint8_t body_cid[32];
};

struct sha_ctx {
    uint8_t message[32];
    uint8_t out[32];
};

struct x25519_ctx {
    EVP_PKEY *priv;
    EVP_PKEY *peer;
    uint8_t shared[32];
    size_t shared_len;
};

struct p256_ctx {
    EC_KEY *key;
    uint8_t priv_be[32];
    uint8_t message[32];
    uint8_t pub_uncompressed[65];
    ECDSA_SIG *sig;
};

struct chain_ctx {
    struct v3_hello_payload hello;
    uint8_t frame[FRAME_HEADER_LEN + HELLO_PAYLOAD_LEN];
    size_t frame_len;
};

static volatile uint64_t sink_u64 = 0;
static volatile uint8_t sink_u8 = 0;

static uint64_t now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

static void store_u32be(uint8_t *out, uint32_t value) {
    out[0] = (uint8_t)(value >> 24);
    out[1] = (uint8_t)(value >> 16);
    out[2] = (uint8_t)(value >> 8);
    out[3] = (uint8_t)value;
}

static uint32_t load_u32be(const uint8_t *in) {
    return ((uint32_t)in[0] << 24) |
           ((uint32_t)in[1] << 16) |
           ((uint32_t)in[2] << 8) |
           (uint32_t)in[3];
}

static void fill_bytes(uint8_t *out, size_t n, uint8_t seed) {
    for (size_t i = 0; i < n; ++i) {
        out[i] = (uint8_t)(seed + (uint8_t)(i * 17u));
    }
}

static void hmac_sha256(const uint8_t *key, size_t key_len,
                        const uint8_t *data, size_t data_len,
                        uint8_t out[32]) {
    unsigned int out_len = 0;
    HMAC(EVP_sha256(), key, (int)key_len, data, data_len, out, &out_len);
}

static int make_x25519_key(const uint8_t priv[32], EVP_PKEY **out_key) {
    EVP_PKEY *key = EVP_PKEY_new_raw_private_key_ex(NULL, "X25519", NULL, priv, 32);
    if (key == NULL) {
        return 0;
    }
    *out_key = key;
    return 1;
}

static int derive_x25519(EVP_PKEY *priv, EVP_PKEY *peer, uint8_t out[32], size_t *out_len) {
    EVP_PKEY_CTX *ctx = EVP_PKEY_CTX_new_from_pkey(NULL, priv, NULL);
    if (ctx == NULL) {
        return 0;
    }
    int ok = EVP_PKEY_derive_init(ctx) == 1 &&
             EVP_PKEY_derive_set_peer(ctx, peer) == 1;
    size_t want = 32;
    if (ok) {
        ok = EVP_PKEY_derive(ctx, out, &want) == 1;
    }
    EVP_PKEY_CTX_free(ctx);
    if (!ok || want != 32) {
        return 0;
    }
    *out_len = want;
    return 1;
}

static int make_p256_key(const uint8_t priv_be[32], EC_KEY **out_key) {
    int ok = 0;
    EC_KEY *key = NULL;
    BIGNUM *d = NULL;
    EC_POINT *pub = NULL;
    BN_CTX *bn_ctx = NULL;

    key = EC_KEY_new_by_curve_name(NID_X9_62_prime256v1);
    if (key == NULL) {
        goto done;
    }
    d = BN_bin2bn(priv_be, 32, NULL);
    if (d == NULL) {
        goto done;
    }
    if (EC_KEY_set_private_key(key, d) != 1) {
        goto done;
    }
    bn_ctx = BN_CTX_new();
    pub = EC_POINT_new(EC_KEY_get0_group(key));
    if (bn_ctx == NULL || pub == NULL) {
        goto done;
    }
    if (EC_POINT_mul(EC_KEY_get0_group(key), pub, d, NULL, NULL, bn_ctx) != 1) {
        goto done;
    }
    if (EC_KEY_set_public_key(key, pub) != 1) {
        goto done;
    }
    ok = 1;
    *out_key = key;
    key = NULL;

done:
    EC_POINT_free(pub);
    BN_free(d);
    BN_CTX_free(bn_ctx);
    EC_KEY_free(key);
    return ok;
}

static int p256_public_bytes(EC_KEY *key, uint8_t out[65]) {
    unsigned char *p = out;
    return i2o_ECPublicKey(key, &p) == 65;
}

static int rfc6979_k_p256(const uint8_t priv_be[32], const uint8_t digest[32],
                          const BIGNUM *order, BIGNUM **out_k) {
    uint8_t V[32];
    uint8_t K[32];
    uint8_t bx[64];
    uint8_t step[32 + 1 + 64];

    memset(V, 0x01, sizeof(V));
    memset(K, 0x00, sizeof(K));
    memcpy(bx, priv_be, 32);
    memcpy(bx + 32, digest, 32);

    memcpy(step, V, 32);
    step[32] = 0x00;
    memcpy(step + 33, bx, 64);
    hmac_sha256(K, sizeof(K), step, sizeof(step), K);
    hmac_sha256(K, sizeof(K), V, sizeof(V), V);

    memcpy(step, V, 32);
    step[32] = 0x01;
    memcpy(step + 33, bx, 64);
    hmac_sha256(K, sizeof(K), step, sizeof(step), K);
    hmac_sha256(K, sizeof(K), V, sizeof(V), V);

    for (;;) {
        BIGNUM *k = NULL;
        hmac_sha256(K, sizeof(K), V, sizeof(V), V);
        k = BN_bin2bn(V, 32, NULL);
        if (k != NULL && !BN_is_zero(k) && BN_cmp(k, order) < 0) {
            *out_k = k;
            return 1;
        }
        BN_free(k);
        memcpy(step, V, 32);
        step[32] = 0x00;
        hmac_sha256(K, sizeof(K), step, 33, K);
        hmac_sha256(K, sizeof(K), V, sizeof(V), V);
    }
}

static int ecdsa_sign_rfc6979_message(EC_KEY *key, const uint8_t priv_be[32],
                                      const uint8_t *message, size_t message_len,
                                      ECDSA_SIG **out_sig) {
    int ok = 0;
    uint8_t digest[32];
    const EC_GROUP *group = NULL;
    BN_CTX *bn_ctx = NULL;
    BIGNUM *order = NULL;
    BIGNUM *k = NULL;
    BIGNUM *kinv = NULL;
    BIGNUM *x = NULL;
    BIGNUM *y = NULL;
    BIGNUM *rp = NULL;
    EC_POINT *point = NULL;
    ECDSA_SIG *sig = NULL;

    SHA256(message, message_len, digest);
    group = EC_KEY_get0_group(key);
    bn_ctx = BN_CTX_new();
    order = BN_new();
    x = BN_new();
    y = BN_new();
    rp = BN_new();
    point = EC_POINT_new(group);
    if (group == NULL || bn_ctx == NULL || order == NULL || x == NULL ||
        y == NULL || rp == NULL || point == NULL) {
        goto done;
    }
    if (EC_GROUP_get_order(group, order, bn_ctx) != 1) {
        goto done;
    }
    if (!rfc6979_k_p256(priv_be, digest, order, &k)) {
        goto done;
    }
    kinv = BN_mod_inverse(NULL, k, order, bn_ctx);
    if (kinv == NULL) {
        goto done;
    }
    if (EC_POINT_mul(group, point, k, NULL, NULL, bn_ctx) != 1) {
        goto done;
    }
    if (EC_POINT_get_affine_coordinates(group, point, x, y, bn_ctx) != 1) {
        goto done;
    }
    if (BN_nnmod(rp, x, order, bn_ctx) != 1) {
        goto done;
    }
    sig = ECDSA_do_sign_ex(digest, 32, kinv, rp, key);
    if (sig == NULL) {
        goto done;
    }
    *out_sig = sig;
    sig = NULL;
    ok = 1;

done:
    ECDSA_SIG_free(sig);
    BN_free(order);
    BN_free(k);
    BN_free(kinv);
    BN_free(x);
    BN_free(y);
    BN_free(rp);
    EC_POINT_free(point);
    BN_CTX_free(bn_ctx);
    return ok;
}

static int ecdsa_verify_message(EC_KEY *key, const uint8_t *message, size_t message_len,
                                const ECDSA_SIG *sig) {
    uint8_t digest[32];
    SHA256(message, message_len, digest);
    return ECDSA_do_verify(digest, 32, sig, key) == 1;
}

static void encode_hello_payload(const struct v3_hello_payload *payload, uint8_t out[HELLO_PAYLOAD_LEN]) {
    size_t off = 0;
    memcpy(out + off, payload->peer_id, 32);
    off += 32;
    store_u32be(out + off, payload->address.depth);
    off += 4;
    memcpy(out + off, payload->address.digits, 32);
    off += 32;
    memcpy(out + off, payload->tip_event_cid, 32);
    off += 32;
    memcpy(out + off, payload->summary.state_forest_cid, 32);
    off += 32;
    memcpy(out + off, payload->summary.event_root_cid, 32);
    off += 32;
    memcpy(out + off, payload->summary.account_root_cid, 32);
}

static int decode_hello_payload(const uint8_t *in, size_t in_len, struct v3_hello_payload *payload) {
    size_t off = 0;
    if (in_len != HELLO_PAYLOAD_LEN) {
        return 0;
    }
    memcpy(payload->peer_id, in + off, 32);
    off += 32;
    payload->address.depth = load_u32be(in + off);
    off += 4;
    if (payload->address.depth > 32) {
        return 0;
    }
    memcpy(payload->address.digits, in + off, 32);
    off += 32;
    memcpy(payload->tip_event_cid, in + off, 32);
    off += 32;
    memcpy(payload->summary.state_forest_cid, in + off, 32);
    off += 32;
    memcpy(payload->summary.event_root_cid, in + off, 32);
    off += 32;
    memcpy(payload->summary.account_root_cid, in + off, 32);
    return 1;
}

static void encode_frame_header(const struct v3_frame_header *header, uint8_t out[FRAME_HEADER_LEN]) {
    store_u32be(out + 0, header->magic);
    store_u32be(out + 4, header->version);
    store_u32be(out + 8, header->kind);
    store_u32be(out + 12, header->flags);
    store_u32be(out + 16, header->payload_len);
    store_u32be(out + 20, header->epoch);
    store_u32be(out + 24, header->ganzhi_index);
    store_u32be(out + 28, header->reserved0);
    memcpy(out + 32, header->source_peer_id, 32);
    memcpy(out + 64, header->body_cid, 32);
}

static int decode_frame_header(const uint8_t *in, size_t in_len, struct v3_frame_header *header) {
    if (in_len < FRAME_HEADER_LEN) {
        return 0;
    }
    header->magic = load_u32be(in + 0);
    header->version = load_u32be(in + 4);
    header->kind = load_u32be(in + 8);
    header->flags = load_u32be(in + 12);
    header->payload_len = load_u32be(in + 16);
    header->epoch = load_u32be(in + 20);
    header->ganzhi_index = load_u32be(in + 24);
    header->reserved0 = load_u32be(in + 28);
    memcpy(header->source_peer_id, in + 32, 32);
    memcpy(header->body_cid, in + 64, 32);
    return header->magic == FRAME_MAGIC && header->version == FRAME_VERSION;
}

static int wrap_hello_frame(const struct v3_hello_payload *payload,
                            uint32_t epoch,
                            uint32_t ganzhi_index,
                            uint8_t out[FRAME_HEADER_LEN + HELLO_PAYLOAD_LEN],
                            size_t *out_len) {
    struct v3_frame_header header;
    uint8_t payload_buf[HELLO_PAYLOAD_LEN];

    encode_hello_payload(payload, payload_buf);
    memset(&header, 0, sizeof(header));
    header.magic = FRAME_MAGIC;
    header.version = FRAME_VERSION;
    header.kind = FRAME_KIND_HELLO;
    header.payload_len = HELLO_PAYLOAD_LEN;
    header.epoch = epoch;
    header.ganzhi_index = ganzhi_index;
    memcpy(header.source_peer_id, payload->peer_id, 32);
    SHA256(payload_buf, HELLO_PAYLOAD_LEN, header.body_cid);
    encode_frame_header(&header, out);
    memcpy(out + FRAME_HEADER_LEN, payload_buf, HELLO_PAYLOAD_LEN);
    *out_len = FRAME_HEADER_LEN + HELLO_PAYLOAD_LEN;
    return 1;
}

static int unwrap_hello_frame(const uint8_t *frame, size_t frame_len, struct v3_hello_payload *payload) {
    struct v3_frame_header header;
    uint8_t cid[32];
    if (!decode_frame_header(frame, frame_len, &header)) {
        return 0;
    }
    if (header.kind != FRAME_KIND_HELLO || header.payload_len != HELLO_PAYLOAD_LEN) {
        return 0;
    }
    if (frame_len != FRAME_HEADER_LEN + HELLO_PAYLOAD_LEN) {
        return 0;
    }
    SHA256(frame + FRAME_HEADER_LEN, HELLO_PAYLOAD_LEN, cid);
    if (memcmp(cid, header.body_cid, 32) != 0) {
        return 0;
    }
    return decode_hello_payload(frame + FRAME_HEADER_LEN, HELLO_PAYLOAD_LEN, payload);
}

static void bench_sha256(void *opaque) {
    struct sha_ctx *ctx = (struct sha_ctx *)opaque;
    SHA256(ctx->message, sizeof(ctx->message), ctx->out);
    sink_u8 ^= ctx->out[0];
}

static void bench_x25519(void *opaque) {
    struct x25519_ctx *ctx = (struct x25519_ctx *)opaque;
    ctx->shared_len = 0;
    if (!derive_x25519(ctx->priv, ctx->peer, ctx->shared, &ctx->shared_len)) {
        fprintf(stderr, "x25519 derive failed\n");
        exit(1);
    }
    sink_u8 ^= ctx->shared[0];
}

static void bench_p256_pubkey(void *opaque) {
    struct p256_ctx *ctx = (struct p256_ctx *)opaque;
    if (!p256_public_bytes(ctx->key, ctx->pub_uncompressed)) {
        fprintf(stderr, "p256 public key failed\n");
        exit(1);
    }
    sink_u8 ^= ctx->pub_uncompressed[1];
}

static void bench_p256_sign(void *opaque) {
    struct p256_ctx *ctx = (struct p256_ctx *)opaque;
    ECDSA_SIG *sig = NULL;
    if (!ecdsa_sign_rfc6979_message(ctx->key, ctx->priv_be, ctx->message, sizeof(ctx->message), &sig)) {
        fprintf(stderr, "p256 sign failed\n");
        exit(1);
    }
    const BIGNUM *r = NULL;
    ECDSA_SIG_get0(sig, &r, NULL);
    sink_u8 ^= (uint8_t)BN_get_word(r);
    ECDSA_SIG_free(sig);
}

static void bench_p256_verify(void *opaque) {
    struct p256_ctx *ctx = (struct p256_ctx *)opaque;
    if (!ecdsa_verify_message(ctx->key, ctx->message, sizeof(ctx->message), ctx->sig)) {
        fprintf(stderr, "p256 verify failed\n");
        exit(1);
    }
    sink_u8 ^= 1u;
}

static void bench_chain_encode(void *opaque) {
    struct chain_ctx *ctx = (struct chain_ctx *)opaque;
    if (!wrap_hello_frame(&ctx->hello, 7, 19, ctx->frame, &ctx->frame_len)) {
        fprintf(stderr, "chain encode failed\n");
        exit(1);
    }
    sink_u8 ^= ctx->frame[0];
}

static void bench_chain_decode(void *opaque) {
    struct chain_ctx *ctx = (struct chain_ctx *)opaque;
    struct v3_hello_payload out;
    if (!unwrap_hello_frame(ctx->frame, ctx->frame_len, &out)) {
        fprintf(stderr, "chain decode failed\n");
        exit(1);
    }
    sink_u8 ^= out.address.digits[0];
}

static void run_bench(const char *name, size_t iters, void (*fn)(void *), void *ctx) {
    uint64_t start = now_ns();
    for (size_t i = 0; i < iters; ++i) {
        fn(ctx);
    }
    uint64_t end = now_ns();
    uint64_t total = end - start;
    double per = (double)total / (double)iters;
    printf("%-18s iters=%-8zu total_ns=%-14llu ns_per_op=%.2f\n",
           name,
           iters,
           (unsigned long long)total,
           per);
    sink_u64 ^= total;
}

static void init_sha_ctx(struct sha_ctx *ctx) {
    fill_bytes(ctx->message, sizeof(ctx->message), 0x31);
    memset(ctx->out, 0, sizeof(ctx->out));
}

static void init_x25519_ctx(struct x25519_ctx *ctx) {
    static const uint8_t priv_a[32] = {
        0x77,0x07,0x6d,0x0a,0x73,0x18,0xa5,0x7d,0x3c,0x16,0xc1,0x72,0x51,0xb2,0x66,0x45,
        0xdf,0x4c,0x2f,0x87,0xeb,0xc0,0x99,0x2a,0xb1,0x77,0xfb,0xa5,0x1d,0xb9,0x2c,0x2a
    };
    static const uint8_t priv_b[32] = {
        0x5d,0xab,0x08,0x7e,0x62,0x4a,0x8a,0x4b,0x79,0xe1,0x7f,0x8b,0x83,0x80,0x0e,0xe6,
        0x6f,0x3b,0xb1,0x29,0x26,0x18,0xb6,0xfd,0x1c,0x2f,0x8b,0x27,0xff,0x88,0xe0,0xeb
    };
    memset(ctx, 0, sizeof(*ctx));
    if (!make_x25519_key(priv_a, &ctx->priv) || !make_x25519_key(priv_b, &ctx->peer)) {
        fprintf(stderr, "x25519 init failed\n");
        exit(1);
    }
}

static void init_p256_ctx(struct p256_ctx *ctx) {
    static const uint8_t priv_be[32] = {
        0x00,0x01,0x02,0x03,0x04,0x05,0x06,0x07,0x08,0x09,0x0a,0x0b,0x0c,0x0d,0x0e,0x0f,
        0x10,0x11,0x12,0x13,0x14,0x15,0x16,0x17,0x18,0x19,0x1a,0x1b,0x1c,0x1d,0x1e,0x1f
    };
    memset(ctx, 0, sizeof(*ctx));
    memcpy(ctx->priv_be, priv_be, 32);
    fill_bytes(ctx->message, sizeof(ctx->message), 0x52);
    if (!make_p256_key(ctx->priv_be, &ctx->key)) {
        fprintf(stderr, "p256 init failed\n");
        exit(1);
    }
    if (!p256_public_bytes(ctx->key, ctx->pub_uncompressed)) {
        fprintf(stderr, "p256 public init failed\n");
        exit(1);
    }
    if (!ecdsa_sign_rfc6979_message(ctx->key, ctx->priv_be, ctx->message, sizeof(ctx->message), &ctx->sig)) {
        fprintf(stderr, "p256 signature init failed\n");
        exit(1);
    }
    if (!ecdsa_verify_message(ctx->key, ctx->message, sizeof(ctx->message), ctx->sig)) {
        fprintf(stderr, "p256 signature verify init failed\n");
        exit(1);
    }
}

static void init_chain_ctx(struct chain_ctx *ctx) {
    memset(ctx, 0, sizeof(*ctx));
    fill_bytes(ctx->hello.peer_id, 32, 0x10);
    ctx->hello.address.depth = 6;
    ctx->hello.address.digits[0] = 0;
    ctx->hello.address.digits[1] = 2;
    ctx->hello.address.digits[2] = 4;
    ctx->hello.address.digits[3] = 6;
    ctx->hello.address.digits[4] = 1;
    ctx->hello.address.digits[5] = 3;
    fill_bytes(ctx->hello.tip_event_cid, 32, 0x20);
    fill_bytes(ctx->hello.summary.state_forest_cid, 32, 0x30);
    fill_bytes(ctx->hello.summary.event_root_cid, 32, 0x40);
    fill_bytes(ctx->hello.summary.account_root_cid, 32, 0x50);
    if (!wrap_hello_frame(&ctx->hello, 7, 19, ctx->frame, &ctx->frame_len)) {
        fprintf(stderr, "chain frame init failed\n");
        exit(1);
    }
}

static void cleanup_x25519_ctx(struct x25519_ctx *ctx) {
    EVP_PKEY_free(ctx->priv);
    EVP_PKEY_free(ctx->peer);
}

static void cleanup_p256_ctx(struct p256_ctx *ctx) {
    ECDSA_SIG_free(ctx->sig);
    EC_KEY_free(ctx->key);
}

int main(void) {
    struct sha_ctx sha_ctx;
    struct x25519_ctx x25519_ctx;
    struct p256_ctx p256_ctx;
    struct chain_ctx chain_ctx;

    init_sha_ctx(&sha_ctx);
    init_x25519_ctx(&x25519_ctx);
    init_p256_ctx(&p256_ctx);
    init_chain_ctx(&chain_ctx);

    run_bench("sha256", 200000, bench_sha256, &sha_ctx);
    run_bench("x25519_shared", 10000, bench_x25519, &x25519_ctx);
    run_bench("p256_pubkey", 5000, bench_p256_pubkey, &p256_ctx);
    run_bench("p256_sign", 1000, bench_p256_sign, &p256_ctx);
    run_bench("p256_verify", 2000, bench_p256_verify, &p256_ctx);
    run_bench("chain_encode", 200000, bench_chain_encode, &chain_ctx);
    run_bench("chain_decode", 200000, bench_chain_decode, &chain_ctx);

    cleanup_x25519_ctx(&x25519_ctx);
    cleanup_p256_ctx(&p256_ctx);

    printf("sink=%llu/%u\n",
           (unsigned long long)sink_u64,
           (unsigned int)sink_u8);
    return 0;
}
