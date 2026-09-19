"""Build an evidence index from real test reports, never synthesize test success."""
import json,re,datetime
from pathlib import Path
root=Path(__file__).resolve().parents[1];p=root/'reports'
def read(name):return json.loads((p/name).read_text(encoding='utf-8-sig'))
evidence={name:read(name) for name in ('axis_sweep.json','capture_first_run.json','capture_reopen.json','capture_advanced.json','contacts.json','contacts_extended.json','contacts_reopen.json','lifecycle.json','winding.json','soak_gui.json')}
native=read('M1_UE/index.json');rig=read('rig_fixture_results.json');soak=evidence['soak_gui.json']
python=(p/'M2_python_tests_final.log').read_text(encoding='utf-8-sig')
reference=(p/'reference_baseline_utf8.log').read_text(encoding='utf-8-sig')
statuses={
    'M0': 'PASS' if 'Result: Succeeded' in (p/'build_latest.log').read_text(encoding='utf-8-sig') else 'FAIL',
    'M1': 'PASS' if native['succeeded']==3 and native['failed']==0 and native['notRun']==0 and 'OK' in reference else 'FAIL',
    'M2': 'PASS' if '26 passed' in python else 'FAIL',
    'M3': 'PASS' if rig['passed'] and evidence['axis_sweep.json']['passed'] else 'FAIL',
    'M4': 'PASS' if all(evidence[n]['passed'] for n in ('capture_first_run.json','capture_reopen.json','capture_advanced.json','winding.json')) else 'FAIL',
    'M5': 'PASS' if all(evidence[n]['passed'] for n in ('contacts.json','contacts_extended.json','contacts_reopen.json')) else 'FAIL',
    'M6': 'PASS' if soak.get('passed') and evidence['lifecycle.json']['passed'] else 'IN PROGRESS / NOT ACCEPTED',
}
summary={'generated_at':datetime.datetime.now().astimezone().isoformat(),'milestones':statuses,'all_passed':all(v=='PASS' for v in statuses.values()),
    'rig_fingerprint':rig['rig_runtime_sha256'],'pose_cases':evidence['axis_sweep.json']['case_count'],
    'soak_phase':soak.get('phase'),'soak_steady_seconds':soak.get('steady_seconds',0),
    'soak_processing_preview_p95_ms':soak.get('max_observed_processing_preview_p95_ms'),
    'soak_receive_preview_p95_ms':soak.get('max_observed_receive_preview_p95_ms')}
(p/'acceptance_summary.json').write_text(json.dumps(summary,ensure_ascii=False,indent=2),encoding='utf-8')
rows=[
    ('M0','环境、完整编译、实际 Rig/API 发现','environment.json、rig_inventory.json、rig_runtime_probe.json、mcp_capabilities.json、build_latest.log'),
    ('M1','18 项数学基线；3 项原生 Automation 测试，5×54 节点双实现对照','reference_baseline_utf8.log、M1_UE/index.json、M1_UE.log'),
    ('M2','44 轴 Qt/OpenGL 模拟器；26 项单元/网络测试；真实 GUI','M2_python_tests_final.log、simulator_ui_final.png、transport_probe.json'),
    ('M3','原始 Manny Rig；44×5 单轴 +40 随机组合；无骨长变化','rig_fixture_results.json、axis_sweep.json、single_solve_validation.log'),
    ('M4','24fps 0/4/8；手工编辑、Undo/Redo、新进程重开；有理帧率、掩码、放置、连续旋转','capture_first_run.json、capture_reopen.json、capture_advanced.json、winding.json'),
    ('M5','单/双脚与手锁定、3 种高度、方向自由、不可达拒绝、保存重开','contacts.json、contacts_extended.json、contacts_reopen.json'),
    ('M6','30分钟实际 GUI、60Hz、10次面板循环、重连、生命周期、MCP关闭','soak_gui.json、soak_gui_final.log、soak_source_final.json、lifecycle.json'),
]
lines=['# PoseDoll Lab 验收记录','',f"更新：{summary['generated_at']}",'',
    '本机：UE 5.8.2 / CL 56702186，Development Editor Win64；Ryzen 9 5950X、64 GB RAM、RTX 4070 SUPER。D3D12 实测独立显存 11999 MB，显示分辨率 3840×2160。详细驱动/系统信息见 hardware.json。','',
    '| 阶段 | 结果 | 验证范围 | 证据（本目录） |','|---|---|---|---|']
