[中文版](README.zh.md)

The main license of this repository was changed to Apache 2.0 on July 6, 2026.

---

# avb_mini_signer (formerly avb_autosign) — Simple AVB 2.0 RSA4096 add_hash_footer Implementation
This repository has multiple branches. Please switch to the appropriate branch according to your linking requirements. 
This branch uses openssl/boringssl `libcrypto` as the driver. It can run well in some restricted environments, and the resulting build can be smaller than the static version, but it requires dynamically linking to the system libcrypto.so.

**Additional Recommendation** avbroot (GPLv3): Another similar project with more complete features - https://github.com/chenxiaolong/avbroot

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

git clone --depth=1 --branch boringssl https://github.com/Manatsu0721/avb_mini_signer.git
cd avb_mini_signer
ndk-build APP_CFLAGS='-DVERSION=\"<Your version number>\"'
```

## Can't find a libcrypto or its stub suitable for your own architecture, how to build it yourself?
0. Configure the cross-compiler through environment variables.
1. Download the libcrypto source code (From openssl-1.1.1-1.1.1zh)
2. Configure, while trimming unnecessary features.
3. make. Finally, locate the resulting libcrypto.so.

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
2. Use `xxd -i` to convert it into an array that the compiler can recognize and save it as key.c.

```bash
xxd -i private_key.pem >key.c
```
> Please note whether the symbols declared with `extern` at the beginning of `main.c` are consistent with those provided in `key.c` just now. The filename of the `.pem` you enter will directly determine it.

---

![extra.jpg](https://raw.githubusercontent.com/Manatsu0721/Manatsu0721/main/%E5%85%B3%E8%81%94%E5%85%B6%E5%AE%83%E4%BB%93%E5%BA%93%E7%9A%84%E6%8C%81%E4%B9%85%E6%96%87%E4%BB%B6/avb_mini_signer/d88eb5f15609c61cf7dd3d2e5fa73d8a.jpg)
> Image: Subaru (Blue Archive)
> © put_buri / X (x.com/put_buri) — please contact for removal if needed
