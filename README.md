# 7-Zip MOD — TorrentZip 打包器 + PopCap PAK + Unreal Engine PAK

基于 **7-Zip 26.03**（最新稳定版）的三项改造：

1. **TorrentZip 打包器**：将 **TorrentZip 引擎**（`trrntzip` v1.3 的 C 实现，
   GitHub `0-wiz-0/trrntzip`）**字节级内嵌**进 Zip 处理器。用本改造的 7z 生成的
   TorrentZip 归档，与独立 `trrntzip.exe` 的输出**逐字节一致**（已用 SHA-256 验证）。
2. **PopCap PAK 支持**：新增 `pak` 归档处理器，可解压 / 打包 PopCap 游戏（如
   *Plants vs. Zombies*）的 `.pak` 文件，输出与参考工具 `pvz-bintools/pakc`
   **逐字节一致**（已用 SHA-256 验证）。
3. **Unreal Engine PAK 支持（`pak(UE4)`）**：以**内建格式**提供 UE4/UE5 `.pak`
   的读取与写入（handler 编入 `7z.dll` 与 `7zFM.exe`，与 PopCap `pak` 同机制）。
   读取自动识别 Pak 版本（v2–v9）并支持 **zlib 解压**；写入支持 **v2–v9 全版本**
   （简单索引布局），默认 store-only，在「参数」/命令行加 `zlib` 即启用
   **zlib 块压缩**（v8+）。

本仓库是**可直接构建的完整源码树**（不含任何编译产物）。

格式的字节级技术细节见 [`docs/FORMATS.md`](docs/FORMATS.md)，仓库结构与改动文件清单
见 [`docs/STRUCTURE.md`](docs/STRUCTURE.md)，验证记录见 [`docs/VALIDATION.md`](docs/VALIDATION.md)。

---

## 创作说明

本项目由 **豆包 AI（Doubao Work）辅助创作**完成。用户提供改造思路、需求与提示词，
并搭建本地编译与字节对比测试环境；豆包 AI 负责源码改造、构建排错与验证脚本的编写。
涉及的三方代码与依赖版本锁定（见下文「构建」与「许可证」）均由用户指定。

## 功能特性

- **TorrentZip 命令行**：`7z a -tzip -mtz 输出.zip 文件...` 生成 TorrentZip 规范归档。
- **TorrentZip 图形界面（7zFM）**：压缩对话框的「压缩方法」下拉框中新增
  **TorrentZip** 一项；选中后详细压缩参数按 TorrentZip 规范强制锁定，并提示用户不可调整。
- **PopCap PAK 命令行**：`7z a -tPak 输出.pak 文件...` / `7z x 输入.pak`
  分别打包 / 解包；`7z l` 识别 `Type = pak`。
- **PopCap PAK 图形界面（7zFM）**：归档格式选 **pak** 时，压缩对话框的「压缩方法」
  下拉框显示 **PopCap PAK(PC)**（唯一固定方法）；选中后所有详细压缩参数控件置灰
  锁定，并提示用户该方法不可调整参数。
- **PopCap PAK 属性页**：在 7zFM 预览 `.pak` 归档时，属性页「类型」显示
  **PopCap PAK(PC)**（内部格式名保持小写 `pak`，`-t pak` 匹配不受影响）。
- **UE4 PAK 命令行**：`7z a -t"pak(UE4)" -m9 输出.pak 文件...` /
  `7z x 输入.pak` 分别打包 / 解包；`-m2`..`-m9` 选择目标 Pak 版本
  （`-m9` 默认，对应 UE4.25）；加 `-mzlib` 启用 zlib 块压缩（v8+）。
  `7z l` 识别 `Type = pak(UE4)`。
- **UE4 PAK 图形界面（7zFM）**：归档格式选 **pak(UE4)** 时，「压缩方法」下拉框
  显示各 **UE 版本**（`UE4 v2 (4.0-4.2)` … `UE4 v9 (4.25)`，默认 v9）；选中后
  **压缩等级 / 字典 / 字长 / 固态 / 线程 / 内存**控件全部置灰锁定，并提示
  「pak(UE4) 为仅存储格式，压缩参数固定」。如需 zlib 压缩，在「参数(P)」里
  手输 `zlib` 触发（压缩等级控件保持禁用）。
- **无回归**：不触发上述改造时行为与原生 7-Zip 完全一致（已验证）。

---

## 用法

### 命令行（7z.exe）

TorrentZip 模式通过 zip 处理器属性 `tz` 触发：

```
7z a -tzip -mtz  输出.zip 文件...
7z a -tzip -mtz=on 输出.zip 文件...
```

PopCap PAK：

```
7z a -tPak 输出.pak 文件...
7z x 输入.pak
```

