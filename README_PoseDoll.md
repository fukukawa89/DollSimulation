# PoseDoll Lab

本工程包含中文机械人偶模拟器和 UE 5.8.2 PoseDoll 插件。人偶通过按钮采集姿势，结果写成可继续编辑的原生 Control Rig 关键帧。

## 使用

1. 打开包含 Manny 的关卡和顶层 Level Sequence，把角色加入序列。可以使用已经手工编辑过的 `CR_Mannequin_Body` 轨道；插件保留它的名称和已有关键帧。
2. 在关卡中选择角色，打开「窗口 → PoseDoll Lab」，点击「绑定所选角色 / 当前序列」。没有 Rig 轨道时，首次采集会创建一条。
3. 启动 `LaunchSimulator.cmd` 并点击「连接模拟器」；PDS1 静态采集设备使用「连接静态人偶」。
4. 暂停 Sequencer，把播放头放到目标帧，选择采集范围，点击「采集」。采集完成后可直接用 Control Rig 继续修姿势。
5. 「采集并前进」完成一次采集后前进指定帧数，默认 4 帧。它不会启动持续录制。

采集范围包括全身、上半身、下半身、左臂、右臂、左腿、右腿和自定义。下半身默认不含骨盆；需要骨盆时使用全身或自定义。自定义可选择骨盆、腰部、胸部、头颈、左右肩带、上臂、前臂、手腕、大腿、小腿、脚踝和前脚掌。只改手腕时，在自定义中先「清空选择」，再勾选对应手腕。

**采集是覆盖所选关节的相对转角。** 例如只采左手腕，会把人偶手相对前臂的朝向写入角色左手腕，不会把人偶整条手臂的全局朝向带过来，也不会在已修改的手腕角度上累加。已有角色放置、骨骼比例和无关肢体的控制数据保留。

选中父关节后，子关节会按正常层级跟随。例如转动肩膀会带动手腕的位置；未选中不等于锁定世界位置。转换 IK 链时，为保持姿势，同一条链中未选关节的 FK 控制通道也可能需要匹配关键帧。

## IK / FK 与手工编辑

- 所选部位处于 IK 时，先在临时 Rig 中匹配受影响的链，再转换成 FK 并覆盖所选关节；其他肢体继续保持原来的模式。
- 旧 IK 目标在这段 FK 动画中不再驱动角色。插件不删除其他时间的 IK 关键帧。
- 再次手工切换原生 IK/FK 开关时，插件从当前姿势重建控制器，包括手脚目标和肘膝方向。关闭面板或保存重开后，此功能仍适用于已经采集过的轨道。
- 每次采集和原生模式切换都支持一次 Undo / Redo；拖动播放头、播放或收到网络数据不会触发采集写键。

**原生 Rig 的 FK 和 IK 不是完全可逆的。** 任意 FK 扭转不一定能由双骨 IK 或脊柱曲线表达。切换时如果存在超过 0.5° 或 0.1 cm 的差异，插件会显示实际最大偏差，并可撤销恢复。采集对未受所选关节层级影响的骨骼执行回读检查；匹配失败会拒绝或回滚采集，不会当作成功。

正式姿势保存在 `.uasset` 的原生关键帧里。保存后的播放不需要人偶或模拟器连接；继续使用自动 IK/FK 匹配需要启用插件。匹配仅作用于绑定的轨道，以及保存了 PoseDoll 采集标记的轨道。

## 连接与等待中的采集

模拟器使用 `127.0.0.1:39177`；PDS1 静态源使用 `39178`。网络线程只接收数据，不直接操作角色。模拟器输入在点击按钮时读取最近的完整有效样本，超过 250 ms 的旧样本不采集。静态源在点击后启动带请求 ID 的稳定采样，重复点击不会重复写键。

等待期间可继续编辑；手工修改序列、改变播放头、切换目标或采集范围、撤销、取消或断开连接，都会取消该次静态请求。迟到的结果不会覆盖后来的编辑。输入身份、配置、校准、完整性或稳定性校验失败时不写键。

面板不再提供 Live、Clutch、接触锁定和 Placement 编辑工作流。`resume`、`snapshot_clutch` 不再启用持续或增量采集。原有接触求解代码和历史验收脚本仍保留用于诊断，不能用旧的 Live / Clutch 验收结果替代本版本验收。

## O22 文件输入

`Scripts/O22/capture_file.py` 的 `capture(payload_file, target, mask="FullBody", parts=())` 同样支持分区和自定义覆盖。例如 `mask="Custom", parts=("hand_l",)`。

底层 `capture_measured_pose22` 保留原有参数，增加可选的 `capture_mask` 和逗号分隔的 `custom_parts`。Manny / Quinn 仍由各自已验证的 profile 明确选择。O22 不测量骨盆，因此全身采集也保留 UE 中的骨盆姿势，并拒绝自定义骨盆输入。实体资格校验保持原规则；合成测试数据不会被标为实体测量。

## 支持范围

验证版本：**UE 5.8.2 / CL 56702186**。面板针对项目内 `SKM_Manny_Simple` 和 `CR_Mannequin_Body`；运行时层级、参考变换和类型指纹不匹配时拒绝旧映射。

支持一个目标绑定上的一条标准、非叠加 Control Rig 轨道，一个有效可写的 section，完整权重，顶层 Level Sequence、有理显示帧率、Constant / Linear 插值、均匀正缩放及非零 Actor / Component 变换。嵌套 focused sequence、时间扭曲、竞争动画轨道、被禁用的必要通道、只读序列、非均匀或负缩放不属于当前支持范围。

手指、面部、第三方 Rig 自动适配、长时间连续录制、实体设备精度和机械制造尚未由本次修改验证。twist / corrective bones 继续由原生 Rig 管理。

## 构建和验证

```powershell
# 正常关闭 UE 后构建；可传 -EngineRoot 指定引擎
.\Scripts\BuildPoseDoll.ps1
.\Scripts\BuildPoseDoll.ps1 -VerifyUnity
# 独立进程运行采集、重开和模拟 TCP 验收；可加 -IncludeO22
.\Scripts\TestPoseDollEditing.ps1
```

本次 UE 验收脚本均在独立测试目录创建资产：

- `Scripts/test_pose_editing.py`：原有 Rig、覆盖语义、全部范围、六组 IK/FK、局部保持、前后关键帧、撤销及保存。
- `Scripts/test_pose_editing_reopen.py`：前一脚本的结果在新 UE 进程中重开、回读和继续手工编辑。
- `Scripts/test_pose_editing_static.py`：实际 TCP、模拟 PDS1 源、单次按钮采集、手工编辑取消、迟到结果和故障拒绝。
- `Scripts/test_pose_editing_o22.py`：O22 文件分区覆盖及未测量骨盆的保护；需要相邻设计目录中的合成 O22 数据。

使用完整 Editor 的 `-ExecutePythonScript` 启动，不能替换为普通 Python commandlet。报告写入 `reports/pose_editing*.json`，`passed: true` 才表示该项通过。O22 和 TCP 测试均不代表实体硬件验收。

每次面板采集的来源记录写入 `Saved/PoseDoll`，包含采集部位、原始输入、配置 hash、Rig 指纹、实际控制值及时间信息；播放不依赖这些 JSON。架构细节见 [ARCHITECTURE_PoseDoll.md](ARCHITECTURE_PoseDoll.md)。
