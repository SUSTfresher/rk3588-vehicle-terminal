// 将“连接是否存在”和“数据是否新鲜”分别检查。
// 协议：每行一条 JSON，version=1，speed_kmh 为 0..200 整数，gear 为 P/D/R。
#include "vehicle_data_client.h"

#include <QDebug>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>

namespace {
constexpr int reconnectIntervalMs = 2000;
constexpr int freshnessCheckIntervalMs = 100;
constexpr int validDataTimeoutMs = 1000;
const QString serverName = QStringLiteral("/tmp/vehicle-terminal-demo");
const QString waitingText = QStringLiteral("等待车辆数据");
}

VehicleDataClient::VehicleDataClient(QObject* parent)
    : QObject(parent),
      socket_(new QLocalSocket(this)),
      reconnectTimer_(new QTimer(this)),
      freshnessTimer_(new QTimer(this))
{
    // Qt 内部缓冲和下面的拼包缓冲分别限长，防止持续积压。
    socket_->setReadBufferSize(8192);
    reconnectTimer_->setInterval(reconnectIntervalMs);
    freshnessTimer_->setInterval(freshnessCheckIntervalMs);
    freshnessTimer_->setTimerType(Qt::PreciseTimer);

    connect(socket_, &QLocalSocket::connected, this, [this]() {
        receiveBuffer_.clear();
        // 连接成功不等于已有有效数据；首条数据也必须限时到达。
        lastValidData_.start();
        dataTimedOut_ = false;
        emit dataUnavailable(waitingText);
        qInfo() << "Vehicle server connected";
    });

    connect(socket_, &QLocalSocket::disconnected, this, [this]() {
        receiveBuffer_.clear();
        lastValidData_.invalidate();
        dataTimedOut_ = false;
        emit dataUnavailable(waitingText);
        qInfo() << "Vehicle server disconnected";
    });

    connect(socket_, &QLocalSocket::readyRead,
            this, &VehicleDataClient::readPendingData);

    // Qt 5.12 使用 error 信号，需要区分同名的查询函数。
    connect(socket_,
            QOverload<QLocalSocket::LocalSocketError>::of(&QLocalSocket::error),
            this, [this](QLocalSocket::LocalSocketError) {
        qWarning() << "Vehicle connection error:" << socket_->errorString();
        emit dataUnavailable(waitingText);
    });

    connect(reconnectTimer_, &QTimer::timeout,
            this, &VehicleDataClient::tryConnect);
    connect(freshnessTimer_, &QTimer::timeout,
            this, &VehicleDataClient::checkFreshness);
}

void VehicleDataClient::start()
{
    if (reconnectTimer_->isActive()) {
        return;
    }
    emit dataUnavailable(waitingText);
    reconnectTimer_->start();
    freshnessTimer_->start();
    tryConnect();  // 首次立即连接，不先等两秒。
}

void VehicleDataClient::tryConnect()
{
    // 已连接或正在连接时不重复调用 connectToServer。
    if (socket_->state() != QLocalSocket::UnconnectedState) {
        return;
    }
    receiveBuffer_.clear();
    qInfo() << "Trying vehicle server...";
    socket_->connectToServer(serverName);
}

void VehicleDataClient::readPendingData()
{
    // 字节流没有消息边界：半条留到下次，多条按换行逐一取出。
    receiveBuffer_ += socket_->readAll();
    if (receiveBuffer_.size() > 16384) {
        qWarning() << "Vehicle receive buffer too large";
        receiveBuffer_.clear();
        emit dataUnavailable(waitingText);
        socket_->abort();
        return;
    }

    int newline = -1;
    while ((newline = receiveBuffer_.indexOf('\n')) >= 0) {
        const QByteArray line = receiveBuffer_.left(newline);
        receiveBuffer_.remove(0, newline + 1);

        QJsonParseError error;
        const QJsonDocument document = QJsonDocument::fromJson(line, &error);
        if (error.error != QJsonParseError::NoError || !document.isObject()) {
            qWarning() << "Invalid vehicle JSON";
            emit dataUnavailable(waitingText);
            continue;
        }

        const QJsonObject object = document.object();
        const auto version = object.value("version");
        const auto speedValue = object.value("speed_kmh");
        const QString gear = object.value("gear").toString();
        const double value = speedValue.toDouble(-1);

        // 先验证类型和范围，再转整数；P/R挡在本版模拟协议中必须静止。
        if (!version.isDouble() || version.toDouble() != 1 ||
            !speedValue.isDouble() || !(value >= 0 && value <= 200) ||
            value != static_cast<int>(value) ||
            (gear != "P" && gear != "D" && gear != "R") ||
            ((gear == "P" || gear == "R") && value != 0)) {
            qWarning() << "Invalid vehicle fields";
            emit dataUnavailable(waitingText);
            continue;
        }

        // 只有完整且合法的数据刷新单调时钟；非法数据不能掩盖超时。
        lastValidData_.start();
        if (dataTimedOut_) {
            qInfo() << "Vehicle data recovered";
        }
        dataTimedOut_ = false;
        emit stateReceived(static_cast<int>(value), gear);
    }
}

void VehicleDataClient::checkFreshness()
{
    if (socket_->state() != QLocalSocket::ConnectedState ||
        !lastValidData_.isValid() || lastValidData_.elapsed() < validDataTimeoutMs) {
        return;
    }

    // 超时不主动断连，合法数据恢复后沿用原连接。
    // 定时检查依赖事件循环调度，不是硬实时保证。
    if (!dataTimedOut_) {
        dataTimedOut_ = true;
        qWarning() << "Vehicle data timeout: no valid state for 1000 ms";
    }
    emit dataUnavailable(QStringLiteral("车辆数据超时"));
}
