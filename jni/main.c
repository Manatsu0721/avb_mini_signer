/*
 * avb_mini_signer — AVB 2.0 add_hash_footer 实现 (mbedTLS 驱动)
 *
 * 用法: avb_mini_signer <partition_name> <partition_size> <image_path>
 *
 * 私钥通过 ld -r -b binary 嵌入
 * 依赖: mbedTLS (RSA, SHA256, PEM, BIGNUM)
 */

#define MBEDTLS_ALLOW_PRIVATE_ACCESS
#define MBEDTLS_CONFIG_FILE "avb_config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <time.h>
#include <mbedtls/pk.h>
#include <mbedtls/rsa.h>
#include <mbedtls/sha256.h>
#include <mbedtls/bignum.h>

/* ========== 1. 嵌入的私钥 ========== */
extern char _binary_subaru_key_pem_start[];
extern char _binary_subaru_key_pem_end[];

/* ========== 2. AVB 常数 ========== */
#define AVB_FOOTER_MAGIC      "AVBf"
#define AVB_FOOTER_MAGIC_LEN  4
#define AVB_MAGIC             "AVB0"
#define AVB_MAGIC_LEN         4

/* 算法类型 (与 avb_crypto.h 保持一致) */
#define AVB_ALG_TYPE_SHA256_RSA4096  2

/* 算法参数 */
#define HASH_NUM_BYTES       32   /* SHA256 */
#define SIG_NUM_BYTES        512  /* RSA4096 */
#define PUBKEY_NUM_BYTES     1032 /* 8 + 2*4096/8 */

/* AVB 对齐约束 */
#define AVB_ALIGNMENT        64

/* 各结构 reserved 大小 (与 avbtool.py FORMAT_STRING 一致) */
#define FOOTER_RESERVED      28
#define VBMETA_HEADER_RESERVED  80
#define HASH_DESC_RESERVED   60

/* PKCS#1 v1.5 padding for SHA256_RSA4096
 *   0x00 0x01 [458 x 0xff] 0x00 [ASN.1 19 bytes] [digest 32 bytes]
 *   总长 = 512 bytes
 *   ASN.1 = 30 31 30 0d 06 09 60 86 48 01 65 03 04 02 01 05 00 04 20
 */
static const uint8_t PKCS1_ASN1_SHA256[] = {
    0x30, 0x31, 0x30, 0x0d, 0x06, 0x09, 0x60, 0x86,
    0x48, 0x01, 0x65, 0x03, 0x04, 0x02, 0x01, 0x05,
    0x00, 0x04, 0x20
};

static void build_pkcs1_padding(const uint8_t *digest, uint8_t *out) {
    out[0] = 0x00;
    out[1] = 0x01;
    memset(out + 2, 0xff, 458);
    out[460] = 0x00;
    memcpy(out + 461, PKCS1_ASN1_SHA256, 19);
    memcpy(out + 480, digest, 32);
}

/* ========== 3. AVB 结构体定义 ========== */

/* --- AvbFooter (64 bytes) --- */
typedef struct __attribute__((packed)) {
    uint8_t  magic[AVB_FOOTER_MAGIC_LEN];
    uint32_t version_major;
    uint32_t version_minor;
    uint64_t original_image_size;
    uint64_t vbmeta_offset;
    uint64_t vbmeta_size;
    uint8_t  reserved[FOOTER_RESERVED];
} AvbFooter;

/* --- AvbVBMetaImageHeader (256 bytes) --- */
typedef struct __attribute__((packed)) {
    uint8_t  magic[AVB_MAGIC_LEN];
    uint32_t required_libavb_version_major;
    uint32_t required_libavb_version_minor;
    uint64_t authentication_data_block_size;
    uint64_t auxiliary_data_block_size;
    uint32_t algorithm_type;
    uint64_t hash_offset;
    uint64_t hash_size;
    uint64_t signature_offset;
    uint64_t signature_size;
    uint64_t public_key_offset;
    uint64_t public_key_size;
    uint64_t public_key_metadata_offset;
    uint64_t public_key_metadata_size;
    uint64_t descriptors_offset;
    uint64_t descriptors_size;
    uint64_t rollback_index;
    uint32_t flags;
    uint32_t rollback_index_location;
    char     release_string[48];
    uint8_t  reserved[VBMETA_HEADER_RESERVED];
} AvbVBMetaHeader;

