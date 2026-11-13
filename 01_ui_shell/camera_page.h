// 摄像头页面：进入时启动采集，离开时停止采集。
#pragma once

class QWidget;
class QPushButton;
QWidget* createCameraPage(QWidget* parent = nullptr);
void setCameraPageActive(QWidget* page, bool active);
QPushButton* cameraPageBackButton(QWidget* page);
