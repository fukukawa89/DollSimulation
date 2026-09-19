"""Render the recorded acceptance diagnostics with the installed Qt Charts library."""
import os,json
from pathlib import Path
os.environ['QT_QPA_PLATFORM']='offscreen'
from PySide6.QtCore import Qt,QRectF
from PySide6.QtGui import QImage,QPainter,QFont,QPen,QColor,QFontDatabase
from PySide6.QtWidgets import QApplication
from PySide6.QtCharts import QChart,QChartView,QLineSeries,QValueAxis
root=Path(__file__).resolve().parents[1];r=json.loads((root/'reports/soak_gui.json').read_text(encoding='utf-8'))
assert r.get('passed'),'Only render an accepted final soak'
app=QApplication([])
for font in ('C:/Windows/Fonts/segoeui.ttf','C:/Windows/Fonts/segoeuib.ttf'):
    assert QFontDatabase.addApplicationFont(font)>=0,font
app.setFont(QFont('Segoe UI',10))
canvas=QImage(1600,1050,QImage.Format.Format_ARGB32);canvas.fill(QColor('#f5f7fa'))
p=QPainter(canvas);p.setRenderHint(QPainter.RenderHint.Antialiasing)
p.setPen(QColor('#15233b'));p.setFont(QFont('Segoe UI',25,QFont.Weight.Bold));p.drawText(40,52,'PoseDoll Lab / 30-minute editor soak')
p.setFont(QFont('Segoe UI',11));p.setPen(QColor('#526076'))
p.drawText(40,82,'UE 5.8.2  |  Ryzen 9 5950X / RTX 4070 SUPER  |  60 Hz source  |  MCP disabled  |  PASS')
plots=[('CPU processing + preview P95','main_thread_with_preview_p95_ms','ms',2.),
       ('Receive to preview P95','receive_to_preview_p95_ms','ms',1000/30),
       ('Measured editor frame rate','fps','fps',None),
       ('Editor working set','memory_mb','MB',None)]
views=[]
for i,(title,key,unit,target) in enumerate(plots):
    chart=QChart();chart.setTitle(title);chart.setTitleFont(QFont('Segoe UI',14,QFont.Weight.DemiBold));chart.setBackgroundRoundness(8)
    series=QLineSeries();series.setName('Recorded');series.setPen(QPen(QColor('#205de0'),2.5))
    values=[]
    for row in r['history']:series.append(row['seconds']/60,row[key]);values.append(row[key])
    chart.addSeries(series)
    x=QValueAxis();x.setRange(0,30);x.setTickCount(7);x.setTitleText('Elapsed steady minutes');x.setLabelFormat('%.0f')
    y=QValueAxis();low=min(values);high=max(values+[target] if target else values);span=max(high-low,.2 if unit!='MB' else 4)
    y.setRange(max(0,low-span*.18),high+span*.2);y.setTitleText(unit);y.setTickCount(5);y.setLabelFormat('%.2f' if unit=='ms' else '%.1f')
    chart.addAxis(x,Qt.AlignmentFlag.AlignBottom);chart.addAxis(y,Qt.AlignmentFlag.AlignLeft);series.attachAxis(x);series.attachAxis(y)
    if target is not None:
        limit=QLineSeries();limit.setName(f'Target < {target:.2f} {unit}');limit.setPen(QPen(QColor('#df5639'),1.7,Qt.PenStyle.DashLine));limit.append(0,target);limit.append(30,target)
        chart.addSeries(limit);limit.attachAxis(x);limit.attachAxis(y)
    else:chart.legend().hide()
    view=QChartView(chart);view.setRenderHint(QPainter.RenderHint.Antialiasing);view.resize(750,435);view.show();app.processEvents();views.append(view)
    view.render(p,QRectF(40+(i%2)*780,112+(i//2)*450,750,435),view.rect())
p.setFont(QFont('Segoe UI',10));p.setPen(QColor('#526076'))
p.drawText(40,1030,'Rolling diagnostic windows sampled every 30 s. Latency is UE receive to CPU preview application, not source to photon.')
p.end();assert canvas.save(str(root/'reports/soak_performance.png'))
for view in views:view.close()
print(root/'reports/soak_performance.png')
