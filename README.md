# avb_mini_signer（旧名称avb_autosign） — 静态链接的 AVB 2.0 RSA4096 add_hash_footer 实现。
可以很好地在一些受限的环境中运行，能够做到不依赖系统库和程序。

用法: 
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

git clone --depth=1 https://github.com/Manatsu0721/avb_mini_signer.git
cd avb_mini_signer
ndk-build

```

# 如何自行构建所需的libcrypto？
1. 下载libcrypto源码 (From openssl-1.1.1-1.1.1zh)
2. Configue，同时裁剪无用功能。
3. make。最后将产物libcrypto.a找出来。

> 若需要配置到其它架构的设备上使用，请先自行通过环境变量配置好交叉编译器。

```bash
wget https://codeload.github.com/kzalewski/openssl-1.1.1/zip/refs/tags/1.1.1zh
unzip 1.1.1zh
cd openssl-1.1.1-1.1.1zh
./Configure <架构> \
  no-shared no-asm no-threads no-err no-sock no-dso \
  no-idea no-camellia no-seed no-bf no-cast no-des no-rc2 no-rc4 no-rc5 \
  no-md2 no-md4 no-ripemd no-mdc2 no-dsa no-dh no-ec no-ecdsa no-ecdh \
  no-tls no-cms no-ocsp no-ct no-comp no-crypto-mdebug no-ec no-ecdsa no-ecdh no-ec2m \
  -O2 -ffunction-sections -fdata-sections 
make
```


# 怎么将自己的私钥构建入二进制？
1. 确保你准备的是SHA256_RSA4096 private key。
2. 使用与目标架构匹配的ld或objcopy转为目标文件。命令示例见下。
3. （如果使用安卓NDK构建）还需要把这个目标文件打包为小静态库，因为ndk-build脚本不支持目标文件直接输入。
```bash
ld.lld -r -b binary -m <架构> subaru_key.pem -o subaru_key_aarch64.o
ar rcs subaru_key_aarch64.a subaru_key_aarch64.o
```
> 请注意main.c开头extern声明的函数与刚刚的目标文件是否一致。你输入的.pem的文件名会直接决定目标文件的符号。


![extra.jpg](https://raw.githubusercontent.com/Manatsu0721/Manatsu0721/main/%E5%85%B3%E8%81%94%E5%85%B6%E5%AE%83%E4%BB%93%E5%BA%93%E7%9A%84%E6%8C%81%E4%B9%85%E6%96%87%E4%BB%B6/avb_mini_signer/d88eb5f15609c61cf7dd3d2e5fa73d8a.jpg)
> Image: Subaru (Bluearchive)
© put_buri / X（x.com/put_buri），侵删
