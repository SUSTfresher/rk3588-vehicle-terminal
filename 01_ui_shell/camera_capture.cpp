// 摄像头测试已确认 v4l2src + NV12 + videoconvert + RGB 可自然结束。
// appsink 每次只取最新帧，避免显示线程落后时无限堆积旧画面。
#include "camera_capture.h"
#include "camera_color.h"

#include <QDebug>
#include <QMetaType>

namespace {
constexpr int pollIntervalMs = 30;
constexpr int width = 720;
constexpr int height = 480;
const char* device = "/dev/video-camera0";
}

CameraCapture::CameraCapture(QObject* parent) : QObject(parent)
{
    qRegisterMetaType<QImage>("QImage");
    // 默认使用已确认的补偿。启动程序前设置 VEHICLE_CAMERA_COLOR=0 可看原图。
    colorCorrectionEnabled_ = qgetenv("VEHICLE_CAMERA_COLOR") != "0";
    pollTimer_.setInterval(pollIntervalMs);
    pollTimer_.setTimerType(Qt::PreciseTimer);
    connect(&pollTimer_, &QTimer::timeout, this, &CameraCapture::poll);
}

CameraCapture::~CameraCapture()
{
    stop();
}

void CameraCapture::start()
{
    if (running_) return;

    GError* initError = nullptr;
    if (!gst_is_initialized() && !gst_init_check(nullptr, nullptr, &initError)) {
        const QString text = initError
            ? QString::fromUtf8(initError->message)
            : QStringLiteral("GStreamer 初始化失败");
        if (initError) g_error_free(initError);
        fail(text);
        return;
    }

    GError* parseError = nullptr;
    const QString description = QStringLiteral(
        "v4l2src device=%1 io-mode=mmap ! "
        "video/x-raw,format=NV12,width=%2,height=%3,framerate=30/1 ! "
        "videoconvert ! video/x-raw,format=RGB,width=%2,height=%3 ! "
        "appsink name=camera_sink sync=false max-buffers=1 drop=true")
        .arg(device).arg(width).arg(height);
    pipeline_ = gst_parse_launch(description.toUtf8().constData(), &parseError);
    if (!pipeline_ || parseError) {
        const QString text = parseError
            ? QString::fromUtf8(parseError->message)
            : QStringLiteral("无法创建摄像头流水线");
        if (parseError) g_error_free(parseError);
        fail(text);
        return;
    }

    GstElement* sinkElement = gst_bin_get_by_name(
        GST_BIN(pipeline_), "camera_sink");
    if (!sinkElement || !GST_IS_APP_SINK(sinkElement)) {
        if (sinkElement) gst_object_unref(sinkElement);
        fail(QStringLiteral("appsink 创建失败"));
        return;
    }
    sink_ = GST_APP_SINK(sinkElement);
    gst_app_sink_set_emit_signals(sink_, FALSE);
    gst_app_sink_set_drop(sink_, TRUE);
    gst_app_sink_set_max_buffers(sink_, 1);
    bus_ = gst_element_get_bus(pipeline_);

    if (gst_element_set_state(pipeline_, GST_STATE_PLAYING)
        == GST_STATE_CHANGE_FAILURE) {
        fail(QStringLiteral("摄像头启动失败，请检查设备节点"));
        return;
    }

    running_ = true;
    qInfo() << "Camera color correction:"
            << (colorCorrectionEnabled_ ? "ON (R=1.48 G=0.88 B=1.12)" : "OFF");
    framesInWindow_ = 0;
    fpsClock_.start();
    pollTimer_.start();
    emit fpsChanged(0.0);
    emit statusChanged(QStringLiteral("摄像头已启动"));
}

void CameraCapture::stop()
{
    pollTimer_.stop();
    running_ = false;
    if (pipeline_) gst_element_set_state(pipeline_, GST_STATE_NULL);
    if (bus_) {
        gst_object_unref(bus_);
        bus_ = nullptr;
    }
    if (sink_) {
        gst_object_unref(sink_);
        sink_ = nullptr;
    }
    if (pipeline_) {
        gst_object_unref(pipeline_);
        pipeline_ = nullptr;
    }
    framesInWindow_ = 0;
    emit fpsChanged(0.0);
}

void CameraCapture::fail(const QString& message)
{
    qWarning() << "Camera error:" << message;
    stop();
    emit statusChanged(QStringLiteral("摄像头错误：") + message);
}

void CameraCapture::reportBusMessages()
{
    for (int i = 0; bus_ && i < 16; ++i) {
        GstMessage* message = gst_bus_pop(bus_);
        if (!message) break;
        if (GST_MESSAGE_TYPE(message) == GST_MESSAGE_ERROR) {
            GError* error = nullptr;
            gchar* debug = nullptr;
            gst_message_parse_error(message, &error, &debug);
            const QString text = error
                ? QString::fromUtf8(error->message)
                : QStringLiteral("未知 GStreamer 错误");
            if (debug) qWarning() << "GStreamer:" << debug;
            if (error) g_error_free(error);
            g_free(debug);
            gst_message_unref(message);
            fail(text);
            return;
        }
        gst_message_unref(message);
    }
}

void CameraCapture::poll()
{
    if (!running_ || !sink_) return;
    reportBusMessages();
    if (!running_) return;

    GstSample* sample = gst_app_sink_try_pull_sample(sink_, 0);
    if (!sample) return;
    GstBuffer* buffer = gst_sample_get_buffer(sample);
    GstCaps* caps = gst_sample_get_caps(sample);
    GstStructure* structure = caps ? gst_caps_get_structure(caps, 0) : nullptr;
    gint sampleWidth = 0;
    gint sampleHeight = 0;
    const gchar* format = structure
        ? gst_structure_get_string(structure, "format") : nullptr;
    if (!buffer || !format ||
        !gst_structure_get_int(structure, "width", &sampleWidth) ||
        !gst_structure_get_int(structure, "height", &sampleHeight) ||
        QString::fromUtf8(format) != QStringLiteral("RGB")) {
        gst_sample_unref(sample);
        fail(QStringLiteral("摄像头输出格式不是 RGB"));
        return;
    }

    GstMapInfo map;
    if (!gst_buffer_map(buffer, &map, GST_MAP_READ)) {
        gst_sample_unref(sample);
        fail(QStringLiteral("无法读取摄像头帧"));
        return;
    }
    const int bytesPerLine = sampleWidth * 3;
    const int expectedSize = bytesPerLine * sampleHeight;
    if (map.size < static_cast<gsize>(expectedSize)) {
        gst_buffer_unmap(buffer, &map);
        gst_sample_unref(sample);
        fail(QStringLiteral("摄像头帧大小不完整"));
        return;
    }
    const QImage image(static_cast<const uchar*>(map.data), sampleWidth, sampleHeight,
                       bytesPerLine, QImage::Format_RGB888);
    // 先复制为 Qt 自有图像并释放采集缓冲区，再修改颜色。
    // 不写入 GStreamer 的只读映射，也不会在同一帧上重复累积校正。
    QImage frame = image.copy();
    gst_buffer_unmap(buffer, &map);
    gst_sample_unref(sample);
    if (frame.isNull()) {
        fail(QStringLiteral("无法复制摄像头图像"));
        return;
    }
    if (colorCorrectionEnabled_) applyCameraColorCorrection(frame);
    emit frameReady(frame);

    ++framesInWindow_;
    if (fpsClock_.elapsed() >= 1000) {
        emit fpsChanged(framesInWindow_ * 1000.0 / fpsClock_.elapsed());
        framesInWindow_ = 0;
        fpsClock_.restart();
    }
}
