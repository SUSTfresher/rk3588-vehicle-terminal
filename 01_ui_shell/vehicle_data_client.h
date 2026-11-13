// 车辆数据客户端：只负责通信和数据有效性，不依赖任何界面控件。
// main.cpp 通过信号获得合法状态或不可用原因。
#pragma once

#include <QObject>
#include <QByteArray>
#include <QElapsedTimer>
#include <QLocalSocket>
#include <QString>
#include <QTimer>

class VehicleDataClient : public QObject
{
    Q_OBJECT

public:
    explicit VehicleDataClient(QObject* parent = nullptr);
    // 在界面接好信号后调用；重复调用不会重复创建连接或定时器。
    void start();

signals:
    void stateReceived(int speedKmh, const QString& gear);
    void dataUnavailable(const QString& reason);

private:
    void tryConnect();
    void readPendingData();
    void checkFreshness();

    // 全部在所属线程的事件循环运行，不创建工作线程，也不阻塞等待。
    QLocalSocket* socket_;
    QTimer* reconnectTimer_;
    QTimer* freshnessTimer_;
    QByteArray receiveBuffer_;
    QElapsedTimer lastValidData_;
    bool dataTimedOut_ = false;
};
