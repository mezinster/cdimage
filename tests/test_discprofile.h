#ifndef TEST_DISCPROFILE_H
#define TEST_DISCPROFILE_H
#include <QObject>
class TestDiscProfile : public QObject {
    Q_OBJECT
private slots:
    void roundtrip_preserves_all_fields();
    void fromJson_uses_defaults_for_missing_fields();
    void dvd_dl_roundtrip();
};
#endif
