#include "map_page.h"
#include "map_search_client.h"

#include <QCache>
#include <QDir>
#include <QFile>
#include <QFont>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QHideEvent>
#include <QImage>
#include <QLabel>
#include <QLineF>
#include <QLineEdit>
#include <QListWidget>
#include <QMouseEvent>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPainter>
#include <QPushButton>
#include <QResizeEvent>
#include <QScroller>
#include <QSet>
#include <QSslSocket>
#include <QTimer>
#include <QTouchEvent>
#include <QUrl>
#include <QUrlQuery>
#include <QVBoxLayout>
#include <QVector>
#include <cmath>
#include <memory>

namespace {

// 收起搜索面板或离开地图时取消请求，避免迟到结果改写当前界面。
class SearchPanel final : public QWidget
{
public:
    MapSearchClient* client = nullptr;
    QLabel* hint = nullptr;
protected:
    void hideEvent(QHideEvent* event) override
    {
        if (client) client->cancel();
        if (hint && hint->text() == QStringLiteral("正在搜索西安市内地点…"))
            hint->setText(QStringLiteral("搜索已取消，可重新搜索"));
        QWidget::hideEvent(event);
    }
};

class MapCanvas final : public QWidget
{
public:
    explicit MapCanvas(QLabel* status, QWidget* parent = nullptr)
        : QWidget(parent), status_(status), images_(64)
    {
        setMinimumSize(320, 200);
        setAttribute(Qt::WA_AcceptTouchEvents, true);
        network_ = new QNetworkAccessManager(this);
        loadKey();

        refresh_.setSingleShot(true);
        refresh_.setInterval(150);
        connect(&refresh_, &QTimer::timeout, this, [this]() { requestVisible(); });
        home();
    }

