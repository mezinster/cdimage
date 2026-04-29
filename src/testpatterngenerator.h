#ifndef TESTPATTERNGENERATOR_H
#define TESTPATTERNGENERATOR_H

#include "discprofile.h"
#include <QImage>
#include <QString>

class TestPatternGenerator {
public:
    // 1000×1000 radial gradient: black at hub, white at rim, linear in between.
    // Inner 25% of radius is black (hub area), outer 5% is clamped white.
    static QImage generateGradientImage(int size = 1000);

    // Three concentric white rings at r0+{4,16,32} mm with a 20° notch at angle 0.
    // Burning this pattern and photographing the disc enables phase-measurement
    // calibration (see PhotoCalibration::PatternType::Rings).
    static QImage generateRingsImage(int size = 1200);

    // Convert the gradient image to an audio track using the given profile geometry.
    // Returns the output path on success, empty string on failure.
    static QString generateTrack(const DiscProfile& profile,
                                  const QString& outputPath);

    // Convert the rings image to an audio track. Preferred over generateTrack
    // when the disc will be analysed with PhotoCalibration::PatternType::Rings.
    static QString generateRingsTrack(const DiscProfile& profile,
                                       const QString& outputPath);
};

#endif