/* --- AvbHashDescriptor (固定部分 132 bytes) --- */
/* 序列化: [tag(8)][nbf(8)][image_size(8)][hash_algo(32)]
 *         [name_len(4)][salt_len(4)][digest_len(4)][flags(4)]
 *         [reserved(60)]
 *         [name][salt][digest][padding(to 8)]
 */
typedef struct __attribute__((packed)) {
    uint64_t tag;
    uint64_t num_bytes_following;
    uint64_t image_size;
    char     hash_algorithm[32];
    uint32_t partition_name_len;
    uint32_t salt_len;
    uint32_t digest_len;
    uint32_t flags;
    uint8_t  reserved[HASH_DESC_RESERVED];
} AvbHashDescriptorFixed;

/* ========== 4. 辅助函数 ========== */

static uint64_t round_up(uint64_t v, uint64_t align) {
    return (v + align - 1) & ~(align - 1);
}

/* 大端序列化辅助 (AVB 磁盘格式为大端) */

static inline void put_u32_be(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)(v);
}

static inline void put_u64_be(uint8_t *p, uint64_t v) {
    p[0] = (uint8_t)(v >> 56);
    p[1] = (uint8_t)(v >> 48);
    p[2] = (uint8_t)(v >> 40);
    p[3] = (uint8_t)(v >> 32);
    p[4] = (uint8_t)(v >> 24);
    p[5] = (uint8_t)(v >> 16);
    p[6] = (uint8_t)(v >> 8);
    p[7] = (uint8_t)(v);
}

/* 简单 RNG (用于 mbedtls blinding, 只用一次, 不需要密码学安全) */
static int simple_rng(void *ctx, unsigned char *buf, size_t len) {
    (void)ctx;
    static unsigned int seed = 0;
    if (seed == 0) seed = (unsigned int)((unsigned long)&seed ^ time(NULL) ^ getpid());
    for (size_t i = 0; i < len; i++) {
        seed = seed * 1103515245U + 12345U;
        buf[i] = (unsigned char)(seed >> 16);
    }
    return 0;
}

static int load_embedded_key(mbedtls_pk_context *pk) {
    long raw_len = _binary_subaru_key_pem_end - _binary_subaru_key_pem_start;
    /* mbedtls PEM 解析要求 null-terminated 字符串 */
    unsigned char *key_buf = malloc(raw_len + 1);
    if (!key_buf) return -1;
    memcpy(key_buf, _binary_subaru_key_pem_start, raw_len);
    key_buf[raw_len] = '\0';

    int ret = mbedtls_pk_parse_key(pk, key_buf, raw_len + 1,
                                   NULL, 0, NULL, NULL);
    free(key_buf);
    if (ret != 0) {
        fprintf(stderr, "Failed to parse key: -0x%04x\n", (unsigned)-ret);
        return -1;
    }
    if (mbedtls_pk_get_type(pk) != MBEDTLS_PK_RSA) {
        fprintf(stderr, "Key is not RSA\n");
        return -1;
    }
    /* 确保 RSA context 内部参数完整 (设置 len, 检查 CRT 参数等) */
    mbedtls_rsa_context *rsa = mbedtls_pk_rsa(*pk);
    if ((ret = mbedtls_rsa_complete(rsa)) != 0) {
        fprintf(stderr, "rsa_complete failed: -0x%04x\n", (unsigned)-ret);
        return -1;
    }
    return 0;
}

