#ifndef WINDOWSDISCBACKEND_H
#define WINDOWSDISCBACKEND_H

#include "idiscbackend.h"

class WindowsDiscBackend : public IDiscBackend {
public:
    QStringList     availableDevices()                                        override;
    RawDiscInfo     queryDisc(const QString& devicePath)                      override;
    BurnResult      burnTestPattern(const QString& devicePath,
                                    const QString& trackFile)                 override;
    QVector<qint64> measureSeekTimes(const QString& devicePath,
                                     const QVector<qint64>& sectors)          override;
private:
    void*       openDevice(const QString& path);  // returns HANDLE cast to void*
    bool        sendCommand(void* handle, unsigned char* cdb, int cdbLen,
                            unsigned char* buf, int bufLen);
    RawDiscInfo readDiscInfo(void* handle);
    RawDiscInfo readAtip(void* handle);
    MediaType   mediaTypeFromDiscTypeByte(quint8 b);
};

#endif
