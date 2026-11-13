#include "map_search_client.h"

#include <QDebug>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSslSocket>
#include <QStringList>
#include <QTimer>
#include <QUrlQuery>
#include <QVariant>
#include <cmath>

namespace {
constexpr int maxResponseBytes = 512 * 1024;

int integerValue(const QJsonValue& value, int fallback = -1)
{
    if (value.isDouble()) return value.toInt(fallback);
    bool ok = false;
    const int number = value.toString().toInt(&ok);
    return ok ? number : fallback;
}
}

QString readMapServiceKey()
{
    QFile file(QDir::homePath() + QStringLiteral("/.config/vehicle-map-probe/tianditu.key"));
    if (!file.open(QIODevice::ReadOnly) || file.size() > 256) return {};
    const QByteArray key = file.readAll().trimmed();
    for (char c : key) {
        if (c <= ' ' || c >= 127 || c == '\"' || c == '\'') return {};
    }
    return QString::fromLatin1(key);
}

QUrl makeMapSearchUrl(const QString& keyword, const QString& key)
{
    // v2 行政区划搜索，不依赖当前地图视野；第一版范围固定为西安市。
    const QJsonObject parameters{
        {QStringLiteral("keyWord"), keyword},
        {QStringLiteral("queryType"), 12},
        {QStringLiteral("specify"), QStringLiteral("156610100")},
        {QStringLiteral("start"), 0},
        {QStringLiteral("count"), 10}
    };
    QUrl url(QStringLiteral("https://api.tianditu.gov.cn/v2/search"));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("postStr"),
        QString::fromUtf8(QJsonDocument(parameters).toJson(QJsonDocument::Compact)));
    query.addQueryItem(QStringLiteral("type"), QStringLiteral("query"));
    query.addQueryItem(QStringLiteral("tk"), key);
    url.setQuery(query);
    return url;
}

MapSearchResult parseMapSearchResponse(const QByteArray& json)
{
    MapSearchResult result;
    if (json.size() > maxResponseBytes) {
        result.message = QStringLiteral("搜索响应过大，请换个关键词");
        return result;
    }
    QJsonParseError error;
    const auto document = QJsonDocument::fromJson(json, &error);
    if (error.error != QJsonParseError::NoError || !document.isObject()) {
        result.message = QStringLiteral("搜索响应格式异常，请重试");
        return result;
    }
    const auto root = document.object();
    QJsonValue status = root.value(QStringLiteral("status"));
    if (status.isArray()) {
        const auto entries = status.toArray();
        status = entries.isEmpty() ? QJsonValue() : entries.at(0);
    }
    const int code = integerValue(status.toObject().value(QStringLiteral("infocode")));
    if (code == 3001) {
        result.message = QStringLiteral("西安市内未找到地点，请换个关键词");
        return result;
    }
    if (code != 1000) {
        // 不显示服务端原文，避免把含 Key 的 URL 或内部错误放到界面/日志。
        result.message = code < 0
            ? QStringLiteral("搜索服务未返回有效状态，请检查 Key 与搜索权限")
            : QStringLiteral("搜索服务拒绝请求（状态 %1），请检查权限或稍后重试").arg(code);
        return result;
    }
    if (integerValue(root.value(QStringLiteral("count"))) == 0) {
        result.message = QStringLiteral("西安市内未找到地点，请换个关键词");
        return result;
    }
    if (integerValue(root.value(QStringLiteral("resultType"))) != 1) {
        result.message = QStringLiteral("请填写更具体的地点名称，例如“西安北站”");
        return result;
    }
    const auto pois = root.value(QStringLiteral("pois")).toArray();
    for (const auto& value : pois) {
        const auto poi = value.toObject();
        const QString name = poi.value(QStringLiteral("name")).toString().simplified().left(120);
        const auto coordinates = poi.value(QStringLiteral("lonlat")).toString().split(',');
        if (name.isEmpty() || coordinates.size() != 2) continue;
        bool lonOk = false, latOk = false;
        const double lon = coordinates[0].trimmed().toDouble(&lonOk);
        const double lat = coordinates[1].trimmed().toDouble(&latOk);
        if (!lonOk || !latOk || !std::isfinite(lon) || !std::isfinite(lat) ||
            lon < -180 || lon > 180 || lat < -85.05112878 || lat > 85.05112878) continue;
        result.places.append(MapPlace{name,
            poi.value(QStringLiteral("address")).toString().simplified().left(200), QPointF(lon, lat)});
        if (result.places.size() == 10) break;
    }
    result.message = result.places.isEmpty()
        ? QStringLiteral("没有可定位的地点，请换个关键词")
        : QStringLiteral("找到 %1 个候选地点，点击查看").arg(result.places.size());
    return result;
}

MapSearchClient::MapSearchClient(QObject* parent)
    : QObject(parent), network_(new QNetworkAccessManager(this)) {}

void MapSearchClient::cancel()
{
    // 先清空当前请求，再 abort；旧请求的 finished 不能覆盖新一轮结果。
    const auto previous = pending_;
    pending_.clear();
    if (previous) previous->abort();
}

void MapSearchClient::search(const QString& input)
{
    cancel();
    const QString keyword = input.trimmed();
    if (keyword.isEmpty() || keyword.size() > 80) {
        emit finished({}, QStringLiteral("请输入 1～80 个字符的地点名称"));
        return;
    }
    const QString key = readMapServiceKey();
    if (key.isEmpty() || !QSslSocket::supportsSsl()) {
        emit finished({}, key.isEmpty() ? QStringLiteral("请先配置有效的地图 Key")
                                       : QStringLiteral("系统 HTTPS 暂不可用"));
        return;
    }
    QNetworkRequest request(makeMapSearchUrl(keyword, key));
    request.setRawHeader("User-Agent", "SUSTfresher-VehicleTerminal-Map/0.1");
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
    QNetworkReply* reply = network_->get(request);
    pending_ = reply;
    auto* deadline = new QTimer(reply);
    deadline->setSingleShot(true);
    connect(deadline, &QTimer::timeout, reply, [reply]() {
        reply->setProperty("timedOut", true);
        reply->abort();
    });
    deadline->start(12000);
    connect(reply, &QNetworkReply::readyRead, reply, [reply]() {
        if (reply->bytesAvailable() > maxResponseBytes) {
            reply->setProperty("tooLarge", true);
            reply->abort();
        }
    });
    connect(reply, &QNetworkReply::finished, this, [this, reply, deadline]() {
        deadline->stop();
        if (pending_ != reply) { reply->deleteLater(); return; }
        pending_.clear();
        const int http = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        MapSearchResult result;
        if (reply->property("timedOut").toBool())
            result.message = QStringLiteral("搜索超时，请检查网络后重试");
        else if (reply->property("tooLarge").toBool())
            result.message = QStringLiteral("搜索响应过大，请换个关键词");
        else if (http == 401 || http == 403)
            result.message = QStringLiteral("搜索未获授权，请检查 Key 的服务权限");
        else if (reply->error() != QNetworkReply::NoError || http != 200)
            result.message = QStringLiteral("搜索连接失败（HTTP %1），请重试").arg(http);
        else
            result = parseMapSearchResponse(reply->readAll());
        qInfo() << "Map search: HTTP=" << http << "networkError=" << static_cast<int>(reply->error())
                << "timeout=" << reply->property("timedOut").toBool()
                << "results=" << result.places.size();
        reply->deleteLater();
        emit finished(result.places, result.message);
    });
}
