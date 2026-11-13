// 天地图最小验证：异步下载底图与中文注记瓦片，QPainter 分层拼接。
// 只验证显示、拖动和缩放，不提供定位、搜索或导航，不使用 WebEngine/OpenGL。
#include <QApplication>
#include <QCache>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFont>
#include <QHBoxLayout>
#include <QImage>
#include <QLabel>
#include <QLineF>
#include <QMouseEvent>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPainter>
#include <QPushButton>
#include <QResizeEvent>
#include <QSet>
#include <QSslSocket>
#include <QTimer>
#include <QTouchEvent>
#include <QUrl>
#include <QUrlQuery>
#include <QVBoxLayout>
#include <QVector>
#include <cmath>

class MapCanvas : public QWidget
{
public:
    explicit MapCanvas(QLabel* status, QWidget* parent = nullptr)
        : QWidget(parent), status_(status), images_(64)
    {
        setMinimumSize(320, 200);
        setAttribute(Qt::WA_AcceptTouchEvents, true);
        network_ = new QNetworkAccessManager(this);
        // 请求 URL 含 Key；这一版只用内存缓存，不把带 Key 的 URL 写入磁盘缓存。
        loadKey();
        // 拖动结束或连续缩放稍作停顿后，只请求最终可见的瓦片。
        refresh_.setSingleShot(true);
        refresh_.setInterval(150);
        connect(&refresh_, &QTimer::timeout, this, [this]() { requestVisible(); });
        home();
    }

    void home()
    {
        zoom_ = 14;
        // 陕西科技大学西安校区示例位置，WGS84；不是 GPS 实时定位。
        constexpr double lon = 108.9717629;
        constexpr double lat = 34.3823758;
        constexpr double pi = 3.14159265358979323846;
        const double radians = lat * pi / 180.0;
        center_ = QPointF((lon + 180.0) / 360.0 * worldSize(),
            (1.0 - std::asinh(std::tan(radians)) / pi) / 2.0 * worldSize());
        scheduleRefresh();
    }

    void changeZoom(int step)
    {
        const int next = qBound(3, zoom_ + step, 17);
        if (next == zoom_) return;
        center_ *= std::pow(2.0, next - zoom_);
        zoom_ = next;
        scheduleRefresh();
    }

    void retry()
    {
        loadKey(); // 配置补好后可直接重试，无需重新启动。
        failed_.clear();
        scheduleRefresh();
    }

protected:
    bool event(QEvent* event) override
    {
        switch (event->type()) {
        case QEvent::TouchBegin:
        case QEvent::TouchUpdate:
        case QEvent::TouchEnd:
        case QEvent::TouchCancel:
            return handleTouchEvent(static_cast<QTouchEvent*>(event));
        default:
            return QWidget::event(event);
        }
    }

    void resizeEvent(QResizeEvent* event) override
    {
        QWidget::resizeEvent(event);
        scheduleRefresh();
    }

    void paintEvent(QPaintEvent*) override
    {
        QPainter painter(this);
        painter.fillRect(rect(), QColor("#e4e8eb"));
        for (const Tile& tile : visibleTiles()) {
            const QRect target(tile.left, tile.top, 256, 256);
            const QString baseKey = layerKey(tile, QStringLiteral("vec"));
            if (const QImage* image = images_.object(baseKey)) {
                painter.drawImage(target, *image);
                // 注记包含透明背景，必须画在底图之后。
                if (const QImage* labels = images_.object(layerKey(tile, QStringLiteral("cva"))))
                    painter.drawImage(target, *labels);
            } else {
                painter.fillRect(target, QColor("#dce2e6"));
                painter.setPen(QColor("#c2cbd2"));
                painter.drawRect(target.adjusted(0, 0, -1, -1));
                painter.setPen(QColor("#53616b"));
                painter.drawText(target, Qt::AlignCenter,
                    apiKey_.isEmpty() ? QStringLiteral("请配置地图 Key")
                        : failed_.contains(baseKey) ? QStringLiteral("加载失败")
                                                   : QStringLiteral("加载中"));
            }
        }
        // 只是视图中心十字，不画定位蓝点，避免误解为真实位置。
        painter.setPen(QPen(QColor("#3e6ae1"), 2));
        const QPoint c = rect().center();
        painter.drawLine(c + QPoint(-8, 0), c + QPoint(8, 0));
        painter.drawLine(c + QPoint(0, -8), c + QPoint(0, 8));
    }

    void mousePressEvent(QMouseEvent* event) override
    {
        if (event->button() != Qt::LeftButton) return;
        dragging_ = true;
        dragStart_ = event->pos();
        dragCenter_ = center_;
        refresh_.stop();
        event->accept();
    }

