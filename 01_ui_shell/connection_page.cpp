#include "connection_page.h"

#include <QDir>
#include <QFile>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QAbstractSocket>
#include <QHostAddress>
#include <QLabel>
#include <QNetworkInterface>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPointer>
#include <QUrl>
#include <QPushButton>
#include <QScrollArea>
#include <QScroller>
#include <QStringList>
#include <QTimer>
#include <QVariant>
#include <QVBoxLayout>

namespace {

struct NetworkSnapshot {
    QString wired;
    QString wifi;
    QString bluetooth;
    QString addresses;
    QString route;
};

bool hasDefaultRoute()
{
    QFile file(QStringLiteral("/proc/net/route"));
    if (!file.open(QIODevice::ReadOnly)) return false;
    const QStringList lines = QString::fromUtf8(file.readAll()).split('\n');
    for (int i = 1; i < lines.size(); ++i) {
        const QStringList fields = lines[i].split('\t', QString::SkipEmptyParts);
        if (fields.size() >= 3 && fields[1] == QStringLiteral("00000000")
            && fields[2] != QStringLiteral("00000000")) {
            return true;
        }
    }
    return false;
}

bool hasInterfacePrefix(const QString& prefix, QStringList* names = nullptr)
{
    bool found = false;
    for (const QNetworkInterface& interface : QNetworkInterface::allInterfaces()) {
        const QString name = interface.name();
        if (!name.startsWith(prefix) || !(interface.flags() & QNetworkInterface::IsUp)
            || (interface.flags() & QNetworkInterface::IsLoopBack)) {
            continue;
        }
        found = true;
        if (names) names->append(name);
    }
    return found;
}

NetworkSnapshot snapshot()
{
    NetworkSnapshot result;
    QStringList wiredNames;
    QStringList wifiNames;
    const bool wired = hasInterfacePrefix(QStringLiteral("eth"), &wiredNames)
        || hasInterfacePrefix(QStringLiteral("en"), &wiredNames);
    const bool wifi = hasInterfacePrefix(QStringLiteral("wlan"), &wifiNames)
        || hasInterfacePrefix(QStringLiteral("wl"), &wifiNames);
    result.wired = wired
        ? QStringLiteral("已发现：%1").arg(wiredNames.join(QStringLiteral(", ")))
        : QStringLiteral("未发现有线网卡");
    result.wifi = wifi
        ? QStringLiteral("已发现：%1").arg(wifiNames.join(QStringLiteral(", ")))
        : QStringLiteral("未发现 Wi‑Fi 网卡");

    const QDir bluetoothDir(QStringLiteral("/sys/class/bluetooth"));
    const QStringList bluetoothNames = bluetoothDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    result.bluetooth = bluetoothNames.isEmpty()
        ? QStringLiteral("未发现蓝牙适配器")
        : QStringLiteral("已发现：%1").arg(bluetoothNames.join(QStringLiteral(", ")));

    QStringList addresses;
    for (const QNetworkInterface& interface : QNetworkInterface::allInterfaces()) {
        if (!(interface.flags() & QNetworkInterface::IsUp)
            || (interface.flags() & QNetworkInterface::IsLoopBack)) continue;
        for (const QNetworkAddressEntry& entry : interface.addressEntries()) {
            if (entry.ip().protocol() == QAbstractSocket::IPv4Protocol)
                addresses.append(interface.name() + QStringLiteral(": ") + entry.ip().toString());
        }
    }
    result.addresses = addresses.isEmpty()
        ? QStringLiteral("无 IPv4 地址") : addresses.join(QStringLiteral("\n"));
    result.route = hasDefaultRoute()
        ? QStringLiteral("已发现默认路由") : QStringLiteral("未发现默认路由");
    return result;
}

} // namespace

