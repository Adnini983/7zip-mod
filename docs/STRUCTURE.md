# 仓库结构与本改造的改动

## 仓库结构

```
Github/
├── README.md              # 介绍 + 使用指南 + 编译指南 + 版权声明
├── LICENSE.md             # 许可证汇总说明（GPL 系列合规）
├── NOTICE                 # 归属 / 版权 / 无担保声明
├── COPYING                # GNU GPL v2 全文（trrntzip 引擎）
├── COPYING.LESSER         # GNU LGPL v2.1 全文（7-Zip 主体）
├── docs/                  # 技术文档（本目录）
│   ├── FORMATS.md         # 三项格式的字节级技术要点
│   ├── STRUCTURE.md       # 本文件：仓库结构 + 改动文件清单
│   └── VALIDATION.md      # 验证记录 + 已修 bug
└── src/                   # 完整可构建源码树
    ├── Asm/ C/ CPP/ DOC/  # 7-Zip 26.03 源码（含本改造的全部改动）
    ├── trrntzip-engine/   # 内嵌引擎静态库源码（本仓库新增）
    ├── trrntzip-main/     # trrntzip v1.3 原版源码（GPLv2，含其 COPYING）
    └── zlib-1.2.2/        # zlib 依赖源码（zlib 许可，见其 README）
```

> `pak(UE4)` handler 编入 `7z.dll`（`Bundles/Format7zF/Arc.mak` 的 `AR_OBJS`
> 追加 `UEPakHandler.obj`），与 PopCap `pak` 同机制，7zFM/7z.exe 无需外部插件即可读写。

## 本改造涉及的 7-Zip 改动文件

### TorrentZip

- `src/CPP/7zip/Archive/Zip/ZipHandlerOut.cpp`、`ZipHandler.h`：
  - `SetProperties` 解析 `tz` 属性；
  - `UpdateItems` 在 TorrentZip 模式下写临时 zip → 关闭 → 引擎原地规范化 →
    复制结果到真实输出流（CLI / GUI 同一路径）。
- `src/CPP/Build.mak`：把 `..\..\..\..\trrntzip-engine\TorrentZipEngine.lib` 加入共享
  `LIBS`（链接在 bundle 目录运行，4 级 `..` 上溯到 `src\`）。
- `src/CPP/7zip/Bundles/Format7zF/makefile`：把 `FileStreams.obj`
  （`COutFileStream` / `CInFileStream`）拉进 7z.dll，供临时文件流使用。

### 压缩对话框（共用）

- `src/CPP/7zip/UI/GUI/CompressDialog.{h,cpp,rc,Res.h}`（7zFM 压缩对话框）：
  - zip 的压缩方法下拉框追加合成项 **TorrentZip**（哨兵方法 ID `-2`）；
    选中后 `SetTorrentZipMode()` 锁定级别 9、禁用详细参数控件并显示提示；
    OnOK 固定级别 9 并向 `Info.Options` 注入 `tz`。
  - pak 格式下 `SetMethod2()` 特判，方法下拉框显示唯一固定方法 **PopCap PAK(PC)**；
    选中后 `SetPakMode()` 禁用全部详细参数控件并显示提示。
  - `pak(UE4)` 格式下 `SetMethod2()` 特判，「压缩方法」下拉列出各 UE 版本
    （哨兵 ItemData `-(100+版本)`，`kUEPak_MethodId_Base=-100`）；
    选中后 `SetUEPakMode()` 禁用**压缩等级 / 字典/字长/固态/线程/内存**并显示提示。
  - 提示文案走语言文件（新增 `IDS_COMPRESS_TORRENTZIP_LOCKED`、
    `IDS_COMPRESS_PAK_LOCKED`、`IDS_COMPRESS_UEPAK_LOCKED`；zh-cn/zh-tw 已译，
    其它语言回退 .rc 英文默认）。
  - `GetMethodSpec()` 对 UE 版本返回版本数字串，经 `-m{ver}`（属性名 `m`）
    传给处理器选择版本。

### PopCap PAK

- `src/CPP/7zip/Archive/PakHandler.cpp`（新增，PopCap PAK 处理器）：
  - `CXorInStream` / `CXorOutStream` 逐字节 XOR `0xF7` 的读 / 写流；
  - `Open` 顺读明文索引（magic + 条目 + EOF），`Extract` / `GetStream` 走受限流，
    逐项还原 FILETIME；`UpdateItems` 内存拼明文索引、`cmp_paths` 排序、整体 XOR 后
    逐条加密写数据；
  - `REGISTER_ARC_IO("pak","pak",NULL,0xE8,...)` 同时注册解包与打包（格式 ID
    `0xE8` 不与既有 handler 冲突；格式名全小写，便于 `-t pak` 匹配与压缩选单显示）。
- `src/CPP/7zip/Bundles/Format7zF/Arc.mak`：`AR_OBJS` 追加 `PakHandler.obj`
  （Fm 与 Format7zF 共用此文件，故 7z.dll 与 7zFM 同时生效）。
- `src/CPP/7zip/UI/Common/LoadCodecs.h`：`CArcInfoEx` 新增 `Is_Pak()`、`Is_UEPak()`。
- `src/CPP/7zip/UI/Agent/Agent.cpp`：`GetArcProp(kpidType)` 对 pak 归档返回显示名
  **PopCap PAK(PC)**（仅属性页「类型」展示层，不影响内部小写格式名 `pak`）。

### Unreal Engine PAK

- `src/CPP/7zip/Archive/UEPakHandler.cpp`（新增，UE4 PAK 处理器，内建）：
  - 读取端：尾部 `FPakInfo` 与 5 字节 footer 标记双模型、自动版本识别
    （v2–v9，简单索引）、`FString`（u32 长度 / 负长 UTF-16）、`FPakEntry`
    （v8A 压缩槽 u8 特判）、store 提取、**zlib 块解压**（`Compress/ZlibDecoder` +
    `Common/StreamObjects`，按压缩名表 `CompressionNames[slot]=="Zlib"` 判定）；
    其它压缩 / 加密条目报 `UnsupportedMethod`。
  - 写入端（`IOutArchive` + `ISetProperties`）：`SetProperties` 收
    `Version/v/m` 属性（含数字串）钳制到 v2–v9，并识别 **`zlib`** 属性启用
    zlib 块压缩（v8+）；`UpdateItems` 逐文件读入、可选 **zlib 块压缩**
    （64 KiB 固定块、每块独立 `Compress/ZlibEncoder` 流、压缩后不缩小则回退
    store、SHA-1 对压缩数据累计）、写数据区、按路径排序的简单索引、
    `FPakInfo`（启用 zlib 时 `CompressionNames[1]="Zlib"`）；create-only、
    >1 GiB 单文件拒绝。
  - `REGISTER_ARC_IO_CLS_NO_SIG("pak(UE4)","pak",NULL,0xE9,...)` 注册读写；
    格式 ID `0xE9` 不与既有冲突；flags=0（保留多文件压缩对话框可见）。
- `src/CPP/7zip/Bundles/Format7zF/Arc.mak`：`AR_OBJS` 追加 `UEPakHandler.obj`
  （Fm 与 Format7zF 共用此文件，故 7z.dll 与 7zFM 同时内置 `pak(UE4)`；
  C 依赖 Sha1/Sha1Opt/CpuArch 已在 `C_OBJS` 与 `Sha1.mak` 中；
  zlib 编解码器 ZlibEncoder/ZlibDecoder obj 已在 `CODEC_OBJS`）。

## 语言文件

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
