#include "testpatterngenerator.h"
#include "converter.h"
#include <cmath>

QImage TestPatternGenerator::generateGradientImage(int size) {
    QImage img(size, size, QImage::Format_RGB32);
    img.fill(Qt::black);

    const double cx    = size / 2.0;
    const double cy    = size / 2.0;
    const double rHub  = size * 0.25;   // inner hub: no data
    const double rRim  = size * 0.475;  // outer data edge

    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            const double r = std::sqrt((x - cx) * (x - cx) + (y - cy) * (y - cy));
            int gray = 0;
            if (r >= rHub && r <= rRim)
                gray = static_cast<int>(255.0 * (r - rHub) / (rRim - rHub));
            else if (r > rRim)
                gray = 255;
            img.setPixel(x, y, qRgb(gray, gray, gray));
        }
    }
    return img;
}

QString TestPatternGenerator::generateTrack(const DiscProfile& profile,
                                             const QString& outputPath) {
    const QImage img = generateGradientImage(3000);
    Converter conv(nullptr, profile);
    if (conv.convert(img, outputPath))
        return outputPath;
    return {};
}
