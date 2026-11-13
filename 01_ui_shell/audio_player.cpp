// playbin 自动选择解码器，alsasink 输出到已经验证过的板载耳机接口。
// 用 Qt 定时器非阻塞读取消息总线，无需额外启动 GLib 主循环。
#include "audio_player.h"

#include <QDebug>
#include <QFileInfo>
#include <QUrl>

AudioPlayer::AudioPlayer(QObject* parent) : QObject(parent)
{
    GError* error = nullptr;
    if (!gst_init_check(nullptr, nullptr, &error)) {
        initError_ = QStringLiteral("GStreamer 初始化失败");
        if (error) {
            qWarning() << error->message;
            g_error_free(error);
        }
        return;
    }

    pipeline_ = gst_element_factory_make("playbin", nullptr);
    GstElement* sink = gst_element_factory_make("alsasink", nullptr);
    GstElement* volume = gst_element_factory_make("volume", nullptr);
    GstElement* convert = gst_element_factory_make("audioconvert", nullptr);
    GstElement* resample = gst_element_factory_make("audioresample", nullptr);
    if (!pipeline_ || !sink || !volume || !convert || !resample) {
        initError_ = QStringLiteral("缺少 GStreamer 音频插件");
        if (sink) gst_object_unref(sink);
        if (volume) gst_object_unref(volume);
        if (convert) gst_object_unref(convert);
        if (resample) gst_object_unref(resample);
        if (pipeline_) gst_object_unref(pipeline_);
        pipeline_ = nullptr;
        return;
    }

    // 显式接管 playbin 的浮动引用。
    gst_object_ref_sink(pipeline_);
    g_object_set(sink, "device", "plughw:CARD=rockchipes8388,DEV=0", nullptr);

    // playbin 的 audio-sink 接收一个完整的音频 sink。把音量元素放在
    // alsasink 前面，确保滑块和静音实际改变 PCM 数据，而不是只改变
    // playbin 的状态属性。
    GstElement* audioBin = gst_bin_new("audio_output");
    gst_bin_add_many(GST_BIN(audioBin), volume, convert, resample, sink, nullptr);
    if (!gst_element_link_many(volume, convert, resample, sink, nullptr)) {
        initError_ = QStringLiteral("GStreamer 音频链路创建失败");
        gst_object_unref(audioBin);
        gst_object_unref(pipeline_);
        pipeline_ = nullptr;
        return;
    }
    GstPad* volumeSinkPad = gst_element_get_static_pad(volume, "sink");
    GstPad* ghostPad = gst_ghost_pad_new("sink", volumeSinkPad);
    gst_object_unref(volumeSinkPad);
    gst_element_add_pad(audioBin, ghostPad);

    // GstPlayFlags 的 audio 位为 1<<1；不创建视频/字幕输出。
    g_object_set(pipeline_, "audio-sink", audioBin,
                 "flags", static_cast<guint>(1u << 1), nullptr);
    gst_object_unref(audioBin);
    volumeElement_ = volume;
    applyVolume();
    bus_ = gst_element_get_bus(pipeline_);

    busTimer_.setInterval(50);
    connect(&busTimer_, &QTimer::timeout, this, &AudioPlayer::pollBus);
    busTimer_.start();

    // 时间统一以毫秒交给界面；GStreamer 时间单位为纳秒。
    progressTimer_.setInterval(250);
    connect(&progressTimer_, &QTimer::timeout, this, [this]() {
        if (targetState_ == GST_STATE_NULL) return;
        gint64 position = 0, duration = 0;
        if (!gst_element_query_position(pipeline_, GST_FORMAT_TIME, &position))
            position = 0;
        if (!gst_element_query_duration(pipeline_, GST_FORMAT_TIME, &duration))
            duration = 0;
        emit progressChanged(qMax<qint64>(0, position / GST_MSECOND),
                             qMax<qint64>(0, duration / GST_MSECOND));
    });
    progressTimer_.start();
}

AudioPlayer::~AudioPlayer()
{
    busTimer_.stop();
    progressTimer_.stop();
    if (pipeline_) gst_element_set_state(pipeline_, GST_STATE_NULL);
    if (bus_) gst_object_unref(bus_);
    if (pipeline_) gst_object_unref(pipeline_);
    volumeElement_ = nullptr;
}

void AudioPlayer::setVolume(double normalized)
{
    volume_ = qBound(0.0, normalized, 1.0);
    applyVolume();
}

void AudioPlayer::setMuted(bool muted)
{
    muted_ = muted;
    applyVolume();
}

void AudioPlayer::applyVolume()
{
    if (!pipeline_) return;
    const double effective = muted_ ? 0.0 : volume_;
    g_object_set(pipeline_, "volume", effective, nullptr);
    if (volumeElement_)
        g_object_set(volumeElement_, "volume", effective, nullptr);
    qInfo() << "Audio volume:" << effective << "(user=" << volume_
            << ", muted=" << muted_ << ")";
}

