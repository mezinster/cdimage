#include "test_discprofile.h"
#include "../src/discprofile.h"
#include <QTest>
#include <QJsonObject>

void TestDiscProfile::roundtrip_preserves_all_fields() {
    DiscProfile p;
    p.discId = "97m24s01f"; p.name = "Test"; p.mediaType = MediaType::CD_R;
    p.tr0 = 22951.52; p.dtr = 1.3865961; p.r0 = 24.5;
    p.layerCount = 1; p.layerBreakSector = 0; p.mixColors = true;
    DiscProfile p2 = fromJson(toJson(p));
    QCOMPARE(p2.discId,            p.discId);
    QCOMPARE(p2.name,              p.name);
    QCOMPARE(p2.mediaType,         p.mediaType);
    QCOMPARE(p2.tr0,               p.tr0);
    QCOMPARE(p2.dtr,               p.dtr);
    QCOMPARE(p2.r0,                p.r0);
    QCOMPARE(p2.layerCount,        p.layerCount);
    QCOMPARE(p2.layerBreakSector,  p.layerBreakSector);
    QCOMPARE(p2.mixColors,         p.mixColors);
}

void TestDiscProfile::fromJson_uses_defaults_for_missing_fields() {
    QJsonObject o;
    o["discId"] = "test"; o["name"] = "Minimal";
    o["tr0"] = 22951.52; o["dtr"] = 1.3865961;
    DiscProfile p = fromJson(o);
    QCOMPARE(p.r0,               24.5);
    QCOMPARE(p.layerCount,       1);
    QCOMPARE(p.layerBreakSector, qint64(0));
    QCOMPARE(p.mixColors,        false);
}

void TestDiscProfile::dvd_dl_roundtrip() {
    DiscProfile p;
    p.mediaType = MediaType::DVD_DL;
    p.layerCount = 2;
    p.layerBreakSector = 1913760LL;
    DiscProfile p2 = fromJson(toJson(p));
    QCOMPARE(p2.mediaType,        MediaType::DVD_DL);
    QCOMPARE(p2.layerCount,       2);
    QCOMPARE(p2.layerBreakSector, qint64(1913760));
}
