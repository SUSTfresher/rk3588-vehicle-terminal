#include "vehicle_data_client.h"
#include "audio_player.h"
#include "media_page.h"
#include "camera_page.h"
#include "map_page.h"
#include "system_page.h"
#include "connection_page.h"
#include <memory>
#include <QDateTime>
#include <QDebug>
#include <QEvent>
#include <QMouseEvent>
#include <QTimer>
#include <QPixmap>
#include <QPainter>
#include <QPolygonF>
#include <QPen>
#include <QColor>
#include <QSize>
#include <QIcon>
#include <QCoreApplication>
#include <QFont>
#include <QButtonGroup>
#include <QApplication>
#include <QWidget>
#include <QLabel>
#include <QPushButton>
#include <QAbstractButton>
#include <QSlider>
#include <QStackedLayout>
#include <QStackedWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QFrame>
#include <QSizePolicy>
#include <QScreen>
#include <QStringList>

// 底部导航使用本地绘制的线框图标，避免依赖板端图标字体。
static QPixmap makeNavigationIcon(int index, const QColor& color)
{
    QPixmap icon(32, 32);
    icon.fill(Qt::transparent);

    QPainter painter(&icon);
    painter.setRenderHint(QPainter::Antialiasing, true);
    QPen pen(color);
    pen.setWidth(2);
    pen.setCapStyle(Qt::RoundCap);
    pen.setJoinStyle(Qt::RoundJoin);
    painter.setPen(pen);
    painter.setBrush(Qt::NoBrush);

    if (index == 0) { // 车辆状态：仪表盘
        painter.drawArc(6, 6, 20, 20, 25 * 16, 130 * 16);
        painter.drawLine(16, 16, 21, 11);
        painter.drawEllipse(14, 14, 4, 4);
    } else if (index == 1) { // 多媒体：音符
        painter.drawEllipse(13, 21, 6, 4);
        painter.drawLine(18, 22, 18, 9);
        painter.drawLine(18, 9, 25, 11);
        painter.drawLine(25, 11, 25, 16);
        painter.drawEllipse(20, 15, 6, 4);
    } else if (index == 2) { // 摄像头
        painter.drawRoundedRect(6, 11, 20, 15, 2, 2);
        painter.drawLine(11, 11, 13, 8);
        painter.drawLine(13, 8, 19, 8);
        painter.drawLine(19, 8, 21, 11);
        painter.drawEllipse(12, 14, 8, 8);
    } else { // 应用：四宫格
        painter.drawRoundedRect(7, 7, 8, 8, 1, 1);
        painter.drawRoundedRect(17, 7, 8, 8, 1, 1);
        painter.drawRoundedRect(7, 17, 8, 8, 1, 1);
        painter.drawRoundedRect(17, 17, 8, 8, 1, 1);
    }

    return icon;
}

// 设置页图标也使用本地线条绘制，不依赖 Unicode 图标字体。
enum class SettingsIcon { Back, Sound, Display, Connection, Headphones };

static QPixmap makeSettingsIcon(SettingsIcon type, const QColor& color)
{
    QPixmap icon(32, 32);
    icon.fill(Qt::transparent);
    QPainter painter(&icon);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(QPen(color, 2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter.setBrush(Qt::NoBrush);
    switch (type) {
    case SettingsIcon::Back:
        painter.drawLine(19, 8, 11, 16);
        painter.drawLine(11, 16, 19, 24);
        break;
    case SettingsIcon::Sound: {
        QPolygonF speaker;
        speaker << QPointF(6, 12) << QPointF(11, 12) << QPointF(17, 7)
                << QPointF(17, 25) << QPointF(11, 20) << QPointF(6, 20)
                << QPointF(6, 12);
        painter.drawPolyline(speaker);
        painter.drawArc(17, 9, 10, 14, -60 * 16, 120 * 16);
        break;
    }
    case SettingsIcon::Display:
        painter.drawEllipse(11, 11, 10, 10);
        for (int i = 0; i < 8; ++i) {
            painter.save();
            painter.translate(16, 16);
            painter.rotate(i * 45);
            painter.drawLine(0, -9, 0, -12);
            painter.restore();
        }
        break;
    case SettingsIcon::Connection:
        painter.drawArc(3, 7, 26, 26, 45 * 16, 90 * 16);
        painter.drawArc(8, 13, 16, 16, 45 * 16, 90 * 16);
        painter.drawArc(12, 19, 8, 8, 45 * 16, 90 * 16);
        painter.drawPoint(16, 26);
        break;
    case SettingsIcon::Headphones:
        painter.drawArc(6, 5, 20, 22, 0, 180 * 16);
        painter.drawRoundedRect(5, 16, 5, 10, 2, 2);
        painter.drawRoundedRect(22, 16, 5, 10, 2, 2);
        break;
    }
    return icon;
}

// 轨道是 52×30，触摸区域是 64×48；QAbstractButton 提供点击和选中状态。
// 只负责绘制，不涉及音频，沿用已验证的 AudioPlayer 静音接口。
class SoundMuteSwitch : public QAbstractButton
{
public:
    explicit SoundMuteSwitch(QWidget* parent = nullptr) : QAbstractButton(parent)
    {
        setCheckable(true);
        setFixedSize(64, 48);
        setFocusPolicy(Qt::NoFocus);
        setAccessibleName(QStringLiteral("媒体静音"));
    }

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(isChecked() ? "#3e6ae1" : "#424b55"));
        painter.drawRoundedRect(QRectF(6, 9, 52, 30), 15, 15);
        painter.setBrush(QColor(isDown() ? "#d6dce4" : "#f3f5f7"));
        painter.drawEllipse(QRectF(isChecked() ? 31 : 9, 12, 24, 24));
    }
};

