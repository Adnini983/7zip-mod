# 验证记录与已修 Bug

## 验证记录

### TorrentZip

- 引擎库与参考 `trrntzip.exe` 输出**字节一致**（eng_test + mod 端到端，SHA-256 相同）。
- `-mtz` / `-mtz=on` 均生效；无 `-mtz` 无回归。

### PopCap PAK

- **PAK 解包**：`7z l` 识别 `Type = pak`；`7z x` 还原 3 个文件内容与
  Windows FILETIME（逐文件 SHA-256 一致，时间戳精确还原）。
- **PAK 打包**：`7z a -tPak` 输出与参考 `pakc` 的 `test.pak` **逐字节一致**
  （SHA-256 `8B74B80E...FC40A71`，121 字节）。

### Unreal Engine PAK

- **写入**：`7z a -t"pak(UE4)" -m2..-m9` 各版本 footer 版本号正确、体积随版本
  区分，`7z x` 解出与源文件 SHA-256 一致（v2/v3/v4/v5/v7/v8/v9 全部往返字节一致）；
  `-m9` 默认。
- **读取**：读取端对自写与 `repak` 生成的 store pak（v2/v7/v8A/v8B/v9）均能识别
  并解包，`7z l` 报告正确 Pak 版本。
- **zlib**：`-mzlib` 打包（v9）→ `7z l` 条目方法显示 `Store`（不可压缩随机文件）
  与 `Zlib`（可压缩文件，168000→718 字节）；`7z x` 解出与源文件 SHA-256 一致；
  **独立 Python 实现**（`A:\tzuepak\verify_zlib.py`，不依赖 7-Zip）解析
  footer/索引/块表并用 Python `zlib` 逐块解压，两文件均逐字节匹配
  （结构对齐 + 交叉解压已验）。

### 无回归

- 不触发上述改造时行为与原生 7-Zip 完全一致（已验证）。

## 已修 Bug

- （UE）`-v4` 是分卷参数而非版本（误用），版本经 `-mVersion/-m` 属性传递。
- （UE）`MultiByteToUnicodeString` 第二参为 codePage 触发 `Internal Error
  #282228`（改逐字节转 wchar；zlib 压缩名 `"Zlib"` 首次触发）。
- （UE）PCH 与 `/MP8` 竞争旧 `a.pch` 需清理重建。
- （UE）写入钳制 v2–v9（v10/v11 需二级索引，未实现）。
- 引擎头新增成员后未重编译依赖文件导致对象布局错位（`_torrentZipMode` 误读为真），
  干净重建后消除。
- 语言文件末尾追加过小 ID（4021）破坏严格递增顺序导致 "Error in Lang file"，
  改用 > 最大 ID 的 30001 后修复；Pak 提示串用 30002、30003 递增追加。
- PakHandler 用 `IInStream` 宏写错（`Z7_CLASS_IMP_NOQIB_1` → 专用
  `Z7_CLASS_IMP_IInStream`）、`API_FUNC_static_IsArc` 的 `extern "C" {` 漏补闭合
  `}`、格式 ID 原取 `0xEE` 与 Tar 冲突（改 `0xE8`）后逐一修复。
