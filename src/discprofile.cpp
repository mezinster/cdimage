#include "discprofile.h"
#include <QJsonObject>

QJsonObject toJson(const DiscProfile& p) {
    QJsonObject o;
    o["discId"]           = p.discId;
    o["name"]             = p.name;
    o["mediaType"]        = static_cast<int>(p.mediaType);
    o["tr0"]              = p.tr0;
    o["dtr"]              = p.dtr;
    o["r0"]               = p.r0;
    o["layerCount"]       = p.layerCount;
    o["layerBreakSector"] = static_cast<double>(p.layerBreakSector);
    o["mixColors"]        = p.mixColors;
    return o;
}

DiscProfile fromJson(const QJsonObject& o) {
    DiscProfile p;
    p.discId           = o["discId"].toString();
    p.name             = o["name"].toString();
    p.mediaType        = static_cast<MediaType>(o["mediaType"].toInt());
    p.tr0              = o["tr0"].toDouble(22951.52052);
    p.dtr              = o["dtr"].toDouble(1.3865961805);
    p.r0               = o["r0"].toDouble(24.5);
    p.layerCount       = o["layerCount"].toInt(1);
    p.layerBreakSector = static_cast<qint64>(o["layerBreakSector"].toDouble(0));
    p.mixColors        = o["mixColors"].toBool(false);
    return p;
}
