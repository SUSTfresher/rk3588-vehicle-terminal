// 方案A：实时影像占据页面主体，状态和控制叠加在画面上下边缘。
// 全局底部导航暂时保留，后续倒车挡自动进入时再切换为真正全屏。
#include "camera_page.h"
#include "camera_capture.h"

#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPixmap>
#include <QPushButton>
#include <QSizePolicy>
#include <QVBoxLayout>
#include <QWidget>

class CameraPage : public QWidget
{
public:
    explicit CameraPage(QWidget* parent = nullptr) : QWidget(parent)
    {
        setObjectName("cameraPage");
        setStyleSheet(
            "#cameraPage { background: #0f1316; }"
            "#cameraPage #preview { background: #0c1013; color: #8b969d; }"
            "#cameraPage #topBar, #cameraPage #bottomBar {"
            " background: rgba(16, 20, 24, 235); }"
            "#cameraPage #title { color: #f5f7f8; font-size: 18px; }"
            "#cameraPage #status, #cameraPage #fps {"
            " background: transparent; color: #c0c9ce; font-size: 13px; }"
            "#cameraPage QPushButton { background: #252e35; border: none;"
            " color: #d9e1e5; font-size: 14px; padding: 0 16px; }"
            "#cameraPage QPushButton:pressed { background: #303b43; color: #3e6ae1; }"
            "#cameraPage QPushButton:disabled { color: #555555; }");

        auto* layout = new QVBoxLayout(this);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(0);

        auto* previewFrame = new QWidget;
        previewFrame->setObjectName("previewFrame");
        auto* previewStack = new QGridLayout(previewFrame);
        previewStack->setContentsMargins(0, 0, 0, 0);
        previewStack->setSpacing(0);

        view_ = new QLabel(QStringLiteral("进入摄像头页面后开始采集"));
        view_->setObjectName("preview");
        view_->setAlignment(Qt::AlignCenter);
        // 预览图像会不断替换 pixmap，不能让 QLabel 根据上一帧的尺寸
        // 把整个页面的最小尺寸撑大，否则返回时可能挤掉底部导航栏。
        view_->setMinimumSize(0, 0);
        view_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);
        previewStack->addWidget(view_, 0, 0);

        auto* topBar = new QWidget;
        topBar->setObjectName("topBar");
        topBar->setAttribute(Qt::WA_StyledBackground, true);
        auto* topLayout = new QHBoxLayout(topBar);
        topLayout->setContentsMargins(14, 4, 14, 4);
        topLayout->setSpacing(12);
        auto* title = new QLabel(QStringLiteral("摄像头预览"));
        title->setObjectName("title");
        topLayout->addWidget(title);
        auto* subTitle = new QLabel(QStringLiteral("实时影像"));
        subTitle->setStyleSheet("background: transparent; color: #9aa5ab; font-size: 13px;");
        topLayout->addWidget(subTitle);
        topLayout->addStretch();
        status_ = new QLabel(QStringLiteral("摄像头未启动"));
        status_->setObjectName("status");
        topLayout->addWidget(status_);
        topLayout->addSpacing(10);
        back_ = new QPushButton(QStringLiteral("返回"));
        back_->setMinimumHeight(34);
        back_->setFocusPolicy(Qt::NoFocus);
        topLayout->addWidget(back_);
        previewStack->addWidget(topBar, 0, 0, Qt::AlignTop);

        auto* bottomBar = new QWidget;
        bottomBar->setObjectName("bottomBar");
        bottomBar->setAttribute(Qt::WA_StyledBackground, true);
        auto* bottomLayout = new QHBoxLayout(bottomBar);
        bottomLayout->setContentsMargins(14, 4, 14, 4);
        bottomLayout->setSpacing(8);
        fps_ = new QLabel(QStringLiteral("0.0 FPS"));
        fps_->setObjectName("fps");
        bottomLayout->addWidget(fps_);
        bottomLayout->addStretch();
        start_ = new QPushButton(QStringLiteral("开始预览"));
        stop_ = new QPushButton(QStringLiteral("停止预览"));
        for (auto* button : {start_, stop_}) {
            button->setMinimumHeight(42);
            button->setFocusPolicy(Qt::NoFocus);
        }
        bottomLayout->addWidget(start_);
        bottomLayout->addWidget(stop_);
        previewStack->addWidget(bottomBar, 0, 0, Qt::AlignBottom);
        layout->addWidget(previewFrame, 1);

        capture_ = new CameraCapture(this);
        connect(capture_, &CameraCapture::frameReady, this,
                [this](const QImage& image) {
            if (view_->width() <= 0 || view_->height() <= 0) return;
            view_->setPixmap(QPixmap::fromImage(image).scaled(
                view_->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
        });
        connect(capture_, &CameraCapture::statusChanged, status_,
                &QLabel::setText);
        connect(capture_, &CameraCapture::fpsChanged, this,
                [this](double fps) {
            fps_->setText(QStringLiteral("%1 FPS").arg(fps, 0, 'f', 1));
        });
        connect(start_, &QPushButton::clicked, this, [this]() {
            capture_->start();
            start_->setEnabled(false);
            stop_->setEnabled(true);
        });
        connect(stop_, &QPushButton::clicked, this, [this]() {
            capture_->stop();
            clearPreview(QStringLiteral("摄像头已停止"));
            start_->setEnabled(true);
            stop_->setEnabled(false);
        });
        stop_->setEnabled(false);
    }

    void setActive(bool active)
    {
        if (active) {
            capture_->start();
            start_->setEnabled(false);
            stop_->setEnabled(true);
        } else {
            capture_->stop();
            clearPreview(QStringLiteral("进入摄像头页面后开始采集"));
            start_->setEnabled(true);
            stop_->setEnabled(false);
            status_->setText(QStringLiteral("摄像头未启动"));
        }
    }

    QPushButton* backButton() const { return back_; }

private:
    void clearPreview(const QString& text)
    {
        view_->setPixmap(QPixmap());
        view_->setText(text);
        fps_->setText(QStringLiteral("0.0 FPS"));
    }

    CameraCapture* capture_;
    QLabel* view_;
    QLabel* status_;
    QLabel* fps_;
    QPushButton* start_;
    QPushButton* stop_;
    QPushButton* back_;
};

QWidget* createCameraPage(QWidget* parent)
{
    return new CameraPage(parent);
}

void setCameraPageActive(QWidget* page, bool active)
{
    if (page) static_cast<CameraPage*>(page)->setActive(active);
}

QPushButton* cameraPageBackButton(QWidget* page)
{
    return page ? static_cast<CameraPage*>(page)->backButton() : nullptr;
}
