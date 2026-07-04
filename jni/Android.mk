LOCAL_PATH := $(call my-dir)

# ---- 根据 ABI 选择对应的库文件 ----
ifeq ($(TARGET_ARCH_ABI),armeabi-v7a)
    CRYPTO_LIB := libcrypto_32.a
    KEY_LIB    := subaru_key_armv7a.a
else ifeq ($(TARGET_ARCH_ABI),arm64-v8a)
    CRYPTO_LIB := libcrypto.a
    KEY_LIB    := subaru_key_aarch64.a
else
    $(error Unsupported ABI: $(TARGET_ARCH_ABI))
endif

# ---- libcrypto 预编译库 ----
include $(CLEAR_VARS)
LOCAL_MODULE := libcrypto-prebuilt
LOCAL_SRC_FILES := mini_openssl_prebuilt/$(CRYPTO_LIB)
LOCAL_EXPORT_C_INCLUDES := $(LOCAL_PATH)/mini_openssl_prebuilt/include
include $(PREBUILT_STATIC_LIBRARY)

# ---- private_key_object 预编译库 ----
include $(CLEAR_VARS)
LOCAL_MODULE := private_key_object
LOCAL_SRC_FILES := private_key_object/$(KEY_LIB)
include $(PREBUILT_STATIC_LIBRARY)

# ---- 主可执行文件 ----
include $(CLEAR_VARS)
LOCAL_MODULE := avb_mini_signer
LOCAL_SRC_FILES := main.c
LOCAL_C_INCLUDES := $(LOCAL_PATH)/mini_openssl_prebuilt/include
LOCAL_STATIC_LIBRARIES := libcrypto-prebuilt private_key_object
LOCAL_CFLAGS := -O2 -fdata-sections -ffunction-sections -fvisibility=hidden -std=c11 -fPIE -Wall -Wextra
LOCAL_LDFLAGS := -static \
                 -Wl,--build-id=md5 \
                 -Wl,--gc-sections \
                 -Wl,-Map=output.map
include $(BUILD_EXECUTABLE)