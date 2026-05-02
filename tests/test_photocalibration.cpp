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
    // Synthesise a step-edge disc photo with realistic CD proportions so the
    // gradient-path edge detection has unambiguous transitions to lock onto.
    // Real CDs have inner data start ~24.5 mm and outer data edge ~60 mm. We
    // map outer=60 mm to 950 px, so px-per-mm ≈ 15.83 and inner→24.5 mm at
    // 388 px. Three zones: black hub, mid-grey data, white background.
    const int    size       = 2000;
    const double cx         = size / 2.0;
    const double cy         = size / 2.0;
    const double rHub_px    = 388.0;
    const double rOuter_px  = 950.0;
    QImage syntheticPhoto(size, size, QImage::Format_RGB32);
    syntheticPhoto.fill(qRgb(255, 255, 255));
    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            const double r = std::sqrt((x-cx)*(x-cx) + (y-cy)*(y-cy));
            int gray;
            if      (r < rHub_px)   gray = 0;
            else if (r < rOuter_px) gray = 128;
            else                    gray = 255;
            syntheticPhoto.setPixel(x, y, qRgb(gray, gray, gray));
        }
    }

    QTemporaryFile tmp;
    tmp.setFileTemplate(QDir::tempPath() + "/photo_XXXXXX.png");
    tmp.open();
    syntheticPhoto.save(tmp.fileName());

    RawDiscInfo disc; disc.discId = "synth"; disc.mediaType = MediaType::CD_RW;
    PhotoCalibration cal(tmp.fileName(), PhotoCalibration::PatternType::Gradient);
    QSignalSpy doneSpy(&cal, &PhotoCalibration::finished);
    QSignalSpy failSpy(&cal, &PhotoCalibration::failed);

    cal.start(disc);

    if (failSpy.count() > 0)
        QFAIL(failSpy[0][0].toString().toLocal8Bit().constData());
    QCOMPARE(doneSpy.count(), 1);

    DiscProfile result = doneSpy[0][0].value<DiscProfile>();
    // Expected r0 ≈ 24.5 mm; allow ±1.5 mm for sub-pixel edge interpolation.
    QVERIFY2(result.r0 > 23.0 && result.r0 < 26.0,
             qPrintable(QString("r0=%1 outside expected 23-26 mm range").arg(result.r0)));
    QVERIFY(result.dtr > 0.0);
}

void TestPhotoCalibration::findRingPhase_locates_notch_in_generated_image() {
    // generateRingsImage places a 20° notch centred at angle 0 (rightward).
    const QImage img = TestPatternGenerator::generateRingsImage(600);
    const double cx      = 300.0;
    const double cy      = 300.0;
    const double pxPerMm = 300.0 / 58.0;
    const double rs1_px  = (24.5 + 4.0) * pxPerMm;   // innermost ring

    const double phase = PhotoCalibration::findRingPhase(img, cx, cy, rs1_px);

    // Allow ±15° (half the notch width plus measurement tolerance).
    const double tol = 15.0 * M_PI / 180.0;
    const bool nearZero    = std::abs(phase)             < tol;
    const bool nearTwoPi   = std::abs(phase - 2*M_PI)   < tol;
    QVERIFY2(nearZero || nearTwoPi, "findRingPhase did not locate notch near angle 0");
}

void TestPhotoCalibration::solveRingsGeometry_identity_for_zero_offsets() {
    // n1=n2=0 means no correction needed; solver should return the guess unchanged.
    const double tr0 = 22951.26, dtr = 1.38659585;
    const DiscProfile result =
        PhotoCalibration::solveRingsGeometry(tr0, dtr, 24.5, 0.0, 0.0);
    QVERIFY(std::abs(result.tr0 - tr0) < 1e-6);
    QVERIFY(std::abs(result.dtr - dtr) < 1e-12);
}

void TestPhotoCalibration::solveRingsGeometry_modifies_params_for_nonzero_offsets() {
    // Non-zero n1, n2 should cause the solver to adjust at least one parameter.
    const double tr0 = 22951.26, dtr = 1.38659585;
    const DiscProfile result =
        PhotoCalibration::solveRingsGeometry(tr0, dtr, 24.5, 0.01, 0.005);
    const bool tr0Changed = std::abs(result.tr0 - tr0) > 0.01;
    const bool dtrChanged = std::abs(result.dtr - dtr) > 1e-7;
    QVERIFY2(tr0Changed || dtrChanged, "Solver left parameters unchanged for non-zero n1/n2");
}
