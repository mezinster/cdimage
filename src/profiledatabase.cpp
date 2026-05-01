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

bool ProfileDatabase::saveUserProfile(const DiscProfile& profile) {
    const QList<DiscProfile> snapshot = m_user;
    m_user.removeIf([&](const DiscProfile& p){ return p.discId == profile.discId; });
    m_user.append(profile);
    if (!persist()) {
        m_user = snapshot;
        return false;
    }
    return true;
}

bool ProfileDatabase::removeUserProfile(const QString& discId) {
    const QList<DiscProfile> snapshot = m_user;
    m_user.removeIf([&](const DiscProfile& p){ return p.discId == discId; });
    if (!persist()) {
        m_user = snapshot;
        return false;
    }
    return true;
}

bool ProfileDatabase::persist() {
    const QString dir = QFileInfo(m_userPath).absolutePath();
    if (!QDir().mkpath(dir)) {
        emit saveFailed(QStringLiteral("Cannot create directory: %1").arg(dir));
        return false;
    }
    QFile f(m_userPath);
    if (!f.open(QIODevice::WriteOnly)) {
        emit saveFailed(QStringLiteral("Cannot open %1 for writing: %2")
                        .arg(m_userPath, f.errorString()));
        return false;
    }
    QJsonArray arr;
    for (const auto& p : m_user)
        arr.append(toJson(p));
    const QByteArray bytes = QJsonDocument(arr).toJson();
    if (f.write(bytes) != bytes.size()) {
        emit saveFailed(QStringLiteral("Short write to %1: %2")
                        .arg(m_userPath, f.errorString()));
        return false;
    }
    return true;
}