static uint8_t* read_file(const char *path, size_t *out_size) {
    FILE *fp = fopen(path, "rb");
    if (!fp) { perror("fopen"); return NULL; }
    fseek(fp, 0, SEEK_END);
    long size = ftell(fp);
    if (size < 0) { perror("ftell"); fclose(fp); return NULL; }
    rewind(fp);
    uint8_t *buf = malloc(size);
    if (!buf) { fclose(fp); return NULL; }
    if (fread(buf, 1, size, fp) != (size_t)size) {
        perror("fread"); free(buf); fclose(fp); return NULL;
    }
    fclose(fp);
    *out_size = (size_t)size;
    return buf;
}

static int write_file_at(FILE *fp, uint64_t offset,
                         const uint8_t *data, size_t size)
{
    if (fseeko(fp, offset, SEEK_SET) != 0) {
        perror("fseeko"); return -1;
    }
    if (fwrite(data, 1, size, fp) != size) {
        perror("fwrite"); return -1;
    }
    return 0;
}

/* 扩展文件到指定大小，用零填充 */
static int extend_file(FILE *fp, uint64_t new_size) {
    if (fseeko(fp, 0, SEEK_END) != 0) return -1;
    uint64_t cur = ftello(fp);
    if (cur >= new_size) return 0;
    /* 一次 write 一个零字节，然后 ftruncate 到目标大小 */
    uint8_t zero = 0;
    if (fwrite(&zero, 1, 1, fp) != 1) return -1;
    fflush(fp);
    if (ftruncate(fileno(fp), new_size) != 0) {
        perror("ftruncate"); return -1;
    }
    return 0;
}

/* ========== 5. AVB RSA 公开密钥编码 ========== */

/*
 * AvbRSAPublicKeyHeader (8 bytes) + modulus (key_bits/8) + rrmodn (key_bits/8)
 * struct __attribute__((packed)) {
 *     uint32_t key_num_bits;
 *     uint32_t n0inv;     // -1/n[0] mod 2^32
 *     uint8_t  modulus[key_num_bits/8];
 *     uint8_t  rrmodn[key_num_bits/8];
 * };
 */
static int encode_avb_pubkey(const mbedtls_rsa_context *rsa,
                             uint8_t *out, size_t *out_len)
{
    const mbedtls_mpi *n = &rsa->N;

    int key_bits = mbedtls_mpi_bitlen(n);
    /* 向上取整到 2 的幂 */
    int key_bits_rounded = 1;
    while (key_bits_rounded < key_bits) key_bits_rounded <<= 1;

    if (key_bits_rounded != 4096) {
        fprintf(stderr, "Key bits=%d, expected 4096\n", key_bits_rounded);
        return -1;
    }

    int key_bytes = key_bits_rounded / 8;

    /* 计算 n0inv = -1/n[0] mod 2^32 (Montgomery inverse of low word)
     * 直接从 bn2bin 提取 modulus 的低 32 位 (大端表示的最后 4 字节) */
    uint8_t mod_buf[512];
    memset(mod_buf, 0, sizeof(mod_buf));
    mbedtls_mpi_write_binary(n, mod_buf, key_bytes);

    uint32_t n0 = ((uint32_t)mod_buf[key_bytes-4] << 24) |
                  ((uint32_t)mod_buf[key_bytes-3] << 16) |
                  ((uint32_t)mod_buf[key_bytes-2] << 8)  |
                  ((uint32_t)mod_buf[key_bytes-1]);
    /* Newton iteration for modular inverse: inv = inv * (2 - n0 * inv) (mod 2^32)
     * n0 is always odd for RSA, so inverse exists */
    uint32_t inv = 1U;
    for (int i = 0; i < 5; i++) inv = inv * (2 - n0 * inv);
    uint32_t n0inv = 0U - inv;

    /* 计算 rr = r^2 mod N, 其中 r = 2^key_bits */
    mbedtls_mpi r_mpi, rr_mpi;
    mbedtls_mpi_init(&r_mpi);
    mbedtls_mpi_init(&rr_mpi);

    mbedtls_mpi_set_bit(&r_mpi, key_bits_rounded, 1);
    mbedtls_mpi_exp_mod(&rr_mpi, &r_mpi, &r_mpi, n, NULL);

    uint8_t rr_buf[512];
    memset(rr_buf, 0, sizeof(rr_buf));
    mbedtls_mpi_write_binary(&rr_mpi, rr_buf, key_bytes);

    mbedtls_mpi_free(&r_mpi);
    mbedtls_mpi_free(&rr_mpi);

    /* 写入输出: [key_num_bits(4)][n0inv(4)][modulus(key_bytes)][rr(key_bytes)] */
    put_u32_be(out, key_bits_rounded);
    put_u32_be(out + 4, n0inv);
    memcpy(out + 8, mod_buf, key_bytes);
    memcpy(out + 8 + key_bytes, rr_buf, key_bytes);
    *out_len = 8 + 2 * key_bytes;

    return 0;
}

