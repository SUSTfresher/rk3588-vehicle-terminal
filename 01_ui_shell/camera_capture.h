// 摄像头采集后端：V4L2 -> GStreamer -> appsink -> Qt QImage。
// 目前使用 videoconvert 做 RGB 转换，先保证功能稳定，再评估 RGA/零拷贝。
#pragma once

#include <QObject>
#include <QElapsedTimer>
#include <QImage>
#include <QTimer>
#include <gst/gst.h>
#include <gst/app/gstappsink.h>

class CameraCapture : public QObject
{
    Q_OBJECT

public:
    explicit CameraCapture(QObject* parent = nullptr);
    ~CameraCapture() override;

public slots:
    void start();
    void stop();

signals:
    void frameReady(const QImage& image);
    void statusChanged(const QString& text);
    void fpsChanged(double fps);

private:
    void poll();
    void reportBusMessages();
    void fail(const QString& message);

    GstElement* pipeline_ = nullptr;
    GstAppSink* sink_ = nullptr;
    GstBus* bus_ = nullptr;
    QTimer pollTimer_;
    QElapsedTimer fpsClock_;
    int framesInWindow_ = 0;
    bool running_ = false;
    bool colorCorrectionEnabled_ = true;
};
