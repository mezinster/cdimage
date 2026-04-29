#include "drivereadbackcalibration.h"
#include <numeric>
#include <cmath>
#include <stdexcept>

DriveReadbackCalibration::DriveReadbackCalibration(IDiscBackend* backend,
                                                    const QString& devicePath,
                                                    QObject* parent)
    : ICalibrationMethod(parent), m_backend(backend), m_devicePath(devicePath) {}

// Estimates dtr (mm/sector) from consecutive seek-time deltas.
// Principle: seek time between two sectors ∝ radial distance between them.
// We normalise against the known physical data-area span (ECMA standard)
// so no reference disc is required.
double DriveReadbackCalibration::estimateDtr(const QVector<qint64>& seekDeltas,
                                              double totalSpanMm, int nSectors) {
    if (seekDeltas.isEmpty() || nSectors <= 0) return 0.0;
    const qint64 totalUs = std::accumulate(seekDeltas.begin(), seekDeltas.end(), 0LL);
    if (totalUs == 0) return 0.0;
    const int sectorsPerStep = nSectors / (seekDeltas.size() + 1);
    if (sectorsPerStep <= 0) return 0.0;
    const double mmPerUs = totalSpanMm / static_cast<double>(totalUs);
    return mmPerUs * (static_cast<double>(totalUs) / seekDeltas.size()) / sectorsPerStep;
}

void DriveReadbackCalibration::start(const RawDiscInfo& disc) {
    emit progressChanged(5);

    // ECMA-130 (CD): data area 25–58 mm; ECMA-267 (DVD): 24–58 mm
    const bool isDvd = (disc.mediaType == MediaType::DVD_R   ||
                        disc.mediaType == MediaType::DVD_RW  ||
                        disc.mediaType == MediaType::DVD_DL);
    const double r0Nominal  = isDvd ? 24.0 : 25.0;
    const double rOuterMm   = 58.0;
    const double spanMm     = rOuterMm - r0Nominal;
    const int    totalSectors = 336100;  // 74-min CD baseline

    // Sample 10 evenly-spaced points
    const int nSteps = 10;
    QVector<qint64> sectorPoints;
    sectorPoints.reserve(nSteps + 1);
    for (int i = 0; i <= nSteps; ++i)
        sectorPoints.append(static_cast<qint64>(i) * totalSectors / nSteps);

    QVector<qint64> seekTimes;
    try {
        seekTimes = m_backend->measureSeekTimes(m_devicePath, sectorPoints);
    } catch (const std::exception& e) {
        emit failed(QString("Seek measurement failed: %1").arg(e.what()));
        return;
    }
    emit progressChanged(70);

    // Build consecutive deltas (skip first entry — arbitrary starting position)
    QVector<qint64> deltas;
    for (int i = 1; i < seekTimes.size(); ++i)
        deltas.append(std::abs(seekTimes[i] - seekTimes[i-1]));

    const double dtr_actual = estimateDtr(deltas, spanMm, totalSectors);
    if (dtr_actual <= 0.0) {
        emit failed("Could not compute dtr from seek times — all seek times were zero. "
                    "The drive may not support the SEEK command.");
        return;
    }

    const double tr0_actual = r0Nominal / dtr_actual;

    DiscProfile result;
    result.discId     = disc.discId;
    result.mediaType  = disc.mediaType;
    result.r0         = r0Nominal;
    result.dtr        = dtr_actual;
    result.tr0        = tr0_actual;
    result.layerCount = isDvd && disc.mediaType == MediaType::DVD_DL ? 2 : 1;
    emit progressChanged(100);
    emit finished(result);
}
