import argparse
import json
import time
from pathlib import Path

from .core import DeviceProfile, SensorDecoder, default_shared
from .transport import SensorServer


def main():
    parser=argparse.ArgumentParser(description='PoseDoll 44-channel raw sensor simulator')
    parser.add_argument('--shared',type=Path,default=default_shared())
    parser.add_argument('--port',type=int)
    parser.add_argument('--static',action='store_true',help='PDS1 request-driven snapshots; default port 39178')
    parser.add_argument('--headless',action='store_true')
    parser.add_argument('--seconds',type=float,default=0)
    parser.add_argument('--fixture',type=Path)
    parser.add_argument('--screenshot',type=Path)
    args=parser.parse_args()
    profile=DeviceProfile(args.shared)
    if args.static:
        from .static_transport import StaticSensorServer
        server=StaticSensorServer(profile,args.port or 39178)
    else:server=SensorServer(profile,args.port or 39177)
    if args.fixture:
        decoder=SensorDecoder(profile)
        decoder.handshake(json.loads((args.shared/'Fixtures/hello.json').read_text(encoding='utf-8')))
        server.set_pose(decoder.sample(json.loads(args.fixture.read_text(encoding='utf-8'))))
    server.start()
    if args.headless:
        try:
            deadline=time.monotonic()+args.seconds if args.seconds else float('inf')
            while time.monotonic()<deadline:time.sleep(.1)
        except KeyboardInterrupt:pass
        finally:server.close()
        print(json.dumps({'sent':server.sent,'error':server.error,'session':server.session}))
        return
    from PySide6.QtCore import QTimer
    from PySide6.QtGui import QSurfaceFormat
    from PySide6.QtWidgets import QApplication
    from .ui import SimulatorWindow
    fmt=QSurfaceFormat();fmt.setVersion(2,1);fmt.setProfile(QSurfaceFormat.OpenGLContextProfile.CompatibilityProfile);fmt.setDepthBufferSize(24);fmt.setSamples(4);QSurfaceFormat.setDefaultFormat(fmt)
    app=QApplication([]);window=SimulatorWindow(profile,server);window.show()
    if args.screenshot:
        def capture():
            window.preset.setCurrentIndex(4);window.apply_preset()
            def save():
                args.screenshot.parent.mkdir(parents=True,exist_ok=True)
                if not window.grab().save(str(args.screenshot)):raise RuntimeError('Screenshot failed')
                window.close()
            QTimer.singleShot(500,save)
        QTimer.singleShot(1200,capture)
    app.exec()


if __name__=='__main__':main()
