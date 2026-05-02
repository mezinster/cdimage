#include <QCoreApplication>
#include <QTest>
#include "test_discprofile.h"
#include "test_profiledatabase.h"
#include "test_discdetector.h"
#include "test_photocalibration.h"
#include "test_burnresult.h"

int main(int argc, char* argv[]) {
    QCoreApplication app(argc, argv);
    app.setApplicationName("cdimage_tests");
    int status = 0;
    auto run = [&](QObject* t){ status |= QTest::qExec(t, argc, argv); delete t; };
    run(new TestDiscProfile);
    run(new TestProfileDatabase);
    run(new TestDiscDetector);
    run(new TestPhotoCalibration);
    run(new TestBurnResult);
    return status;
}
