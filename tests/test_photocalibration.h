#ifndef TEST_PHOTOCALIBRATION_H
#define TEST_PHOTOCALIBRATION_H
#include <QObject>
class TestPhotoCalibration : public QObject {
    Q_OBJECT
private slots:
    void radialProfile_peaks_at_correct_radius();
    void findEdge_locates_transition();
    void start_extracts_geometry_from_synthetic_photo();
    void findRingPhase_locates_notch_in_generated_image();
    void solveRingsGeometry_identity_for_zero_offsets();
    void solveRingsGeometry_modifies_params_for_nonzero_offsets();
};
#endif