Unreal Engine PAK：

```
7z a -t"pak(UE4)" -m9  输出.pak 文件...
7z a -t"pak(UE4)" -mzlib 输出.pak 文件...   # zlib 块压缩（仅 v8+）
7z x 输入.pak -o输出目录
```

- `-m2`..`-m9` 选择目标 Pak 版本；缺省 `-m9`（= UE4.25）。
  版本对应：`v2`=4.0-4.2、`v3`=4.3-4.15、`v4`=4.16-4.19、`v5`=4.20、
  `v7`=4.21、`v8`=4.22-4.24（8A）、`v9`=4.25。
- 默认 **store-only（不压缩）**；加 `-mzlib` 启用 **zlib 块压缩**（仅 v8+）。
  小文件（<100 字节）与压缩后不缩小者自动回退 store。
- 读取端自动识别 v2–v9；**zlib 压缩条目可正常解压**；其它压缩方法
  （Gzip/Oodle/Zstd…）与加密条目报 `UnsupportedMethod`。
- `7z l` 识别 `Type = pak(UE4)`；条目「方法」显示 `Store` / `Zlib`。

### 图形界面（7zFM）

- **TorrentZip**：归档格式选 **zip** 时，「压缩方法」下拉框出现 **TorrentZip**
  一项。选中后压缩级别强制锁定为 **9 - Maximum**，级别 / 字典 / 字长 / 固态块 /
  线程 / 内存控件全部置灰，并提示「TorrentZip 压缩方法不允许调整详细压缩参数」。
  方法本身可随时切回。该方法选择不入注册表记忆。
- **PopCap PAK**：归档格式选 **pak** 时，「压缩方法」下拉框显示 **PopCap PAK(PC)**
  （唯一固定方法）。选中后所有详细压缩参数控件置灰，提示
  「PopCap PAK(PC) 压缩方法不允许调整详细压缩参数」。
- **Unreal Engine PAK**：归档格式选 **pak(UE4)** 时，「压缩方法」下拉框列出各
  **UE 版本**（默认 v9）。「压缩方法」即 UE 版本选择器（并非压缩算法）；
  选中后压缩等级 / 字典 / 字长 / 固态 / 线程 / 内存控件全部置灰，提示
  「pak(UE4) 为仅存储格式，压缩参数固定」。如需 zlib 压缩，在「参数(P)」字段
  手输 `zlib`（对应 `-mzlib`）；压缩等级控件保持禁用。

---

## 构建

### 依赖与工具链

- **zlib 1.2.2**（`src/zlib-1.2.2/`）：trrntzip 的 README 注明只支持 zlib 1.2.2，
  必须使用该版本。用 `win32/Makefile.msc` 编出 `/MT` 静态 `zlib.lib`。
- **CMake 3.12**：仅用于编译 trrntzip 参考程序（`trrntzip-main/`），7-Zip 本体不用。
- 7-Zip 本体用 **nmake**（MSVC x64，`-Wall -WX -MT`），不使用 CMake。

### 1) 构建内嵌引擎库 `TorrentZipEngine.lib`

从 `src/trrntzip-engine/` 编译。`trrntzip.c` 只 `#include` 各实现文件的**头文件**
（`util.h` / `platform.h` / `minizip.h`），**不含** `.c` 实现，因此必须把实现文件与
包装层一起**单独编译**并入库：

- 编译以下 **7 个源**（`TorrentZipEngine.c` 是包装层，内部 `#include trrntzip.c`；
  另需 `logging_quiet.c`、`util.c`、`platform.c`、`minizip\ioapi.c`、
  `minizip\unzip.c`、`minizip\zip.c`）；
- 必需宏（防止符号与 7-Zip 冲突，`getch` 除外），全部源统一使用：

  ```
  /DWIN32 /Dz_crc_t=unsigned
  /Dopendir=TorrentZip_opendir /Dreaddir=TorrentZip_readdir
  /Dclosedir=TorrentZip_closedir /Dmkstemp=TorrentZip_mkstemp
  ```

- `TorrentZipEngine.c` 内需在 `#include trrntzip.c` 之前 `#define TZ_VERSION "1.3"`；
- 用 `lib` 合并 7 个 `.obj` 与 `zlib.lib`（zlib 1.2.2，`/MT`）；
- 产物命名为 **`src/trrntzip-engine/TorrentZipEngine.lib`**（`CPP/Build.mak`
  的 4 级相对路径 `..\..\..\..\trrntzip-engine\TorrentZipEngine.lib` 依此解析）。
- 完整命令见 `scripts/build.ps1` 的 `Build-Engine`（CI 与本机构建共用）。

### 2) 构建 7-Zip（7z.dll / 7zFM.exe）

进入对应 bundle 目录，用 nmake：

