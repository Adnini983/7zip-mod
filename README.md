# 7-Zip MOD — TorrentZip 打包器 + PopCap PAK + Unreal Engine PAK

基于 **7-Zip 26.03**（最新稳定版）的三项改造：

1. **TorrentZip 打包器**：将 **TorrentZip 引擎**（`trrntzip` v1.3 的 C 实现，
   GitHub `0-wiz-0/trrntzip`）**字节级内嵌**进 Zip 处理器。用本改造的 7z 生成的
   TorrentZip 归档，与独立 `trrntzip.exe` 的输出**逐字节一致**（已用 SHA-256 验证）。
2. **PopCap PAK 支持**：新增 `pak` 归档处理器，可解压 / 打包 PopCap 游戏（如
   *Plants vs. Zombies*）的 `.pak` 文件，输出与参考工具 `pvz-bintools/pakc`
   **逐字节一致**（已用 SHA-256 验证）。
3. **Unreal Engine PAK 支持（`pak(UE4)`）**：以**内建格式**提供 UE4/UE5 `.pak`
   的读取与写入（handler 编入 `7z.dll` 与 `7zFM.exe`，与 PopCap
   `pak` 同机制）。读取自动识别 Pak 版本（v2–v9）并支持 **zlib 解压**；写入支持
   **v2–v9 全版本**（简单索引布局），默认 store-only，在「参数」/命令行加 `zlib`
   即启用 **zlib 块压缩**（v8+）。7zFM 压缩对话框以「压缩方法」下拉选择目标 UE
   版本，并锁定无关压缩参数。

本仓库是**可直接构建的完整源码树**（不含任何编译产物）。

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
- **UE4 PAK 属性页**：7zFM 预览 `.pak(UE4)` 归档时属性页「类型」显示
  **pak(UE4)**；归档注释携带 Pak 版本、文件数与压缩方法名。
- **UE4 PAK 字节级**：store 写入与 `repak`（MIT OR Apache-2.0，仅作字节对照）
  布局一致：数据区 → 简单索引 → FPakInfo；逐文件 SHA-1 校验、
  mount 点 `../../../`、按路径排序；zlib 块压缩结构参照 `panzi/rust-u4pak`
  （MPL-2.0，仅字节格式对照）：64 KiB 固定块、每块独立 zlib 流、
  压缩名表 `CompressionNames[1]="Zlib"`、SHA-1 对压缩数据累计
  （已由独立 Python 实现解析 + zlib 解压交叉验证，见 `A:\tzuepak\verify_zlib.py`）。
- **TorrentZip 字节级复刻**：归档注释 `TORRENTZIPPED-XXXXXXXX`（中央目录 CRC32）、
  条目按 `CanonicalCmp` 排序、固定时间戳 1996-12-24 23:32 GMT+1、统一
  zlib `Z_DEFLATED` + `Z_BEST_COMPRESSION`、去除冗余目录条目。
- **PAK 字节级复刻**：全文件按字节 XOR `0xF7`、`cmp_paths` 排序、Windows FILETIME
  逐项还原、仅存文件不存目录、store-only（不压缩）。
- **无回归**：不触发上述改造时行为与原生 7-Zip 完全一致（已验证）。

---

## 用法

### 命令行（7z.exe）
TorrentZip 模式通过 zip 处理器属性 `tz` 触发：

```
7z a -tzip -mtz  输出.zip 文件...
7z a -tzip -mtz=on 输出.zip 文件...
```

### 图形界面（7zFM）
归档格式选 **zip** 时，压缩对话框的「压缩方法」下拉框里会出现 **TorrentZip**
一项（其它格式无此项）。选中后：

- 压缩级别强制锁定为 **9 - Maximum**（= zlib `Z_BEST_COMPRESSION`）；
- 级别 / 字典大小 / 字长 / 固态块 / 线程 / 内存用量的控件全部置灰禁用；
- 对话框提示「TorrentZip 压缩方法不允许调整详细压缩参数」；
- 压缩方法本身保持可选，可随时切回其它方法。

确认后生成的 zip 与命令行 `-mtz` 输出字节一致。TorrentZip 方法选择不入注册表记忆。

### PopCap PAK（7zFM）
归档格式选 **pak** 时，压缩对话框的「压缩方法」下拉框显示 **PopCap PAK(PC)**，
这是 pak 唯一的、固定的压缩方法：

