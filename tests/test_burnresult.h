#ifndef TEST_BURNRESULT_H
#define TEST_BURNRESULT_H
#include <QObject>
class TestBurnResult : public QObject {
    Q_OBJECT
private slots:
    void burn_with_missing_cdrecord_returns_failure();
};
#endif
