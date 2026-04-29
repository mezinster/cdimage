#include "test_discdetector.h"
#include "../src/discdetector.h"
#include "../src/profiledatabase.h"
#include "mockdiscbackend.h"
#include <QTest>
#include <QSignalSpy>
#include <QTemporaryFile>

static const int s_rawDiscInfoMetatype  = qRegisterMetaType<RawDiscInfo>("RawDiscInfo");
static const int s_discProfileMetatype  = qRegisterMetaType<DiscProfile>("DiscProfile");

void TestDiscDetector::emits_profileFound_when_disc_in_db() {
    MockDiscBackend backend;
    backend.m_discInfo.discId     = "known_disc";
    backend.m_discInfo.mediaType  = MediaType::CD_RW;

    QTemporaryFile tmp; tmp.open();
    ProfileDatabase db(tmp.fileName());
    DiscProfile p; p.discId = "known_disc"; p.name = "My CD-RW";
    db.saveUserProfile(p);

    DiscDetector detector(&backend, &db);
    QSignalSpy found(&detector, &DiscDetector::profileFound);
    QSignalSpy failed(&detector, &DiscDetector::detectionFailed);

    detector.detectAsync("/dev/mock");
    QVERIFY(found.wait(2000));
    QCOMPARE(found.count(), 1);
    QCOMPARE(failed.count(), 0);
    QCOMPARE(found[0][0].value<DiscProfile>().discId, QString("known_disc"));
}

void TestDiscDetector::emits_profileNotFound_when_disc_unknown() {
    MockDiscBackend backend;
    backend.m_discInfo.discId = "unknown_disc";

    QTemporaryFile tmp; tmp.open();
    ProfileDatabase db(tmp.fileName());

    DiscDetector detector(&backend, &db);
    QSignalSpy notFound(&detector, &DiscDetector::profileNotFound);

    detector.detectAsync("/dev/mock");
    QVERIFY(notFound.wait(2000));
    QCOMPARE(notFound.count(), 1);
    QCOMPARE(notFound[0][0].value<RawDiscInfo>().discId, QString("unknown_disc"));
}

void TestDiscDetector::emits_detectionFailed_on_backend_error() {
    MockDiscBackend backend;
    backend.m_queryFails = true;

    QTemporaryFile tmp; tmp.open();
    ProfileDatabase db(tmp.fileName());

    DiscDetector detector(&backend, &db);
    QSignalSpy failed(&detector, &DiscDetector::detectionFailed);

    detector.detectAsync("/dev/mock");
    QVERIFY(failed.wait(2000));
    QCOMPARE(failed.count(), 1);
}
