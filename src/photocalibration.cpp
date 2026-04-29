#include "photocalibration.h"
#include <cmath>
#include <numeric>
#include <limits>

PhotoCalibration::PhotoCalibration(const QString& photoPath, PatternType pattern,
                                    QObject* parent)
    : ICalibrationMethod(parent), m_photoPath(photoPath), m_pattern(pattern) {}

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

double PhotoCalibration::findRingPhase(const QImage& img, double cx, double cy,
                                        double ringRadius_px) {
    const int    nSamples  = 720;  // 0.5° resolution
    const double bandPx    = std::max(4.0, ringRadius_px * 0.04);

    QVector<double> angProfile(nSamples, 0.0);
    QVector<int>    counts(nSamples, 0);

    for (int dr = static_cast<int>(-bandPx); dr <= static_cast<int>(bandPx); ++dr) {
        const double r = ringRadius_px + dr;
        if (r <= 0.0) continue;
        for (int s = 0; s < nSamples; ++s) {
            const double angle = 2.0 * M_PI * s / nSamples;
            const int px = static_cast<int>(std::round(cx + r * std::cos(angle)));
            const int py = static_cast<int>(std::round(cy + r * std::sin(angle)));
            if (px >= 0 && px < img.width() && py >= 0 && py < img.height()) {
                angProfile[s] += qGray(img.pixel(px, py));
                counts[s]++;
            }
        }
    }
    for (int s = 0; s < nSamples; ++s)
        if (counts[s] > 0) angProfile[s] /= counts[s];

    const double mean = std::accumulate(angProfile.begin(), angProfile.end(), 0.0) / nSamples;

    // Use maximum deviation from mean: handles bright-notch-in-dark-ring (burned disc
    // photo) and dark-notch-in-bright-ring (synthetic source image) equally.
    int    maxIdx = 0;
    double maxDev = -1.0;
    for (int s = 0; s < nSamples; ++s) {
        const double dev = std::abs(angProfile[s] - mean);
        if (dev > maxDev) { maxDev = dev; maxIdx = s; }
    }
    return 2.0 * M_PI * maxIdx / nSamples;
}

DiscProfile PhotoCalibration::solveRingsGeometry(double tr0_guess, double dtr_guess,
                                                   double r0, double n1, double n2) {
    const double rs1 = r0 + 4.0;
    const double rs2 = r0 + 16.0;
    const double rs3 = r0 + 32.0;

    // Direct translation of upstream solver.py (arduinocelentano/cdimage PR#29).
    // D(e1,e2,rs) = fractional-turn correction at radius rs given errors e1 (tr0), e2 (dtr).
    auto computeD = [&](double e1, double e2, double rs) -> double {
        const double t0 = tr0_guess - e1;
        const double d0 = dtr_guess - e2;
        if (d0 <= 0.0 || t0 <= 0.0) return 0.0;
        const double LS = (rs / r0 - 1.0) * t0 / d0;
        const double A  = LS + 1.0;
        const double B  = (LS + 1.0) * LS / 2.0;
        const double C  = 4*t0*t0 - 4*t0*d0 + d0*d0 + 8*A*d0*t0 + 8*B*d0*d0;
        const double inner = C + 8*e1*A*d0 + 8*e2*B*d0;
        if (inner <= 0.0 || C <= 0.0) return 0.0;
        return (std::sqrt(inner) - std::sqrt(C)) / (2.0 * d0);
    };

    double e1 = 0.0, e2 = 0.0;
    const double h = 1e-4;  // step for numerical Jacobian

    for (int iter = 0; iter < 100; ++iter) {
        const double D1 = computeD(e1, e2, rs1);
        const double D2 = computeD(e1, e2, rs2);
        const double D3 = computeD(e1, e2, rs3);

        const double F1 = D2 - D1 - n1;
        const double F2 = D3 - D2 - n2;
        if (std::abs(F1) < 1e-10 && std::abs(F2) < 1e-10) break;

        const double J11 = ((computeD(e1+h,e2,rs2)-computeD(e1+h,e2,rs1)) - (D2-D1)) / h;
        const double J12 = ((computeD(e1,e2+h,rs2)-computeD(e1,e2+h,rs1)) - (D2-D1)) / h;
        const double J21 = ((computeD(e1+h,e2,rs3)-computeD(e1+h,e2,rs2)) - (D3-D2)) / h;
        const double J22 = ((computeD(e1,e2+h,rs3)-computeD(e1,e2+h,rs2)) - (D3-D2)) / h;

        const double det = J11*J22 - J12*J21;
        if (std::abs(det) < 1e-20) break;

        e1 -= ( J22*F1 - J12*F2) / det;
        e2 -= (-J21*F1 + J11*F2) / det;
    }

    DiscProfile result;
    result.tr0 = tr0_guess - e1;
    result.dtr = dtr_guess - e2;
    return result;
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
    emit progressChanged(40);

    // Locate inner data edge and outer disc edge; ECMA-130 outer = 60 mm.
    const double rDataStart_px = findEdge(profile, 12.75);   // 5% of 255
    const double rDiscEdge_px  = findEdge(profile, 229.5);   // 90% of 255

    if (rDataStart_px < 0 || rDiscEdge_px < 0 || rDiscEdge_px <= rDataStart_px) {
        emit failed("Could not detect disc boundary in photo. "
                    "Ensure the photo shows the full disc on a dark background.");
        return;
    }
    emit progressChanged(60);

    const double pxPerMm   = rDiscEdge_px / 60.0;
    const double r0_actual = rDataStart_px / pxPerMm;

    if (m_pattern == PatternType::Rings) {
        // Phase-measurement path: measure angular notch positions for the three rings,
        // then invert the spiral equations to get corrected tr0 and dtr.
        const double rs1_px = (r0_actual + 4.0)  * pxPerMm;
        const double rs2_px = (r0_actual + 16.0) * pxPerMm;
        const double rs3_px = (r0_actual + 32.0) * pxPerMm;

        const double theta1 = findRingPhase(gray, cx, cy, rs1_px);
        const double theta2 = findRingPhase(gray, cx, cy, rs2_px);
        const double theta3 = findRingPhase(gray, cx, cy, rs3_px);
        emit progressChanged(80);

        // Inter-ring phase in fractional turns, normalised to [-0.5, 0.5].
        auto normTurns = [](double dTheta) -> double {
            return std::fmod(dTheta / (2.0 * M_PI) + 0.5, 1.0) - 0.5;
        };
        const double n1 = normTurns(theta2 - theta1);
        const double n2 = normTurns(theta3 - theta2);

        // Initial guess: use CD-R constants when the disc is recorded, CD-RW otherwise.
        const bool isCDR = (disc.mediaType == MediaType::CD_R);
        const double tr0_guess = isCDR ? 23349.76    : 22951.52052;
        const double dtr_guess = isCDR ? 1.389989    : 1.3865961805;

        DiscProfile result = solveRingsGeometry(tr0_guess, dtr_guess, r0_actual, n1, n2);
        result.r0         = r0_actual;
        result.discId     = disc.discId;
        result.mediaType  = disc.mediaType;
        result.layerCount = (disc.mediaType == MediaType::DVD_DL) ? 2 : 1;
        emit progressChanged(100);
        emit finished(result);
        return;
    }

    // Gradient path: infer tr0/dtr from r0 alone using ECMA-130 geometry.
    const double rDataEndMm   = 58.0;
    const double spanMm       = rDataEndMm - r0_actual;
    const int    totalSectors = 336100;
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
