# 7-Zip MOD — TorrentZip 打包器 + PopCap PAK 支持

基于 **7-Zip 26.03**（最新稳定版）的两项改造：

1. **TorrentZip 打包器**：将 **TorrentZip 引擎**（`trrntzip` v1.3 的 C 实现，
   GitHub `0-wiz-0/trrntzip`）**字节级内嵌**进 Zip 处理器。用本改造的 7z 生成的
   TorrentZip 归档，与独立 `trrntzip.exe` 的输出**逐字节一致**（已用 SHA-256 验证）。
2. **PopCap PAK 支持**：新增 `pak` 归档处理器，可解压 / 打包 PopCap 游戏（如
   *Plants vs. Zombies*）的 `.pak` 文件，输出与参考工具 `pvz-bintools/pakc`
   **逐字节一致**（已用 SHA-256 验证）。

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
- `src/CPP/7zip/UI/Common/LoadCodecs.h`：`CArcInfoEx` 新增 `Is_Pak()`。
- `src/CPP/7zip/UI/Agent/Agent.cpp`：`GetArcProp(kpidType)` 对 pak 归档返回显示名
  **PopCap PAK(PC)**（仅属性页「类型」展示层，不影响内部小写格式名 `pak`）。

### 语言文件
7-Zip 的语言不编译进 exe，7zFM 运行时从同目录 `Lang\` 加载（与官方发行一致）。
本仓库不随附 `Lang\` 目录——它取自官方 26.03 发行包（93 种语言，含简体/繁体
中文）。构建后请从官方 `7z2603-x64.exe` 中解出 `Lang\` 放到 7zFM 同目录。
如需简体/繁体中文里 TorrentZip 与 PopCap PAK 的锁定提示，需在
`zh-cn.txt` / `zh-tw.txt` 末尾**依次**追加 `30001`、`30002` 两条
（ID 必须 > 文件内已有最大 ID 且严格递增，否则 7-Zip 语言解析器会报
"Error in Lang file"）：

```
30001
TorrentZip 压缩方法不允许调整详细压缩参数
30002
PopCap PAK(PC) 压缩方法不允许调整详细压缩参数
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
- 已修 bug：引擎头新增成员后未重编译依赖文件导致对象布局错位
  （`_torrentZipMode` 误读为真），干净重建后消除。
- 已修 bug：语言文件末尾追加过小 ID（4021）破坏严格递增顺序导致
  "Error in Lang file"，改用 > 最大 ID 的 30001 后修复；Pak 提示串用 30002 递增追加。
- 已修 bug：PakHandler 用 `IInStream` 宏写错（`Z7_CLASS_IMP_NOQIB_1` → 专用
  `Z7_CLASS_IMP_IInStream`）、`API_FUNC_static_IsArc` 的 `extern "C" {` 漏补
  闭合 `}`、格式 ID 原取 `0xEE` 与 Tar 冲突（改 `0xE8`）后逐一修复。
