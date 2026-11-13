#include "vehicle_simulator.h"

VehicleSimulator::VehicleSimulator(QObject* parent)
    : QObject(parent)
{
    timer_.setInterval(100);

    connect(&timer_, &QTimer::timeout,
            this, &VehicleSimulator::advance);
}

void VehicleSimulator::start()
{
    phase_ = 0;
    ticks_ = 0;
    speedKmh_ = 0;

    // 立即发布初始状态。
    emit stateChanged(speedKmh_, gear());
    timer_.start();
}

void VehicleSimulator::advance()
{
    switch (phase_) {
    case 0:
        if (++ticks_ >= 30) {
            ticks_ = 0;
            phase_ = 1;
        }
        break;

    case 1:
        if (++speedKmh_ >= 60) {
            speedKmh_ = 60;
            ticks_ = 0;
            phase_ = 2;
        }
        break;

    case 2:
        if (++ticks_ >= 30) {
            ticks_ = 0;
            phase_ = 3;
        }
        break;

    case 3:
        if (--speedKmh_ <= 0) {
            speedKmh_ = 0;
            ticks_ = 0;
            phase_ = 4;
        }
        break;

    case 4:
        // 模拟倒车：保持 R 挡和 0 km/h 约4秒。
        if (++ticks_ >= 40) {
            ticks_ = 0;
            phase_ = 0;
        }
        break;
    }

    emit stateChanged(speedKmh_, gear());
}

QString VehicleSimulator::gear() const
{
    if (phase_ == 0 || phase_ == 4) {
        return phase_ == 4 ? QStringLiteral("R") : QStringLiteral("P");
    }
    return QStringLiteral("D");
}
