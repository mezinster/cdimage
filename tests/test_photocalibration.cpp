#include "test_photocalibration.h"
#include "../src/photocalibration.h"
#include "../src/testpatterngenerator.h"
#include <QTest>
#include <QSignalSpy>
#include <QDir>
#include <QTemporaryFile>
#include <cmath>

void TestPhotoCalibration::radialProfile_peaks_at_correct_radius() {
    QImage img(100, 100, QImage::Format_Grayscale8);
    img.fill(0);
    for (int y = 0; y < 100; ++y)
        for (int x = 0; x < 100; ++x) {
            const int r = static_cast<int>(
                std::round(std::sqrt((x-50.0)*(x-50.0) + (y-50.0)*(y-50.0))));
            if (r == 40) img.setPixel(x, y, 255);
        }
    const auto profile = PhotoCalibration::radialProfile(img, 50, 50, 50);
    int peakR = 0;
    for (int r = 1; r < profile.size(); ++r)
        if (profile[r] > profile[peakR]) peakR = r;
    QCOMPARE(peakR, 40);
}

void TestPhotoCalibration::findEdge_locates_transition() {
    QVector<double> profile(100, 0.0);
    for (int r = 50; r < 100; ++r) profile[r] = 255.0;
    const double edge = PhotoCalibration::findEdge(profile, 128.0);
    QVERIFY(std::abs(edge - 50.0) < 1.0);
}

void TestPhotoCalibration::start_extracts_geometry_from_synthetic_photo() {
    const QImage syntheticPhoto = TestPatternGenerator::generateGradientImage(2000);
    QTemporaryFile tmp;
    tmp.setFileTemplate(QDir::tempPath() + "/photo_XXXXXX.png");
    tmp.open();
    syntheticPhoto.save(tmp.fileName());

    RawDiscInfo disc; disc.discId = "synth"; disc.mediaType = MediaType::CD_RW;
    PhotoCalibration cal(tmp.fileName());
    QSignalSpy doneSpy(&cal, &PhotoCalibration::finished);
    QSignalSpy failSpy(&cal, &PhotoCalibration::failed);

    cal.start(disc);

    if (failSpy.count() > 0)
        QFAIL(failSpy[0][0].toString().toLocal8Bit().constData());
    QCOMPARE(doneSpy.count(), 1);

    DiscProfile result = doneSpy[0][0].value<DiscProfile>();
    QVERIFY(result.r0 > 20.0 && result.r0 < 30.0);
    QVERIFY(result.dtr > 0.0);
}