// 识别横向滑动切换页面。按钮和进度条区域交给原控件处理，
// 避免滑动音乐进度条时误触发页面切换。
class SwipeEventFilter : public QObject
{
public:
    explicit SwipeEventFilter(QStackedWidget* pages, QObject* parent = nullptr)
        : QObject(parent), pages_(pages) {}

protected:
    bool eventFilter(QObject* watched, QEvent* event) override
    {
        const auto type = event->type();
        if (type != QEvent::MouseButtonPress &&
            type != QEvent::MouseButtonRelease &&
            type != QEvent::MouseMove) {
            return false;
        }

        if (isInteractive(watched)) {
            tracking_ = false;
            return false;
        }

        auto* mouse = static_cast<QMouseEvent*>(event);
        if (type == QEvent::MouseButtonPress &&
            mouse->button() == Qt::LeftButton) {
            start_ = mouse->globalPos();
            tracking_ = true;
            return false;
        }

        if (type == QEvent::MouseButtonRelease && tracking_ &&
            mouse->button() == Qt::LeftButton) {
            const QPoint delta = mouse->globalPos() - start_;
            tracking_ = false;
            if (qAbs(delta.x()) < 70 ||
                qAbs(delta.x()) <= qAbs(delta.y()) * 5 / 4) {
                return false;
            }

            const int current = pages_->currentIndex();
            const int next = delta.x() < 0 ? current + 1 : current - 1;
            if (next >= 0 && next < pages_->count()) {
                pages_->setCurrentIndex(next);
                return true;
            }
        }

        return false;
    }

private:
    static bool isInteractive(QObject* object)
    {
        for (QObject* current = object; current; current = current->parent()) {
            // 设置页面中的滑块、按钮和整块面板都不参与页面左右滑动。
            if (current->property("swipeBlock").toBool()) {
                return true;
            }
            if (qobject_cast<QAbstractButton*>(current) ||
                qobject_cast<QSlider*>(current)) {
                return true;
            }
        }
        return false;
    }

    QStackedWidget* pages_;
    QPoint start_;
    bool tracking_ = false;
};