    void home()
    {
        hasPlace_ = false;
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

    void showPlace(const MapPlace& place)
    {
        place_ = place;
        hasPlace_ = true;
        zoom_ = 16;
        center_ = projected(place.lonLat);
        dragging_ = false;
        pinchActive_ = false;
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
        loadKey();
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
        painter.setPen(QPen(QColor("#3e6ae1"), 2));
        const QPoint c = rect().center();
        painter.drawLine(c + QPoint(-8, 0), c + QPoint(8, 0));
        painter.drawLine(c + QPoint(0, -8), c + QPoint(0, 8));
        if (hasPlace_) {
            // 标记跟随地理坐标，而不是固定贴在屏幕中央。
            QPointF offset = projected(place_.lonLat) - center_;
            offset.setX(std::remainder(offset.x(), worldSize()));
            const QPointF point = offset + QPointF(width() / 2.0, height() / 2.0);
            if (rect().adjusted(-12, -12, 12, 12).contains(point.toPoint())) {
                painter.setRenderHint(QPainter::Antialiasing);
                painter.setPen(QPen(Qt::white, 3));
                painter.setBrush(QColor("#e64a4a"));
                painter.drawEllipse(point, 9, 9);
                const QString label = painter.fontMetrics().elidedText(place_.name, Qt::ElideRight, 240);
                const int labelWidth = painter.fontMetrics().horizontalAdvance(label) + 20;
                const QRect box(qBound(0, qRound(point.x()) - labelWidth / 2, width() - labelWidth),
                                qBound(0, qRound(point.y()) - 46, height() - 32), labelWidth, 32);
                painter.setPen(Qt::NoPen);
                painter.setBrush(QColor("#171d22"));
                painter.drawRoundedRect(box, 5, 5);
                painter.setPen(Qt::white);
                painter.drawText(box, Qt::AlignCenter, label);
            }
        }
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

    static QList<QPointF> activeTouchPoints(const QTouchEvent* event)
    {
        QList<QPointF> result;
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
        for (const auto& point : event->points()) {
            if (point.state() != QEventPoint::State::Released) result.append(point.position());
        }
#else
        for (const QTouchEvent::TouchPoint& point : event->touchPoints()) {
            if (point.state() != Qt::TouchPointReleased) result.append(point.pos());
        }
#endif
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

        const QList<QPointF> points = activeTouchPoints(event);
        if (event->type() == QEvent::TouchBegin) {
            dragging_ = false;
            refresh_.stop();
            if (points.size() >= 2) {
                beginPinch(points[0], points[1]);
            } else if (points.size() == 1) {
                touchLast_ = points.first();
            }
            event->accept();
            return true;
        }

        if (points.size() >= 2) {
            if (!pinchActive_) beginPinch(points[0], points[1]);
            updatePinch(points[0], points[1]);
        } else if (points.size() == 1) {
            if (pinchActive_) {
                pinchActive_ = false;
                touchLast_ = points.first();
            } else {
                const QPointF current = points.first();
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
        apiKey_ = readMapServiceKey();
        qInfo() << "Map Key configured=" << !apiKey_.isEmpty();
    }

    QPointF projected(const QPointF& lonLat) const
    {
        constexpr double pi = 3.14159265358979323846;
        const double radians = lonLat.y() * pi / 180.0;
        return QPointF((lonLat.x() + 180.0) / 360.0 * worldSize(),
            (1.0 - std::asinh(std::tan(radians)) / pi) / 2.0 * worldSize());
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
                                         : QStringLiteral("Qt HTTPS 不可用，请查看终端日志"));
            return;
        }
        const QVector<Tile> tiles = visibleTiles();
        int errors = 0;
        for (const QString& layer : {QStringLiteral("vec"), QStringLiteral("cva")}) {
            for (const Tile& tile : tiles) {
                const QString key = layerKey(tile, layer);
                if (images_.contains(key)) {
                    continue;
                }
                if (failed_.contains(key)) { ++errors; continue; }
                if (pending_.contains(key) || pending_.size() >= 4) continue;
                fetch(tile, layer);
            }
        }
        if (errors > 0) {
            status_->setText(QStringLiteral("地图加载失败 %1 块，请点击重试").arg(errors));
        } else {
            status_->clear();
        }
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
        request.setRawHeader("User-Agent", "SUSTfresher-VehicleTerminal-Map/0.1");
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
                qInfo() << "Map tile OK:" << key;
            } else {
                if (failed_.size() >= 256) failed_.clear();
                failed_.insert(key);
                qWarning() << "Map tile FAILED:" << key
                           << "HTTP=" << http
                           << "timeout=" << reply->property("timedOut").toBool()
                           << "tooLarge=" << reply->property("tooLarge").toBool()
                           << "networkError=" << static_cast<int>(reply->error())
                           << "imageValid=" << valid;
            }
            reply->deleteLater();
            update();
            if (!dragging_ && !refresh_.isActive()) requestVisible();
        });
    }

    QLabel* status_;
    QNetworkAccessManager* network_;
    QString apiKey_;
    QCache<QString, QImage> images_;
    QSet<QString> pending_, failed_;
    QTimer refresh_;
    QPointF center_, dragCenter_;
    QPoint dragStart_;
    QPointF touchLast_, pinchAnchorWorld_;
    double pinchStartDistance_ = 0.0;
    int pinchStartZoom_ = 14;
    int zoom_ = 14;
    bool dragging_ = false;
    bool pinchActive_ = false;
    MapPlace place_;
    bool hasPlace_ = false;
};

} // namespace

