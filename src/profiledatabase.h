#ifndef PROFILEDATABASE_H
#define PROFILEDATABASE_H

#include "discprofile.h"
#include <QList>
#include <QObject>
#include <optional>

class ProfileDatabase : public QObject {
    Q_OBJECT
public:
    explicit ProfileDatabase(const QString& userProfilePath = QString(),
                             QObject* parent = nullptr);

    std::optional<DiscProfile> findById(const QString& discId) const;
    QList<DiscProfile>         allProfiles() const;
    QList<DiscProfile>         userProfiles() const    { return m_user; }
    QList<DiscProfile>         bundledProfiles() const { return m_bundled; }
    QString                    userProfilePath() const { return m_userPath; }

    bool                       saveUserProfile(const DiscProfile&);
    bool                       removeUserProfile(const QString& discId);

signals:
    void saveFailed(QString errorMessage);

private:
    void loadBundled();
    void loadUser();
    bool persist();

    QList<DiscProfile> m_bundled;
    QList<DiscProfile> m_user;
    QString            m_userPath;
};

#endif
