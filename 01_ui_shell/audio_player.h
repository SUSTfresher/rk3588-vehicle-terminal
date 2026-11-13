// 本地音频后端：Qt 按钮调用控制方法，GStreamer 完成解码和音频输出。
// 不操作 QWidget；通过信号把状态交回界面。
#pragma once

#include <QObject>
#include <QString>
#include <QTimer>
#include <gst/gst.h>

class AudioPlayer : public QObject
{
    Q_OBJECT
public:
    explicit AudioPlayer(QObject* parent = nullptr);
    ~AudioPlayer() override;
    void playFile(const QString& path);
    void pause();
    void stop();
    void seek(qint64 positionMs);
    void setVolume(double normalized);
    void setMuted(bool muted);
    double volume() const { return volume_; }
    bool isMuted() const { return muted_; }

signals:
    void statusChanged(const QString& text);
    void playingChanged(bool playing);
    void progressChanged(qint64 positionMs, qint64 durationMs);
    void metadataChanged(const QString& title, const QString& artist);
    void finished();

private:
    void pollBus();
    void fail(const QString& message);
    void applyVolume();
    GstElement* pipeline_ = nullptr;
    // 显式音量元素，避免不同版本 playbin 的 volume 属性只改界面状态。
    GstElement* volumeElement_ = nullptr;
    GstBus* bus_ = nullptr;
    QTimer busTimer_;
    QTimer progressTimer_;
    QString initError_;
    QString currentPath_;
    GstState targetState_ = GST_STATE_NULL;
    double volume_ = 0.20;
    bool muted_ = false;
};
