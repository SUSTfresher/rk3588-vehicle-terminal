#pragma once

class QPushButton;
class QWidget;

// 应用页中的在线地图子页面。地图 Key 从当前用户配置目录读取。
QWidget* createMapPage(QWidget* parent = nullptr);
QPushButton* mapPageBackButton(QWidget* page);