- 所有详细压缩参数（级别 / 字典 / 字长 / 固态 / 线程 / 内存）控件全部置灰禁用；
- 对话框提示「PopCap PAK(PC) 压缩方法不允许调整详细压缩参数」；
- 压缩方法本身保持可选，可随时切回。

### PopCap PAK 格式要点
- 磁盘签名（已 XOR）：`37 BD 37 4D F7 F7 F7 F7`；
- 明文布局：`magic u32 LE 0xBAC04AC0` + `version u32 LE 0`（8 字节头）→
  索引（`0x00` + 路径长 + ASCII 路径 + 文件大小 u32 + FILETIME u64，每条）
  → `0x80` 结束符 → 按索引顺序的原始文件数据（不压缩）；
- 全文件（含头）整体 XOR `0xF7`；
- 条目按 `cmp_paths` 排序（`-` < 空格 < `.` < 字母数字<大小写不敏感> < 其它 ASCII）；
- 路径用 `\` 分隔、仅 ASCII、单条 < 255 字节、不存目录项、文件大小 < 4 GiB；
- 解包时逐项还原 Windows FILETIME（`GetFileTimeType` 返回 `kWindows`）。

### Unreal Engine PAK（命令行）
格式名 `pak(UE4)`（handler 内建进 `7z.dll`，与 PopCap `pak` 相同机制）：
```
7z a -t"pak(UE4)" -m9  输出.pak 文件...
7z a -t"pak(UE4)" -m7  输出.pak 文件...
7z x 输入.pak  -o输出目录
```
- `-m2`..`-m9` 选择目标 Pak 版本（简单索引布局）；缺省 `-m9`。
  版本与 UE 引擎对应：`v2`=4.0-4.2、`v3`=4.3-4.15、`v4`=4.16-4.19、
  `v5`=4.20、`v7`=4.21、`v8`=4.22-4.24（8A）、`v9`=4.25。
- 默认 **store-only（不压缩）**；加 `-mzlib` 启用 **zlib 块压缩**（仅 v8+，
  因 v2–v7 的 `FPakInfo` 无压缩名表）。小文件（<100 字节）与压缩后不缩小者
  自动回退 store。
- 读取端自动识别 v2–v9 与尾部 footer 标记；**zlib 压缩条目可正常解压**；
  其它压缩方法（Gzip/Oodle/Zstd…）与加密条目报 `UnsupportedMethod`。
- `7z l` 识别 `Type = pak(UE4)`，归档注释给出 Pak 版本；条目「方法」显示
  `Store` / `Zlib`。

### Unreal Engine PAK（7zFM）
归档格式选 **pak(UE4)** 时，「压缩方法」下拉框列出各 **UE 版本**
（`UE4 v2 (4.0-4.2)` … `UE4 v9 (4.25)`，默认选中 v9）：

- 「压缩方法」即 UE 版本选择器（并非压缩算法）；选中后该版本作为
  `-mVersion` 传给处理器；
- 压缩等级 / 字典 / 字长 / 固态 / 线程 / 内存控件全部置灰禁用
  （默认 store-only，这些参数无意义；压缩等级控件同样禁用）；
- 如需 zlib 压缩，在「参数(P)」字段手输 `zlib`（对应 `-mzlib`）；
  压缩等级控件保持禁用（处理器使用 7-Zip 内置 zlib 默认压缩级别）；
- 对话框提示「pak(UE4) 为仅存储格式，压缩参数固定」；
- 压缩方法本身保持可选，可随时切换版本。

### Unreal Engine PAK 格式要点（写入）
- 布局：数据区（每条 `FPakEntry` + 原文/zlib 流）→ 简单索引 → 尾部 `FPakInfo`；
- `FPakEntry`（store）：`offset/compressed/uncompressed/compression(store=0)/hash20`，
  v3+ 追加 `flags(0)+blocksize(0)`；v8A 压缩槽为 u8、其余 u32；
- `FPakEntry`（zlib，v3+）：压缩槽=slot+1（zlib slot 1 → 字段=2），hash20 后
  追加 `块数 u32 + 块表(Start u64,End u64 × N)`（相对 entry 头）+ `flags(0)+blocksize`；
  每块独立 zlib 流（`78 DA` 头 + deflate + Adler32 大端），SHA-1 对压缩数据累计；
- 索引：`mount "../../../"` FString + 条目数 + 每条 `路径 FString + FPakEntry`，
  按路径排序；`FPakInfo`：`guid(16,v7+)/bEncryptedIndex(0,v4+)/magic
  0x5A6F12E1/version/indexOffset/indexSize/hash30/frozen(0,v9)/压缩名`；
  启用 zlib 时压缩名表写 `CompressionNames[1]="Zlib"`（v8A 4 槽 / v8B+ 5 槽，
  其余槽空），否则全空；
- v10/v11（UE5）为二级 path-hash 索引，**写入暂不支持**（读取亦拒绝）；
  后续如需支持需实现 PHI/FDI 生成器。

---

## 仓库结构

```
Github/
├── README.md              # 本文件
├── LICENSE.md             # 许可证汇总说明（GPL 系列合规）
├── NOTICE                 # 归属 / 版权 / 无担保声明
├── COPYING                # GNU GPL v2 全文（trrntzip 引擎）
├── COPYING.LESSER         # GNU LGPL v2.1 全文（7-Zip 主体）
└── src/                   # 完整可构建源码树
    ├── Asm/ C/ CPP/ DOC/  # 7-Zip 26.03 源码（含本改造的全部改动）
    ├── trrntzip-engine/   # 内嵌引擎静态库源码（本仓库新增）
    ├── trrntzip-main/     # trrntzip v1.3 原版源码（GPLv2，含其 COPYING）
    └── zlib-1.2.2/        # zlib 依赖源码（zlib 许可，见其 README）
