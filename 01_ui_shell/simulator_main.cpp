#include <QCoreApplication>
#include <QDebug>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocalServer>
#include <QLocalSocket>

#include "vehicle_simulator.h"

int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);

    QLocalServer server;
    server.setSocketOptions(QLocalServer::UserAccessOption);

    // 先使用固定的本地接口名。
    // 不自动删除已有接口，避免误删另一个正在运行的实例。
    if (!server.listen(QStringLiteral("vehicle-terminal-demo"))) {
        qCritical() << "Listen failed:" << server.errorString();
        return 1;
    }

    qInfo() << "Listening on:" << server.fullServerName();

    VehicleSimulator simulator;

    QObject::connect(
        &server, &QLocalServer::newConnection,
        &app, [&]() {
            while (server.hasPendingConnections()) {
                QLocalSocket* client = server.nextPendingConnection();
                if (!client) {
                    continue;
                }

                qInfo() << "Client connected";

                // 客户端断开后，释放对应连接对象。
                QObject::connect(
                    client, &QLocalSocket::disconnected,
                    client, &QObject::deleteLater
                );

                QObject::connect(
                    &simulator, &VehicleSimulator::stateChanged,
                    client,
                    [client](int speedKmh, const QString& gear) {
                        if (client->state() !=
                            QLocalSocket::ConnectedState) {
                            return;
                        }

                        // 客户端不读取时，不让发送缓冲无限增长。
                        if (client->bytesToWrite() > 4096) {
                            qWarning() << "Slow client disconnected";
                            client->abort();
                            client->deleteLater();
                            return;
                        }

                        QJsonObject state;
                        state.insert(QStringLiteral("version"), 1);
                        state.insert(QStringLiteral("speed_kmh"), speedKmh);
                        state.insert(QStringLiteral("gear"), gear);

                        QByteArray message =
                            QJsonDocument(state).toJson(
                                QJsonDocument::Compact);

                        // 教学阶段：每条 JSON 后加换行，作为消息边界。
                        message.append('\n');

                        if (client->write(message) < 0) {
                            qWarning() << "Send failed:"
                                       << client->errorString();
                            client->abort();
                            client->deleteLater();
                        }
                    }
                );
            }
        }
    );

    simulator.start();
    return app.exec();
}
