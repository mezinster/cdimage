#include "test_profiledatabase.h"
#include "../src/profiledatabase.h"
#include <QTest>
#include <QTemporaryFile>

void TestProfileDatabase::bundled_presets_loaded() {
    ProfileDatabase db;
    QVERIFY(db.allProfiles().size() >= 4);
}

void TestProfileDatabase::findById_returns_nullopt_for_empty_id() {
    ProfileDatabase db;
    QVERIFY(!db.findById("").has_value());
}

void TestProfileDatabase::findById_finds_user_profile() {
    QTemporaryFile tmp; tmp.open();
    ProfileDatabase db(tmp.fileName());
    DiscProfile p; p.discId = "test123"; p.name = "My Disc";
    db.saveUserProfile(p);

    auto found = db.findById("test123");
    QVERIFY(found.has_value());
    QCOMPARE(found->name, QString("My Disc"));
}

void TestProfileDatabase::user_profile_overrides_bundled() {
    QTemporaryFile tmp; tmp.open();
    ProfileDatabase db(tmp.fileName());
    DiscProfile p; p.discId = "override_id"; p.name = "User Override"; p.tr0 = 99999.0;
    db.saveUserProfile(p);

    auto found = db.findById("override_id");
    QVERIFY(found.has_value());
    QCOMPARE(found->tr0, 99999.0);
}

void TestProfileDatabase::remove_user_profile() {
    QTemporaryFile tmp; tmp.open();
    ProfileDatabase db(tmp.fileName());
    DiscProfile p; p.discId = "to_remove"; p.name = "Remove Me";
    db.saveUserProfile(p);
    QVERIFY(db.findById("to_remove").has_value());

    db.removeUserProfile("to_remove");
    QVERIFY(!db.findById("to_remove").has_value());
}
