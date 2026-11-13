// OV13855 室内演示用色彩补偿：与 refined.png 的 RGB 增益一致。
// 这是固定增益，不是自动白平衡；光照变化后可能需要关闭或重新调整。
#pragma once

#include <QImage>
#include <array>
#include <cmath>

inline void applyCameraColorCorrection(QImage& image)
{
    if (image.format() != QImage::Format_RGB888) return;

    // 每个通道只有 256 种输入值。查找表只生成一次，逐帧处理不做浮点乘法。
    static const auto tables = [] {
        std::array<std::array<uchar, 256>, 3> result{};
        constexpr float gains[] = {1.48f, 0.88f, 1.12f};
        for (int channel = 0; channel < 3; ++channel) {
            for (int value = 0; value < 256; ++value) {
                const int corrected = static_cast<int>(
                    std::nearbyint(static_cast<float>(value) * gains[channel]));
                result[channel][value] = static_cast<uchar>(qBound(0, corrected, 255));
            }
        }
        return result;
    }();

    // 按实际行地址访问，只修改 RGB 像素，不触碰每行末尾的对齐填充。
    for (int y = 0; y < image.height(); ++y) {
        uchar* row = image.scanLine(y);
        for (int x = 0; x < image.width(); ++x) {
            row[3 * x]     = tables[0][row[3 * x]];
            row[3 * x + 1] = tables[1][row[3 * x + 1]];
            row[3 * x + 2] = tables[2][row[3 * x + 2]];
        }
    }
}
