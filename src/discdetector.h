#ifndef DISCDETECTOR_H
#define DISCDETECTOR_H

#include "discprofile.h"
#include "idiscbackend.h"
#include "profiledatabase.h"
#include <QObject>

class DiscDetector : public QObject {
    Q_OBJECT
public:
    explicit DiscDetector(IDiscBackend* backend, ProfileDatabase* db,
                          QObject* parent = nullptr);
    void detectAsync(const QString& devicePath);

signals:
    void profileFound(DiscProfile);
    void profileNotFound(RawDiscInfo);
    void detectionFailed(QString error);

private:
    IDiscBackend*    m_backend;
    ProfileDatabase* m_db;
};

#endif