/* 将 AvbVBMetaHeader 序列化为大端字节数组 (256 bytes) */
static void encode_header_be(const AvbVBMetaHeader *hdr, uint8_t *out) {
    int o = 0;
    memcpy(out + o, hdr->magic, 4); o += 4;
    put_u32_be(out + o, hdr->required_libavb_version_major); o += 4;
    put_u32_be(out + o, hdr->required_libavb_version_minor); o += 4;
    put_u64_be(out + o, hdr->authentication_data_block_size); o += 8;
    put_u64_be(out + o, hdr->auxiliary_data_block_size); o += 8;
    put_u32_be(out + o, hdr->algorithm_type); o += 4;
    put_u64_be(out + o, hdr->hash_offset); o += 8;
    put_u64_be(out + o, hdr->hash_size); o += 8;
    put_u64_be(out + o, hdr->signature_offset); o += 8;
    put_u64_be(out + o, hdr->signature_size); o += 8;
    put_u64_be(out + o, hdr->public_key_offset); o += 8;
    put_u64_be(out + o, hdr->public_key_size); o += 8;
    put_u64_be(out + o, hdr->public_key_metadata_offset); o += 8;
    put_u64_be(out + o, hdr->public_key_metadata_size); o += 8;
    put_u64_be(out + o, hdr->descriptors_offset); o += 8;
    put_u64_be(out + o, hdr->descriptors_size); o += 8;
    put_u64_be(out + o, hdr->rollback_index); o += 8;
    put_u32_be(out + o, hdr->flags); o += 4;
    put_u32_be(out + o, hdr->rollback_index_location); o += 4;
    memcpy(out + o, hdr->release_string, 48); o += 48;
    memset(out + o, 0, 80); /* reserved */
}

/* 将 AvbFooter 序列化为大端字节数组 (64 bytes) */
static void encode_footer_be(const AvbFooter *footer, uint8_t *out) {
    int o = 0;
    memcpy(out + o, footer->magic, 4); o += 4;
    put_u32_be(out + o, footer->version_major); o += 4;
    put_u32_be(out + o, footer->version_minor); o += 4;
    put_u64_be(out + o, footer->original_image_size); o += 8;
    put_u64_be(out + o, footer->vbmeta_offset); o += 8;
    put_u64_be(out + o, footer->vbmeta_size); o += 8;
    memset(out + o, 0, FOOTER_RESERVED); /* reserved */
}

/* ========== 6. AVB 签名 (mbedTLS raw RSA) ========== */

/*
 * avbtool.py 的签名方式:
 *   digest = SHA256(data_to_sign)
 *   padded_block = PKCS1_padding + digest
 *   signature = RSA_private_encrypt(padded_block, RSA_NO_PADDING)
 *   等价于: openssl rsautl -sign -raw -inkey key.pem
 */