QWidget* createConnectionPage(QWidget* parent)
{
    auto* page = new QWidget(parent);
    page->setObjectName(QStringLiteral("connectionPage"));
    page->setProperty("swipeBlock", QVariant(true));
    page->setStyleSheet(
        "#connectionPage { background: #0f1316; }"
        "#connectionPage #connectionTopBar { background: #171d22; }"
        "#connectionPage #connectionTitle { color: #f5f7f8; font-size: 20px; }"
        "#connectionPage QLabel { color: #aeb7c0; font-size: 16px; }"
        "#connectionPage QLabel#connectionValue { color: #f0f4f6; font-size: 15px; }"
        "#connectionPage QPushButton { min-height: 40px; background: #252e35; border: none;"
        " border-radius: 6px; padding: 0 12px; font-size: 15px; color: #eff1f3; }"
        "#connectionPage QPushButton:pressed { background: #3e6ae1; }");

    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    auto* topBar = new QWidget;
    topBar->setObjectName(QStringLiteral("connectionTopBar"));
    topBar->setFixedHeight(52);
    auto* topLayout = new QHBoxLayout(topBar);
    topLayout->setContentsMargins(8, 4, 8, 4);
    auto* back = new QPushButton(QStringLiteral("返回"));
    back->setObjectName(QStringLiteral("connectionBack"));
    back->setFocusPolicy(Qt::NoFocus);
    auto* title = new QLabel(QStringLiteral("连接状态"));
    title->setObjectName(QStringLiteral("connectionTitle"));
    topLayout->addWidget(back);
    topLayout->addWidget(title);
    topLayout->addStretch();

    auto* content = new QWidget;
    auto* form = new QFormLayout(content);
    form->setContentsMargins(28, 22, 28, 22);
    form->setHorizontalSpacing(24);
    form->setVerticalSpacing(16);
    form->setLabelAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    const auto addRow = [form](const QString& name) {
        auto* label = new QLabel;
        label->setObjectName(QStringLiteral("connectionValue"));
        label->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        label->setWordWrap(true);
        form->addRow(new QLabel(name), label);
        return label;
    };
    auto* wired = addRow(QStringLiteral("有线网络"));
    auto* wifi = addRow(QStringLiteral("Wi‑Fi"));
    auto* bluetooth = addRow(QStringLiteral("蓝牙"));
    auto* addresses = addRow(QStringLiteral("IPv4 地址"));
    auto* route = addRow(QStringLiteral("默认路由"));
    auto* internet = addRow(QStringLiteral("互联网访问"));
    auto* network = new QNetworkAccessManager(page);
    auto* activeReply = new QPointer<QNetworkReply>(nullptr);
    const auto checkInternet = [internet, network, activeReply]() {
        if (*activeReply) return;
        internet->setText(QStringLiteral("检测中…"));
        QNetworkRequest request(QUrl(QStringLiteral("https://www.baidu.com/")));
        request.setRawHeader("User-Agent", "SUSTfresher-VehicleTerminal/0.1");
        request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                             QNetworkRequest::NoLessSafeRedirectPolicy);
        QNetworkReply* reply = network->head(request);
        *activeReply = reply;
        auto* deadline = new QTimer(reply);
        deadline->setSingleShot(true);
        QObject::connect(deadline, &QTimer::timeout, reply, [reply]() {
            reply->setProperty("timedOut", true);
            reply->abort();
        });
        deadline->start(3000);
        QObject::connect(reply, &QNetworkReply::finished, internet,
                         [internet, activeReply, reply, deadline]() {
            deadline->stop();
            const int http = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            const bool reachable = reply->error() == QNetworkReply::NoError
                && http >= 200 && http < 400;
            internet->setText(reachable ? QStringLiteral("可访问（HTTP %1）").arg(http)
                                        : QStringLiteral("不可访问"));
            *activeReply = nullptr;
            reply->deleteLater();
        });
    };
    const auto updateValues = [wired, wifi, bluetooth, addresses, route]() {
        const NetworkSnapshot state = snapshot();
        wired->setText(state.wired);
        wifi->setText(state.wifi);
        bluetooth->setText(state.bluetooth);
        addresses->setText(state.addresses);
        route->setText(state.route);
    };
    updateValues();
    checkInternet();
    auto* timer = new QTimer(page);
    timer->setInterval(2000);
    QObject::connect(timer, &QTimer::timeout, page, updateValues);
    timer->start();
    auto* internetTimer = new QTimer(page);
    internetTimer->setInterval(5000);
    QObject::connect(internetTimer, &QTimer::timeout, page, checkInternet);
    internetTimer->start();

    // 网卡增加或 IPv4 地址换行时，只增加内容的滚动范围，
    // 不让连接页（包括隐藏状态）抬高主窗口的最小高度。
    auto* scroll = new QScrollArea;
    scroll->setObjectName(QStringLiteral("connectionScroll"));
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

QPushButton* connectionPageBackButton(QWidget* page)
{
    return page ? page->findChild<QPushButton*>(QStringLiteral("connectionBack")) : nullptr;
}