    void mouseMoveEvent(QMouseEvent* event) override
    {
        if (!dragging_) return;
        center_ = dragCenter_ - QPointF(event->pos() - dragStart_);
        clampCenter();
        update();
        event->accept();
    }

    void mouseReleaseEvent(QMouseEvent* event) override
    {
        if (event->button() != Qt::LeftButton || !dragging_) return;
        center_ = dragCenter_ - QPointF(event->pos() - dragStart_);
        clampCenter();
        dragging_ = false;
        scheduleRefresh();
        event->accept();
    }

private:
    struct Tile { int x, y, zoom, left, top; QString key; };

    static QList<QTouchEvent::TouchPoint> activeTouchPoints(const QTouchEvent* event)
    {
        QList<QTouchEvent::TouchPoint> result;
        for (const QTouchEvent::TouchPoint& point : event->touchPoints()) {
            if (point.state() != Qt::TouchPointReleased) {
                result.append(point);
            }
        }
        return result;
    }

    bool handleTouchEvent(QTouchEvent* event)
    {
        if (event->type() == QEvent::TouchCancel) {
            pinchActive_ = false;
            dragging_ = false;
            scheduleRefresh();
            event->accept();
            return true;
        }

        const QList<QTouchEvent::TouchPoint> points = activeTouchPoints(event);
        if (event->type() == QEvent::TouchBegin) {
            dragging_ = false;
            refresh_.stop();
            if (points.size() >= 2) {
                beginPinch(points[0].pos(), points[1].pos());
            } else if (points.size() == 1) {
                touchLast_ = points.first().pos();
            }
            event->accept();
            return true;
        }

        if (points.size() >= 2) {
            if (!pinchActive_) beginPinch(points[0].pos(), points[1].pos());
            updatePinch(points[0].pos(), points[1].pos());
        } else if (points.size() == 1) {
            if (pinchActive_) {
                // 收起一根手指后，避免把剩余手指的跳变当成大幅拖动。
                pinchActive_ = false;
                touchLast_ = points.first().pos();
            } else {
                const QPointF current = points.first().pos();
                center_ -= current - touchLast_;
                touchLast_ = current;
                clampCenter();
                update();
            }
        }

        if (event->type() == QEvent::TouchEnd || points.isEmpty()) {
            pinchActive_ = false;
            scheduleRefresh();
        }
        event->accept();
        return true;
    }

    void beginPinch(const QPointF& first, const QPointF& second)
    {
        const QPointF midpoint = (first + second) / 2.0;
        pinchStartDistance_ = QLineF(first, second).length();
        if (pinchStartDistance_ < 1.0) return;
        pinchStartZoom_ = zoom_;
        pinchAnchorWorld_ = center_ - QPointF(width() / 2.0, height() / 2.0)
            + midpoint;
        pinchActive_ = true;
    }

    void updatePinch(const QPointF& first, const QPointF& second)
    {
        if (!pinchActive_ || pinchStartDistance_ < 1.0) return;
        const double distance = QLineF(first, second).length();
        if (distance < 1.0) return;
        const int step = qRound(std::log(distance / pinchStartDistance_) / std::log(2.0));
        const int nextZoom = qBound(3, pinchStartZoom_ + step, 17);
        if (nextZoom == zoom_) return;

        const QPointF midpoint = (first + second) / 2.0;
        const double scale = std::pow(2.0, nextZoom - pinchStartZoom_);
        center_ = pinchAnchorWorld_ * scale - midpoint
            + QPointF(width() / 2.0, height() / 2.0);
        zoom_ = nextZoom;
        clampCenter();
        update();
    }