static int avb_sign(mbedtls_rsa_context *rsa,
                    const uint8_t *data_to_sign, size_t data_len,
                    uint8_t *signature, size_t *sig_len)
{
    /* 1. SHA256(data_to_sign) */
    uint8_t digest[32];
    mbedtls_sha256(data_to_sign, data_len, digest, 0);

    /* 2. 构建 PKCS#1 v1.5 填充块
     *    布局: [0x00][0x01][458 x 0xff][0x00][19 ASN.1 header][32 digest]
     *    total = 512 bytes
     */
    uint8_t padded[SIG_NUM_BYTES];
    build_pkcs1_padding(digest, padded);

    /* 3. Raw RSA signing (RSA_NO_PADDING) */
    int ret = mbedtls_rsa_private(rsa, simple_rng, NULL, padded, signature);
    if (ret != 0) {
        fprintf(stderr, "mbedtls_rsa_private failed: -0x%04x\n", (unsigned)-ret);
        return -1;
    }

    *sig_len = SIG_NUM_BYTES;
    return 0;
}

/* ========== 7. Hash Descriptor 编码 ========== */

static size_t encode_hash_descriptor(const char *partition_name,
                                     size_t image_size,
                                     const uint8_t *salt, size_t salt_len,
                                     const uint8_t *digest, size_t digest_len,
                                     uint8_t *out, size_t out_cap)
{
    size_t name_len = strlen(partition_name);
    /* 固定部分 132 bytes (大端序列化) */
    size_t fixed_size = 132;
    size_t var_size = name_len + salt_len + digest_len;
    size_t total = fixed_size + var_size;
    size_t padded_total = round_up(total, 8);
    size_t padding = padded_total - total;

    if (out_cap < padded_total) {
        fprintf(stderr, "Hash descriptor buffer too small\n");
        return 0;
    }

    int o = 0;
    put_u64_be(out + o, 2); o += 8;  /* tag = AVB_DESCRIPTOR_TAG_HASH */
    put_u64_be(out + o, padded_total - 16); o += 8;  /* num_bytes_following */
    put_u64_be(out + o, image_size); o += 8;
    memset(out + o, 0, 32);
    strcpy((char*)out + o, "sha256"); o += 32;
    put_u32_be(out + o, (uint32_t)name_len); o += 4;
    put_u32_be(out + o, (uint32_t)salt_len); o += 4;
    put_u32_be(out + o, (uint32_t)digest_len); o += 4;
    put_u32_be(out + o, 0); o += 4; /* flags = 0 */
    memset(out + o, 0, 60); o += 60; /* reserved */

    /* 变长部分 */
    memcpy(out + o, partition_name, name_len); o += name_len;
    memcpy(out + o, salt, salt_len); o += salt_len;
    memcpy(out + o, digest, digest_len); o += digest_len;
    memset(out + o, 0, padding);

    return padded_total;
}

/* ========== generate_vbmeta: 完整 VBMeta 生成 ========== */

