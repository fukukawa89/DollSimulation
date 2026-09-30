# 静态采集恢复：提交与审查入口

本次整理 `90e5104` 之后尚未提交的 UE 插件与模拟器改动，提交到本仓库 `main`。与硬件设计仓库 O15 交付对应，但插件中的恢复改动起源于 O5，并未更改 PDS1/1 线格式。

## 改动行为

取消或完成一次采集后，其合法迟到 ACK/SCAN 不再进入新请求的稳定窗口。Transport 在完成 CRC、结构、身份、启动标识和数据状态验证后，按 capture_id 分流；保留最多 32 个已结束 ID、最长 30 秒。重复取消不延长保留期，重连清空记录；未知、过期、被淘汰的 ID 和坏包仍拒绝。诊断状态增加 `retired_replies`。

UE 的稳定窗口、漂移与新鲜度判断、3 秒请求超时、一次性 Capture、Undo/Redo 和遮罩行为保持原要求。实现与范围详见 [恢复说明](O5_RECOVERY.zh-CN.md)和 [PDS1 协议](PDS1_PROTOCOL.md)。

主要文件：`PoseDollStatic.h/.cpp`、`PoseDollTransport.h/.cpp`、`PoseDollSession.cpp`、Python `static_protocol.py`；新增原生 Router 测试、Python 迟到/坏包测试，以及隔离 UE 网络重采脚本。

## 提交前验证（2026-09-30）

- 在当前源码上重新运行模拟器测试：**75 项通过**，包含 14 项新增恢复与严格拒绝用例。
- 当前插件全部 **26 个源文件**与已执行非 Unity/Unity 构建及原生测试所用 O5 源码哈希逐个匹配。
- 读取已封存真实 UE 报告：8 组迟到/重采网络用例、PDG5 桥接、原编辑器回归、保存后重开均为通过。此次整理没有重新运行 UE 编译或网络集成测试。
- O15 的合成 PDG15 → 正式桥接 → 真实 UE 正常/缺失/CRC/重启联调证据在设计仓库；这不等于串口、MCU 或实体硬件已测试。

复跑 Python：

```powershell
.\Tools\PoseDollSimulator\.venv\Scripts\python.exe -m pytest Tools/PoseDollSimulator/tests -q -p no:cacheprovider
```

`Scripts/test_static_late_o5.py` 会创建测试地图和序列，仅在隔离测试项目运行；不要在用户正在使用的场景上直接执行。

## 对应证据

设计仓库固定提交：`c0d3ec0d7c547c00ad75669ed502ea8cd642b117`。

- [O15 总审查入口](https://github.com/fukawachan/PoseDollLab_Design/blob/c0d3ec0d7c547c00ad75669ed502ea8cd642b117/Hardware/PoseDoll44/docs/REVIEW_HANDOFF_O15.zh-CN.md)
- [O5 已测试插件源码哈希与结果](https://github.com/fukawachan/PoseDollLab_Design/blob/c0d3ec0d7c547c00ad75669ed502ea8cd642b117/Hardware/PoseDoll44/verification/revO5/runs/o5_20260924_r1/results_revO5.json)
- [O5 完整编译与集成证据目录](https://github.com/fukawachan/PoseDollLab_Design/tree/c0d3ec0d7c547c00ad75669ed502ea8cd642b117/Hardware/PoseDoll44/verification/revO5/runs/o5_20260924_r1)
- [O15 UE 桥接结果](https://github.com/fukawachan/PoseDollLab_Design/blob/c0d3ec0d7c547c00ad75669ed502ea8cd642b117/Hardware/PoseDoll44/generated/revO15/runs/o15_20260929_r1/ue_bridge_result.json)

上述远端链接需要用户先推送对应设计提交。原证据存储工作文件原始字节，插件仓库按既有 `.gitattributes` 在 Git 中规范化为 LF；不能把纯换行差异误判为算法变更。

`Config/DefaultEditor.ini` 是本轮前已有的用户预览设置，已确认与当时记录的哈希相同，保留本地未提交。UE 资产、测试生成内容和编译缓存继续排除在源码提交之外。