```
> `pak(UE4)` handler 编入 `7z.dll`（`Bundles/Format7zF/Arc.mak` 的
> `AR_OBJS` 追加 `UEPakHandler.obj`），与 PopCap `pak` 同机制，7zFM/7z.exe
> 无需外部插件即可读写。
```

### 本改造涉及的 7-Zip 改动文件
- `src/CPP/7zip/Archive/Zip/ZipHandlerOut.cpp`、`ZipHandler.h`：
  - `SetProperties` 解析 `tz` 属性；
  - `UpdateItems` 在 TorrentZip 模式下写临时 zip → 关闭 → 引擎原地规范化 →
    复制结果到真实输出流（CLI / GUI 同一路径）。
- `src/CPP/Build.mak`：把 `..\..\..\..\trrntzip-engine\TorrentZipEngine.lib` 加入共享
  `LIBS`（链接在 bundle 目录运行，4 级 `..` 上溯到 `src\`）。
- `src/CPP/7zip/Bundles/Format7zF/makefile`：把 `FileStreams.obj`
  （`COutFileStream` / `CInFileStream`）拉进 7z.dll，供临时文件流使用。
- `src/CPP/7zip/UI/GUI/CompressDialog.{h,cpp,rc,Res.h}`（7zFM 压缩对话框）：
  - zip 的压缩方法下拉框追加合成项 **TorrentZip**（哨兵方法 ID `-2`）；
  - 选中后 `SetTorrentZipMode()` 锁定级别 9、禁用详细参数控件并显示提示；
    OnOK 固定级别 9 并向 `Info.Options` 注入 `tz`；
  - pak 格式下 `SetMethod2()` 特判，方法下拉框显示唯一固定方法 **PopCap PAK(PC)**；
  - 选中后 `SetPakMode()` 禁用全部详细参数控件并显示提示；
  - 提示文案走语言文件（新增 `IDS_COMPRESS_TORRENTZIP_LOCKED`、
    `IDS_COMPRESS_PAK_LOCKED`；zh-cn/zh-tw 已译，其它语言回退 .rc 英文默认）。
- `src/CPP/7zip/Archive/PakHandler.cpp`（新增，PopCap PAK 处理器）：
  - `CXorInStream` / `CXorOutStream` 逐字节 XOR `0xF7` 的读 / 写流；
  - `Open` 顺读明文索引（magic + 条目 + EOF），`Extract` / `GetStream` 走受限流，
    逐项还原 FILETIME；`UpdateItems` 内存拼明文索引、`cmp_paths` 排序、整体 XOR 后
    逐条加密写数据；
  - `REGISTER_ARC_IO("pak","pak",NULL,0xE8,...)` 同时注册解包与打包（格式 ID
    `0xE8` 不与既有 handler 冲突；格式名全小写，便于 `-t pak` 匹配与压缩选单显示）。
- `src/CPP/7zip/Bundles/Format7zF/Arc.mak`：`AR_OBJS` 追加 `PakHandler.obj`
  （Fm 与 Format7zF 共用此文件，故 7z.dll 与 7zFM 同时生效）。
- `src/CPP/7zip/UI/Common/LoadCodecs.h`：`CArcInfoEx` 新增 `Is_Pak()`、
  `Is_UEPak()`。
- `src/CPP/7zip/UI/Agent/Agent.cpp`：`GetArcProp(kpidType)` 对 pak 归档返回显示名
  **PopCap PAK(PC)**（仅属性页「类型」展示层，不影响内部小写格式名 `pak`）。
- `src/CPP/7zip/Archive/UEPakHandler.cpp`（新增，UE4 PAK 处理器，内建）：
  - 读取端：尾部 `FPakInfo` 与 5 字节 footer 标记双模型、自动版本识别
    （v2–v9，简单索引）、`FString`（u32 长度 / 负长 UTF-16）、`FPakEntry`
    （v8A 压缩槽 u8 特判）、store 提取、**zlib 块解压**（`Compress/ZlibDecoder` +
    `Common/StreamObjects`，按压缩名表 `CompressionNames[slot]=="Zlib"` 判定）；
    其它压缩 / 加密条目报 `UnsupportedMethod`；
  - 写入端（`IOutArchive` + `ISetProperties`）：`SetProperties` 收
    `Version/v/m` 属性（含数字串）钳制到 v2–v9，并识别 **`zlib`** 属性启用
    zlib 块压缩（v8+）；`UpdateItems` 逐文件读入、可选 **zlib 块压缩**
    （64 KiB 固定块、每块独立 `Compress/ZlibEncoder` 流、压缩后不缩小则回退
    store、SHA-1 对压缩数据累计）、写数据区、按路径排序的简单索引、
    `FPakInfo`（启用 zlib 时 `CompressionNames[1]="Zlib"`）；create-only、
    >1 GiB 单文件拒绝；
  - `REGISTER_ARC_IO_CLS_NO_SIG("pak(UE4)","pak",NULL,0xE9,...)` 注册读写；
    格式 ID `0xE9` 不与既有冲突；flags=0（保留多文件压缩对话框可见）。
- `src/CPP/7zip/Bundles/Format7zF/Arc.mak`：`AR_OBJS` 追加 `UEPakHandler.obj`
  （Fm 与 Format7zF 共用此文件，故 7z.dll 与 7zFM 同时内置 `pak(UE4)`；
  C 依赖 Sha1/Sha1Opt/CpuArch 已在 `C_OBJS` 与 `Sha1.mak` 中；
  zlib 编解码器 ZlibEncoder/ZlibDecoder obj 已在 `CODEC_OBJS`）。
- `src/CPP/7zip/UI/GUI/CompressDialog.{h,cpp,rc,Res.h}`（UE 追加）：
  - `pak(UE4)` 格式下 `SetMethod2()` 特判，「压缩方法」下拉列出各 UE 版本
    （哨兵 ItemData `-(100+版本)`，`kUEPak_MethodId_Base=-100`）；
  - 选中后 `SetUEPakMode()` 禁用**压缩等级 / 字典/字长/固态/线程/内存**并显示提示
    （新增 `IDS_COMPRESS_UEPAK_LOCKED`、`IDT_COMPRESS_UEPAK_HINT`）；
  - `GetMethodSpec()` 对 UE 版本返回版本数字串，经 `-m{ver}`（属性名 `m`）
    传给处理器选择版本。

### 语言文件
7-Zip 的语言不编译进 exe，7zFM 运行时从同目录 `Lang\` 加载（与官方发行一致）。
本仓库不随附 `Lang\` 目录——它取自官方 26.03 发行包（93 种语言，含简体/繁体
中文）。构建后请从官方 `7z2603-x64.exe` 中解出 `Lang\` 放到 7zFM 同目录。
如需简体/繁体中文里 TorrentZip、PopCap PAK 与 pak(UE4) 的锁定提示，需在
`zh-cn.txt` / `zh-tw.txt` 末尾**依次**追加 `30001`、`30002`、`30003` 三条
（ID 必须 > 文件内已有最大 ID 且严格递增，否则 7-Zip 语言解析器会报
"Error in Lang file"）：

```
30001
TorrentZip 压缩方法不允许调整详细压缩参数
30002
PopCap PAK(PC) 压缩方法不允许调整详细压缩参数
30003
pak(UE4) 为仅存储格式，压缩参数固定，请在上方选择目标 UE4 版本
```

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
| **UE4 PAK 处理器**（`src/CPP/7zip/Archive/UEPakHandler.cpp`，本仓库新增，内建进 `7z.dll`/`7zFM.exe`） | **原创代码，随整体按 GPLv2 分发** | 格式规范逆向自 Unreal Engine `.pak`；参考工具 `trumank/repak`（**MIT OR Apache-2.0**）仅作字节格式对照，本实现为独立编写，未引用其代码 |

要点：

1. 把 GPLv2 的 trrntzip 引擎**并入** 7-Zip（LGPL）后，合并结果须按更严格的
   **GPLv2** 分发。
2. 若分发本改造的**二进制**（`7z.dll` / `7zFM.exe` / `7z.exe`），必须随附本
   源码，或提供明确、可验证的源码获取方式（GPLv2 §3）。
3. 7-Zip 部分仍受其自身 LGPL / unRAR 限制约束，详见 `src/DOC/License.txt`。
4. zlib 1.2.2 仅需保留其版权与许可声明（见 `src/zlib-1.2.2/README`），不并入 GPL。
5. Pak 处理器是本仓库原创代码（基于格式事实逆向编写），不引入 pvz-bintools 的
   GPLv3 代码；随整体合并分发按 GPLv2 处理。
6. 完整归属、版权与无担保声明见 **`NOTICE`**。

---

## 验证记录

- 引擎库与参考 `trrntzip.exe` 输出**字节一致**（eng_test + mod 端到端，SHA-256 相同）。
- `-mtz` / `-mtz=on` 均生效；无 `-mtz` 无回归。
- **PAK 解包**：`7z l` 识别 `Type = pak`；`7z x` 还原 3 个文件内容与
  Windows FILETIME（逐文件 SHA-256 一致，时间戳精确还原）。
- **PAK 打包**：`7z a -tPak` 输出与参考 `pakc` 的 `test.pak` **逐字节一致**
  （SHA-256 `8B74B80E...FC40A71`，121 字节）。
- **UE4 PAK 写入**：`7z a -t"pak(UE4)" -m2..-m9` 各版本 footer 版本号正确、
  体积随版本区分，`7z x` 解出与源文件 SHA-256 一致（v2/v3/v4/v5/v7/v8/v9
  全部往返字节一致）；`-m9` 默认。
- **UE4 PAK 读取**：读取端对自写与 `repak` 生成的 store pak（v2/v7/v8A/v8B/v9）
  均能识别并解包，`7z l` 报告正确 Pak 版本。
- **UE4 PAK zlib**：`-mzlib` 打包（v9）→ `7z l` 条目方法显示 `Store`（不可压缩
  随机文件）与 `Zlib`（可压缩文件，168000→718 字节）；`7z x` 解出与源文件
  SHA-256 一致；**独立 Python 实现**（`A:\tzuepak\verify_zlib.py`，不依赖 7-Zip）
  解析 footer/索引/块表并用 Python `zlib` 逐块解压，两文件均逐字节匹配
  （结构对齐 + 交叉解压已验）。
- 已修 bug（UE）：`-v4` 是分卷参数而非版本（误用），版本经 `-mVersion/-m` 属性
  传递；`MultiByteToUnicodeString` 第二参为 codePage 触发 `Internal Error
  #282228`（改逐字节转 wchar）；PCH 与 `/MP8` 竞争旧 `a.pch` 需清理重建；
  写入钳制 v2–v9（v10/v11 需二级索引，未实现）。
- 已修 bug：引擎头新增成员后未重编译依赖文件导致对象布局错位
  （`_torrentZipMode` 误读为真），干净重建后消除。
- 已修 bug：语言文件末尾追加过小 ID（4021）破坏严格递增顺序导致
  "Error in Lang file"，改用 > 最大 ID 的 30001 后修复；Pak 提示串用 30002 递增追加。
- 已修 bug：PakHandler 用 `IInStream` 宏写错（`Z7_CLASS_IMP_NOQIB_1` → 专用
  `Z7_CLASS_IMP_IInStream`）、`API_FUNC_static_IsArc` 的 `extern "C" {` 漏补
  闭合 `}`、格式 ID 原取 `0xEE` 与 Tar 冲突（改 `0xE8`）后逐一修复。
