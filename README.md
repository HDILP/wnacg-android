# wnacg-android

在 **真·Android 2.3（API 9 / Gingerbread，2010 年设备）** 上搜索并下载 wnacg 漫画。

移植自开源项目 `lanyeeee/wnacg-downloader`（Rust 后端 + Vue 前端）。原版在 Android 2.3 上
根本跑不起来（没有现代 TLS、没有能用的 webview）。本仓库用 **纯 C + BearSSL 0.6**
重写了搜索和单线程下载，编译成一个 **单文件静态原生二进制**，再用一个极薄的 Java 壳
（`minSdk=9`）通过 `Runtime.exec` 调用它——**完全绕开 2.3 的系统 TLS 和 JNI**。

> 功能范围：搜索、按标签搜索、下载整本。单线程、纯命令行逻辑（壳里套一个输入框即可）。

---

## 为什么这样设计

- **BearSSL 静态链接**：自带的 TLS 栈在 API 9 上早就过期，且 2.3 的 `HttpsURLConnection`
  不支持现代 cipher，握手必失败。BearSSL 体积小数百 KB、可静态链接，自己搞定握手。
- **不碰 JNI**：原生逻辑全在 C 里，Java 只负责「解压 assets 里的二进制 → chmod 700 →
  Runtime.exec 接收参数 → 把 stdout/stderr 回显到 TextView」。2.3 的 NDK/JNI 坑太多，
  能不碰就不碰。
- **证书校验默认关闭**：站点的 CA 链不在 2010 年的系统信任库里，且漫画站 TLS 配置多变。
  出于实用主义，二进制里把 x509 校验做成 no-op（只取叶子证书公钥完成握手），仅做
  加密传输、不做身份认证。这是下载工具的取舍，已在代码注释里标清，可加编译开关开启。
- **armeabi（ARMv5TE）**：一个二进制覆盖所有 ARM 安卓机（v5/v7/v8 32 位都能跑）。

---

## 命令行用法（原生二进制）

```
wnacg search   <关键词> [页码]          搜索漫画
wnacg tag      <标签>   [页码]          按标签搜索
wnacg download <漫画ID> [保存目录]      下载整本到目录（单线程）
wnacg detail   <漫画ID>                 打印漫画详情（图数/标签）
```

示例：

```
wnacg search 百合
wnacg download 257351 /sdcard/wnacg
```

> 默认域名：`www.wn09.shop`（在 `src/wnacg.c`、`Makefile`、说明注释里各一处；改域名时三处一起改）。

---

## 本地开发 / 验证（桌面 gcc）

桌面用 **同一份 C 源码 + 同一棵 BearSSL 树** 编译，保证「本机测的逻辑和安卓完全一致」——
安卓版只是换了工具链（`arm-linux-androideabi-gcc`）并加 `-static`，没有第二条代码路径。

```bash
# 1) 编译主机二进制 + 跑解析单测
./build.sh test            # 顺便跑 tests/ 里的样例 HTML 解析测试

# 2) 手动跑真实链路（需要网络可达 wn09.shop）
./wnacg search 百合
./wnacg detail 380585
./wnacg download 380585 /tmp/dl
```

---

## 交叉编译 + 打包 APK

需要 **Android NDK r16b**（最后一个能干净支持 API 9 的 NDK；用 GCC 4.9 预编译 +
`platforms/android-9`）和 **Android SDK**（build-tools 29.0.3 + `platforms;android-9`）。

```bash
export ANDROID_NDK_HOME=/path/to/android-ndk-r16b
export ANDROID_HOME=/path/to/android-sdk

./build-android.sh        # 交叉编译 -> android/app/src/main/assets/wnacg
./packapk.sh              # aapt2 + d8 + apksigner -> android/app/build/outputs/wnacg.apk
```

`packapk.sh` 不依赖 Android Gradle Plugin（AGP 早已不支持 API 9），直接驱动 SDK 的
build-tools 命令行，用一次性 debug keystore 签名，产出的 apk 可直接 `adb install`。

---

## 真机部署

1. 拿到 `wnacg.apk`（自己编，或去 Actions 下载 artifact）。
2. 设备开「未知来源」安装：`设置 → 应用程序 → 未知来源`。
3. `adb install wnacg.apk`（或拷到 sdcard 用文件管理器点装）。
4. 打开 app，输入框里直接打命令，例如 `download 257351 /sdcard/wnacg`，
   点「运行」。图片会按 `<保存目录>/<漫画ID>/0001.webp ...` 存盘。
5. 也可以 `adb shell` 进到 `/data/data/com.wnacg.android/files/` 直接跑 `./wnacg`。

> 存储权限：manifest 已声明 `INTERNET` 和 `WRITE_EXTERNAL_STORAGE`（API 9 是安装期权限，
> 不需运行时弹窗）。若下载到 `/sdcard` 失败，先确认存储卡可写。

---

## 工程结构

```
src/
  wnacg.c   命令行主程序：search/tag/detail/download + URL 编码
  html.c    HTML 解析：搜索结果列表 + imglist（含 fast_img_host 变量替换）
  net.c     HTTP GET、流式分块/定长读取、换行 reader
  tls.c     BearSSL 客户端封装（手动驱动引擎，关闭证书校验）
thirdparty/bearssl-0.6/    BearSSL 0.6（静态库，主机+安卓各编一份）
tests/                  样例 HTML 解析单测（parse_test.c + 样例）
android/app/src/main/   Java 壳（minSdk=9）+ manifest + resources + assets/
build.sh / build-android.sh / packapk.sh   编译与打包脚本
.github/workflows/      CI：ubuntu 上编 NDK/SDK + 出 apk artifact
```

---

## 已知限制

- 单线程下载，大本较慢（2.3 设备本来也不适合并发）。
- 证书不校验（见上）。如要开启，改 `src/tls.c` 的 x509 校验回调。
- 仅验证过 `www.wn09.shop`；换镜像站需同步改三处域名宏。
- NDK r16b 是硬依赖；更老的 NDK 缺 armeabi，更新的 NDK 抬高了最低 API。
