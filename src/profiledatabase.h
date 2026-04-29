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
    void                       saveUserProfile(const DiscProfile&);
    void                       removeUserProfile(const QString& discId);

private:
    void loadBundled();
    void loadUser();
    void persist() const;

    QList<DiscProfile> m_bundled;
    QList<DiscProfile> m_user;
    QString            m_userPath;
};

#endif
