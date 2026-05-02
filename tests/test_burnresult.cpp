#include "test_burnresult.h"
#include "../src/idiscbackend.h"
#include <QTest>
#include <QStandardPaths>
#include <QProcess>

void TestBurnResult::burn_with_missing_cdrecord_returns_failure() {
    if (!QStandardPaths::findExecutable("cdrecord").isEmpty())
        QSKIP("cdrecord is installed on this host; cannot test missing-binary path");

    QScopedPointer<IDiscBackend> backend(createDiscBackend());
    BurnResult r = backend->burnTestPattern("/dev/null", "/tmp/cdimage_nonexistent.cdr");
    QVERIFY(!r.succeeded());
    QVERIFY(!r.started);
    QVERIFY(!r.errorMessage.isEmpty());
}
