LOCAL_PATH := $(call my-dir)

# ---- 根据 ABI 选择对应的库文件 ----
ifeq ($(TARGET_ARCH_ABI),armeabi-v7a)
    CRYPTO_LIB := libcrypto_32.so
else ifeq ($(TARGET_ARCH_ABI),arm64-v8a)
    CRYPTO_LIB := libcrypto.so
else
    $(error Unsupported ABI: $(TARGET_ARCH_ABI))
endif

# ---- STUB libcrypto ----
include $(CLEAR_VARS)
LOCAL_MODULE := libcrypto
LOCAL_SRC_FILES := boringssl/$(CRYPTO_LIB)
LOCAL_EXPORT_C_INCLUDES := $(LOCAL_PATH)/boringssl/include
include $(PREBUILT_SHARED_LIBRARY)

# ---- 主可执行文件 ----
include $(CLEAR_VARS)
LOCAL_MODULE := avb_mini_signer
LOCAL_SRC_FILES := main.c key.c
LOCAL_C_INCLUDES := $(LOCAL_PATH)/boringssl/include
LOCAL_SHARED_LIBRARIES := libcrypto
LOCAL_CFLAGS := -O2 -fdata-sections -ffunction-sections -fvisibility=hidden -std=c11 -fPIE -Wall -Wextra
LOCAL_LDFLAGS := -pie \
                 -Wl,--build-id=md5 \
                 -Wl,--gc-sections \
                 -Wl,-Map=$(LOCAL_PATH)/../obj/local/$(TARGET_ARCH_ABI)/output.map
include $(BUILD_EXECUTABLE)