[中文版](README.zh.md)

The main license of this repository was changed to Apache 2.0 on July 6, 2026.

> After consideration, using GPL for these little-known repositories I created is clearly overkill. For developers who might stumble upon them and attempt to use them in closed-source projects, proper freedom should be granted.
---

# avb_mini_signer (formerly avb_autosign) — Statically Linked AVB 2.0 RSA4096 add_hash_footer Implementation

Runs well in constrained environments with zero dependency on system libraries or runtime.

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

git clone --depth=1 https://github.com/Manatsu0721/avb_mini_signer.git
cd avb_mini_signer
ndk-build
```

## Building libcrypto from Source

1. Download the libcrypto source (OpenSSL 1.1.1 — 1.1.1zh).
2. Configure with minimal feature set.
3. Run `make`. Locate the resulting `libcrypto.a`.

> For cross-compilation to other architectures, configure the cross-compiler toolchain via environment variables beforehand.

```bash
wget https://codeload.github.com/kzalewski/openssl-1.1.1/zip/refs/tags/1.1.1zh
unzip 1.1.1zh
cd openssl-1.1.1-1.1.1zh
./Configure <architecture> \
  no-shared no-asm no-threads no-err no-sock no-dso \
  no-idea no-camellia no-seed no-bf no-cast no-des no-rc2 no-rc4 no-rc5 \
  no-md2 no-md4 no-ripemd no-mdc2 no-dsa no-dh no-ec no-ecdsa no-ecdh \
  no-tls no-cms no-ocsp no-ct no-comp no-crypto-mdebug no-ec no-ecdsa no-ecdh no-ec2m \
  -O2 -ffunction-sections -fdata-sections
make
```

## Embedding a Custom Private Key

1. Ensure you have a SHA256_RSA4096 private key.
2. Use `ld` or `objcopy` (matching your target architecture) to convert the key into an object file. See command examples below.
3. (When using the Android NDK) Package the object file into a static library archive, since the ndk-build script does not accept raw object files as direct input.

```bash
ld.lld -r -b binary -m <architecture> subaru_key.pem -o private_key.o
ar rcs private_key.a private_key.o
```

> Ensure the `extern` symbol declarations in `main.c` match the object file generated from your `.pem` — the symbol name is derived from the input `.pem` filename.

---

![extra.jpg](https://raw.githubusercontent.com/Manatsu0721/Manatsu0721/main/%E5%85%B3%E8%81%94%E5%85%B6%E5%AE%83%E4%BB%93%E5%BA%93%E7%9A%84%E6%8C%81%E4%B9%85%E6%96%87%E4%BB%B6/avb_mini_signer/d88eb5f15609c61cf7dd3d2e5fa73d8a.jpg)
> Image: Subaru (Blue Archive)
> © put_buri / X (x.com/put_buri) — please contact for removal if needed
