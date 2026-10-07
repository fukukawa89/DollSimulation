#include "PoseDollSession.h"

namespace PoseDoll
{
FString FSession::SourceLabel() const
{
    if(bFixture)return TEXT("数据来源：离线测试");
    if(!Input)return TEXT("数据来源：未连接");
    const auto Source=Input->Snapshot();
    if(!Source.bConnected)return TEXT("数据来源：等待连接");
    if(!Source.bStatic)return TEXT("数据来源：模拟器");
    return Source.StaticIdentity.SourceKind==TEXT("hardware")?TEXT("数据来源：实体人偶"):TEXT("数据来源：模拟器（静态测试）");
}

FString FSession::UserStatusLabel() const
{
    if(StaticWindow.Pending())return TEXT("正在采集，请保持人偶静止……");
    if(!Error.IsEmpty())
    {
        if(Error.StartsWith(TEXT("Keys saved")))return TEXT("姿势已写入，但来源记录保存失败。详情见“调试与测试”。");
        if(Error.Contains(TEXT("Pause Sequencer")))return TEXT("请先暂停 Sequencer，再采集姿势。");
        if(Error.Contains(TEXT("whole display frame")))return TEXT("请将播放头放在整数帧，再采集姿势。");
        if(Error.Contains(TEXT("at least one capture part")))return TEXT("请至少选择一个采集部位。");
        if(Error.Contains(TEXT("read-only")))return TEXT("当前序列为只读，请解除锁定后再采集。");
        if(Error.Contains(TEXT("Add this actor")))return TEXT("请先将角色加入当前序列，再绑定角色与序列。");
        if(Error.Contains(TEXT("Select exactly one"))||Error.Contains(TEXT("Select one actor"))||Error.Contains(TEXT("exactly one skeletal mesh")))return TEXT("请选择一个目标角色，并打开要采集的序列。");
        if(Error.Contains(TEXT("Target actor/component/sequence"))||Error.Contains(TEXT("binding"))||Error.Contains(TEXT("rebind")))return TEXT("请重新绑定目标角色与当前序列。");
        if(Error.Contains(TEXT("Open the bound top-level")))return TEXT("请打开已绑定的顶层序列，再采集姿势。");
        if(Error.Contains(TEXT("Connect a")))return TEXT("请先连接人偶，再采集姿势。");
        if(Error.Contains(TEXT("fresh")))return TEXT("尚未收到完整的新姿势，请检查连接后重试。");
        if(Error.Contains(TEXT("Finish the current edit")))return TEXT("请先结束当前编辑操作，再采集姿势。");
        if(State==TEXT("SnapshotTimedOut"))return TEXT("采集超时，请检查连接、保持人偶静止后重试。");
        if(State==TEXT("SnapshotCancelled"))return TEXT("编辑或目标已变化，本次采集已自动取消。需要时可重新采集。");
        if(Error==TEXT("Disconnected")||Error==TEXT("Source disconnected"))return TEXT("连接已断开，请重新连接后采集。");
        if(Error.Contains(TEXT("PDS1")))return TEXT("输入数据未通过校验，请检查设备和校准配置。详情见“调试与测试”。");
        return TEXT("操作未完成，请在“调试与测试”中查看详细原因。");
    }
    if(!Sequence.IsValid()||!Component.IsValid())return TEXT("请选择角色并打开序列，然后点击“绑定所选角色 / 当前序列”。");
    if(State==TEXT("Captured")||State==TEXT("SnapshotCommitted"))return TEXT("采集成功，可在 Sequencer 中检查和编辑姿势。");
    if(bFixture)return TEXT("离线测试姿势已就绪，点击采集可写入当前帧。");
    if(!Input)return TEXT("请连接静态人偶，再摆姿采集。");
    if(!Input->Snapshot().bConnected)return TEXT("正在连接，请检查设备及采集程序。");
    return TEXT("准备就绪。摆好人偶后点击采集。");
}
}