```
cmd /c "call vcvars64.bat && cd /d src/CPP/7zip/Bundles/Format7zF && nmake PLATFORM=x64"
cmd /c "call vcvars64.bat && cd /d src/CPP/7zip/Bundles/Fm         && nmake PLATFORM=x64"
```

- `Format7zF` → `7z.dll`（内嵌引擎）；`Fm` → `7zFM.exe`。
- 改动 `.h` 后须删除对应 `.obj`（如 `CompressDialog.obj`、`resource.res`）强制重建，
  否则 nmake 不追踪头依赖会导致对象布局错位（本项目踩过此坑）。

### 3) UE4 PAK 已内建

`pak(UE4)` 处理器随 `7z.dll` / `7zFM.exe` 一起构建（`Arc.mak` 的
`UEPakHandler.obj`），**无需外部插件**。构建步骤仅需重编 `Format7zF`（7z.dll）
与 `Fm`（7zFM.exe）即可，无单独插件产物。

### 4) 语言文件

7-Zip 的语言不编译进 exe，7zFM 运行时从同目录 `Lang\` 加载。本仓库不随附
`Lang\`——取自官方 26.03 发行包（含简体/繁体中文）。构建后从官方 `7z2603-x64.exe`
解出 `Lang\` 放到 7zFM 同目录。

如需中文锁定提示，需在 `zh-cn.txt` / `zh-tw.txt` 末尾**依次**追加 `30001`、`30002`、
`30003` 三条（ID 必须 > 文件内已有最大 ID 且严格递增，否则报 "Error in Lang file"）。
三条文案与完整说明见 [`docs/STRUCTURE.md`](docs/STRUCTURE.md)。

### 参考基准

字节对比用的 `trrntzip.exe` 用 CMake 3.12 编译 `trrntzip-main/` 得到；对比时注意
其异常输入会卡 `getch`，需先删除同目录 `error.log` / `trrntzip.log`。

---

## 许可证（GPL 系列合规）

本仓库由 GPL 系列组件与新增的原始代码组成，**整体对外分发受 GPLv2 约束**：

| 组件 | 许可 | 出处 |
|------|------|------|
| **trrntzip 引擎**（`src/trrntzip-engine`、`src/trrntzip-main`） | **GNU GPL v2** | `0-wiz-0/trrntzip`，随附 `COPYING`（顶层）与 `trrntzip-main/COPYING` |
| **7-Zip 26.03**（`src/Asm|C|CPP|DOC`） | **GNU LGPL v2.1+**，部分文件 unRAR 限制 / BSD | `7-zip.org`，随附 `src/DOC/License.txt`、`src/DOC/copying.txt`、顶层 `COPYING.LESSER` |
| **zlib 1.2.2**（`src/zlib-1.2.2`） | **zlib 许可**（非 GPL，须保留版权声明） | `zlib.net`，随附 `src/zlib-1.2.2/README` |
| **Pak 处理器**（`src/CPP/7zip/Archive/PakHandler.cpp`，本仓库新增） | **原创代码，随整体按 GPLv2 分发** | 格式规范逆向自 PopCap `.pak`；参考工具 `Pistonight/pvz-bintools`（GPLv3）仅作格式对照，本实现为独立编写，未引用其代码 |
| **UE4 PAK 处理器**（`src/CPP/7zip/Archive/UEPakHandler.cpp`，本仓库新增，内建进 `7z.dll`/`7zFM.exe`） | **原创代码，随整体按 GPLv2 分发** | 格式规范逆向自 Unreal Engine `.pak`；参考工具 `trumank/repak`（**MIT OR Apache-2.0**）与 `panzi/rust-u4pak`（**MPL-2.0**）仅作字节格式对照，本实现为独立编写，未引用其代码 |

要点：

1. 把 GPLv2 的 trrntzip 引擎**并入** 7-Zip（LGPL）后，合并结果须按更严格的
   **GPLv2** 分发。
2. 若分发本改造的**二进制**（`7z.dll` / `7zFM.exe` / `7z.exe`），必须随附本
   源码，或提供明确、可验证的源码获取方式（GPLv2 §3）。
3. 7-Zip 部分仍受其自身 LGPL / unRAR 限制约束，详见 `src/DOC/License.txt`。
4. zlib 1.2.2 仅需保留其版权与许可声明（见 `src/zlib-1.2.2/README`），不并入 GPL。
5. Pak / UE4 PAK 处理器是本仓库原创代码（基于格式事实逆向编写），不引入
   pvz-bintools 的 GPLv3 / repak 的 MIT / rust-u4pak 的 MPL 代码；随整体合并分发
   按 GPLv2 处理。
6. 完整归属、版权与无担保声明见 **`NOTICE`**。