void AudioPlayer::playFile(const QString& path)
{
    if (!pipeline_) {
        emit statusChanged(initError_);
        emit playingChanged(false);
        return;
    }
    const QFileInfo file(path);
    if (!file.isFile() || !file.isReadable()) {
        fail(QStringLiteral("找不到或无法读取音频文件"));
        return;
    }

    // 暂停后继续不重设 URI，因此保留播放位置；停止后则从头播放。
    const QString absolutePath = file.absoluteFilePath();
    if (currentPath_ != absolutePath || targetState_ == GST_STATE_NULL) {
        gst_element_set_state(pipeline_, GST_STATE_NULL);
        currentPath_ = absolutePath;
        emit progressChanged(0, 0);
        emit metadataChanged(QString(), QString());
        const QByteArray uri = QUrl::fromLocalFile(currentPath_).toEncoded();
        g_object_set(pipeline_, "uri", uri.constData(), nullptr);
        // playbin 在切换 URI 后可能重建内部音频链，重新应用用户音量。
        applyVolume();
    }
    targetState_ = GST_STATE_PLAYING;
    emit playingChanged(true);
    emit statusChanged(QStringLiteral("正在加载…"));
    if (gst_element_set_state(pipeline_, targetState_) == GST_STATE_CHANGE_FAILURE) {
        fail(QStringLiteral("启动播放失败，请检查音频文件或声卡"));
    } else {
        // 重复点击播放时不一定再产生状态变化消息。
        GstState current = GST_STATE_NULL;
        gst_element_get_state(pipeline_, &current, nullptr, 0);
        if (current == GST_STATE_PLAYING)
            emit statusChanged(QStringLiteral("播放中"));
    }
}

void AudioPlayer::pause()
{
    if (!pipeline_ || targetState_ != GST_STATE_PLAYING) return;
    targetState_ = GST_STATE_PAUSED;
    emit playingChanged(false);
    emit statusChanged(QStringLiteral("正在暂停…"));
    if (gst_element_set_state(pipeline_, targetState_) == GST_STATE_CHANGE_FAILURE) {
        fail(QStringLiteral("暂停失败"));
    } else {
        GstState current = GST_STATE_NULL;
        gst_element_get_state(pipeline_, &current, nullptr, 0);
        if (current == GST_STATE_PAUSED)
            emit statusChanged(QStringLiteral("已暂停"));
    }
}

void AudioPlayer::stop()
{
    targetState_ = GST_STATE_NULL;
    // NULL 释放音频设备并清理流水线，下次播放重新打开文件。
    if (pipeline_) gst_element_set_state(pipeline_, GST_STATE_NULL);
    emit playingChanged(false);
    emit progressChanged(0, 0);
    emit statusChanged(QStringLiteral("已停止"));
}

void AudioPlayer::seek(qint64 positionMs)
{
    if (!pipeline_ || targetState_ == GST_STATE_NULL) return;
    gint64 duration = 0;
    if (!gst_element_query_duration(pipeline_, GST_FORMAT_TIME, &duration) ||
        duration <= 0) return;
    // 只在用户松开滑块时跳转，避免拖动期间连续执行解码重置。
    const qint64 maximumMs = qMax<qint64>(0, duration / GST_MSECOND - 1);
    const gint64 target = qBound<qint64>(0, positionMs, maximumMs) * GST_MSECOND;
    if (!gst_element_seek_simple(pipeline_, GST_FORMAT_TIME,
            static_cast<GstSeekFlags>(GST_SEEK_FLAG_FLUSH | GST_SEEK_FLAG_KEY_UNIT),
            target)) {
        emit statusChanged(QStringLiteral("此文件暂不支持跳转"));
    }
}

void AudioPlayer::fail(const QString& message)
{
    targetState_ = GST_STATE_NULL;
    if (pipeline_) gst_element_set_state(pipeline_, GST_STATE_NULL);
    emit playingChanged(false);
    emit progressChanged(0, 0);
    qWarning() << "Audio playback error:" << message;
    emit statusChanged(QStringLiteral("播放失败：") + message);
}

void AudioPlayer::pollBus()
{
    // 每轮最多处理32条，不阻塞等待，给车辆数据和触摸事件留出执行机会。
    for (int i = 0; bus_ && i < 32; ++i) {
        GstMessage* message = gst_bus_pop(bus_);
        if (!message) break;
        switch (GST_MESSAGE_TYPE(message)) {
        case GST_MESSAGE_ERROR: {
            GError* error = nullptr;
            gchar* debug = nullptr;
            gst_message_parse_error(message, &error, &debug);
            const QString text = error ? QString::fromUtf8(error->message)
                                       : QStringLiteral("未知错误");
            if (debug) qWarning() << "GStreamer:" << debug;
            if (error) g_error_free(error);
            g_free(debug);
            fail(text);
            break;
        }
        case GST_MESSAGE_EOS:
            stop();
            emit statusChanged(QStringLiteral("播放结束"));
            emit finished();
            break;
        case GST_MESSAGE_TAG: {
            GstTagList* tags = nullptr;
            gst_message_parse_tag(message, &tags);
            gchar* title = nullptr;
            gchar* artist = nullptr;
            gst_tag_list_get_string(tags, GST_TAG_TITLE, &title);
            gst_tag_list_get_string(tags, GST_TAG_ARTIST, &artist);
            emit metadataChanged(title ? QString::fromUtf8(title) : QString(),
                                 artist ? QString::fromUtf8(artist) : QString());
            g_free(title);
            g_free(artist);
            gst_tag_list_unref(tags);
            break;
        }
        case GST_MESSAGE_STATE_CHANGED:
            if (GST_MESSAGE_SRC(message) == GST_OBJECT(pipeline_)) {
                GstState oldState, newState, pending;
                gst_message_parse_state_changed(message, &oldState, &newState, &pending);
                // 加载时也会经过 PAUSED；只报告符合当前用户目标的状态。
                if (newState == targetState_ && newState == GST_STATE_PLAYING)
                    emit statusChanged(QStringLiteral("播放中"));
                else if (newState == targetState_ && newState == GST_STATE_PAUSED)
                    emit statusChanged(QStringLiteral("已暂停"));
            }
            break;
        default:
            break;
        }
        gst_message_unref(message);
    }
}
