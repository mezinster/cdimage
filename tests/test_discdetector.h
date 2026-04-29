#ifndef TEST_DISCDETECTOR_H
#define TEST_DISCDETECTOR_H
#include <QObject>
class TestDiscDetector : public QObject {
    Q_OBJECT
private slots:
    void emits_profileFound_when_disc_in_db();
    void emits_profileNotFound_when_disc_unknown();
    void emits_detectionFailed_on_backend_error();
};
#endif
