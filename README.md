# PoseDoll Lab

UE 5.8.2 的机械人偶姿态映射插件，以及独立的中文 Python 桌面模拟器。
模拟器发送 44 路原始传感器角度，UE 独立完成校准、连续角展开、54 节点 FK 和 Manny Control Rig 映射，并将姿势捕获为可编辑的 Sequencer 关键帧。

## 仓库内容

- `Plugins/PoseDoll`：核心算法、TCP 接收、Rig 映射与编辑器面板。
- `Plugins/PoseDollAutomation`：可选的低频自动化测试接口。
- `Tools/PoseDollSimulator`：Python 桌面程序、锁定依赖及单元测试。
- `Shared`：协议、设备配置、Manny 映射和测试数据。
- `Scripts`：安装、构建、启动及 UE 集成验证脚本。
- `Source`、`Config`、`DollSimulation.uproject`：开发工程的源码和配置。

本仓库只保存源码与文本配置。虚拟环境、编译产物、UE 缓存、测试输出以及 `Content` 中的 Manny、地图和已采集序列均由 `.gitignore` 排除。克隆仓库后需重新创建 Python 环境，并自行准备 UE 项目所需资产。

## Python 模拟器

在 Windows 安装 Python **3.13**，然后在仓库根目录执行：

```powershell
.\Scripts\SetupSimulator.ps1
.\LaunchSimulator.cmd
```

安装脚本在 `Tools/PoseDollSimulator/.venv` 创建本地环境，并从 `requirements.lock` 安装依赖。插件与模拟器共用根目录的 `Shared`，请保留目录结构。

## UE 插件

验证版本为 **UE 5.8.2 / CL 56702186**。把 `Plugins/PoseDoll` 放到目标工程的 `Plugins` 下，并把 `Shared` 放到工程根目录。启用 Control Rig、Level Sequence Editor 及 PoseDoll，重新生成项目文件并编译。

当前 Manny 配置依赖以下资产，仓库不包含这些二进制资产：

- `/Game/Characters/Mannequins/Meshes/SKM_Manny_Simple`
- `/Game/Characters/Mannequins/Rigs/CR_Mannequin_Body`

Rig 层级、控制器与参考变换须符合 `Shared/Profiles/manny_body_ue582_v1.json`；不匹配时需重新验证映射。创建包含目标 Manny 的关卡及顶层 Level Sequence，打开「窗口 → PoseDoll Lab」，绑定所选角色和当前序列，连接模拟器，选择全身、分区或自定义范围后点击采集。已有 Control Rig 可继续手工编辑，采集仅在按钮触发时覆盖所选关节。

仓库中的 `DollSimulation.uproject` 保留原开发工程配置，启用了 MCP 相关开发插件。`PoseDollAutomation` 依赖 `ToolsetRegistry` 和 `PythonScriptPlugin`，仅用于自动化验证；普通使用只需主 `PoseDoll` 插件。原开发机的 UE 构建脚本支持指定引擎目录：

```powershell
.\Scripts\BuildPoseDoll.ps1 -EngineRoot 'E:\UnrealEngine\UE_5.8'
```

## 文档与验证

- [本次静态采集恢复提交与审查入口](Docs/O5_COMMIT_REVIEW.zh-CN.md)
- [PDS1 静态采集协议](Docs/PDS1_PROTOCOL.md)与[迟到回复处理说明](Docs/O5_RECOVERY.zh-CN.md)

- [完整使用说明](README_PoseDoll.md)：功能、操作和支持范围；其中示例地图、序列及报告路径指原本地验收工程，不随源码上传。
- [架构说明](ARCHITECTURE_PoseDoll.md)
- Python 测试：`Tools/PoseDollSimulator/tests`
- 原生 UE 测试：`Plugins/PoseDoll/Source/PoseDollCore/Private/PoseDollCoreTests.cpp`
- UE 集成验证：`Scripts/test_*.py`；部分脚本针对原开发工程及测试资产，迁移后需检查路径和准备条件。

当前采集流程与验收脚本见完整使用说明。旧的 Live、Clutch、接触及持续流验收记录仅对应历史版本。仓库不包含生成的日志、截图或报告。
