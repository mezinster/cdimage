#ifndef DISCPROFILE_H
#define DISCPROFILE_H

#include <QString>
#include <QtGlobal>
#include <optional>

class QJsonObject;

enum class MediaType { CD_R, CD_RW, DVD_R, DVD_RW, DVD_DL };

struct DiscProfile {
    QString   discId;
    QString   name;
    MediaType mediaType        = MediaType::CD_RW;
    double    tr0              = 22951.52052;
    double    dtr              = 1.3865961805;
    double    r0               = 24.5;
    int       layerCount       = 1;
    qint64    layerBreakSector = 0;
    bool      mixColors        = false;
};

QJsonObject toJson(const DiscProfile&);
DiscProfile fromJson(const QJsonObject&);

#endif
