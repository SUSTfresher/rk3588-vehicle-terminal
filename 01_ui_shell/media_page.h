// 多媒体页工厂：页面布局与播放器后端分离。
#pragma once
class QWidget;
class AudioPlayer;
QWidget* createMediaPage(AudioPlayer* player, QWidget* parent = nullptr);
