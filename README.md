[中文版](README.zh.md)

The main license of this repository was changed to Apache 2.0 on July 6, 2026.

> After consideration, using GPL for these little-known repositories I created is clearly overkill. For developers who might stumble upon them and attempt to use them in closed-source projects, proper freedom should be granted.

---

# avb_mini_signer (formerly avb_autosign) — Simple AVB 2.0 RSA4096 add_hash_footer Implementation
This repository has multiple branches. Please switch to the appropriate branch according to your linking requirements. 
This branch uses mbedTLS as the crypto backend, which can be built small and statically linked. Runs well in constrained environments with zero dependency on system libraries or runtime.

**Additional recommendation** avbroot (GPLv3): Another similar project with more complete features - https://github.com/chenxiaolong/avbroot

## Usage

```bash
./avb_mini_signer <partition_name> <partition_size> <image_path>
```

Functionally equivalent to:
```bash
avbtool.py add_hash_footer \
  --partition_name <name> \
  --image <path> \
  --algorithm SHA256_RSA4096 \
  --key <embedded> \
  --partition_size <size>
```

**Note:** Output overwrites the original image file in-place.

## Building a Native Android Executable

1. Clone this repository.
2. Download the Android NDK (r27d recommended).
3. Enter the project directory and run `ndk-build`.
4. Locate the built binary under `libs/`.

```bash
wget https://googledownloads.cn/android/repository/android-ndk-r27d-linux.zip
unzip android-ndk-r27d-linux.zip -d ~/
export PATH=~/android-ndk-r27d:$PATH

git clone --depth=1 --branch mbedtls https://github.com/Manatsu0721/avb_mini_signer.git
cd avb_mini_signer
ndk-build APP_CFLAGS='-DVERSION=\"<Your version number>\"'
```

## Building mbedTLS (libmbedcrypto) from Source

1. Download the mbedTLS source (from mbedtls-3.6).
2. Build it.

> For cross-compilation to other architectures, configure the cross-compiler toolchain via environment variables beforehand.

```bash
git clone --branch mbedtls-3.6 https://github.com/Mbed-TLS/mbedtls.git
cd mbedtls
make CFLAGS="-O2 -ffunction-sections -fdata-sections"
```
3. It is actually recommended to directly use the file list in `jni/mbedtls/library` of this branch for a minimal build. This project integrates those individual functional sources directly.
```bash
SRC="aes.c asn1parse.c asn1write.c base64.c bignum.c bignum_core.c \
     bignum_mod.c bignum_mod_raw.c cipher.c cipher_wrap.c constant_time.c \
     md.c oid.c pem.c pk.c pkparse.c pk_wrap.c platform.c platform_util.c \
     rsa.c rsa_alt_helpers.c sha256.c"
     
cd "jni/mbedtls/library/"
for f in $SRC; do
  echo "  $f"
  <your_compiler> -c -std=c99 -O2 -fPIC -fvisibility=hidden -fdata-sections -ffunction-sections \
    -I"../../../jni/mbedtls/include" \
    -DMBEDTLS_CONFIG_FILE='"avb_config.h"' \
    "./$f"     2>&1 || exit 1
done

ar rcs ../../../libmbedcrypto.a *.o
rm -f ../../../*.o
cd ../../..
```

## Embedding a Custom Private Key

1. Ensure you have a SHA256_RSA4096 private key.
2. Use `xxd -i` to convert it into an array that the compiler can recognize and save it as key.c.

```bash
xxd -i private_key.pem >key.c
```
> Please note whether the symbols declared with `extern` at the beginning of `main.c` are consistent with those provided in `key.c` just now. The filename of the `.pem` you enter will directly determine it.

---

![extra.jpg](https://raw.githubusercontent.com/Manatsu0721/Manatsu0721/main/%E5%85%B3%E8%81%94%E5%85%B6%E5%AE%83%E4%BB%93%E5%BA%93%E7%9A%84%E6%8C%81%E4%B9%85%E6%96%87%E4%BB%B6/avb_mini_signer/d88eb5f15609c61cf7dd3d2e5fa73d8a.jpg)
> Image: Subaru (Blue Archive)
> © put_buri / X (x.com/put_buri) — please contact for removal if needed