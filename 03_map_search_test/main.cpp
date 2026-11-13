// Offline checks use synthetic search responses, never a real API key or POI request.
#include "../01_ui_shell/map_search_client.h"
#include "../01_ui_shell/map_page.h"
#include <QApplication>
#include <QFontDatabase>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QStackedWidget>
#include <QTest>
#include <QTimer>
#include <QUrlQuery>
#include <QVBoxLayout>
#include <iostream>

static int failures = 0;
static void check(bool ok, const char* message)
{
    if (!ok) { std::cerr << "FAIL: " << message << '\n'; ++failures; }
}

int main(int argc, char** argv)
{
    QApplication app(argc, argv);
#ifdef Q_OS_WIN
    const int fontId = QFontDatabase::addApplicationFont("C:/Windows/Fonts/NotoSansSC-VF.ttf");
    const auto families = QFontDatabase::applicationFontFamilies(fontId);
    if (!families.isEmpty()) app.setFont(QFont(families.first()));
#endif
    const auto url = makeMapSearchUrl(QStringLiteral("西安北站 & A+B"), "test-not-a-key");
    const auto params = QJsonDocument::fromJson(QUrlQuery(url).queryItemValue(
        "postStr", QUrl::FullyDecoded).toUtf8()).object();
    check(url.scheme() == "https" && url.host() == "api.tianditu.gov.cn", "HTTPS endpoint");
    check(params["queryType"].toInt() == 12 && params["specify"].toString() == "156610100", "Xi'an request");
    check(params["keyWord"].toString() == QStringLiteral("西安北站 & A+B"), "keyword encoding");
    const QByteArray valid = R"({"status":{"infocode":1000},"resultType":1,"count":2,"pois":[
        {"name":"TEST STATION","address":"Test address","lonlat":"108.94,34.38"},
        {"name":"invalid","lonlat":"NaN,34.0"}]})";
    auto response = parseMapSearchResponse(valid);
    check(response.places.size() == 1 && response.places[0].lonLat == QPointF(108.94,34.38), "valid POI and reject NaN");
    check(parseMapSearchResponse("<html>error</html>").places.isEmpty(), "invalid JSON");
    check(parseMapSearchResponse(R"({"status":[]})").places.isEmpty(), "empty status array");
    check(parseMapSearchResponse(R"({"status":{"infocode":3001}})").message.contains(QStringLiteral("未找到")), "no result");
    check(parseMapSearchResponse(R"({"status":{"infocode":2001},"pois":[{"name":"bad","lonlat":"1,2"}]})").places.isEmpty(), "service error with HTTP 200");
    check(parseMapSearchResponse(R"({"status":{"infocode":"1000"},"resultType":"1","count":"0"})").places.isEmpty(), "numeric strings and zero count");
    check(parseMapSearchResponse(R"({"status":[{"infocode":1000}],"resultType":3,"count":1})").message.contains(QStringLiteral("具体")), "area result handled");
    check(parseMapSearchResponse(QByteArray(512*1024+1, ' ')).places.isEmpty(), "oversize input");

    if (!readMapServiceKey().isEmpty()) {
        std::cerr << "Run offline UI checks in a desktop account without a configured map key.\n";
        return 2;
    }

    QWidget window;
    QVBoxLayout shell(&window);
    shell.setContentsMargins(12,4,12,8);
    shell.setSpacing(4);
    QLabel clock("12:00"); clock.setFixedHeight(32); shell.addWidget(&clock);
    QStackedWidget stack; shell.addWidget(&stack,1);
    auto* dashboard = new QWidget; stack.addWidget(dashboard);
    auto* page = createMapPage(); stack.addWidget(page);
    QWidget nav; nav.setFixedHeight(56); shell.addWidget(&nav);
    window.setStyleSheet("QWidget{background:#0f1316;color:#f5f7f8;} QLabel{font-size:24px;}");
    window.resize(720,480); window.show();
    // This keyless account cannot issue tile requests.
    auto* canvas = page->findChild<QWidget*>("mapCanvas");
    stack.setCurrentWidget(page); nav.hide();
    QTest::qWait(20);
    auto* toggle = page->findChild<QPushButton*>("mapSearchToggle");
    auto* input = page->findChild<QLineEdit*>("mapSearchInput");
    auto* list = page->findChild<QListWidget*>("mapSearchResults");
    auto* client = page->findChild<MapSearchClient*>();
    toggle->click();
    QTest::qWait(20);
    check(window.size() == QSize(720,480), "search panel does not enlarge window");
    check(list->height() >= 120, "touch results viewport has useful height");
    client->search(""); // No key required; empty input must fail synchronously.
    check(page->findChild<QLabel*>("searchHint")->text().contains(QStringLiteral("请输入")), "empty input message");
    QVector<MapPlace> places;
    for (int i=0;i<10;++i) places.append(MapPlace{QStringLiteral("测试地点 %1 西安北站").arg(i+1),
        QStringLiteral("测试地址 · 本地合成数据，不是真实搜索结果"), QPointF(108.94+i*.001,34.38)});
    client->finished(places, QStringLiteral("找到 10 个候选地点，点击查看"));
    QTest::qWait(20);
    window.grab().save("search-panel.png");
    check(list->count()==10 && list->isVisible(), "candidate list");
    QTest::mouseClick(list->viewport(), Qt::LeftButton, Qt::NoModifier, list->visualItemRect(list->item(0)).center());
    QTest::qWait(20);
    check(!toggle->isChecked() && !list->isVisible(), "select closes panel");
    check(page->findChild<QLabel*>("selectedPlace")->isVisible(), "selected place caption");
    const QImage view = canvas->grab().toImage();
    const QColor center = view.pixelColor(view.width()/2,view.height()/2);
    check(center.red()>180 && center.green()<120, "geographic marker initially centered");
    window.grab().save("selected-place.png");
    const QPoint centerPoint(canvas->width()/2, canvas->height()/2);
    QTest::mousePress(canvas, Qt::LeftButton, Qt::NoModifier, centerPoint);
    QTest::mouseMove(canvas, centerPoint + QPoint(40, 20));
    QTest::mouseRelease(canvas, Qt::LeftButton, Qt::NoModifier, centerPoint + QPoint(40, 20));
    QTest::qWait(10);
    const QColor moved = canvas->grab().toImage().pixelColor(centerPoint + QPoint(40,20));
    check(moved.red()>180 && moved.green()<120, "marker moves with map drag");
    for (auto* button : page->findChildren<QPushButton*>()) {
        if (button->text()==QStringLiteral("+")) button->click();
    }
    QTest::qWait(10);
    const QColor zoomed = canvas->grab().toImage().pixelColor(centerPoint + QPoint(80,40));
    check(zoomed.red()>180 && zoomed.green()<120, "marker scales with map zoom");
    for (auto* button : page->findChildren<QPushButton*>()) {
        if (button->text()==QStringLiteral("陕科大")) button->click();
    }
    check(!page->findChild<QLabel*>("selectedPlace")->isVisible(), "home clears selected place");
    toggle->click();
    input->setFocus();
    QTest::keyClicks(input, "abc");
    check(list->count()==0, "editing clears obsolete results");
    stack.setCurrentWidget(dashboard); nav.show(); QTest::qWait(20);
    check(window.size()==QSize(720,480) && nav.geometry().bottom()==471, "navigation restored within screen");
    std::cout << "Search checks failed: " << failures << '\n';
    return failures ? 1 : 0;
}