    void loadKey()
    {
        apiKey_.clear();
        const QString path = QDir::homePath()
            + QStringLiteral("/.config/vehicle-map-probe/tianditu.key");
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly) || file.size() > 256) {
            qWarning() << "Map Key file missing, unreadable or too large:" << path;
            return;
        }
        const QByteArray key = file.readAll().trimmed();
        // 只接受一行可见 ASCII，避免粘贴空格、引号或多行说明。
        for (char c : key) {
            if (c <= ' ' || c >= 127 || c == '\"' || c == '\'') {
                qWarning() << "Map Key file must contain only the Key on one line";
                return;
            }
        }
        apiKey_ = QString::fromLatin1(key);
        qInfo() << "Map Key configured=" << !apiKey_.isEmpty();
    }

    static QString layerKey(const Tile& tile, const QString& layer)
    {
        return layer + QLatin1Char('/') + tile.key;
    }

    double worldSize() const { return 256.0 * (1 << zoom_); }

    void clampCenter()
    {
        const double size = worldSize();
        center_.setX(std::fmod(std::fmod(center_.x(), size) + size, size));
        center_.setY(qBound(height() / 2.0, center_.y(), size - height() / 2.0));
    }

    QVector<Tile> visibleTiles() const
    {
        QVector<Tile> result;
        const int count = 1 << zoom_;
        const QPointF origin = center_ - QPointF(width() / 2.0, height() / 2.0);
        const int firstX = static_cast<int>(std::floor(origin.x() / 256.0));
        const int firstY = static_cast<int>(std::floor(origin.y() / 256.0));
        const int lastX = static_cast<int>(std::floor((origin.x() + width() - 1) / 256.0));
        const int lastY = static_cast<int>(std::floor((origin.y() + height() - 1) / 256.0));
        for (int y = firstY; y <= lastY; ++y) {
            if (y < 0 || y >= count) continue;
            for (int x = firstX; x <= lastX; ++x) {
                const int wrappedX = (x % count + count) % count;
                result.append(Tile{wrappedX, y, zoom_,
                    qRound(x * 256.0 - origin.x()), qRound(y * 256.0 - origin.y()),
                    QStringLiteral("%1/%2/%3").arg(zoom_).arg(wrappedX).arg(y)});
            }
        }
        return result;
    }

    void scheduleRefresh()
    {
        clampCenter();
        update();
        refresh_.start();
    }

    void requestVisible()
    {
        if (dragging_) return;
        if (apiKey_.isEmpty() || !QSslSocket::supportsSsl()) {
            status_->setText(apiKey_.isEmpty() ? QStringLiteral("请配置 tianditu.key，然后点击重试")
                                         : QStringLiteral("Qt HTTPS 不可用，请查看终端 SSL 日志"));
            return;
        }
        const QVector<Tile> tiles = visibleTiles();
        int bases = 0, labels = 0, errors = 0;
        // 先安排底图，再安排中文注记；两层共用最多 4 个并发请求。
        for (const QString& layer : {QStringLiteral("vec"), QStringLiteral("cva")}) {
            for (const Tile& tile : tiles) {
                const QString key = layerKey(tile, layer);
                if (images_.contains(key)) {
                    if (layer == QStringLiteral("vec")) ++bases;
                    else ++labels;
                    continue;
                }
                if (failed_.contains(key)) { ++errors; continue; }
                if (pending_.contains(key) || pending_.size() >= 4) continue;
                fetch(tile, layer);
            }
        }
        const QString progress = QStringLiteral("底图 %1/%3 · 地名 %2/%3 · 缩放 %4")
            .arg(bases).arg(labels).arg(tiles.size()).arg(zoom_);
        status_->setText(progress + (errors > 0
            ? QStringLiteral(" · %1 块失败，请重试").arg(errors)
            : bases == tiles.size() && labels == tiles.size()
                ? QStringLiteral(" · 拖动可浏览") : QStringLiteral(" · 加载中…")));
        update();
    }

    void fetch(const Tile& tile, const QString& layer)
    {
        const QString key = layerKey(tile, layer);
        QUrl url(QStringLiteral("https://t0.tianditu.gov.cn/%1_w/wmts").arg(layer));
        QUrlQuery query;
        query.addQueryItem(QStringLiteral("SERVICE"), QStringLiteral("WMTS"));
        query.addQueryItem(QStringLiteral("REQUEST"), QStringLiteral("GetTile"));
        query.addQueryItem(QStringLiteral("VERSION"), QStringLiteral("1.0.0"));
        query.addQueryItem(QStringLiteral("LAYER"), layer);
        query.addQueryItem(QStringLiteral("STYLE"), QStringLiteral("default"));
        query.addQueryItem(QStringLiteral("TILEMATRIXSET"), QStringLiteral("w"));
        query.addQueryItem(QStringLiteral("FORMAT"), QStringLiteral("tiles"));
        query.addQueryItem(QStringLiteral("TILEMATRIX"), QString::number(tile.zoom));
        query.addQueryItem(QStringLiteral("TILEROW"), QString::number(tile.y));
        query.addQueryItem(QStringLiteral("TILECOL"), QString::number(tile.x));
        query.addQueryItem(QStringLiteral("tk"), apiKey_);
        url.setQuery(query);
        QNetworkRequest request(url);
        request.setRawHeader("User-Agent", "SUSTfresher-VehicleTerminal-MapProbe/0.2");
        // 不自动把携带 Key 的请求跟随重定向到其他主机。
        request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                             QNetworkRequest::ManualRedirectPolicy);
        pending_.insert(key);
        QNetworkReply* reply = network_->get(request);
        auto* deadline = new QTimer(reply);
        deadline->setSingleShot(true);
        connect(deadline, &QTimer::timeout, reply, [reply]() {
            reply->setProperty("timedOut", true);
            reply->abort();
        });
        deadline->start(12000);
        connect(reply, &QNetworkReply::readyRead, reply, [reply]() {
            // 标准瓦片很小；异常响应也不能无限占用内存。
            if (reply->bytesAvailable() > 1024 * 1024) {
                reply->setProperty("tooLarge", true);
                reply->abort();
            }
        });
        connect(reply, &QNetworkReply::finished, this, [this, reply, deadline, key]() {
            deadline->stop();
            pending_.remove(key);
            QImage image;
            const int http = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            const bool valid = reply->error() == QNetworkReply::NoError
                && http == 200 && image.loadFromData(reply->readAll())
                && image.size() == QSize(256, 256);
            if (valid) {
                images_.insert(key, new QImage(image));
                qInfo() << "Tile OK:" << key;
            } else {
                // 失败不自动反复请求。用户点击重试后才重新尝试。
                if (failed_.size() >= 256) failed_.clear();
                failed_.insert(key);
                // errorString、完整 URL 和响应正文都可能包含 Key，不能直接打印。
                qWarning() << "Tile FAILED:" << key
                           << "HTTP=" << http
                           << "timeout=" << reply->property("timedOut").toBool()
                           << "tooLarge=" << reply->property("tooLarge").toBool()
                           << "networkError=" << static_cast<int>(reply->error())
                           << "imageValid=" << valid;
            }
            reply->deleteLater();
            update();
            // 用户仍在缩放时，不为中间画面追加请求。
            if (!dragging_ && !refresh_.isActive()) requestVisible();
        });
    }

    QLabel* status_;
    QNetworkAccessManager* network_;
    QString apiKey_;
    QCache<QString, QImage> images_; // 最多保留 64 张已解码瓦片。
    QSet<QString> pending_, failed_;
    QTimer refresh_;
    QPointF center_, dragCenter_;
    QPoint dragStart_;
    QPointF touchLast_;
    QPointF pinchAnchorWorld_;
    double pinchStartDistance_ = 0.0;
    int pinchStartZoom_ = 14;
    int zoom_ = 14;
    bool dragging_ = false;
    bool pinchActive_ = false;
};

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("vehicle-map-probe"));
    app.setFont(QFont(QStringLiteral("Noto Sans CJK SC"), 11));
    qInfo() << "Map probe v2 | provider=Tianditu | Qt" << qVersion() << "platform=" << app.platformName();
    qInfo() << "SSL available=" << QSslSocket::supportsSsl()
            << "build=" << QSslSocket::sslLibraryBuildVersionString()
            << "runtime=" << QSslSocket::sslLibraryVersionString();

    QWidget window;
    window.setWindowTitle(QStringLiteral("地图最小验证"));
    window.resize(720, 480);
    window.setStyleSheet(
        "QWidget { background: #101316; color: #eff1f3; }"
        "QLabel { font-size: 14px; }"
        "QPushButton { min-height: 44px; background: #252e35; border: none;"
        " border-radius: 6px; padding: 0 12px; font-size: 17px; }"
        "QPushButton:pressed { background: #3e6ae1; }");
    auto* layout = new QVBoxLayout(&window);
    layout->setContentsMargins(8, 6, 8, 6);
    layout->setSpacing(6);
    auto* toolbar = new QHBoxLayout;
    auto* title = new QLabel(QStringLiteral("在线地图 · 无定位"));
    toolbar->addWidget(title);
    toolbar->addStretch();
    auto* status = new QLabel(QStringLiteral("准备加载…"));
    auto* map = new MapCanvas(status);
    const auto button = [toolbar](const QString& text) {
        auto* result = new QPushButton(text);
        result->setFocusPolicy(Qt::NoFocus);
        toolbar->addWidget(result);
        return result;
    };
    QObject::connect(button(QStringLiteral("−")), &QPushButton::clicked, map, [map]() { map->changeZoom(-1); });
    QObject::connect(button(QStringLiteral("+")), &QPushButton::clicked, map, [map]() { map->changeZoom(1); });
    QObject::connect(button(QStringLiteral("陕西科技大学")), &QPushButton::clicked, map, [map]() { map->home(); });
    QObject::connect(button(QStringLiteral("重试")), &QPushButton::clicked, map, [map]() { map->retry(); });
    QObject::connect(button(QStringLiteral("退出")), &QPushButton::clicked, &window, &QWidget::close);
    layout->addLayout(toolbar);
    layout->addWidget(map, 1);
    layout->addWidget(status);
    auto* attribution = new QLabel(QStringLiteral(
        "地图来源：天地图 · 陕西科技大学西安校区示例 · 非实际定位"));
    attribution->setAlignment(Qt::AlignRight);
    attribution->setStyleSheet("font-size: 12px; color: #aeb7c0;");
    layout->addWidget(attribution);
    window.showFullScreen();
    return app.exec();
}
