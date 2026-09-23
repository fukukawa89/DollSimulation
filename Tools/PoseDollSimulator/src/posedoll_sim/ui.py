import json
import math
import time
from pathlib import Path

from PySide6.QtCore import Qt, QTimer
from PySide6.QtWidgets import (QMainWindow,QWidget,QHBoxLayout,QVBoxLayout,QLabel,QTabWidget,
    QScrollArea,QFormLayout,QSlider,QDoubleSpinBox,QPushButton,QCheckBox,QComboBox,
    QGroupBox,QFileDialog,QMessageBox,QTableWidget,QTableWidgetItem,QHeaderView)

from .core import SensorDecoder
from .diagnostics import diagnose
from .view import DollView


class SimulatorWindow(QMainWindow):
    def __init__(self, profile, server):
        super().__init__()
        self.profile,self.server=profile,server
        self.static_mode=hasattr(server,'static_fault')
        self.q=dict.fromkeys(profile.order,0.0)
        self.controls={}
        self.selected='elbow_l.flex'
        self.updating=False
        self.decoder=SensorDecoder(profile)
        self.decoder.handshake(profile.hello(server.session,server.body35))
        self.decoded=dict(self.q)
        self.diagnostic=''
        self.last_sequence=None
        self.scan_start=time.monotonic()
        self.last_mechanical=0
        self.mechanical_diagnostic=''
        self.setWindowTitle('PoseDoll Lab · '+('静态采集模拟器 · 41 路 + 3 固定槽' if self.static_mode else '全身传感器人偶模拟器'))
        self.resize(1560,980)
        container=QWidget(); self.setCentralWidget(container)
        root=QVBoxLayout(container)
        top=QHBoxLayout();root.addLayout(top)
        title=QLabel('POSEDOLL LAB   /   全身姿势输入');title.setStyleSheet('font-size:21px;font-weight:600;padding:8px')
        top.addWidget(title);top.addStretch()
        self.connection=QLabel();top.addWidget(self.connection)
        body=QHBoxLayout();root.addLayout(body,1)
        left=QVBoxLayout();body.addLayout(left)
        self.tabs=QTabWidget();self.tabs.setFixedWidth(365);left.addWidget(self.tabs,1)
        groups=[('躯干',lambda a:a['side']=='center'),('左臂',lambda a:a['side']=='left' and a['category'] in ('shoulder_girdle','arms','hands')),
                ('右臂',lambda a:a['side']=='right' and a['category'] in ('shoulder_girdle','arms','hands')),
                ('左腿',lambda a:a['side']=='left' and a['category'] in ('legs','feet')),
                ('右腿',lambda a:a['side']=='right' and a['category'] in ('legs','feet'))]
        self.axis_tabs={}
        for tab_index,(label,predicate) in enumerate(groups):
            page=QWidget();layout=QVBoxLayout(page)
            for aid in profile.order:
                axis=profile.axes[aid]
                if not predicate(axis):continue
                box=QGroupBox(axis['label_zh']);form=QVBoxLayout(box)
                row=QHBoxLayout();form.addLayout(row)
                choose=QPushButton('选轴');choose.setFixedWidth(52);choose.clicked.connect(lambda checked=False,a=aid:self.select(a));row.addWidget(choose)
                spin=QDoubleSpinBox();spin.setRange(*[math.degrees(v) for v in axis['limits_rad']]);spin.setDecimals(1);spin.setSuffix('°');spin.setSingleStep(1);row.addWidget(spin,1)
                slider=QSlider(Qt.Orientation.Horizontal);slider.setRange(int(spin.minimum()*10),int(spin.maximum()*10));form.addWidget(slider)
                slider.valueChanged.connect(lambda value,a=aid:self.set_angle(a,math.radians(value/10)))
                spin.valueChanged.connect(lambda value,a=aid:self.set_angle(a,math.radians(value)))
                self.controls[aid]=(spin,slider,box);self.axis_tabs[aid]=tab_index;layout.addWidget(box)
            layout.addStretch();scroll=QScrollArea();scroll.setWidgetResizable(True);scroll.setWidget(page);self.tabs.addTab(scroll,label)
        self.symmetric=QCheckBox('左右对称编辑');left.addWidget(self.symmetric)
        row=QHBoxLayout();left.addLayout(row)
        for label,fn in [('中立复位',self.neutral),('镜像姿势',self.mirror)]:
            button=QPushButton(label);button.clicked.connect(fn);row.addWidget(button)
        self.variant=QComboBox();self.variant.addItems(['Full44 · 完整 44 轴','Body35 · 9 轴机械锁定']);self.variant.currentIndexChanged.connect(self.change_variant);left.addWidget(self.variant)
        if self.static_mode:
            self.variant.clear();self.variant.addItem('Static41 · 骨盆 3 槽固定');self.variant.setEnabled(False)
        middle=QVBoxLayout();body.addLayout(middle,1)
        self.axis_label=QLabel();middle.addWidget(self.axis_label)
        self.view=DollView(profile);self.view.axis_selected.connect(self.select);self.view.angle_dragged.connect(self.set_angle);middle.addWidget(self.view,1)
        hint=QLabel('蓝色：人偶左侧   橙色：右侧   黄色：选中机械轴\n左键选轴 / 拖旋转环 · 右键环绕 · 滚轮缩放');hint.setStyleSheet('color:#97acc0;padding:6px');middle.addWidget(hint)
        presets=QHBoxLayout();middle.addLayout(presets)
        self.preset=QComboBox();self.preset.addItems(['中立 N pose','左肘 90°','左肩前举 90°','左膝 90°','非对称全身']);presets.addWidget(self.preset,1)
        b=QPushButton('应用预设');b.clicked.connect(self.apply_preset);presets.addWidget(b)
        row=QHBoxLayout();middle.addLayout(row)
        for label,fn in [('保存姿势',self.save_pose),('加载姿势',self.load_pose),('重启设备会话',self.restart)]:
            button=QPushButton(label);button.clicked.connect(fn);row.addWidget(button)
        right=QVBoxLayout();body.addLayout(right)
        faults=QGroupBox('故障注入 · 作用于当前选轴');form=QFormLayout(faults);right.addWidget(faults)
        for label,attribute,maximum in [('噪声 σ（度）','noise_degrees',10),('原始零偏（度）','bias_degrees',180),('量化步长（度）','quantum_degrees',30)]:
            spin=QDoubleSpinBox();spin.setDecimals(2);spin.setRange(-maximum if attribute=='bias_degrees' else 0,maximum);spin.setSingleStep(.1)
            spin.valueChanged.connect(lambda value,key=attribute:setattr(server.faults,key,value));form.addRow(label,spin)
        for label,attribute in [('传感器卡死','stuck'),('当前轴缺失','missing'),('暂停采样','paused'),('重复序号','duplicate_sequence'),('过期序号','stale_sequence'),('强制 TCP 分包','fragment'),('合并两帧发送（粘包）','coalesce')]:
            if self.static_mode and attribute in ('duplicate_sequence','stale_sequence','coalesce'):continue
            check=QCheckBox(label);check.toggled.connect(lambda value,key=attribute:setattr(server.faults,key,value));form.addRow(check)
        if self.static_mode:
            faults_choice=QComboBox();faults_choice.addItems(['正常','CRC 错误','传感器缺失','传感器故障','节点重启','远端重启','重复扫描','请求错配','超时'])
            modes=['','crc','missing','fault','node_restart','satellite_restart','duplicate_scan','wrong_request','timeout']
            faults_choice.currentIndexChanged.connect(lambda i:setattr(server,'static_fault',modes[i]));form.addRow('采集事务故障',faults_choice)
        self.scan=QCheckBox('连续扫动当前轴');form.addRow(self.scan)
        self.fault_view=QCheckBox('三维显示本地反解姿势');form.addRow(self.fault_view)
        self.table=QTableWidget(44,4);self.table.setHorizontalHeaderLabels(['关节','理想°','raw°','反解°']);self.table.verticalHeader().hide();self.table.setFixedWidth(395)
        self.table.setEditTriggers(QTableWidget.EditTrigger.NoEditTriggers)
        self.table.horizontalHeader().setSectionResizeMode(0,QHeaderView.ResizeMode.Stretch)
        for i in range(1,4):self.table.setColumnWidth(i,62)
        for i,aid in enumerate(profile.order):
            self.table.setItem(i,0,QTableWidgetItem(profile.axes[aid]['label_zh']))
            for col in range(1,4):self.table.setItem(i,col,QTableWidgetItem('0'))
        self.table.cellClicked.connect(lambda row,col:self.select(profile.order[row]));right.addWidget(self.table,1)
        note=QLabel('反解值为模拟器本地诊断。\nUE 的独立校准结果在插件面板查看。'+('\n空闲时保留上次读数，UE 点击静态采集才请求新扫描。' if self.static_mode else ''));note.setStyleSheet('color:#97acc0');right.addWidget(note)
        self.status=QLabel();self.status.setWordWrap(True);root.addWidget(self.status)
        self.setStyleSheet('QMainWindow,QWidget{background:#17212d;color:#dce6f1} QGroupBox{border:1px solid #354655;border-radius:5px;margin-top:10px;padding:6px} QGroupBox::title{subcontrol-origin:margin;left:9px} QPushButton,QComboBox,QDoubleSpinBox{background:#273848;border:1px solid #46576a;padding:6px;border-radius:4px} QPushButton:hover{background:#36536b} QTableWidget{background:#13202b;gridline-color:#2b3d4d} QHeaderView::section,QTabBar::tab{background:#243546;padding:7px} QTabBar::tab:selected{background:#39617e} QSlider::groove:horizontal{height:5px;background:#394d61} QSlider::handle:horizontal{background:#6fc5ed;width:12px;margin:-4px 0;border-radius:5px}')
        self.timer=QTimer(self);self.timer.timeout.connect(self.tick);self.timer.start(33)
        self.select(self.selected);self.refresh()

    def select(self,aid):
        self.selected=aid;self.view.selected=aid;self.server.faults.axis=aid
        self.tabs.setCurrentIndex(self.axis_tabs[aid])
        self.axis_label.setText('当前轴：'+self.profile.axes[aid]['label_zh']+'   ·   '+aid)
        self.table.selectRow(self.profile.index[aid]);self.view.update()

    def set_angle(self,aid,value):
        if self.updating:return
        if self.static_mode and aid in self.profile.order[:3]:return
        if self.server.body35 and aid in self.profile.fixed:return
        lo,hi=self.profile.axes[aid]['limits_rad']
        self.q[aid]=max(lo,min(hi,value))
        mirror=self.profile.axes[aid]['mirrored_axis_id']
        if self.symmetric.isChecked() and mirror:self.q[mirror]=self.q[aid]
        self.refresh()

    def refresh(self):
        self.updating=True
        if self.server.body35:self.q.update(self.profile.fixed)
        if self.static_mode:self.q.update(dict.fromkeys(self.profile.order[:3],0.0))
        for aid,(spin,slider,box) in self.controls.items():
            spin.setValue(math.degrees(self.q[aid]));slider.setValue(round(math.degrees(self.q[aid])*10));box.setEnabled(not((self.server.body35 and aid in self.profile.fixed) or (self.static_mode and aid in self.profile.order[:3])))
        self.updating=False
        self.server.set_pose(self.q)
        self.view.q=dict(self.decoded if self.fault_view.isChecked() else self.q);self.view.update()

    def neutral(self):
        self.q=dict.fromkeys(self.profile.order,0.0);self.refresh()

    def mirror(self):
        original=dict(self.q)
        for aid in self.q:
            pair=self.profile.axes[aid]['mirrored_axis_id']
            self.q[aid]=original[pair] if pair else (-original[aid] if aid.endswith(('.yaw','.roll')) else original[aid])
        self.refresh()

    def change_variant(self,index):
        if self.static_mode:return
        self.server.body35=bool(index);self.restart();self.refresh()

    def restart(self):
        self.server.restart();self.decoder.handshake(self.profile.hello(self.server.session,self.server.body35));self.last_sequence=None

    def apply_preset(self):
        names=['neutral','left_elbow_90','left_shoulder_forward_90','left_knee_90','asymmetric_pose']
        path=self.profile.shared/'Fixtures'/(names[self.preset.currentIndex()]+'.sample.json')
        d=SensorDecoder(self.profile);d.handshake(json.loads((self.profile.shared/'Fixtures/hello.json').read_text(encoding='utf-8')))
        self.q=d.sample(json.loads(path.read_text(encoding='utf-8')));self.refresh()

    def save_pose(self):
        path,_=QFileDialog.getSaveFileName(self,'保存机械姿势','','PoseDoll pose (*.json)')
        if not path:return
        Path(path).write_text(json.dumps({'type':'posedoll.pose/1','profile_sha256':self.profile.profile_hash,'calibration_sha256':self.profile.calibration_hash,'angles_rad':self.q},ensure_ascii=False,indent=2),encoding='utf-8')

    def load_pose(self):
        path,_=QFileDialog.getOpenFileName(self,'加载机械姿势','','PoseDoll pose (*.json)')
        if not path:return
        try:
            data=json.loads(Path(path).read_text(encoding='utf-8'));q=data['angles_rad']
            if data['type']!='posedoll.pose/1' or data['profile_sha256']!=self.profile.profile_hash or data['calibration_sha256']!=self.profile.calibration_hash or set(q)!=set(self.q):raise ValueError('配置、校准或通道不匹配')
            for aid,value in q.items():
                lo,hi=self.profile.axes[aid]['limits_rad']
                if not isinstance(value,(int,float)) or not math.isfinite(value) or not lo<=value<=hi:raise ValueError('角度超限：'+aid)
            self.q=q;self.refresh()
        except (ValueError,KeyError,OSError) as exc:QMessageBox.warning(self,'加载失败',str(exc))

    def tick(self):
        if self.scan.isChecked():
            lo,hi=self.profile.axes[self.selected]['limits_rad'];q=(lo+hi)/2+(hi-lo)*.45*math.sin((time.monotonic()-self.scan_start)*.8)
            self.set_angle(self.selected,q)
        if not self.static_mode and not self.server.state.startswith('已连接') and not self.server.faults.paused:
            self.server.snapshot()
        latest=self.server.latest
        if latest and (latest['sequence'],latest['session_id'])!=self.last_sequence:
            try:
                self.decoded=self.decoder.sample(latest);self.diagnostic='44 路校准有效'
            except ValueError as exc:self.diagnostic='保持最后有效反解：'+str(exc)
            self.last_sequence=(latest['sequence'],latest['session_id'])
        if self.fault_view.isChecked():self.view.q=dict(self.decoded);self.view.update()
        for row,aid in enumerate(self.profile.order):
            self.table.item(row,1).setText(f'{math.degrees(self.q[aid]):.1f}')
            raw=latest['raw_angles_rad'][row] if latest else None
            if self.static_mode and row<3:raw=None
            self.table.item(row,2).setText('—' if raw is None else f'{math.degrees(raw):.1f}')
            self.table.item(row,3).setText(f'{math.degrees(self.decoded[aid]):.1f}')
        self.connection.setText(self.server.state+'  |  已发送 '+str(self.server.sent))
        if time.monotonic()-self.last_mechanical>.25:
            self.last_mechanical=time.monotonic()
            result=diagnose(self.profile,self.q)
            singular='、'.join(result['near_singular']) or '无'
            self.mechanical_diagnostic=f"轴限位：{len(result['limits'])} · 近奇异轴组：{singular} · 粗略连杆干涉：{len(result['rough_collisions'])} 对（5 mm 半径假设，不代表外壳检查）"
        self.status.setText((self.server.error+'  '+self.diagnostic+'\n'+self.mechanical_diagnostic).strip())

    def closeEvent(self,event):
        self.timer.stop();self.server.close();event.accept()
