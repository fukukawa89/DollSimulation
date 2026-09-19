# PoseDoll Lab

本项目包含独立的中文 44 轴机械人偶模拟器，以及 UE 5.8.2 的 PoseDoll 插件。模拟器发送原始传感器角度，UE 独立完成校准、连续角展开、54 节点 FK 和真实 Manny Control Rig 映射。预览使用临时实例，点击 Capture 才向正式 Sequencer 写关键帧。

## 开始使用

1. 双击工程根目录的 `LaunchSimulator.cmd`，打开 Python 桌面模拟器。依赖已安装在 `Tools/PoseDollSimulator/.venv`。
2. 在 UE 打开 `/Game/PoseDollLab/PoseDoll_Test` 关卡和 `/Game/PoseDollLab/PoseDoll_Poses` 序列。该序列已提供 24 fps 的 0、4、8 帧全身姿势。
3. 选择关卡中的 `PoseDoll_TestManny`，通过「窗口 → PoseDoll Lab」打开面板，点击「绑定所选角色 / 当前序列」。当前适配对象是项目实际存在的 `SKM_Manny_Simple` 和 `CR_Mannequin_Body`。
4. 点击「连接模拟器」，连接进入 Ready 后点击「Live / 恢复」。改变模拟器的滑块或拖动黄色机械轴旋转环，蓝色源连杆和目标 Manny 会在独立视口中更新。
5. 选择 FullBody、UpperBody 或单肢掩码，把播放头移到要采集的显示帧，点击 Capture。成功后进入 Frozen，可关闭模拟器并继续编辑正式 Control Rig。
6. 「Capture + 前进」默认前进 4 帧，可修改步长。下一次采集前明确点击 Live 或 Clutch。

正式关键帧使用一条名为 `PoseDoll / Manny` 的受管 Control Rig 轨道。每次采集是一项 Undo 事务。关闭面板会释放网络线程、预览 Rig 和绑定；重新打开后重新绑定和连接。

面板打开期间会临时关闭 UE 的后台 CPU 降频，保证操作 Python 模拟器时 UE 仍实时刷新；关闭面板后恢复原来的设置，不写入全局偏好文件。

## 模拟器

- 五个身体部位页包含全部 44 轴，度数输入和滑块同步；协议传输弧度。
- 左键选轴、拖旋转环；右键环绕；滚轮缩放。蓝色为人偶自身左侧，橙色为右侧。
- 支持中立 N pose、左肘/肩/膝测试、非对称全身姿势、镜像、左右对称编辑、保存/加载机械姿势、选轴连续扫动。
- Body35 会明确锁定胸部、肩带和前脚掌的 9 个可选轴，并通过握手声明能力。missing 与 fixed 含义不同。
- 故障注入包含零偏、噪声、量化、卡死、缺失、停流、重复和过期序号、TCP 分包与合并两帧发送。
- 「三维显示本地反解姿势」显示含故障 raw 的本地反解。右侧的反解值属于模拟器诊断；UE 独立结果位于插件面板的「44 路接收与校准诊断」。
- 机械诊断分别列出限位、三轴近奇异和粗略连杆干涉。胶囊半径只是软件假设，不代表真实外壳/CAD 验证。

## 编辑模式与接触锁定

Absolute 把源 N pose 映射到已校准的 Manny N pose。Clutch 记录启用时的源姿势和当前正式控制器基准，将后续增量加到固定基准；更换播放头后需要重新建立基准。

UpperBody 保留腿部和骨盆的局部控制值，不保证脚的世界位置不动。需要脚或手固定时，在有效姿势下点击对应接触按钮。锁定默认为位置和方向同时固定，下面的「方向：锁定 / 自由」按钮可单独关闭方向约束。

接触求解采用不拉伸的解析双骨 IK，结果转换为原有 FK controls，原 Rig 完成最终骨骼求值。界面报告残差和不可达状态，不可达姿势拒绝 Capture。最终 FK/IK 开关和 space 状态随采集写 key。

