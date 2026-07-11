[English](README.md)

本仓库主许可证已于2026年7月6日变更为Apache 2.0。
> 经过我的考量，对我创建的这些无人问津的存储库使用GPL显然有些过分，考虑到一些偶然发现它并希望闭源使用的开发者，应该给予恰当的自由度。
---

# avb_mini_signer（旧名称avb_autosign） — 简单的 AVB 2.0 RSA4096 add_hash_footer 实现。
本仓库存在多个分支，请根据链接方式需求自行切换至合适分支。
该分支核心部分使用可以做小且静态链接的mbedTLS驱动。可以很好地在一些受限的环境中运行，能够做到不依赖系统库和程序。

**额外推荐** avbroot (GPLv3)：另一个功能更完整的类似项目 - https://github.com/chenxiaolong/avbroot

# 用法: 
```bash
./avb_mini_signer <partition_name> <partition_size> <image_path>
```

功能等价于:
```bash
   avbtool.py add_hash_footer \
     --partition_name <name> \
     --image <path> \
     --algorithm SHA256_RSA4096 \
     --key <embedded> \
     --partition_size <size>
```
注意：输出直接覆写原镜像文件。

# 怎么利用此项目直接构建得到安卓原生的可执行版本？
1. clone此仓库。
2. 下载Android NDK （推荐使用r27d版本）。
3. 进入项目目录，调用ndk-build。
4. 完成后进入libs/获取需要的可执行文件。
```bash
wget https://googledownloads.cn/android/repository/android-ndk-r27d-linux.zip
unzip android-ndk-r27d-linux.zip -d ~/
export PATH=~/android-ndk-r27d:$PATH

git clone --depth=1 --branch mbedtls https://github.com/Manatsu0721/avb_mini_signer.git
cd avb_mini_signer
ndk-build APP_CFLAGS='-DVERSION=\"<你的版本号>\"'

```

# 关于它所使用的libmbedcrypto，希望获取它的动态/静态库？
1. 下载mbedTLS源码 (From mbedtls-3.6)
2. 构建。

> 若需要配置到其它架构的设备上使用，请先自行通过环境变量配置好交叉编译器。
```bash
git clone --branch mbedtls-3.6 https://github.com/Mbed-TLS/mbedtls.git
cd mbedtls
make CFLAGS="-O2 -ffunction-sections -fdata-sections"
```
3. 其实更推荐直接使用本分支的`jni/mbedtls/library`内的文件列表，以使用最小化构建。本项目便是使用这些单独的功能代码直接集成的。
```bash
SRC="aes.c asn1parse.c asn1write.c base64.c bignum.c bignum_core.c \
     bignum_mod.c bignum_mod_raw.c cipher.c cipher_wrap.c constant_time.c \
     md.c oid.c pem.c pk.c pkparse.c pk_wrap.c platform.c platform_util.c \
     rsa.c rsa_alt_helpers.c sha256.c"
     
cd "jni/mbedtls/library/"
for f in $SRC; do
  echo "  $f"
  <你的编译器> -c -std=c99 -O2 -fPIC -fvisibility=hidden -fdata-sections -ffunction-sections \
    -I"../../../jni/mbedtls/include" \
    -DMBEDTLS_CONFIG_FILE='"avb_config.h"' \
    "./$f"     2>&1 || exit 1
done

ar rcs ../../../libmbedcrypto.a *.o
rm -f ../../../*.o
cd ../../..
```

# 怎么将自己的私钥构建入二进制？
1. 确保你准备的是SHA256_RSA4096 private key
2. 使用`xxd -i`将其转换为编译器可以识别的数组，保存于key.c。

```bash
xxd -i private_key.pem >key.c
```
> 请注意main.c开头extern声明的符号与刚刚的key.c里提供的是否一致。你输入的.pem的文件名会直接决定它。


![extra.jpg](https://raw.githubusercontent.com/Manatsu0721/Manatsu0721/main/%E5%85%B3%E8%81%94%E5%85%B6%E5%AE%83%E4%BB%93%E5%BA%93%E7%9A%84%E6%8C%81%E4%B9%85%E6%96%87%E4%BB%B6/avb_mini_signer/d88eb5f15609c61cf7dd3d2e5fa73d8a.jpg)
> Image: Subaru (Bluearchive)
© put_buri / X（x.com/put_buri），侵删
