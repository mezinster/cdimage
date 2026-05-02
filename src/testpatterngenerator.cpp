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

QImage TestPatternGenerator::generateRingsImage(int size) {
    QImage img(size, size, QImage::Format_RGB32);
    img.fill(Qt::black);

    const double cx       = size / 2.0;
    const double cy       = size / 2.0;
    // Outer data area = 58 mm; image half-width maps to that physical radius.
    const double pxPerMm  = (size / 2.0) / 58.0;
    // Ring radii from upstream solver: r0 + {4, 16, 32} mm (r0 = 24.5 mm).
    const double rings_mm[3] = { 28.5, 40.5, 56.5 };
    // Ring band half-width in pixels: ~4 px at 1200, ~10 px at 3000.
    const double halfWidthPx  = std::max(3.0, size / 300.0);
    // Notch: 20° gap centred at angle 0 (rightward from centre).
    const double notchHalf = 10.0 * M_PI / 180.0;

    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            const double dx   = x - cx;
            const double dy   = y - cy;
            const double rpx  = std::sqrt(dx*dx + dy*dy);
            const double rmm  = rpx / pxPerMm;
            const double ang  = std::atan2(dy, dx);   // -π .. π

            for (double rr : rings_mm) {
                if (std::abs(rmm - rr) * pxPerMm < halfWidthPx) {
                    if (std::abs(ang) > notchHalf)
                        img.setPixel(x, y, qRgb(255, 255, 255));
                    break;
                }
            }
        }
    }
    return img;
}

QString TestPatternGenerator::generateTrack(const DiscProfile& profile,
                                             const QString& outputPath,
                                             qint64 maxAudioBytes) {
    const QImage img = generateGradientImage(3000);
    Converter conv(nullptr, profile);
    if (conv.convert(img, outputPath, maxAudioBytes))
        return outputPath;
    return {};
}

QString TestPatternGenerator::generateRingsTrack(const DiscProfile& profile,
                                                   const QString& outputPath,
                                                   qint64 maxAudioBytes) {
    const QImage img = generateRingsImage(3000);
    Converter conv(nullptr, profile);
    if (conv.convert(img, outputPath, maxAudioBytes))
        return outputPath;
    return {};
}