场景放置使用单独的 XYZ cm 和三个角度，不计入 44 个传感器通道。采集时可创建 `PoseDoll / Placement` 轨道。调整放置前须解除已有接触锁定。

## 连接与故障

模拟器是 `127.0.0.1:39177` TCP 服务端，UE 是客户端。每条 UTF-8 JSON 消息使用四字节大端长度前缀，最大 65,536 字节。必须先通过 profile、calibration、轴顺序和会话身份校验。

250 ms 没有有效样本时保持上一姿势并进入 Stale；1 秒后冻结。重连后进入 Ready，需要明确恢复，避免旧设备数据覆盖手工修改。网络线程只接收最新完整样本，不向 UObject 或 Rig 写值，也不积压补播。

如果无法连接，检查模拟器是否已启动，以及是否已有另一个测试源占用端口。只允许一个模拟器实例使用默认端口。

## 支持范围

当前版本针对 UE **5.8.2 / CL 56702186** 与这套项目内 Manny Rig。运行时层级、参考变换和 control 类型指纹不匹配时拒绝旧映射，需要重新验证。

支持顶层 Level Sequence、24/30/24000:1001 等有理帧率、Constant 与 Linear 插值、均匀正缩放和非零 Actor/Component 变换。嵌套 focused sequence、时间扭曲、非均匀/负缩放、竞争动画轨道和歧义绑定会明确拒绝。

手指、面部、第三方 Rig 自动适配、长时动作录制、真实空间定位及实体机械制造不属于此版本。原生 Rig 的 twist/corrective bones 由原 Rig 管理，插件不重复分配。

## 构建与验证

以下命令在工程目录的 PowerShell 中运行：

```powershell
# 新电脑：使用独立 Python 3.13 创建项目本地环境并安装锁定依赖
.\Scripts\SetupSimulator.ps1

# 先正常关闭 UE，再进行完整构建
.\Scripts\BuildPoseDoll.ps1

# 模拟器单元/网络测试
.\Tools\PoseDollSimulator\.venv\Scripts\python.exe -X utf8 -m pytest .\Tools\PoseDollSimulator\tests -q

# 启动桌面程序
.\Scripts\StartSimulator.ps1
```

UE 测试脚本位于 `Scripts`，报告在 `reports`。`test_capture.py` 会重建专用测试角色及三帧示例，只应在 `/Game/PoseDollLab/` 验收环境运行。`test_capture_reopen.py` 检查新进程重开和手动控制器修改。`run_validation.py` 组合 FK、帧率/掩码、接触和重开测试。

`soak_driver.py` 是 60 Hz 持续测试源，`start_soak_editor.py` 在真实 Slate tick 上执行重连、面板循环、故障和 30 分钟验证。它们通过 `reports/soak_control.json` 和停止信号协调，不用于日常创作。

可选 `PoseDollAutomation` 提供具名的 MCP 业务工具：状态、fixture、Rig 验证、显式会话操作和固定验收套件。主插件和桌面模拟器不需要 MCP，也不调用任何 LLM API。不要把 MCP 作为高频传感器通道。

每次成功 Capture 还会在 `Saved/PoseDoll/` 写入来源记录：raw/q、配置 hash、目标指纹、控制掩码、Clutch 基准、放置、约束、控制值、帧率和误差。序列姿势本身保存在 `.uasset` 关键帧中，不依赖这些 JSON 才能播放。

实际验收结果及已知差异见 `reports/ACCEPTANCE.md`；模块、坐标、Rig 求值和事务细节见 `ARCHITECTURE_PoseDoll.md`。另外提供 `PoseDoll_Contacts` 接触姿势序列和 `PoseDoll_Winding` 连续旋转序列。原始设计保留于相邻的 `PoseDollLab_UE58_Design` 文件夹。
