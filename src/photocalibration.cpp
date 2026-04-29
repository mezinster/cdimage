#include "photocalibration.h"
#include <cmath>
#include <numeric>

PhotoCalibration::PhotoCalibration(const QString& photoPath, QObject* parent)
    : ICalibrationMethod(parent), m_photoPath(photoPath) {}

QVector<double> PhotoCalibration::radialProfile(const QImage& img,
                                                 int cx, int cy, int maxR) {
    QVector<double> profile(maxR, 0.0);
    QVector<int>    counts(maxR, 0);
    const int w = img.width(), h = img.height();
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const int r = static_cast<int>(
                std::round(std::sqrt((x-cx)*(x-cx) + (y-cy)*(y-cy))));
            if (r < maxR) {
                profile[r] += qGray(img.pixel(x, y));
                counts[r]++;
            }
        }
    }
    for (int r = 0; r < maxR; ++r)
        if (counts[r] > 0) profile[r] /= counts[r];
    return profile;
}

double PhotoCalibration::findEdge(const QVector<double>& profile, double threshold) {
    for (int r = 1; r < profile.size(); ++r)
        if (profile[r] >= threshold && profile[r-1] < threshold)
            return r - (profile[r] - threshold) / (profile[r] - profile[r-1]);
    return -1.0;
}

void PhotoCalibration::start(const RawDiscInfo& disc) {
    emit progressChanged(5);

    QImage photo(m_photoPath);
    if (photo.isNull()) {
        emit failed("Cannot load photo: " + m_photoPath);
        return;
    }
    const QImage gray = photo.convertToFormat(QImage::Format_Grayscale8);
    emit progressChanged(20);

    const int cx   = gray.width()  / 2;
    const int cy   = gray.height() / 2;
    const int maxR = std::min(cx, cy);

    const QVector<double> profile = radialProfile(gray, cx, cy, maxR);
    emit progressChanged(60);

    // Find inner data edge (~5% intensity) and outer disc edge (~90% intensity).
    // ECMA-130 defines the disc outer radius as exactly 60 mm; we use this as
    // the physical anchor to convert pixels → mm.
    const double rDataStart_px = findEdge(profile, 12.75);   // 5% of 255
    const double rDiscEdge_px  = findEdge(profile, 229.5);   // 90% of 255

    if (rDataStart_px < 0 || rDiscEdge_px < 0 || rDiscEdge_px <= rDataStart_px) {
        emit failed("Could not detect disc boundary in photo. "
                    "Ensure the photo shows the full disc on a dark background.");
        return;
    }
    emit progressChanged(80);

    const double pxPerMm    = rDiscEdge_px / 60.0;
    const double r0_actual  = rDataStart_px / pxPerMm;
    const double rDataEndMm = 58.0;  // ECMA-130 lead-out starts at ~58 mm

    // Total sectors in the generated test pattern (74-min CD)
    const int    totalSectors = 336100;
    const double spanMm       = rDataEndMm - r0_actual;
    const double dtr_actual   = spanMm / totalSectors;
    const double tr0_actual   = r0_actual / dtr_actual;

    DiscProfile result;
    result.discId     = disc.discId;
    result.mediaType  = disc.mediaType;
    result.r0         = r0_actual;
    result.dtr        = dtr_actual;
    result.tr0        = tr0_actual;
    result.layerCount = (disc.mediaType == MediaType::DVD_DL) ? 2 : 1;
    emit progressChanged(100);
    emit finished(result);
}
