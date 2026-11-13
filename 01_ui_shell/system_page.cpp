#include "system_page.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QScroller>
#include <QStorageInfo>
#include <QStringList>
#include <QVariant>
#include <QTimer>
#include <QVBoxLayout>

namespace {

QString readFile(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return {};
    return QString::fromUtf8(file.readAll()).remove(QChar('\0')).trimmed();
}

QString deviceModel()
{
    const QString model = readFile(QStringLiteral("/proc/device-tree/model"));
    return model.isEmpty() ? QStringLiteral("未知设备") : model;
}

QString memoryUsage()
{
    const QString text = readFile(QStringLiteral("/proc/meminfo"));
    quint64 total = 0;
    quint64 available = 0;
    for (const QString& line : text.split('\n')) {
        const QStringList fields = line.split(' ', QString::SkipEmptyParts);
        if (fields.size() < 2) continue;
        const quint64 value = fields[1].toULongLong();
        if (fields[0] == QStringLiteral("MemTotal:")) total = value;
        if (fields[0] == QStringLiteral("MemAvailable:")) available = value;
    }
    if (total == 0) return QStringLiteral("不可用");
    const quint64 used = total > available ? total - available : 0;
    return QStringLiteral("%1 / %2 MiB")
        .arg(used / 1024).arg(total / 1024);
}

QString storageUsage()
{
    const QStorageInfo storage = QStorageInfo::root();
    if (!storage.isValid() || storage.bytesTotal() <= 0)
        return QStringLiteral("不可用");
    const qint64 total = storage.bytesTotal();
    const qint64 available = storage.bytesAvailable();
    const qint64 used = total > available ? total - available : 0;
    return QStringLiteral("%1 / %2 GiB")
        .arg(QString::number(used / (1024.0 * 1024.0 * 1024.0), 'f', 1))
        .arg(QString::number(total / (1024.0 * 1024.0 * 1024.0), 'f', 1));
}

QString cpuTemperature()
{
    const QDir thermal(QStringLiteral("/sys/class/thermal"));
    const QStringList zones = thermal.entryList(
        QStringList() << QStringLiteral("thermal_zone*"), QDir::Dirs);
    int maximumMilliC = -1;
    for (const QString& zone : zones) {
        bool ok = false;
        const int milliC = readFile(thermal.filePath(zone) + "/temp").toInt(&ok);
        if (ok && milliC > maximumMilliC) maximumMilliC = milliC;
    }
    if (maximumMilliC < 0) return QStringLiteral("不可用");
    return QStringLiteral("%1 °C").arg(maximumMilliC / 1000.0, 0, 'f', 1);
}

QString uptimeText()
{
    bool ok = false;
    const double seconds = readFile(QStringLiteral("/proc/uptime"))
        .section(' ', 0, 0).toDouble(&ok);
    if (!ok) return QStringLiteral("不可用");
    const qint64 minutes = static_cast<qint64>(seconds / 60.0);
    return QStringLiteral("%1 天 %2 小时 %3 分钟")
        .arg(minutes / (24 * 60))
        .arg((minutes / 60) % 24)
        .arg(minutes % 60);
}

QString deviceHealth()
{
    QStringList results;
    const QDir dev(QStringLiteral("/dev"));
    const bool camera = !dev.entryList(QStringList() << QStringLiteral("video*"),
                                       QDir::System | QDir::Readable).isEmpty();
    results.append(camera ? QStringLiteral("摄像头正常") : QStringLiteral("摄像头未发现"));

    QFile soundCards(QStringLiteral("/proc/asound/cards"));
    const bool audio = soundCards.open(QIODevice::ReadOnly)
        && !soundCards.readAll().trimmed().isEmpty();
    results.append(audio ? QStringLiteral("音频正常") : QStringLiteral("音频未发现"));

    QFile routeFile(QStringLiteral("/proc/net/route"));
    bool route = false;
    if (routeFile.open(QIODevice::ReadOnly)) {
        const QStringList lines = QString::fromUtf8(routeFile.readAll()).split('\n');
        for (int i = 1; i < lines.size(); ++i) {
            const QStringList fields = lines[i].split('\t', QString::SkipEmptyParts);
            if (fields.size() >= 3 && fields[1] == QStringLiteral("00000000")
                && fields[2] != QStringLiteral("00000000")) {
                route = true;
                break;
            }
        }
    }
    results.append(route ? QStringLiteral("网络接口正常") : QStringLiteral("无默认路由"));

    const QStorageInfo storage = QStorageInfo::root();
    const bool enoughStorage = storage.isValid() && storage.bytesAvailable() > 512LL * 1024 * 1024;
    results.append(enoughStorage ? QStringLiteral("存储空间正常") : QStringLiteral("存储空间偏低"));

    bool temperatureOk = false;
    const QString temperature = cpuTemperature();
    if (temperature.endsWith(QStringLiteral(" °C"))) {
        const double value = temperature.left(temperature.size() - 3).toDouble(&temperatureOk);
        temperatureOk = temperatureOk && value < 80.0;
    }
    results.append(temperatureOk ? QStringLiteral("温度正常") : QStringLiteral("温度需关注"));
    return results.join(QStringLiteral(" · "));
}

} // namespace