lines += [f'| {key} | {statuses[key]} | {scope} | {files} |' for key,scope,files in rows]
lines += ['',f"目标 Rig 指纹：`{summary['rig_fingerprint']}`。",'',
    '## 长时间与性能', '',f"当前记录状态：{soak.get('phase')}；连续稳定时长 {soak.get('steady_seconds',0):.1f} 秒。"]
if soak.get('passed'):
    h=soak['history'];frames=[x['fps'] for x in h]
    lines += [f"已通过 10 次真实面板关闭/重开，观察到 {len(soak['unique_sessions'])} 个会话 UUID。稳定阶段每段测得帧率 {min(frames):.2f}–{max(frames):.2f} fps。",
        f"90 秒稳定后所有抽样窗口中，处理与预览 CPU 耗时的最大 P95 为 {soak['max_observed_processing_preview_p95_ms']:.3f} ms（目标 <2 ms）；接收到预览 CPU 应用的最大 P95 为 {soak['max_observed_receive_preview_p95_ms']:.3f} ms（目标 <33.333 ms）。",
        f"工作集由 {soak['memory_start_mb']:.1f} MB 到 {soak['memory_final_mb']:.1f} MB；稳态内存序列完整保存在 history。正式关键帧数量和资产 dirty 状态全程未改变。"]
else:lines += ['此处未把短时测试或中断运行算作 30 分钟通过；以最终 passed=true 和性能判据为准。']
lines += ['',
    '测试采用真实 Editor/Slate/D3D12，关闭后台降频并限制约 60fps；UE 测试窗口置于前台开始。未使用系统前台窗口连续监测，因此不声称全程焦点始终不变。MCP 和可选自动化插件通过命令行禁用，主数据路径保持工作。',
    '时延均使用 UE 内部时钟计算；不将两个进程的时钟直接相减，也不宣称测量了源到显示器光子的时延。','',
    '长测后补充了面板范围内的后台刷新偏好管理：打开时保持实时，关闭时恢复原值；未改变数学、网络或 Capture 路径。另行验证原偏好为开/关时共 10 次面板生命周期，见 background_scope.json。性能图见 soak_performance.png。','',
    '## 复现', '',
    '构建：`Scripts/BuildPoseDoll.ps1`。Python 测试使用本地 `.venv`，命令见 README_PoseDoll.md。UE 脚本使用 `UnrealEditor-Cmd.exe <project> -ExecutePythonScript=<absolute script> -NullRHI -Unattended -NoSound -NoSaveConfig -DisablePlugins=ModelContextProtocol,AllToolsets,MCPClientToolset,PoseDollAutomation`。不要用不初始化 Slate 的普通 Python commandlet 运行 Sequencer 测试。',
    '先运行 refresh_delivery.py（其中生命周期测试需要 soak_driver.py 受控源），停止源，再在新进程运行 run_final_reopen.py。长期测试先启动 soak_driver.py，再在真实 UnrealEditor.exe 中运行 start_soak_editor.py；结束信号会让源正常退出。','',
    '## 限制与保留日志', '',
    '只支持已验证的 Manny profile 和顶层 Level Sequence。嵌套时间空间、时间扭曲、非均匀/负缩放、竞争动画和歧义绑定明确拒绝。任意第三方 Rig、手指/面部传感器、长时动作录制及真实定位未实现。硬件打印、装配、磁干扰、手感、传感器精度和 CAD 碰撞未验收。',
    '曾发现并修复掩码基准缓存、VM-only 编译失效处理和控制空间性能问题。backup 日志、space_prediction_probe.log 和 soak_two_solve_short.json 保留开发过程中的失败与停止记录；这些记录不作为最终 PASS 证据。UE 原项目仍有与 PoseDoll 无关的 GameFeatureData 资产管理规则提示，未擅自修改原项目功能设置。',
    '原设计文档保留；新增资产只在 /Game/PoseDollLab/。原有 Manny、Control Rig、动画和 Map1 未保存修改。', '']
(p/'ACCEPTANCE.md').write_text('\n'.join(lines),encoding='utf-8')
print(json.dumps(summary,ensure_ascii=False,indent=2))