QWidget* createMapPage(QWidget* parent)
{
    auto* page = new QWidget(parent);
    page->setObjectName(QStringLiteral("mapPage"));
    page->setProperty("swipeBlock", true);
    page->setStyleSheet(
        "#mapPage { background: #0f1316; }"
        "#mapPage #mapTopBar { background: #171d22; }"
        "#mapPage #mapTitle { color: #f5f7f8; font-size: 20px; }"
        "#mapPage #mapStatus { color: #e4a2a2; font-size: 12px; }"
        "#mapPage #searchPanel { background: #171d22; border: 1px solid #303b43; }"
        "#mapPage QLineEdit { background: #252e35; color: #f5f7f8; border: none;"
        " border-radius: 5px; padding: 6px; font-size: 16px; }"
        "#mapPage QListWidget { background: #171d22; color: #eff1f3; border: none; font-size: 15px; }"
        "#mapPage QListWidget::item { border-bottom: 1px solid #303b43; }"
        "#mapPage QListWidget::item:selected { background: #25334b; }"
        "#mapPage #searchHint { color: #aeb7c0; font-size: 13px; border: none; }"
        "#mapPage #selectedPlace { color: #f5f7f8; font-size: 14px; }"
        "#mapPage QPushButton { min-height: 40px; background: #252e35; border: none;"
        " border-radius: 6px; padding: 0 10px; font-size: 15px; color: #eff1f3; }"
        "#mapPage QPushButton:pressed { background: #3e6ae1; }");

    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    auto* topBar = new QWidget;
    topBar->setObjectName(QStringLiteral("mapTopBar"));
    topBar->setFixedHeight(52);
    auto* topLayout = new QHBoxLayout(topBar);
    topLayout->setContentsMargins(8, 4, 8, 4);
    topLayout->setSpacing(6);
    auto* back = new QPushButton(QStringLiteral("返回"));
    back->setObjectName(QStringLiteral("mapBack"));
    back->setFocusPolicy(Qt::NoFocus);
    auto* title = new QLabel(QStringLiteral("地图"));
    title->setObjectName(QStringLiteral("mapTitle"));
    topLayout->addWidget(back);
    topLayout->addWidget(title);
    topLayout->addStretch();

    auto* status = new QLabel;
    status->setObjectName(QStringLiteral("mapStatus"));
    const auto addButton = [topLayout](const QString& text) {
        auto* button = new QPushButton(text);
        button->setFocusPolicy(Qt::NoFocus);
        topLayout->addWidget(button);
        return button;
    };
    auto* searchToggle = addButton(QStringLiteral("搜索"));
    searchToggle->setObjectName(QStringLiteral("mapSearchToggle"));
    searchToggle->setCheckable(true);
    auto* home = addButton(QStringLiteral("陕科大"));
    auto* retry = addButton(QStringLiteral("重试"));
    auto* zoomOut = addButton(QStringLiteral("−"));
    auto* zoomIn = addButton(QStringLiteral("+"));
    auto* map = new MapCanvas(status, page);
    map->setObjectName(QStringLiteral("mapCanvas"));
    QObject::connect(zoomOut, &QPushButton::clicked, map, [map]() { map->changeZoom(-1); });
    QObject::connect(zoomIn, &QPushButton::clicked, map, [map]() { map->changeZoom(1); });
    QObject::connect(retry, &QPushButton::clicked, map, [map]() { map->retry(); });

    // 搜索浮层覆盖地图左侧，收起后恢复完整视野；列表超长只在内部滚动。
    auto* body = new QWidget(page);
    auto* overlay = new QGridLayout(body);
    overlay->setContentsMargins(0, 0, 0, 0);
    overlay->setSpacing(0);
    overlay->addWidget(map, 0, 0);
    auto* panel = new SearchPanel;
    panel->setObjectName(QStringLiteral("searchPanel"));
    panel->setFixedWidth(320);
    auto* panelLayout = new QVBoxLayout(panel);
    panelLayout->setContentsMargins(10, 10, 10, 10);
    panelLayout->setSpacing(6);
    auto* inputRow = new QHBoxLayout;
    auto* input = new QLineEdit;
    input->setObjectName(QStringLiteral("mapSearchInput"));
    input->setPlaceholderText(QStringLiteral("地点名称（西安市）"));
    input->setMaxLength(80);
    input->setMinimumWidth(0);
    input->setFixedHeight(40);
    auto* submit = new QPushButton(QStringLiteral("查找"));
    submit->setObjectName(QStringLiteral("mapSearchSubmit"));
    submit->setFocusPolicy(Qt::NoFocus);
    inputRow->addWidget(input, 1);
    inputRow->addWidget(submit);
    panelLayout->addLayout(inputRow);
    auto* shortcuts = new QHBoxLayout;
    auto* station = new QPushButton(QStringLiteral("西安北站"));
    auto* university = new QPushButton(QStringLiteral("陕西科技大学"));
    for (auto* button : {station, university}) {
        button->setFocusPolicy(Qt::NoFocus);
        shortcuts->addWidget(button);
    }
    panelLayout->addLayout(shortcuts);
    auto* hint = new QLabel(QStringLiteral("西安市内搜索 · 支持键盘输入"));
    hint->setObjectName(QStringLiteral("searchHint"));
    hint->setTextFormat(Qt::PlainText);
    hint->setWordWrap(true);
    hint->setFixedHeight(42);
    panelLayout->addWidget(hint);
    auto* results = new QListWidget;
    results->setObjectName(QStringLiteral("mapSearchResults"));
    results->setMinimumSize(0, 0);
    results->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);
    results->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    results->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    results->setWordWrap(false);
    results->setTextElideMode(Qt::ElideRight);
    QScroller::grabGesture(results->viewport(), QScroller::TouchGesture);
    panelLayout->addWidget(results, 1);
    overlay->addWidget(panel, 0, 0, Qt::AlignLeft);
    panel->hide();

    auto* search = new MapSearchClient(panel);
    panel->client = search;
    panel->hint = hint;
    const auto places = std::make_shared<QVector<MapPlace>>();
    auto* selected = new QLabel;
    selected->setObjectName(QStringLiteral("selectedPlace"));
    selected->setTextFormat(Qt::PlainText);
    selected->setFixedHeight(28);
    selected->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    selected->hide();
    const auto runSearch = [search, input, hint, results, places]() {
        results->clear();
        places->clear();
        hint->setText(QStringLiteral("正在搜索西安市内地点…"));
        search->search(input->text());
    };
    QObject::connect(submit, &QPushButton::clicked, panel, runSearch);
    QObject::connect(input, &QLineEdit::returnPressed, panel, runSearch);
    QObject::connect(station, &QPushButton::clicked, panel, [input, runSearch]() {
        input->setText(QStringLiteral("西安北站")); runSearch();
    });
    QObject::connect(university, &QPushButton::clicked, panel, [input, runSearch]() {
        input->setText(QStringLiteral("陕西科技大学")); runSearch();
    });
    QObject::connect(input, &QLineEdit::textEdited, panel, [search, results, places, hint]() {
        search->cancel(); results->clear(); places->clear();
        hint->setText(QStringLiteral("输入后点击查找或按回车"));
    });
    QObject::connect(search, &MapSearchClient::finished, panel,
        [results, hint, places](const QVector<MapPlace>& found, const QString& message) {
            *places = found;
            results->clear();
            hint->setText(message);
            for (int i = 0; i < found.size(); ++i) {
                const auto& place = found[i];
                auto* item = new QListWidgetItem(place.name + QLatin1Char('\n') +
                    (place.address.isEmpty() ? QStringLiteral("地址未提供") : place.address), results);
                item->setData(Qt::UserRole, i);
                item->setSizeHint(QSize(0, 62));
            }
        });
    QObject::connect(results, &QListWidget::itemClicked, panel,
        [places, map, searchToggle, selected](QListWidgetItem* item) {
            const int index = item->data(Qt::UserRole).toInt();
            if (index < 0 || index >= places->size()) return;
            const MapPlace& place = places->at(index);
            map->showPlace(place);
            selected->setText(QStringLiteral("已选：%1 · 地点标记，非实时定位").arg(place.name));
            selected->show();
            searchToggle->setChecked(false);
        });
    QObject::connect(searchToggle, &QPushButton::toggled, panel, [panel, input](bool open) {
        panel->setVisible(open);
        if (open) { panel->raise(); input->setFocus(); }
    });
    QObject::connect(home, &QPushButton::clicked, map, [map, selected, searchToggle]() {
        map->home(); selected->hide(); searchToggle->setChecked(false);
    });

    layout->addWidget(topBar);
    layout->addWidget(body, 1);
    layout->addWidget(selected);
    auto* footer = new QHBoxLayout;
    footer->setContentsMargins(8, 0, 8, 2);
    footer->addWidget(status, 1);
    auto* attribution = new QLabel(QStringLiteral("天地图 · 无 GPS 实时定位"));
    attribution->setAlignment(Qt::AlignRight);
    attribution->setStyleSheet("font-size: 11px; color: #7f8a93;");
    footer->addWidget(attribution);
    layout->addLayout(footer);
    return page;
}

QPushButton* mapPageBackButton(QWidget* page)
{
    return page ? page->findChild<QPushButton*>(QStringLiteral("mapBack")) : nullptr;
}
