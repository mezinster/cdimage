#ifndef PHOTOCALIBRATION_H
#define PHOTOCALIBRATION_H

#include "icalibrationmethod.h"
#include <QString>
#include <QImage>
#include <QVector>

class PhotoCalibration : public ICalibrationMethod {
    Q_OBJECT
public:
    enum class PatternType {
        Gradient,  // radial gradient — edge-detection based, measures r0 only
        Rings      // concentric rings — phase-measurement based, measures tr0+dtr+r0
    };

    explicit PhotoCalibration(const QString& photoPath,
                               PatternType pattern = PatternType::Rings,
                               QObject* parent = nullptr);
    void start(const RawDiscInfo& disc) override;

    // Exposed for unit testing
    static QVector<double> radialProfile(const QImage& img, int cx, int cy, int maxR);
    static double          findEdge(const QVector<double>& profile, double threshold);

    // Find the angular position (radians, 0..2π) of the ring's reference notch.
    // Uses maximum-deviation-from-mean so it works on both the source image
    // (dark notch in bright ring) and on burned-disc photos (bright notch in dark ring).
    static double          findRingPhase(const QImage& img, double cx, double cy,
                                         double ringRadius_px);

    // Newton-Raphson solver translated from upstream solver.py (arduinocelentano/cdimage#29).
    // Finds tr0/dtr corrections such that D(rs2)-D(rs1)=n1 and D(rs3)-D(rs2)=n2,
    // where n1/n2 are the observed inter-ring phase shifts in fractional turns.
    static DiscProfile     solveRingsGeometry(double tr0_guess, double dtr_guess,
                                              double r0, double n1, double n2);

private:
    QString     m_photoPath;
    PatternType m_pattern;
};

#endif