int main(int argc, char* argv[])
{
    // 管理图形界面和事件循环。
    QApplication app(argc, argv);

// 明确使用已安装的中文字体，避免自动回退到其他字体。
QFont font(QStringLiteral("Noto Sans CJK SC"));
font.setWeight(QFont::Normal);
app.setFont(font);

    QWidget window;
    window.setWindowTitle(QStringLiteral("车载终端"));

    // 主布局：顶部标题、中间页面、底部导航。
    QVBoxLayout mainLayout(&window);


mainLayout.setContentsMargins(12, 4, 12, 8);
mainLayout.setSpacing(4);

// 顶部状态栏：左侧时间，右侧模拟模式。
QHBoxLayout statusBar;

QLabel clockLabel;
clockLabel.setFixedHeight(32);
clockLabel.setStyleSheet(
    "font-size: 14px; color: #9aa4aa;"
);

QLabel modeLabel(QStringLiteral("模拟模式"));
modeLabel.setStyleSheet(
    "font-size: 14px; color: #9aa4aa;"
);

statusBar.addWidget(&clockLabel);
statusBar.addStretch();
statusBar.addWidget(&modeLabel);

mainLayout.addLayout(&statusBar);

// 先显示一次，避免启动后要等一秒才出现时间。
auto updateClock = [&clockLabel]() {
    clockLabel.setText(
        QDateTime::currentDateTime().toString("MM-dd  HH:mm:ss")
    );
};

updateClock();

QTimer clockTimer;
clockTimer.setInterval(1000);
QObject::connect(
    &clockTimer, &QTimer::timeout,
    &clockLabel, updateClock
);
clockTimer.start();


    // 同时保存多个页面，但每次只显示一个。
    QStackedWidget pages;
    mainLayout.addWidget(&pages, 1);

    // 导航栏使用独立容器并固定高度。进入摄像头页时隐藏整个容器，
    // 返回普通页面时再显示容器，避免只隐藏按钮后布局高度无法恢复。
    QWidget navigationBar;
    navigationBar.setObjectName(QStringLiteral("navigationBar"));
    navigationBar.setFixedHeight(56);
    navigationBar.setSizePolicy(
        QSizePolicy::Expanding, QSizePolicy::Fixed);
    QHBoxLayout navigation(&navigationBar);
    navigation.setContentsMargins(0, 0, 0, 0);
    navigation.setSpacing(0);
    mainLayout.addWidget(&navigationBar);

    const QStringList names = {
        QStringLiteral("车辆状态"),
        QStringLiteral("多媒体"),
        QStringLiteral("摄像头"),
        QStringLiteral("更多")
    };

QButtonGroup navigationGroup;
navigationGroup.setExclusive(true);
    QWidget* cameraPage = nullptr;
    auto reverseCameraActive = std::make_shared<bool>(false);
    auto* audioPlayer = new AudioPlayer(&window);

    // 应用页采用车主功能入口布局，不展示开发者信息或技术栈。
    auto createAppsPage = [&pages, audioPlayer, &navigationBar, &mainLayout]() -> QWidget* {
        auto* page = new QWidget;
        page->setObjectName(QStringLiteral("appsPage"));
        page->setStyleSheet(
            "#appsPage { background: #0f1316; }"
            "#appsPage #pageTitle { color: #f5f7f8; font-size: 26px; }"
            "#appsPage #pageSubtitle { color: #89949a; font-size: 13px; }"
            "#appsPage QFrame#appTile {"
            " background: #1e252b; border: 1px solid #303b43;"
            " border-radius: 12px; }"
            "#appsPage QFrame#appTile[primary=\"true\"] {"
            " background: #1d3548; border: 1px solid #3e6ae1; }"
            "#appsPage #tileIcon { color: #3e6ae1; font-size: 24px; }"
            "#appsPage #tileTitle { color: #f0f4f6; font-size: 17px; }"
            "#appsPage #tileDetail { color: #89949a; font-size: 12px; }"
            "#appsPage QPushButton#appHit { background: transparent;"
            " border: none; border-radius: 12px; }");

        // 应用入口与设置主体互相替换，不叠加两套标题和外边距。
        auto* settingsStack = new QStackedLayout(page);
        settingsStack->setContentsMargins(0, 0, 0, 0);
        auto* overview = new QWidget;
        settingsStack->addWidget(overview);
        auto* pageLayout = new QVBoxLayout(overview);
        pageLayout->setContentsMargins(18, 16, 18, 16);
        pageLayout->setSpacing(6);

        auto* title = new QLabel(QStringLiteral("更多"));
        title->setObjectName(QStringLiteral("pageTitle"));
        pageLayout->addWidget(title);

        auto* subtitle = new QLabel(
            QStringLiteral("车机功能"));
        subtitle->setObjectName(QStringLiteral("pageSubtitle"));
        pageLayout->addWidget(subtitle);
        pageLayout->addSpacing(8);

        auto* grid = new QGridLayout;
        grid->setContentsMargins(0, 0, 0, 0);
        grid->setHorizontalSpacing(12);
        grid->setVerticalSpacing(12);

        auto* hint = new QLabel(QStringLiteral("选择一个功能"));
        hint->setObjectName(QStringLiteral("pageSubtitle"));
        hint->setAlignment(Qt::AlignCenter);
        hint->setVisible(false);

        auto* gridWidget = new QWidget(overview);
        auto* soundPanel = new QWidget;
        soundPanel->setObjectName(QStringLiteral("soundPanel"));
        soundPanel->setProperty("swipeBlock", true);
        settingsStack->addWidget(soundPanel);
        soundPanel->setStyleSheet(
            "#soundPanel, #soundPanel QWidget { background: #101316; }"
            "#soundPanel QLabel { background: transparent; color: #eff1f3; font-size: 18px; }"
            "#soundPanel #settingsSidebar { background: #171b1f;"
            " border: none; border-right: 1px solid #272d32; }"
            "#soundPanel QPushButton { background: transparent; border: none;"
            " border-radius: 8px; text-align: left; padding-left: 10px;"
            " font-size: 17px; color: #aeb7c0; }"
            "#soundPanel QPushButton:pressed { background: #252e35; }"
            "#soundPanel QPushButton:checked { background: #25334b; color: #f6f7f9;"
            " border: none; border-left: 3px solid #3e6ae1; padding-left: 7px; }"
            "#soundPanel QPushButton:disabled { color: #68737d; }"
            "#soundPanel #soundBack { font-size: 15px; }"
            "#soundPanel #soundTitle { font-size: 26px; }"
            "#soundPanel #volumeValue, #soundPanel #outputValue {"
            " font-size: 17px; color: #aeb7c0; }"
            "#soundPanel #muteState { font-size: 13px; color: #8e99a4; }"
            "#soundPanel QFrame#settingsDivider { background: #2a3036; border: none; }"
            "#soundPanel QSlider { background: transparent; }"
            "#soundPanel QSlider::groove:horizontal { height: 5px; background: #343d47; border-radius: 2px; }"
            "#soundPanel QSlider::sub-page:horizontal { background: #3e6ae1; border-radius: 2px; }"
            "#soundPanel QSlider::handle:horizontal { background: #f3f5f7;"
            " width: 22px; margin: -9px 0; border-radius: 11px; }");

        auto* soundLayout = new QHBoxLayout(soundPanel);
        soundLayout->setContentsMargins(0, 0, 0, 0);
        soundLayout->setSpacing(0);
        auto* sidebar = new QFrame;
        sidebar->setObjectName(QStringLiteral("settingsSidebar"));
        sidebar->setFixedWidth(154);
        auto* sideLayout = new QVBoxLayout(sidebar);
        sideLayout->setContentsMargins(12, 14, 12, 14);
        sideLayout->setSpacing(14);
        auto* soundBackButton = new QPushButton(QStringLiteral("更多"));
        soundBackButton->setObjectName(QStringLiteral("soundBack"));
        soundBackButton->setIcon(makeSettingsIcon(SettingsIcon::Back, QColor("#aeb7c0")));
        soundBackButton->setIconSize(QSize(24, 24));
        soundBackButton->setFixedHeight(44);
        soundBackButton->setFocusPolicy(Qt::NoFocus);
        sideLayout->addWidget(soundBackButton);

        const auto addCategory = [sideLayout](const QString& text, SettingsIcon icon,
                                              bool selected) {
            auto* button = new QPushButton(text);
            button->setFixedHeight(50);
            button->setFocusPolicy(Qt::NoFocus);
            button->setCheckable(selected);
            button->setChecked(selected);
            button->setEnabled(selected);
            const QPixmap pixmap = makeSettingsIcon(icon,
                QColor(selected ? "#eff1f3" : "#68737d"));
            QIcon categoryIcon;
            categoryIcon.addPixmap(pixmap, QIcon::Normal);
            categoryIcon.addPixmap(pixmap, QIcon::Disabled);
            button->setIcon(categoryIcon);
            button->setIconSize(QSize(24, 24));
            // 当前只有声音分类可用，点击当前项保持高亮。
            if (selected) QObject::connect(button, &QPushButton::clicked, button,
                                          [button]() { button->setChecked(true); });
            sideLayout->addWidget(button);
        };
        addCategory(QStringLiteral("声音"), SettingsIcon::Sound, true);
        addCategory(QStringLiteral("显示"), SettingsIcon::Display, false);
        addCategory(QStringLiteral("连接"), SettingsIcon::Connection, false);
        sideLayout->addStretch();
        soundLayout->addWidget(sidebar);

        auto* settingsContent = new QWidget;
        auto* contentLayout = new QVBoxLayout(settingsContent);
        contentLayout->setContentsMargins(28, 20, 28, 16);
        contentLayout->setSpacing(0);
        auto* soundTitle = new QLabel(QStringLiteral("声音"));
        soundTitle->setObjectName(QStringLiteral("soundTitle"));
        soundTitle->setFixedHeight(38);
        contentLayout->addWidget(soundTitle);
        contentLayout->addSpacing(20);

        auto* volumeRow = new QHBoxLayout;
        auto* volumeCaption = new QLabel(QStringLiteral("媒体音量"));
        auto* volumeValue = new QLabel;
        volumeValue->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        volumeValue->setObjectName(QStringLiteral("volumeValue"));
        volumeRow->addWidget(volumeCaption);
        volumeRow->addStretch();
        volumeRow->addWidget(volumeValue);
        contentLayout->addLayout(volumeRow);
        contentLayout->addSpacing(10);

        auto* sliderRow = new QHBoxLayout;
        sliderRow->setSpacing(12);
        const auto iconLabel = [](SettingsIcon icon) {
            auto* label = new QLabel;
            label->setFixedSize(28, 28);
            label->setPixmap(makeSettingsIcon(icon, QColor("#aeb7c0"))
                             .scaled(24, 24, Qt::KeepAspectRatio, Qt::SmoothTransformation));
            label->setAlignment(Qt::AlignCenter);
            return label;
        };
        auto* lowVolume = new QLabel(QStringLiteral("−"));
        lowVolume->setFixedWidth(24);
        lowVolume->setAlignment(Qt::AlignCenter);
        sliderRow->addWidget(lowVolume);
        auto* volumeSlider = new QSlider(Qt::Horizontal);
        volumeSlider->setRange(0, 100);
        volumeSlider->setFixedHeight(48);
        volumeSlider->setValue(qBound(0, static_cast<int>(audioPlayer->volume() * 100), 100));
        volumeSlider->setAccessibleName(QStringLiteral("媒体音量"));
        sliderRow->addWidget(volumeSlider, 1);
        sliderRow->addWidget(iconLabel(SettingsIcon::Sound));
        contentLayout->addLayout(sliderRow);

        const auto addDivider = [contentLayout]() {
            auto* line = new QFrame;
            line->setObjectName(QStringLiteral("settingsDivider"));
            line->setFixedHeight(1);
            contentLayout->addSpacing(12);
            contentLayout->addWidget(line);
            contentLayout->addSpacing(16);
        };
        addDivider();
        auto* muteRow = new QHBoxLayout;
        muteRow->addWidget(new QLabel(QStringLiteral("媒体静音")));
        muteRow->addStretch();
        auto* muteButton = new SoundMuteSwitch;
        muteRow->addWidget(muteButton);
        contentLayout->addLayout(muteRow);
        auto* muteState = new QLabel;
        muteState->setObjectName(QStringLiteral("muteState"));
        contentLayout->addWidget(muteState);
        addDivider();

        auto* outputRow = new QHBoxLayout;
        outputRow->setSpacing(8);
        outputRow->addWidget(new QLabel(QStringLiteral("音频输出")));
        outputRow->addStretch();
        outputRow->addWidget(iconLabel(SettingsIcon::Headphones));
        auto* outputInfo = new QLabel(QStringLiteral("有线耳机"));
        outputInfo->setObjectName(QStringLiteral("outputValue"));
        outputRow->addWidget(outputInfo);
        contentLayout->addLayout(outputRow);
        contentLayout->addStretch();
        soundLayout->addWidget(settingsContent, 1);

        const auto updateMuteText = [audioPlayer, muteButton, muteState]() {
            muteButton->setChecked(audioPlayer->isMuted());
            muteState->setText(audioPlayer->isMuted()
                ? QStringLiteral("已静音") : QStringLiteral("关闭"));
        };
        QObject::connect(volumeSlider, &QSlider::valueChanged, soundPanel,
                         [audioPlayer, volumeValue, updateMuteText](int value) {
            audioPlayer->setVolume(value / 100.0);
            if (audioPlayer->isMuted() && value > 0)
                audioPlayer->setMuted(false);
            volumeValue->setText(QStringLiteral("%1%").arg(value));
            updateMuteText();
        });
        volumeValue->setText(QStringLiteral("%1%").arg(volumeSlider->value()));
        updateMuteText();
        QObject::connect(muteButton, &QAbstractButton::clicked, soundPanel,
                         [audioPlayer, updateMuteText]() {
            audioPlayer->setMuted(!audioPlayer->isMuted());
            updateMuteText();
        });
        QObject::connect(soundBackButton, &QPushButton::clicked, page,
                         [settingsStack, page]() {
            settingsStack->setCurrentIndex(0);
            page->setProperty("swipeBlock", false);
        });

        // 地图放在声音设置之后，保持声音页原来的内部索引为 1。
        auto* mapPage = createMapPage(page);
        settingsStack->addWidget(mapPage);
        if (auto* mapBack = mapPageBackButton(mapPage)) {
            QObject::connect(mapBack, &QPushButton::clicked, page,
                             [settingsStack, page, &navigationBar, &mainLayout]() {
                settingsStack->setCurrentIndex(0);
                page->setProperty("swipeBlock", false);
                navigationBar.setVisible(true);
                mainLayout.invalidate();
                mainLayout.activate();
            });
        }

        auto* systemPage = createSystemPage(page);
        settingsStack->addWidget(systemPage);
        if (auto* systemBack = systemPageBackButton(systemPage)) {
            QObject::connect(systemBack, &QPushButton::clicked, page,
                             [settingsStack, page]() {
                settingsStack->setCurrentIndex(0);
                page->setProperty("swipeBlock", false);
            });
        }

        auto* connectionPage = createConnectionPage(page);
        settingsStack->addWidget(connectionPage);
        if (auto* connectionBack = connectionPageBackButton(connectionPage)) {
            QObject::connect(connectionBack, &QPushButton::clicked, page,
                             [settingsStack, page]() {
                settingsStack->setCurrentIndex(0);
                page->setProperty("swipeBlock", false);
            });
        }

        // 用 QPainter 绘制简单线框图标，避免依赖系统图标字体。
        auto makeAppIcon = [](const QString& key) {
            QPixmap icon(32, 32);
            icon.fill(Qt::transparent);

            QPainter painter(&icon);
            painter.setRenderHint(QPainter::Antialiasing, true);
            QPen pen(QColor("#3e6ae1"));
            pen.setWidth(2);
            pen.setCapStyle(Qt::RoundCap);
            pen.setJoinStyle(Qt::RoundJoin);
            painter.setPen(pen);
            painter.setBrush(Qt::NoBrush);

            if (key == QStringLiteral("car")) {
                painter.drawRoundedRect(7, 13, 18, 10, 2, 2);
                painter.drawLine(10, 13, 12, 9);
                painter.drawLine(12, 9, 20, 9);
                painter.drawLine(20, 9, 23, 13);
                painter.drawEllipse(10, 20, 3, 3);
                painter.drawEllipse(19, 20, 3, 3);
            } else if (key == QStringLiteral("sun")) {
                painter.drawEllipse(11, 11, 10, 10);
                painter.drawLine(16, 5, 16, 8);
                painter.drawLine(16, 24, 16, 27);
                painter.drawLine(5, 16, 8, 16);
                painter.drawLine(24, 16, 27, 16);
                painter.drawLine(8, 8, 10, 10);
                painter.drawLine(22, 22, 24, 24);
                painter.drawLine(22, 10, 24, 8);
                painter.drawLine(8, 24, 10, 22);
            } else if (key == QStringLiteral("volume")) {
                painter.drawLine(7, 14, 12, 14);
                painter.drawLine(12, 14, 17, 9);
                painter.drawLine(17, 9, 17, 23);
                painter.drawLine(17, 23, 12, 18);
                painter.drawLine(12, 18, 7, 18);
                painter.drawLine(22, 13, 24, 15);
                painter.drawLine(24, 15, 24, 17);
                painter.drawLine(24, 17, 22, 19);
            } else if (key == QStringLiteral("bluetooth")) {
                painter.drawLine(15, 5, 15, 27);
                painter.drawLine(15, 5, 22, 11);
                painter.drawLine(22, 11, 9, 22);
                painter.drawLine(9, 10, 22, 21);
                painter.drawLine(22, 21, 15, 27);
            } else if (key == QStringLiteral("camera")) {
                painter.drawRoundedRect(6, 11, 20, 15, 2, 2);
                painter.drawLine(11, 11, 13, 8);
                painter.drawLine(13, 8, 19, 8);
                painter.drawLine(19, 8, 21, 11);
                painter.drawEllipse(12, 14, 8, 8);
            } else if (key == QStringLiteral("map")) {
                painter.drawPolyline(QPolygonF() << QPointF(6, 9)
                    << QPointF(13, 6) << QPointF(20, 9) << QPointF(27, 6));
                painter.drawPolyline(QPolygonF() << QPointF(6, 9)
                    << QPointF(6, 25) << QPointF(13, 22) << QPointF(20, 25)
                    << QPointF(27, 22) << QPointF(27, 6));
                painter.drawLine(13, 6, 13, 22);
                painter.drawLine(20, 9, 20, 25);
            } else {
                painter.drawEllipse(7, 7, 18, 18);
                painter.drawLine(16, 14, 16, 20);
                painter.drawPoint(16, 11);
            }

            return icon;
        };

        auto addTile = [grid, settingsStack, page, mapPage, systemPage, connectionPage, &pages, hint,
                        &makeAppIcon, &navigationBar, &mainLayout](int row, int column,
                                            const QString& iconKey,
                                            const QString& tileTitle,
                                            const QString& detail,
                                            int targetPage) {
            auto* tile = new QFrame;
            tile->setObjectName(QStringLiteral("appTile"));
            tile->setProperty("primary", row == 0 && column == 0);
            tile->setMinimumHeight(110);

            auto* tileLayout = new QGridLayout(tile);
            tileLayout->setContentsMargins(14, 10, 14, 10);
            tileLayout->setSpacing(2);

            auto* content = new QWidget(tile);
            auto* contentLayout = new QVBoxLayout(content);
            contentLayout->setContentsMargins(0, 0, 0, 0);
            contentLayout->setSpacing(3);

            auto* iconLabel = new QLabel;
            iconLabel->setObjectName(QStringLiteral("tileIcon"));
            iconLabel->setFixedSize(32, 32);
            iconLabel->setPixmap(makeAppIcon(iconKey));
            contentLayout->addWidget(iconLabel);

            auto* titleLabel = new QLabel(tileTitle);
            titleLabel->setObjectName(QStringLiteral("tileTitle"));
            contentLayout->addWidget(titleLabel);

            auto* detailLabel = new QLabel(detail);
            detailLabel->setObjectName(QStringLiteral("tileDetail"));
            detailLabel->setWordWrap(true);
            contentLayout->addWidget(detailLabel);
            contentLayout->addStretch();
            tileLayout->addWidget(content, 0, 0);

            auto* hit = new QPushButton(tile);
            hit->setObjectName(QStringLiteral("appHit"));
            hit->setFocusPolicy(Qt::NoFocus);
            tileLayout->addWidget(hit, 0, 0);

            QObject::connect(hit, &QPushButton::clicked, tile,
                             [&pages, settingsStack, page, hint,
                              mapPage, systemPage, connectionPage, targetPage, tileTitle,
                              &navigationBar, &mainLayout]() {
                if (targetPage >= 0) {
                    pages.setCurrentIndex(targetPage);
                    return;
                }
                if (targetPage == -2) {
                    settingsStack->setCurrentIndex(1);
                    // 整个设置页（包括留白）都不参与全局左右滑动。
                    page->setProperty("swipeBlock", true);
                    return;
                }
                if (targetPage == -3) {
                    settingsStack->setCurrentWidget(mapPage);
                    page->setProperty("swipeBlock", true);
                    navigationBar.setVisible(false);
                    mainLayout.invalidate();
                    mainLayout.activate();
                    return;
                }
                if (targetPage == -4) {
                    settingsStack->setCurrentWidget(systemPage);
                    page->setProperty("swipeBlock", true);
                    return;
                }
                if (targetPage == -5) {
                    settingsStack->setCurrentWidget(connectionPage);
                    page->setProperty("swipeBlock", true);
                    return;
                }
                hint->setText(tileTitle + QStringLiteral("：功能将在后续接入"));
                hint->setVisible(true);
            });

            grid->addWidget(tile, row, column);
        };

        addTile(0, 0, QStringLiteral("map"), QStringLiteral("在线地图"),
                QStringLiteral("天地图 · 校区示例"), -3);
        addTile(0, 1, QStringLiteral("volume"), QStringLiteral("声音设置"),
                QStringLiteral("音量 · 输出设备"), -2);
        addTile(1, 0, QStringLiteral("bluetooth"), QStringLiteral("连接状态"),
                QStringLiteral("网络 · 蓝牙 · 手机互联"), -5);
        addTile(1, 1, QStringLiteral("info"), QStringLiteral("系统信息"),
                QStringLiteral("设备状态 · 系统版本"), -4);

        gridWidget->setLayout(grid);
        pageLayout->addWidget(gridWidget);
        pageLayout->addSpacing(6);
        pageLayout->addWidget(hint);
        pageLayout->addStretch();
        return page;
    };

    for (int i = 0; i < names.size(); ++i) {
        // 每页暂时只放一个标签，验证页面切换。

if (i == 0) {
    auto* dashboard = new QWidget;

    // 主体左右排列：左边驾驶信息，右边车辆展示。
    auto* row = new QHBoxLayout(dashboard);
    row->setContentsMargins(0, 12, 0, 12);
    row->setSpacing(0);

    auto* info = new QWidget;
    auto* infoLayout = new QVBoxLayout(info);
    infoLayout->setContentsMargins(12, 0, 12, 0);
    infoLayout->setSpacing(6);

    // 上下弹性空间，让整组信息在垂直方向居中。
    infoLayout->addStretch();

    auto* speed = new QLabel(QStringLiteral("0"));
    speed->setAlignment(Qt::AlignCenter);
    speed->setStyleSheet(
        "font-size: 88px; font-weight: normal; color: #f5f7f8;"
    );
    infoLayout->addWidget(speed);

    auto* unit = new QLabel(QStringLiteral("km/h"));
    unit->setAlignment(Qt::AlignCenter);
    unit->setStyleSheet("font-size: 16px; color: #9aa4aa;");
    infoLayout->addWidget(unit);

    infoLayout->addSpacing(20);

    // 暂用富文本突出当前挡位，后面再接入模拟挡位数据。
    auto* gears = new QLabel(QStringLiteral(
        "<span style='color:#f5f7f8;'>P</span>"
        "&nbsp;&nbsp; R &nbsp;&nbsp; N &nbsp;&nbsp; D"
    ));
    gears->setAlignment(Qt::AlignCenter);
    gears->setStyleSheet("font-size: 20px; color: #68737b;");
    infoLayout->addWidget(gears);

    infoLayout->addSpacing(24);

    auto* battery = new QLabel(QStringLiteral("电量  82%"));
    battery->setAlignment(Qt::AlignCenter);
    battery->setStyleSheet("font-size: 18px; color: #e5e8ea;");
    infoLayout->addWidget(battery);

// 显示逻辑只关心车速和挡位，不处理 socket 或 JSON。
auto showState = [speed, gears, &pages, reverseCameraActive]
    (int speedKmh, const QString& gear) {
    speed->setText(QString::number(speedKmh));

    const QString pColor =
        gear == "P" ? "#3e6ae1" : "#68737b";
    const QString dColor =
        gear == "D" ? "#3e6ae1" : "#68737b";
    const QString rColor =
        gear == "R" ? "#3e6ae1" : "#68737b";

    gears->setText(
        QStringLiteral(
            "<span style='color:%1;'>P</span>"
            "&nbsp;&nbsp; <span style='color:%3;'>R</span> &nbsp;&nbsp; N &nbsp;&nbsp;"
            "<span style='color:%2;'>D</span>"
        ).arg(pColor).arg(dColor).arg(rColor)
    );

    // R挡是演示版的自动倒车入口；手动进入摄像头页时不强制接管。
    if (gear == "R") {
        if (pages.currentIndex() != 2) {
            *reverseCameraActive = true;
            pages.setCurrentIndex(2);
        }
    } else if (*reverseCameraActive && pages.currentIndex() == 2) {
        *reverseCameraActive = false;
        pages.setCurrentIndex(0);
    }
};

// 未收到数据时，不把占位值0当作真实车速。
auto showUnavailable = [speed, gears](
    const QString& reason = QStringLiteral("等待车辆数据")) {
    speed->setText(QStringLiteral("--"));
    gears->setText(reason);
};

showUnavailable();

// 接收类只提供数据和状态；界面决定如何显示。
// dashboard 销毁时，客户端及其 socket/定时器也由 Qt 自动释放。
auto* vehicleData = new VehicleDataClient(dashboard);
QObject::connect(
    vehicleData, &VehicleDataClient::stateReceived,
    dashboard, showState
);
QObject::connect(
    vehicleData, &VehicleDataClient::dataUnavailable,
    dashboard, showUnavailable
);
vehicleData->start();  // 先接信号，再启动，避免遗漏首次状态通知。

    infoLayout->addStretch();

    // 右侧先占位，下一步替换为车辆图片。

auto* vehicle = new QLabel;
vehicle->setAlignment(Qt::AlignCenter);

// 相对于可执行文件找图片，不依赖终端当前所在目录。
const QString imagePath =
    QCoreApplication::applicationDirPath() + "/assets/car_white.png";

QPixmap car(imagePath);

if (car.isNull()) {
    vehicle->setText(QStringLiteral("车辆图片加载失败"));
} else {
    vehicle->setPixmap(car.scaled(
        373, 222,
        Qt::KeepAspectRatio,
        Qt::SmoothTransformation
    ));
}

    row->addWidget(info, 35);
    row->addWidget(vehicle, 65);

    pages.addWidget(dashboard);
}

else if (i == 1) {
    pages.addWidget(createMediaPage(audioPlayer));
}
else if (i == 2) {
    cameraPage = createCameraPage();
    pages.addWidget(cameraPage);
}
else {
    pages.addWidget(createAppsPage());
}


        auto* button = new QPushButton;
button->setFixedHeight(56);
button->setCheckable(true);
navigationGroup.addButton(button, i);
button->setChecked(i == 0);
button->setAccessibleName(names[i]);
button->setToolTip(names[i]);
button->setText(QString());
QIcon navigationIcon;
navigationIcon.addPixmap(
    makeNavigationIcon(i, QColor("#9aa4aa")),
    QIcon::Normal, QIcon::Off);
navigationIcon.addPixmap(
    makeNavigationIcon(i, QColor("#3e6ae1")),
    QIcon::Normal, QIcon::On);
button->setIcon(navigationIcon);
button->setIconSize(QSize(28, 28));

// 当前是纯触摸原型，导航按钮不接收键盘焦点。
button->setFocusPolicy(Qt::NoFocus);

        navigation.addWidget(button);

        // 点击这个按钮，就显示对应编号的页面。
        QObject::connect(button, &QPushButton::clicked,
                         &pages, [&pages, i]() {
            pages.setCurrentIndex(i);
        });
    }

    QObject::connect(&pages, &QStackedWidget::currentChanged,
                     &window, [cameraPage, &window, &pages, &navigationGroup,
                               &navigationBar, &mainLayout](int index) {
        setCameraPageActive(cameraPage, index == 2);

        // 自动进入/退出摄像头页时也要同步底部导航的高亮状态。
        if (auto* selected = navigationGroup.button(index)) {
            selected->setChecked(true);
        }

        const bool cameraFullscreen = index == 2;
        navigationBar.setVisible(!cameraFullscreen);

        // 导航高度已经在创建时固定为 56px。长信息页使用滚动视口，
        // 不再通过最小高度把主窗口撑出屏幕；此处只更新显隐后的布局。
        mainLayout.invalidate();
        mainLayout.activate();

        // 可选板端诊断：记录事件循环处理完布局后的实际位置，
        // 便于确认导航栏完整处于屏幕范围内。
        if (qEnvironmentVariableIsSet("VEHICLE_LAYOUT_DEBUG")) {
            QTimer::singleShot(0, &window, [&window, &pages, &navigationBar]() {
                const QScreen* screen = QGuiApplication::primaryScreen();
                qInfo() << "Shell layout: page=" << pages.currentIndex()
                        << "screen=" << (screen ? screen->size() : QSize())
                        << "window=" << window.size()
                        << "minimum=" << window.minimumSizeHint()
                        << "navVisible=" << navigationBar.isVisible()
                        << "nav=" << navigationBar.geometry();
            });
        }
    });

    QObject::connect(
        cameraPageBackButton(cameraPage), &QPushButton::clicked,
        &pages, [&pages]() { pages.setCurrentIndex(0); }
    );

    SwipeEventFilter swipeFilter(&pages);
    app.installEventFilter(&swipeFilter);

window.setStyleSheet(
    "QWidget {"
    "  background: #0f1316;"
    "  color: #f5f7f8;"
    "}"
    "QLabel { font-size: 24px; }"

    "QPushButton {"
    "  background: #1a2126;"
    "  border: none;"
    "  border-top: 2px solid transparent;"
    "  border-radius: 0px;"
    "  font-size: 18px;"
    "  font-weight: normal;"
    "  color: #9aa4aa;"
    "}"
    "QPushButton:checked {"
    "  color: #3e6ae1;"
    "  border-top: 2px solid #3e6ae1;"
    "}"
    "QPushButton:pressed {"
    "  background: #252e35;"
    "}"
);

    window.showFullScreen();
    return app.exec();
}
