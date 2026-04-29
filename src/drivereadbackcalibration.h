#ifndef DRIVEREADBACKCALIBRATION_H
#define DRIVEREADBACKCALIBRATION_H

#include "icalibrationmethod.h"

class DriveReadbackCalibration : public ICalibrationMethod {
    Q_OBJECT
public:
    explicit DriveReadbackCalibration(IDiscBackend* backend,
                                       const QString& devicePath,
                                       QObject* parent = nullptr);
    void start(const RawDiscInfo& disc) override;

    // Exposed for unit testing
    static double estimateDtr(const QVector<qint64>& seekDeltas,
                               double totalSpanMm, int nSectors);

private:
    IDiscBackend* m_backend;
    QString       m_devicePath;
};

#endif
