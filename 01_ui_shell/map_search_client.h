// 天地图地点搜索：只处理请求和结果，不依赖地图界面。
#pragma once

#include <QObject>
#include <QPointF>
#include <QPointer>
#include <QUrl>
#include <QVector>

class QNetworkAccessManager;
class QNetworkReply;

struct MapPlace {
    QString name;
    QString address;
    QPointF lonLat; // 经度在前、纬度在后；与天地图底图使用同一来源。
};

struct MapSearchResult {
    QVector<MapPlace> places;
    QString message;
};

QString readMapServiceKey();
QUrl makeMapSearchUrl(const QString& keyword, const QString& key);
MapSearchResult parseMapSearchResponse(const QByteArray& json);

class MapSearchClient final : public QObject
{
    Q_OBJECT
public:
    explicit MapSearchClient(QObject* parent = nullptr);
    void search(const QString& keyword);
    void cancel();

signals:
    void finished(const QVector<MapPlace>& places, const QString& message);

private:
    QNetworkAccessManager* network_;
    QPointer<QNetworkReply> pending_;
};
