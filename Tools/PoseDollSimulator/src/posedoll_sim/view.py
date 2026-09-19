import math
import numpy as np
from OpenGL.GL import *
from OpenGL.GLU import gluPerspective, gluLookAt, gluProject
from PySide6.QtCore import Qt, Signal
from PySide6.QtOpenGLWidgets import QOpenGLWidget


class DollView(QOpenGLWidget):
    axis_selected = Signal(str)
    angle_dragged = Signal(str, float)

    def __init__(self, profile):
        super().__init__()
        self.profile = profile
        self.q = dict.fromkeys(profile.order, 0.0)
        self.selected = 'elbow_l.flex'
        self.yaw, self.pitch, self.distance = -.35, .22, .85
        self.drag_start = None
        self.projected = {}
        self.ring_screen = []
        self.setMinimumSize(460, 500)
        self.setToolTip('左键选轴，拖动彩色环旋转；右键环绕，滚轮缩放。轴重合时用左侧列表选择。')

    def initializeGL(self):
        glClearColor(.045, .063, .086, 1)
        glEnable(GL_DEPTH_TEST)
        glEnable(GL_BLEND)
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA)
        glEnable(GL_LINE_SMOOTH)

    def paintGL(self):
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT)
        ratio = self.devicePixelRatioF()
        width, height = max(1,int(self.width()*ratio)), max(1,int(self.height()*ratio))
        glViewport(0,0,width,height)
        glMatrixMode(GL_PROJECTION)
        glLoadIdentity()
        gluPerspective(36,width/height,.001,20)
        glMatrixMode(GL_MODELVIEW)
        glLoadIdentity()
        target = np.array([0.,0.,.21])
        eye = target + self.distance*np.array([math.cos(self.pitch)*math.cos(self.yaw), math.cos(self.pitch)*math.sin(self.yaw), math.sin(self.pitch)])
        gluLookAt(*eye,*target,0,0,1)
        model,projection,viewport = glGetDoublev(GL_MODELVIEW_MATRIX),glGetDoublev(GL_PROJECTION_MATRIX),glGetIntegerv(GL_VIEWPORT)
        def project(point):
            x,y,z = gluProject(*point,model,projection,viewport)
            return np.array([x/ratio,self.height()-y/ratio])
        glLineWidth(1)
        glColor4f(.18,.23,.28,1)
        glBegin(GL_LINES)
        for v in np.linspace(-.35,.35,15):
            glVertex3f(v,-.35,0); glVertex3f(v,.35,0)
            glVertex3f(-.35,v,0); glVertex3f(.35,v,0)
        glEnd()
        pose = self.profile.fk(self.q)
        for node in self.profile.nodes:
            point = pose[node['id']][:3,3]
            if node['parent'] is not None:
                parent = pose[node['parent']][:3,3]
                if np.linalg.norm(point-parent)>.0001:
                    glLineWidth(9 if '_l' in node['id'] or '_r' in node['id'] else 12)
                    glColor3f(*((.32,.74,.90) if '_l' in node['id'] else ((.96,.64,.33) if '_r' in node['id'] else (.80,.85,.89))))
                    glBegin(GL_LINES); glVertex3dv(parent); glVertex3dv(point); glEnd()
            if node['kind'] == 'revolute':
                before = (np.eye(4) if node['parent'] is None else pose[node['parent']]) @ self.profile.pre[node['id']]
                center = before[:3,3]
                self.projected[node['axis_id']] = project(center)
                glPointSize(8)
                glColor3f(.9,.93,.96)
                glBegin(GL_POINTS); glVertex3dv(center); glEnd()
                if node['axis_id'] == self.selected:
                    self._draw_ring(center,before[:3,:3]@np.asarray(node['axis_local']),project)
        # Palm planes and sole planes make twist and dorsiflexion visible.
        for side in ('l','r'):
            for part in ('hand','foot'):
                matrix=pose[part+'_'+side]
                if part=='hand':
                    points=[(0,-.014,0),(0,.014,0),(0,.014,-.034),(0,-.014,-.034)]
                    normal=np.array([.023,0,-.016])
                else:
                    points=[(-.014,-.014,-.010),(.055,-.014,-.010),(.055,.014,-.010),(-.014,.014,-.010)]
                    normal=np.array([.025,0,.025])
                glColor4f(*((.23,.7,.88,.8) if side=='l' else (.98,.62,.27,.8)))
                glBegin(GL_QUADS)
                for point in points: glVertex3dv((matrix@np.array([*point,1]))[:3])
                glEnd()
                glLineWidth(2); glColor3f(.9,.95,.55)
                glBegin(GL_LINES); glVertex3dv(matrix[:3,3]);glVertex3dv((matrix@np.array([*normal,1]))[:3]);glEnd()
        # Head marker and canonical orientation triad.
        head=pose['head'][:3,3]+np.array([0,0,.017])
        glPointSize(25); glColor3f(.84,.89,.94); glBegin(GL_POINTS);glVertex3dv(head);glEnd()
        glLineWidth(3)
        for axis,color in zip(np.eye(3),[(1,.25,.3),(.3,.9,.5),(.3,.5,1)]):
            glColor3f(*color);glBegin(GL_LINES);glVertex3f(.20,-.20,.002);glVertex3dv(np.array([.20,-.20,.002])+.05*axis);glEnd()

    def _draw_ring(self, center, axis, project):
        basis=np.array([1.,0.,0.]) if abs(axis[0])<.8 else np.array([0.,1.,0.])
        u=np.cross(axis,basis);u/=np.linalg.norm(u)
        v=np.cross(axis,u)
        points=[center+.034*(math.cos(a)*u+math.sin(a)*v) for a in np.linspace(0,2*math.pi,81)]
        self.ring_screen=[project(p) for p in points]
        self.ring_center=project(center)
        glDisable(GL_DEPTH_TEST)
        glLineWidth(3);glColor3f(.98,.83,.30)
        glBegin(GL_LINE_STRIP)
        for point in points: glVertex3dv(point)
        glEnd()
        glBegin(GL_LINES);glVertex3dv(center-.05*axis);glVertex3dv(center+.05*axis);glEnd()
        glEnable(GL_DEPTH_TEST)

    def mousePressEvent(self,event):
        point=np.array([event.position().x(),event.position().y()])
        self.drag_start=point
        self.start_yaw,self.start_pitch=self.yaw,self.pitch
        self.start_q=self.q[self.selected]
        self.drag_ring=False
        if event.button()==Qt.MouseButton.LeftButton:
            if self.ring_screen and min(np.linalg.norm(point-p) for p in self.ring_screen)<14:
                self.drag_ring=True
                self.ring_start_index=min(range(len(self.ring_screen)),key=lambda i:np.linalg.norm(point-self.ring_screen[i]))
            elif self.projected:
                aid=min(self.projected,key=lambda a:np.linalg.norm(point-self.projected[a]))
                if np.linalg.norm(point-self.projected[aid])<28:
                    self.axis_selected.emit(aid)

    def mouseMoveEvent(self,event):
        if self.drag_start is None:return
        point=np.array([event.position().x(),event.position().y()])
        delta=point-self.drag_start
        if event.buttons() & Qt.MouseButton.RightButton:
            self.yaw=self.start_yaw-delta[0]*.007
            self.pitch=max(-1.3,min(1.3,self.start_pitch+delta[1]*.007))
            self.update()
        elif event.buttons() & Qt.MouseButton.LeftButton and self.drag_ring:
            index=min(range(len(self.ring_screen)),key=lambda i:np.linalg.norm(point-self.ring_screen[i]))
            step=(index-self.ring_start_index+40)%80-40
            self.angle_dragged.emit(self.selected,self.start_q+step*2*math.pi/80)

    def mouseReleaseEvent(self,event):
        self.drag_start=None

    def wheelEvent(self,event):
        self.distance=max(.3,min(2,self.distance*math.exp(-event.angleDelta().y()/1200)))
        self.update()
