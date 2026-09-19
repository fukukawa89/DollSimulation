# PoseDoll Lab 实现说明

基线为相邻目录中未改写的 v0.1 设计与协议。实现针对 UE 5.8.2 / CL 56702186；测试证据见 `reports/ACCEPTANCE.md`。

## 数据流和所有权

```text
Qt 机械 q → 校准反函数 → raw 传感器角度 → TCP 60 Hz
  → PoseDollTransport worker：长度帧、JSON、身份/序号校验、最新样本槽
  → Editor game thread：周期展开/校准 → 54 节点机械 FK → 20 语义段
  → Curated Manny Adapter → 临时 Control Rig → 可选接触约束
  → 临时 PoseableMesh 视口
  → 显式 Capture：正式 Level Sequence 的受管 Control Rig 轨道
```

Python 不导入 `unreal`，UE 生产数据路径不启动 Python，也不消费目标骨骼或 controls 网络消息。两侧独立实现机械数学，以原始 golden vectors 交叉验证。

`PoseDollCore` 只处理配置、矩阵、协议解析和连续解码；`PoseDollTransport` 拥有 socket worker；`PoseDollRig` 拥有临时目标实例与接触求解；`PoseDollEditor` 负责会话、Slate、绑定和事务。可选 `PoseDollAutomation` 单独依赖 Toolset Registry 和引擎 Python，仅提供低频测试入口。主插件不依赖 MCP。

worker 不持有 UObject，不调用 Rig 或 Sequencer。接收缓存有长度上限，完整样本覆盖最新槽；game thread 每 tick 至多取最新样本。旧会话、重复/倒序序号、缺失轴和错误 hash 均不会被补成零值。Body35 的固定轴来自明确的 capability 文件。

关闭面板注销预览场景并释放网络源、Rig、目标绑定和约束。断线及新设备 UUID 都取消 Live，重连后必须明确 Resume。Rig VM 编译事件、对象替换、切关卡、删除目标和切换序列会使旧会话失效。

面板生命周期内临时关闭 Editor 后台 CPU 降频，使 Python 模拟器在前台时仍可实时预览；析构时恢复原偏好，不调用 SaveConfig。此设置管理另有 10 次开关的集成验证，覆盖原值为 true 和 false。

## 数学和目标映射

规范空间采用 RH、X 前/Y 左/Z 上、米/弧度、Hamilton xyzw。每轴保留前后刚性变换，按指定机械树顺序相乘。UE 转换统一使用反射 `H = diag(1,-1,1)`，位置换为 cm；姿态执行 `H R H`。目标 Manny 朝向再按 target profile 的 +90° Z 基变换处理。

连续解码先确定周期分支，再应用 zero/sign/ratio 与机械限位；未知多圈分支拒绝猜测。失败样本不会提交半套连续角状态。配置 hash 基于原始文件内容，不用重新序列化后的 JSON 替代。

target profile 固定到项目内真实 `SKM_Manny_Simple`、`CR_Mannequin_Body`。23 个 controls 对应骨盆、脊柱、颈头、肩带、双臂、双腿与前脚掌；源 N pose 经目标参考骨骼校准，不直接当作导入 A pose。躯干与颈部分配采用共同参考基中的旋转份额，不在每根骨上重复完整角度。

层级指纹包含骨骼、controls、null/space、参考变换、类型和父子关系。当前已验证 SHA-256 为：

`8178edddcdbf500319aa1ce6b548fa9633bfd0ecf244122ecdeacb65d8d8e9f6`

标准 FullBody FK 每帧执行一次原 Rig Forward Solve。转换 controls 前，依据校准骨长预测躯干、肩带和 FK space 的枢轴；手臂和头部空间的方向按原 Rig 的实际规则跟随 body_ctrl，其他空间跟随对应躯干骨骼。14 个 FK/IK、伸缩和 local 开关显式配置并按掩码写 key。最终骨骼仍由原 Rig 求解。未修改原 Rig 图，也没有在其蒙皮结果上强行覆盖骨骼。260 组单轴/组合回读覆盖此优化。

原 Rig 自有 twist/corrective bones 继续由原 Rig 管理。基础旋转误差判据只作用于明确映射骨骼；机械连杆尺寸不写入 Manny 骨长。临时骨骼视口和 raw FK/约束后 pose 报告用于诊断。未提供通用 GeneratedFK 或 Backwards Solve 自动适配。

## 相对编辑、接触和采集

Clutch 在开始时固定源基准和当前正式控制器基准；后续增量相对于这两个快照计算，不累积上一帧输出。更换时间点会冻结 Clutch。更换掩码重新采集正式控制器基准，避免将另一次预览缓存当成未选部位的基准。

接触使用目标 FK 副本上的解析双骨 IK。root 保持源 FK 的策略；每条链保持原骨长，距离夹到几何可达区间但明确报告超出残差，不能以夹取后的目标冒充成功。pole 使用当前弯曲平面；退化时依次使用上一稳定方向及首选方向。首选方向为目标组件坐标中的手臂 -Y、腿 +Y。位置阈值 0.5 cm，锁方向时角度阈值 1°。无拉伸；不可达时拒绝 Capture。

接触目标存于目标组件坐标，场景放置不随 Live 改写。锁定期间禁止编辑 Placement，避免坐标语义变化；移动场景放置前先解锁。位置和方向可独立开关。最终求解转为 FK controls，因此保存后的播放无需继续运行求解器。

Capture 验证确切 sequence/binding/component/track，按显示帧率转换到 tick resolution，冻结输入并开启一个 `FScopedTransaction`。写入受管 controls、必要的 FK/IK/伸缩开关、space 状态和可选 Placement，然后使用正式 Rig 求值回读。失败回滚事务；成功保留 Frozen。欧拉旋转依据相邻已有 key 选择连续分支。

`PoseDoll / Manny` 只管理自己创建的轨道；歧义 Control Rig、竞争动画、嵌套 focused sequence、时间扭曲和非均匀/负缩放均拒绝。UpperBody 不新增腿、手指或面部 key。Placement 使用单独的受管变换轨道，不混入 44 路数据。

`Saved/PoseDoll` 的采集 JSON 记录来源、raw/q、映射指纹、controls、约束、掩码、基准、误差和帧率。可播放结果是 `.uasset` 中的原生关键帧，JSON 不参与序列求值。

## 本机 API 差异与边界

- UE 5.8 本机 `UControlRigBlueprint` 头文件为 `ControlRigBlueprintLegacy.h`；Python 对应类型仍可加载项目现有资产。
- Control Rig Sequencer 的 C++ 类为 `UControlRigSequencerEditorLibrary`，Python 类为 `unreal.ControlRigSequencerLibrary`。
- Windows 本机构建未实现通用平台 SHA-256 入口，目标指纹使用引擎随附 OpenSSL。
- `OnObjectModified` 不足以覆盖 VM-only compile，额外直接订阅 `OnVMCompiled()`。
- 测试 Sequencer 需要完整 Editor/Slate 初始化；使用 `-ExecutePythonScript`，不能用普通 `-run=pythonscript` commandlet 替代。
- 现版本仍使用 UE 5.8 可用但已标记 deprecated 的 `FindBindingFromObject` 重载；升级引擎时须迁移和重新验收，不能宣称跨版本兼容。
- 长时间性能是本机 Development Editor 的 CPU 应用时延；不宣称测得源到显示器光子的端到端时延。
- 实体装配、打印、传感器精度、真实碰撞和任意第三方 Rig 均未验收。
