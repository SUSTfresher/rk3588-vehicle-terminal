#pragma once

#include <QObject>
#include <QString>
#include <QTimer>

// 只负责生成模拟数据，不依赖任何界面控件。
class VehicleSimulator : public QObject
{
    Q_OBJECT

public:
    explicit VehicleSimulator(QObject* parent = nullptr);
    void start();

signals:
    // 告诉订阅者：车速和挡位更新了，gear 为 P/D/R。
    void stateChanged(int speedKmh, const QString& gear);

private:
    void advance();

    QTimer timer_;
    int phase_ = 0;
    int ticks_ = 0;
    int speedKmh_ = 0;

    QString gear() const;
};
