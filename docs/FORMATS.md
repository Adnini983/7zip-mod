# 格式技术文档

本文件记录本仓库三项改造所涉及的**文件格式要点**与字节级约定。使用层面请见根目录
`README.md`。

- [TorrentZip（zip 处理器）](#torrentzipzip-处理器)
- [PopCap PAK（pak）](#popcap-pakpak)
- [Unreal Engine PAK（pak(UE4)）](#unreal-engine-pakpakue4)

---

## TorrentZip（zip 处理器）

TorrentZip 模式由 zip 处理器属性 `tz` 触发（命令行 `-mtz` / `-mtz=on`，图形界面在
压缩对话框选 **TorrentZip** 方法）。引擎为 `trrntzip` v1.3 的 C 实现
（`0-wiz-0/trrntzip`），字节级内嵌进 Zip 处理器。

字节级约定：

- 归档注释为 `TORRENTZIPPED-XXXXXXXX`，其中 `XXXXXXXX` 为中央目录
  （central directory）的 CRC32（小端十六进制）。
- 条目按 `CanonicalCmp` 规则排序。
- 时间戳固定为 `1996-12-24 23:32 GMT+1`（本地时间 UTC+1，含 DST 处理）。
- 统一使用 zlib `Z_DEFLATED` 压缩 + `Z_BEST_COMPRESSION` 级别。
- 去除冗余目录条目。
- 与独立 `trrntzip.exe` 输出**逐字节一致**（SHA-256 已验证）。

图形界面锁定规则：

- 压缩级别强制锁定为 **9 - Maximum**（= zlib `Z_BEST_COMPRESSION`）。
- 级别 / 字典大小 / 字长 / 固态块 / 线程 / 内存用量的控件全部置灰禁用。
- 对话框提示「TorrentZip 压缩方法不允许调整详细压缩参数」。
- 压缩方法本身保持可选，可随时切回其它方法；TorrentZip 方法选择不入注册表记忆。

---

## PopCap PAK（pak）

PopCap 游戏的 `.pak`（如 *Plants vs. Zombies*）为整体 XOR 加密的简单索引归档。
写入与参考工具 `pvz-bintools/pakc` **逐字节一致**（SHA-256 `8B74B80E...FC40A71`）。

格式要点：

- 磁盘签名（已 XOR）：`37 BD 37 4D F7 F7 F7 F7`。
- 明文布局：
  - 头部：`magic u32 LE = 0xBAC04AC0` + `version u32 LE = 0`（8 字节）；
  - 索引：每条 `0x00` + 路径长 + ASCII 路径 + 文件大小 u32 + FILETIME u64；
  - `0x80` 结束符；
  - 按索引顺序的原始文件数据（**不压缩**）。
- 全文件（含头）整体 XOR `0xF7`。
- 条目按 `cmp_paths` 排序（`-` < 空格 < `.` < 字母数字<大小写不敏感> < 其它 ASCII）。
- 路径用 `\` 分隔、仅 ASCII、单条 < 255 字节、不存目录项、文件大小 < 4 GiB。
- 解包时逐项还原 Windows FILETIME（`GetFileTimeType` 返回 `kWindows`）。

---

## Unreal Engine PAK（pak(UE4)）

UE4/UE5 `.pak` 处理器，**内建**进 `7z.dll` / `7zFM.exe`（与 PopCap `pak` 同机制，
无需外部插件）。store 写入与 `repak`（MIT OR Apache-2.0）布局一致；zlib 块压缩
结构参照 `panzi/rust-u4pak`（MPL-2.0，均仅作字节格式对照，本实现独立编写）。

### 读取端

- 尾部 `FPakInfo` + 5 字节 footer 标记双模型自动识别版本（v2–v9，简单索引）。
- `FString`：`u32(长度+1) + ASCII + NUL`；负长度 → UTF-16。
- `FPakEntry`：`offset/compressed/uncompressed/compression/hash20`；
  v8A 压缩槽为 u8、其余 u32。
- 压缩名表 `CompressionNames[slot] == "Zlib"` → 按块解压（每块独立 zlib 流，
  7-Zip 内置 `ZlibDecoder`）；其它压缩方法（Gzip/Oodle/Zstd…）与加密条目报
  `UnsupportedMethod`。

### 写入端

写入布局：数据区（每条 `FPakEntry` + 原文/zlib 流）→ 简单索引 → 尾部 `FPakInfo`。

- `FPakEntry`（store）：`offset/compressed/uncompressed/compression(store=0)/hash20`，
  v3+ 追加 `flags(0)+blocksize(0)`；v8A 压缩槽为 u8、其余 u32。
- `FPakEntry`（zlib，v3+）：压缩槽 = slot+1（zlib slot 1 → 字段=2）；hash20 后追加
  `块数 u32 + 块表(Start u64,End u64 × N)`（块偏移相对 entry 头）+
  `flags(0)+blocksize`；每块独立 zlib 流（`78 DA` 头 + deflate + Adler32 大端）；
  SHA-1 对**压缩数据**累计。
- 索引：`mount "../../../"` FString + 条目数 + 每条 `路径 FString + FPakEntry`，
  按路径排序。
- `FPakInfo`：`guid(16,v7+)/bEncryptedIndex(0,v4+)/magic 0x5A6F12E1/version/
  indexOffset/indexSize/hash30/frozen(0,v9)/压缩名`。
  - 启用 zlib 时压缩名表写 `CompressionNames[1]="Zlib"`（v8A 4 槽 / v8B+ 5 槽，
    其余槽空），否则全空。

### 版本与压缩限制

- `-m2`..`-m9` 选择目标 Pak 版本：`v2`=4.0-4.2、`v3`=4.3-4.15、`v4`=4.16-4.19、
  `v5`=4.20、`v7`=4.21、`v8`=4.22-4.24（8A）、`v9`=4.25。
- 默认 **store-only**；`zlib` 属性（命令行 `-mzlib`，UI「参数(P)」手输 `zlib`）启用
  **zlib 块压缩**，**仅 v8+**（v2–v7 的 `FPakInfo` 无压缩名表，无法表达压缩方法，
  此时 `zlib` 被忽略、回退 store）。
- zlib 压缩级别使用 7-Zip 内置 zlib 默认级别（无级别控制）。
- 小文件（<100 字节）与压缩后不缩小者自动回退 store。
- v10/v11（UE5）为二级 path-hash 索引，**写入暂不支持**（读取亦拒绝）；
  后续如需支持需实现 PHI/FDI 生成器。

### 图形界面锁定规则

- 「压缩方法」下拉即 UE 版本选择器（`UE4 v2 (4.0-4.2)` … `UE4 v9 (4.25)`，
  默认 v9），选中后该版本作为 `-mVersion` 传给处理器。
- 压缩等级 / 字典 / 字长 / 固态 / 线程 / 内存控件全部置灰禁用。
- 对话框提示「pak(UE4) 为仅存储格式，压缩参数固定」。
