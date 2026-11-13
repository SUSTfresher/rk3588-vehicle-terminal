# 独立验证 Qt Widgets + Qt Network 在 linuxfb 上显示在线地图。
QT += widgets network
CONFIG += c++17 console
CONFIG -= app_bundle
TEMPLATE = app
TARGET = map_probe
SOURCES += main.cpp
