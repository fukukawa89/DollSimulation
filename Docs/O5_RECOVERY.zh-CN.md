# O5 静态采集恢复

本轮基于 UE `90e5104c63ea2b807ee2ea6e4cb63d21d8773687`。旧 O4 证据对应的 `608f3ea` 不被改写。用户已有的 `Config/DefaultEditor.ini` 改动保持原样。

取消或完成后，旧 capture_id 的合法迟到 ACK/SCAN 由 Transport 的 `FStaticRouter` 丢弃，不送入新采集的稳定窗口。集合最多 32 个 ID，30 s 后过期；重复 cancel/ack 不续期；重连清空。过期、淘汰或未知 ID 仍严格拒绝。先进行帧 CRC、schema、profile/calibration、boot、状态解码，再按 ID 分流，因此坏旧包不会被无条件吞掉。

新请求在同一互斥锁下注册活动 ID 并清除旧接收队列。命令队列 8、接收队列 32 的上限保持。`retired_replies` 是诊断计数。UE 的 500 ms 稳定窗口、漂移判断、最新数据年龄、3 s 超时、Capture/Undo/Redo、遮罩和一次性写入行为没有放宽。

验证位于设计仓库 `Hardware/PoseDoll44/verification/revO5/runs/o5_20260924_r1`：

- 正常非 Unity 与强制 Unity 编译均通过；5 组原生 Automation 通过。
- Python oracle 共 75 项通过，包括 14 项新增恢复/严格拒绝用例。
- 隔离真实 UE 中 8 组网络用例：旧包先于/晚于/穿插新 ACK、拆包、连续 5 次取消；未知 ID、CRC 错误、boot 变化。5 组合法重采各写入一次，3 组错误写入零次。
- 原静态采集、Undo/Redo、蒙版、Clutch、故障拒绝回归及新进程保存重开通过。
- 4 组新增 PDG5 字节 → 正式 Python 桥接 → PDS1 → UE 联调，正确数据写入一次，missing/CRC/boot 三类错误写入零次。来源明确为 simulated，没有硬件。

`Scripts/test_static_late_o5.py` 会创建测试资产，只在隔离项目运行。设计仓库中的 `Tools/PoseDollHardwareBridge/run_ue_bridge_o5.py` 同理。
