#include "discdetector.h"
#include <QtConcurrent/QtConcurrent>

DiscDetector::DiscDetector(IDiscBackend* backend, ProfileDatabase* db, QObject* parent)
    : QObject(parent), m_backend(backend), m_db(db) {}

void DiscDetector::detectAsync(const QString& devicePath) {
    (void)QtConcurrent::run([this, devicePath]() {
        RawDiscInfo info;
        try {
            info = m_backend->queryDisc(devicePath);
        } catch (const std::exception& e) {
            emit detectionFailed(QString::fromStdString(e.what()));
            return;
        }
        auto profile = m_db->findById(info.discId);
        if (profile.has_value())
            emit profileFound(*profile);
        else
            emit profileNotFound(info);
    });
}
