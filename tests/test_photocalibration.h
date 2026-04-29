#ifndef TEST_PHOTOCALIBRATION_H
#define TEST_PHOTOCALIBRATION_H
#include <QObject>
class TestPhotoCalibration : public QObject {
    Q_OBJECT
private slots:
    void radialProfile_peaks_at_correct_radius();
    void findEdge_locates_transition();
    void start_extracts_geometry_from_synthetic_photo();
};
#endif
