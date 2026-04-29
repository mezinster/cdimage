#ifndef PHOTOCALIBRATION_H
#define PHOTOCALIBRATION_H

#include "icalibrationmethod.h"
#include <QString>
#include <QImage>
#include <QVector>

class PhotoCalibration : public ICalibrationMethod {
    Q_OBJECT
public:
    explicit PhotoCalibration(const QString& photoPath,
                               QObject* parent = nullptr);
    void start(const RawDiscInfo& disc) override;

    // Exposed for unit testing
    static QVector<double> radialProfile(const QImage& img, int cx, int cy, int maxR);
    static double          findEdge(const QVector<double>& profile, double threshold);

private:
    QString m_photoPath;
};

#endif
