#include "profiledatabase.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>

ProfileDatabase::ProfileDatabase(const QString& userProfilePath, QObject* parent)
    : QObject(parent)
{
    if (userProfilePath.isEmpty())
        m_userPath = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
                     + "/profiles.json";
    else
        m_userPath = userProfilePath;
    loadBundled();
    loadUser();
}

void ProfileDatabase::loadBundled() {
    QFile f(":/profiles/default_profiles.json");
    if (!f.open(QIODevice::ReadOnly)) return;
    const QJsonArray arr = QJsonDocument::fromJson(f.readAll()).array();
    for (const QJsonValue& v : arr)
        m_bundled.append(fromJson(v.toObject()));
}

void ProfileDatabase::loadUser() {
    QFile f(m_userPath);
    if (!f.open(QIODevice::ReadOnly)) return;
    const QJsonArray arr = QJsonDocument::fromJson(f.readAll()).array();
    for (const QJsonValue& v : arr)
        m_user.append(fromJson(v.toObject()));
}

std::optional<DiscProfile> ProfileDatabase::findById(const QString& discId) const {
    if (discId.isEmpty()) return std::nullopt;
    for (const auto& p : m_user)
        if (p.discId == discId) return p;
    for (const auto& p : m_bundled)
        if (p.discId == discId) return p;
    return std::nullopt;
}

QList<DiscProfile> ProfileDatabase::allProfiles() const {
    QList<DiscProfile> result = m_user;
    for (const auto& p : m_bundled)
        result.append(p);
    return result;
}

void ProfileDatabase::saveUserProfile(const DiscProfile& profile) {
    m_user.removeIf([&](const DiscProfile& p){ return p.discId == profile.discId; });
    m_user.append(profile);
    persist();
}

void ProfileDatabase::removeUserProfile(const QString& discId) {
    m_user.removeIf([&](const DiscProfile& p){ return p.discId == discId; });
    persist();
}

void ProfileDatabase::persist() const {
    QDir().mkpath(QFileInfo(m_userPath).absolutePath());
    QFile f(m_userPath);
    if (!f.open(QIODevice::WriteOnly)) return;
    QJsonArray arr;
    for (const auto& p : m_user)
        arr.append(toJson(p));
    f.write(QJsonDocument(arr).toJson());
}