QWidget* createSystemPage(QWidget* parent)
{
    auto* page = new QWidget(parent);
    page->setObjectName(QStringLiteral("systemPage"));
    page->setProperty("swipeBlock", QVariant(true));
    page->setStyleSheet(
        "#systemPage { background: #0f1316; }"
        "#systemPage #systemTopBar { background: #171d22; }"
        "#systemPage #systemTitle { color: #f5f7f8; font-size: 20px; }"
        "#systemPage QLabel { color: #aeb7c0; font-size: 16px; }"
        "#systemPage QLabel#systemValue { color: #f0f4f6; font-size: 16px; }"
        "#systemPage QPushButton { min-height: 40px; background: #252e35; border: none;"
        " border-radius: 6px; padding: 0 12px; font-size: 15px; color: #eff1f3; }"
        "#systemPage QPushButton:pressed { background: #3e6ae1; }");

    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    auto* topBar = new QWidget;
    topBar->setObjectName(QStringLiteral("systemTopBar"));
    topBar->setFixedHeight(52);
    auto* topLayout = new QHBoxLayout(topBar);
    topLayout->setContentsMargins(8, 4, 8, 4);
    auto* back = new QPushButton(QStringLiteral("返回"));
    back->setObjectName(QStringLiteral("systemBack"));
    back->setFocusPolicy(Qt::NoFocus);
    auto* title = new QLabel(QStringLiteral("系统信息"));
    title->setObjectName(QStringLiteral("systemTitle"));
    topLayout->addWidget(back);
    topLayout->addWidget(title);
    topLayout->addStretch();

    auto* content = new QWidget;
    auto* form = new QFormLayout(content);
    form->setContentsMargins(28, 22, 28, 22);
    form->setHorizontalSpacing(24);
    form->setVerticalSpacing(16);
    form->setLabelAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    // 让右侧数值占满可用列宽，换行高度与实际显示宽度保持一致。
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);

    const auto addRow = [form](const QString& name) {
        auto* label = new QLabel;
        label->setObjectName(QStringLiteral("systemValue"));
        label->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        form->addRow(new QLabel(name), label);
        return label;
    };
    auto* model = addRow(QStringLiteral("设备型号"));
    auto* temperature = addRow(QStringLiteral("最高温度"));
    auto* memory = addRow(QStringLiteral("内存使用"));
    auto* storage = addRow(QStringLiteral("根目录存储"));
    auto* uptime = addRow(QStringLiteral("运行时间"));
    auto* qtVersion = addRow(QStringLiteral("Qt 版本"));
    auto* health = addRow(QStringLiteral("设备自检"));
    health->setWordWrap(true);
    const auto updateValues = [model, temperature, memory, storage, uptime, qtVersion, health]() {
        model->setText(deviceModel());
        temperature->setText(cpuTemperature());
        memory->setText(memoryUsage());
        storage->setText(storageUsage());
        uptime->setText(uptimeText());
        qtVersion->setText(QString::fromLatin1(qVersion()));
        health->setText(deviceHealth());
    };
    updateValues();
    auto* timer = new QTimer(page);
    timer->setInterval(2000);
    QObject::connect(timer, &QTimer::timeout, page, updateValues);
    timer->start();
    form->addRow(new QLabel(QStringLiteral("数据来源")),
                 new QLabel(QStringLiteral("Linux /proc、/sys 和 Qt")));

    // 720×480 下本页只有约 372px，其中顶栏还要占 52px。
    // 表单放进滚动视口，避免隐藏的长页面通过 QStackedLayout
    // 把整个主窗口撑高，导致退出摄像头后底部导航落到屏幕外。
    auto* scroll = new QScrollArea;
    scroll->setObjectName(QStringLiteral("systemScroll"));
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidgetResizable(true);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    form->setSizeConstraint(QLayout::SetMinAndMaxSize);
    scroll->setWidget(content);
    QScroller::grabGesture(scroll->viewport(), QScroller::TouchGesture);

    layout->addWidget(topBar);
    layout->addWidget(scroll, 1);
    return page;
}

QPushButton* systemPageBackButton(QWidget* page)
{
    return page ? page->findChild<QPushButton*>(QStringLiteral("systemBack")) : nullptr;
}
