LOCAL_PATH := $(call my-dir)


# ---- mbedTLS 代码，仅需要的子集 ----
MBEDTLS_SRC := aes.c asn1parse.c asn1write.c base64.c bignum.c bignum_core.c \
     bignum_mod.c bignum_mod_raw.c cipher.c cipher_wrap.c constant_time.c \
     md.c oid.c pem.c pk.c pkparse.c pk_wrap.c platform.c platform_util.c \
     rsa.c rsa_alt_helpers.c sha256.c


# ---- 主可执行文件 ----
include $(CLEAR_VARS)
LOCAL_MODULE := avb_mini_signer
LOCAL_SRC_FILES := main.c key.c $(addprefix $(LOCAL_PATH)/mbedtls/library/, $(MBEDTLS_SRC))
LOCAL_C_INCLUDES := $(LOCAL_PATH)/mbedtls/include
LOCAL_CFLAGS := -O2 -fdata-sections -ffunction-sections -fvisibility=hidden \
                -std=c11 -fPIE -Wall -Wextra \
                -DMBEDTLS_CONFIG_FILE=\"avb_config.h\" 
#                -DMBEDTLS_ALLOW_PRIVATE_ACCESS

LOCAL_LDFLAGS := -static \
                 -Wl,--build-id=md5 \
                 -Wl,--gc-sections \
                 -Wl,-Map=output.map

include $(BUILD_EXECUTABLE)