static uint8_t* generate_vbmeta(const char *partition_name,
                                const uint8_t *image_data, size_t image_size,
                                const mbedtls_rsa_context *rsa,
                                size_t *out_size)
{
    /* 1. 生成随机盐 */
    uint8_t salt[32];
    {
        FILE *ur = fopen("/dev/urandom", "rb");
        if (!ur) {
            memset(salt, 0, 32);
        } else {
            if (fread(salt, 1, 32, ur) != 32) memset(salt, 0, 32);
            fclose(ur);
        }
    }

    /* 2. 计算图像 digest = SHA256(salt + image_data) */
    uint8_t digest[32];
    {
        mbedtls_sha256_context ctx;
        mbedtls_sha256_init(&ctx);
        mbedtls_sha256_starts(&ctx, 0);
        mbedtls_sha256_update(&ctx, salt, 32);
        mbedtls_sha256_update(&ctx, image_data, image_size);
        mbedtls_sha256_finish(&ctx, digest);
        mbedtls_sha256_free(&ctx);
    }

    /* 3. 编码 hash descriptor */
    size_t desc_buf_size = 1024;
    uint8_t *desc_buf = malloc(desc_buf_size);
    if (!desc_buf) return NULL;

    size_t desc_size = encode_hash_descriptor(
        partition_name, image_size,
        salt, 32, digest, 32,
        desc_buf, desc_buf_size);

    if (desc_size == 0) {
        free(desc_buf);
        return NULL;
    }

    /* 4. 编码公开密钥 */
    uint8_t pubkey_buf[PUBKEY_NUM_BYTES];
    size_t pubkey_size = 0;
    if (encode_avb_pubkey(rsa, pubkey_buf, &pubkey_size) != 0) {
        free(desc_buf);
        return NULL;
    }

    /* 5. 构建 Auxiliary Data Block
     *    布局: descriptors at offset 0, public key after */
    size_t aux_data_size = desc_size + pubkey_size;
    size_t aux_block_size = round_up(aux_data_size, AVB_ALIGNMENT);
    uint8_t *aux_block = calloc(1, aux_block_size);
    if (!aux_block) { free(desc_buf); return NULL; }
    memcpy(aux_block, desc_buf, desc_size);
    memcpy(aux_block + desc_size, pubkey_buf, pubkey_size);
    free(desc_buf);

    /* 6. 构建 VBMeta Header */
    AvbVBMetaHeader hdr;
    memset(&hdr, 0, sizeof(hdr));
    memcpy(hdr.magic, AVB_MAGIC, AVB_MAGIC_LEN);
    hdr.required_libavb_version_major = 1;
    hdr.required_libavb_version_minor = 0;
    hdr.algorithm_type = AVB_ALG_TYPE_SHA256_RSA4096;
    hdr.rollback_index = 0;
    hdr.flags = 0;
    hdr.rollback_index_location = 0;
    strcpy(hdr.release_string, "avb_mini_signer 1.0");

    /* Aux block offsets */
    hdr.auxiliary_data_block_size = aux_block_size;
    hdr.descriptors_offset = 0;
    hdr.descriptors_size = desc_size;
    hdr.public_key_offset = desc_size;
    hdr.public_key_size = pubkey_size;
    hdr.public_key_metadata_offset = desc_size + pubkey_size;
    hdr.public_key_metadata_size = 0;

    /* Auth block: hash + signature */
    size_t auth_data_size = HASH_NUM_BYTES + SIG_NUM_BYTES;
    size_t auth_block_size = round_up(auth_data_size, AVB_ALIGNMENT);
    hdr.authentication_data_block_size = auth_block_size;
    hdr.hash_offset = 0;
    hdr.hash_size = HASH_NUM_BYTES;
    hdr.signature_offset = HASH_NUM_BYTES;
    hdr.signature_size = SIG_NUM_BYTES;

    /* 7. 序列化 header (大端) */
    uint8_t hdr_blob[256];
    encode_header_be(&hdr, hdr_blob);

    /* 8. 计算 hash = SHA256(header_blob + aux_block) */
    size_t hash_input_size = 256 + aux_block_size;
    uint8_t *hash_input = malloc(hash_input_size);
    if (!hash_input) { free(aux_block); return NULL; }
    memcpy(hash_input, hdr_blob, 256);
    memcpy(hash_input + 256, aux_block, aux_block_size);

    uint8_t auth_hash[32];
    mbedtls_sha256(hash_input, hash_input_size, auth_hash, 0);

    /* 9. 签名 hash */
    uint8_t signature[SIG_NUM_BYTES];
    size_t sig_len = 0;
    if (avb_sign((mbedtls_rsa_context*)rsa, hash_input, hash_input_size,
                 signature, &sig_len) != 0) {
        free(aux_block);
        free(hash_input);
        return NULL;
    }
    free(hash_input);

    /* 10. 构建 Authentication Data Block */
    uint8_t *auth_block = calloc(1, auth_block_size);
    if (!auth_block) { free(aux_block); return NULL; }
    memcpy(auth_block, auth_hash, HASH_NUM_BYTES);
    memcpy(auth_block + HASH_NUM_BYTES, signature, SIG_NUM_BYTES);

    /* 11. 组装 VBMeta Blob: header(256) + auth_block + aux_block */
    size_t total = 256 + auth_block_size + aux_block_size;
    uint8_t *vbmeta = malloc(total);
    if (!vbmeta) { free(auth_block); free(aux_block); return NULL; }
    memcpy(vbmeta, hdr_blob, 256);
    memcpy(vbmeta + 256, auth_block, auth_block_size);
    memcpy(vbmeta + 256 + auth_block_size, aux_block, aux_block_size);

    free(auth_block);
    free(aux_block);

    *out_size = total;
    return vbmeta;
}

