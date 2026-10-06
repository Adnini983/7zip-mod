# 许可证汇总（License Summary）

本仓库由 GPL 系列的组件构成，**整体按 GNU GPL v2 分发**（因为内嵌了 GPLv2 的
TorrentZip / trrntzip 引擎）。请随附并遵守下列各组件各自的许可条款。

| 组件 | 许可 | 随附文本 |
|------|------|----------|
| TorrentZip 引擎（`src/trrntzip-engine/`、`src/trrntzip-main/`） | **GNU GPL v2** | `COPYING`（本文件顶层，GPLv2 全文）；`src/trrntzip-main/COPYING` |
| 7-Zip 26.03 源码（`src/Asm|C|CPP|DOC`） | **GNU LGPL v2.1+**；`Rar*` 等文件含 unRAR 限制；个别文件 BSD / 公有领域 | `src/DOC/License.txt`（许可声明）、`src/DOC/copying.txt`（LGPL v2.1 全文）、`COPYING.LESSER`（顶层复本） |
| zlib 1.2.2（`src/zlib-1.2.2/`） | **zlib 许可**（非 GPL，需保留版权与许可声明） | `src/zlib-1.2.2/README` |
| Pak 处理器（`src/CPP/7zip/Archive/PakHandler.cpp`，本仓库新增） | **原创代码**，随整体按 GPLv2 分发；格式逆向自 PopCap `.pak`，参考 `Pistonight/pvz-bintools`（GPLv3）仅作格式对照，未引用其代码 | 见本文件第 5 条 |

## 关键条款

1. **整体 GPLv2**：GPLv2 代码（trrntzip 引擎）并入 LGPL 代码（7-Zip）后，合并
   结果按更严格的 GPLv2 分发。
2. **提供对应源码**：若分发本改造的二进制（`7z.dll` / `7zFM.exe` / `7z.exe`），
   必须随附本源码，或提供符合 GPLv2 §3 的源码获取方式。
3. **7-Zip 自身限制**：7-Zip 的 RAR 相关文件遵循「LGPL + unRAR 限制」，
   RAR 压缩实现不可用于 reRAR 等场景，详见 `src/DOC/License.txt`。
4. **zlib**：zlib 1.2.2 以 zlib 许可分发，仅需保留其版权声明，不受 GPL 传染。
5. **Pak 处理器**：`PakHandler.cpp` 为本仓库新增的原创代码，基于 PopCap `.pak`
   格式事实（魔数、XOR、`cmp_paths` 排序等）独立编写，未复制或改编
   `Pistonight/pvz-bintools` 的代码；该参考工具（GPLv3）仅用于字节对照与验证。
   该文件随整体合并分发按 GPLv2 处理。

具体归属、版权与无担保声明见 `NOTICE`。本文件为说明性汇总，不替代各许可证
原文；发生冲突时以各许可证原文为准。
