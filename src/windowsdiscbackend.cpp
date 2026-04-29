#include "windowsdiscbackend.h"

#include <QProcess>

#include <windows.h>
#include <ntddscsi.h>
#include <cstring>
#include <stdexcept>

QStringList WindowsDiscBackend::availableDevices() {
    QStringList result;
    DWORD drives = GetLogicalDrives();
    for (int i = 0; i < 26; ++i) {
        if (!(drives & (1 << i))) continue;
        QString letter = QString("%1:\\").arg(QChar('A' + i));
        if (GetDriveTypeW(reinterpret_cast<LPCWSTR>(letter.utf16())) == DRIVE_CDROM)
            result << QString("\\\\.\\%1:").arg(QChar('A' + i));
    }
    return result;
}

void* WindowsDiscBackend::openDevice(const QString& path) {
    HANDLE h = CreateFileW(
        reinterpret_cast<LPCWSTR>(path.utf16()),
        GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        nullptr, OPEN_EXISTING, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE)
        throw std::runtime_error("Cannot open device: " + path.toStdString());
    return static_cast<void*>(h);
}

bool WindowsDiscBackend::sendCommand(void* handle, unsigned char* cdb, int cdbLen,
                                     unsigned char* buf, int bufLen) {
    SCSI_PASS_THROUGH_DIRECT sptd;
    std::memset(&sptd, 0, sizeof(sptd));
    sptd.Length             = sizeof(SCSI_PASS_THROUGH_DIRECT);
    sptd.CdbLength          = static_cast<UCHAR>(cdbLen);
    sptd.DataIn             = SCSI_IOCTL_DATA_IN;
    sptd.DataTransferLength = static_cast<ULONG>(bufLen);
    sptd.DataBuffer         = buf;
    sptd.TimeOutValue       = 5;
    std::memcpy(sptd.Cdb, cdb, cdbLen);

    DWORD returned = 0;
    return DeviceIoControl(
        static_cast<HANDLE>(handle),
        IOCTL_SCSI_PASS_THROUGH_DIRECT,
        &sptd, sizeof(sptd), &sptd, sizeof(sptd),
        &returned, nullptr) != 0;
}

RawDiscInfo WindowsDiscBackend::readDiscInfo(void* handle) {
    unsigned char cdb[10] = {0x51, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 34, 0x00};
    unsigned char buf[34] = {};
    if (!sendCommand(handle, cdb, sizeof(cdb), buf, sizeof(buf)))
        throw std::runtime_error("READ DISC INFORMATION failed");
    RawDiscInfo info;
    info.mediaType = mediaTypeFromDiscTypeByte(buf[8]);
    return info;
}

RawDiscInfo WindowsDiscBackend::readAtip(void* handle) {
    unsigned char cdb[10] = {0x43, 0x02, 0x04, 0, 0, 0, 0, 0, 28, 0};
    unsigned char buf[28] = {};
    if (!sendCommand(handle, cdb, sizeof(cdb), buf, sizeof(buf)))
        throw std::runtime_error("READ ATIP failed");
    const quint8 mm = buf[4] & 0x7F;
    const quint8 ss = buf[5];
    const quint8 ff = buf[6];
    RawDiscInfo info;
    info.discId = QString("%1m%2s%3f")
                  .arg(mm, 2, 10, QChar('0'))
                  .arg(ss, 2, 10, QChar('0'))
                  .arg(ff, 2, 10, QChar('0'));
    return info;
}

MediaType WindowsDiscBackend::mediaTypeFromDiscTypeByte(quint8 b) {
    switch (b) {
        case 0x00: return MediaType::CD_R;
        case 0x20: return MediaType::CD_R;
        case 0x21: return MediaType::CD_RW;
        case 0x12: return MediaType::DVD_R;
        case 0x13: return MediaType::DVD_RW;
        case 0x1A: return MediaType::DVD_RW;
        case 0x1B: return MediaType::DVD_R;
        case 0x2B: return MediaType::DVD_DL;
        default:   return MediaType::CD_R;
    }
}

RawDiscInfo WindowsDiscBackend::queryDisc(const QString& devicePath) {
    HANDLE h = static_cast<HANDLE>(openDevice(devicePath));
    RawDiscInfo info;
    try { info = readDiscInfo(h); } catch (...) {}
    if (info.mediaType == MediaType::CD_R || info.mediaType == MediaType::CD_RW) {
        try {
            RawDiscInfo atip = readAtip(h);
            info.discId = atip.discId;
        } catch (...) {}
    }
    CloseHandle(h);
    return info;
}

bool WindowsDiscBackend::burnTestPattern(const QString& devicePath,
                                         const QString& trackFile) {
    // devicePath is e.g. "\\.\D:" — extract drive letter for cdrecord
    const QChar driveLetter = devicePath.at(4);
    QProcess proc;
    proc.start("cdrecord", {"-audio",
                            QString("dev=%1:").arg(driveLetter),
                            trackFile});
    proc.waitForFinished(300000);
    return proc.exitCode() == 0;
}

QVector<qint64> WindowsDiscBackend::measureSeekTimes(const QString& devicePath,
                                                      const QVector<qint64>& sectors) {
    HANDLE h = static_cast<HANDLE>(openDevice(devicePath));
    QVector<qint64> times;
    times.reserve(sectors.size());

    LARGE_INTEGER freq;
    QueryPerformanceFrequency(&freq);

    for (qint64 sector : sectors) {
        const qint64 lba = sector + 150;
        CDROM_SEEK_AUDIO_MSF seek;
        seek.M = static_cast<UCHAR>(lba / 4500);
        seek.S = static_cast<UCHAR>((lba % 4500) / 75);
        seek.F = static_cast<UCHAR>(lba % 75);

        LARGE_INTEGER t0, t1;
        DWORD returned = 0;
        QueryPerformanceCounter(&t0);
        DeviceIoControl(h, IOCTL_CDROM_SEEK_AUDIO_MSF, &seek, sizeof(seek),
                        nullptr, 0, &returned, nullptr);
        QueryPerformanceCounter(&t1);

        times.append((t1.QuadPart - t0.QuadPart) * 1000000LL / freq.QuadPart);
    }
    CloseHandle(h);
    return times;
}

IDiscBackend* createDiscBackend() { return new WindowsDiscBackend(); }
