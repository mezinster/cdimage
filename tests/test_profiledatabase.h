#ifndef TEST_PROFILEDATABASE_H
#define TEST_PROFILEDATABASE_H
#include <QObject>
class TestProfileDatabase : public QObject {
    Q_OBJECT
private slots:
    void bundled_presets_loaded();
    void findById_returns_nullopt_for_empty_id();
    void findById_finds_user_profile();
    void user_profile_overrides_bundled();
    void remove_user_profile();
};
#endif