/* ========== 9. 主函数 ========== */
/* 最大元数据空间估计（用于空间预检）
 * 我们的实际 vbmeta = 2112 bytes, footer = 64 bytes, 
 * 但为了安全（盐随机变化导致 desc 大小微调）用宽松值 */

#define MAX_METADATA_ESTIMATE 8192

int main(int argc, char **argv)
{
    if (argc != 4) {
        fprintf(stderr, "avb_mini_signer 1.0.1 - Linked mbedtls.\n""- Konoka (Manatsu0721@github)\n\n");
        fprintf(stderr, "Usage: %s <partition_name> <partition_size> <image_path>\n"
                        "Example: %s boot 0x200000 boot.img\n"
                        "         %s system 1048576000 system.img\n",
                argv[0], argv[0], argv[0]);
        return 1;
    }

    const char *partition_name = argv[1];
    uint64_t partition_size = strtoull(argv[2], NULL, 0);
    if (partition_size == 0 || partition_size % 4096 != 0) {
        fprintf(stderr, "Invalid partition size: %s\n", argv[2]);
        return 1;
    }
    const char *image_path = argv[3];

    /* 1. 加载私钥 */
    mbedtls_pk_context pk;
    mbedtls_pk_init(&pk);
    if (load_embedded_key(&pk) != 0) { mbedtls_pk_free(&pk); return 1; }
    mbedtls_rsa_context *rsa = mbedtls_pk_rsa(pk);
    if (!rsa) { fprintf(stderr, "No RSA context\n"); mbedtls_pk_free(&pk); return 1; }

    /* 2. 读取镜像文件 */
    size_t file_size;
    uint8_t *image_data = read_file(image_path, &file_size);
    if (!image_data) { mbedtls_pk_free(&pk); return 1; }

    /* 3. 检测并剥离旧 AVB footer */
    uint64_t orig_size = file_size;
    if (file_size >= sizeof(AvbFooter)) {
        uint8_t *footer_area = image_data + file_size - sizeof(AvbFooter);
        if (memcmp(footer_area, AVB_FOOTER_MAGIC, AVB_FOOTER_MAGIC_LEN) == 0) {
            uint64_t old_orig = 0;
            for (int i = 0; i < 8; i++) old_orig = (old_orig << 8) | footer_area[12 + i];
            printf("Found existing footer (original_size=%llu), stripping\n",
                   (unsigned long long)old_orig);
            orig_size = old_orig;
        }
    }

    /* 4. 末尾零扫描 */
    if (orig_size == file_size && file_size > 0) {
        size_t scan = file_size;
        while (scan > 0) { scan--; if (image_data[scan] != 0) { scan++; break; } }
        if (scan < file_size) {
            size_t trimmed = round_up(scan, 4096);
            if (trimmed < file_size) {
                printf("Trimmed trailing zeros: %zu -> %zu bytes\n", file_size, trimmed);
                orig_size = trimmed;
            }
        }
    }

    /* 5. 空间预检 */
    if (orig_size > partition_size - MAX_METADATA_ESTIMATE) {
        fprintf(stderr,
                "ERROR: Image size (%llu) exceeds maximum image size (%llu)\n"
                "       for partition size %llu (need %llu bytes for metadata).\n",
                (unsigned long long)orig_size,
                (unsigned long long)(partition_size - MAX_METADATA_ESTIMATE),
                (unsigned long long)partition_size,
                (unsigned long long)MAX_METADATA_ESTIMATE);
        free(image_data); mbedtls_pk_free(&pk); return 1;
    }

    printf("Image: %s (%zu bytes, partition %llu)\n",
           image_path, orig_size, (unsigned long long)partition_size);

    /* 6. 生成 VBMeta blob */
    size_t vbmeta_size;
    uint8_t *vbmeta = generate_vbmeta(partition_name, image_data, orig_size,
                                      rsa, &vbmeta_size);
    if (!vbmeta) {
        fprintf(stderr, "Failed to generate VBMeta blob\n");
        free(image_data); mbedtls_pk_free(&pk); return 1;
    }

    size_t vbmeta_padded = round_up(vbmeta_size, 4096);
    uint64_t total_meta = vbmeta_padded + sizeof(AvbFooter);
    if (orig_size + total_meta > partition_size) {
        fprintf(stderr, "ERROR: Image + metadata (%llu + %llu = %llu) exceeds "
                        "partition size %llu\n",
                (unsigned long long)orig_size,
                (unsigned long long)total_meta,
                (unsigned long long)(orig_size + total_meta),
                (unsigned long long)partition_size);
        free(vbmeta); free(image_data); mbedtls_pk_free(&pk); return 1;
    }

    printf("VBMeta blob: %zu bytes\n", vbmeta_size);

    /* 7. 写入镜像文件 */
    FILE *fp = fopen(image_path, "rb+");
    if (!fp) { perror("fopen"); free(vbmeta); free(image_data); mbedtls_pk_free(&pk); return 1; }
    if (extend_file(fp, partition_size) != 0) {
        fclose(fp); free(vbmeta); free(image_data); mbedtls_pk_free(&pk); return 1;
    }

    uint64_t vbmeta_off = partition_size - sizeof(AvbFooter) - vbmeta_padded;
    if (orig_size > vbmeta_off) vbmeta_off = round_up(orig_size, 4096);

    uint8_t *vbmeta_buf = calloc(1, vbmeta_padded);
    if (!vbmeta_buf) { fclose(fp); free(vbmeta); free(image_data); mbedtls_pk_free(&pk); return 1; }
    memcpy(vbmeta_buf, vbmeta, vbmeta_size);
    if (write_file_at(fp, vbmeta_off, vbmeta_buf, vbmeta_padded) != 0) {
        free(vbmeta_buf); fclose(fp); free(vbmeta); free(image_data); mbedtls_pk_free(&pk); return 1;
    }
    free(vbmeta_buf);

    /* 8. 写入 Footer */
    AvbFooter footer;
    memset(&footer, 0, sizeof(footer));
    memcpy(footer.magic, AVB_FOOTER_MAGIC, AVB_FOOTER_MAGIC_LEN);
    footer.version_major = 1;
    footer.version_minor = 0;
    footer.original_image_size = orig_size;
    footer.vbmeta_offset = vbmeta_off;
    footer.vbmeta_size = vbmeta_size;

    uint8_t ftr[sizeof(AvbFooter)];
    encode_footer_be(&footer, ftr);

    uint64_t footer_off = partition_size - sizeof(AvbFooter);
    if (write_file_at(fp, footer_off, ftr, sizeof(ftr)) != 0) {
        fprintf(stderr, "Failed to write footer\n");
        fclose(fp); free(vbmeta); free(image_data); mbedtls_pk_free(&pk); return 1;
    }
    fclose(fp);

    printf("Successfully signed %s (partition=%s, offset=%llu, vbmeta=%zu bytes)\n",
           image_path, partition_name,
           (unsigned long long)vbmeta_off, vbmeta_size);

    free(vbmeta); free(image_data);
    mbedtls_pk_free(&pk);
    return 0;
}
