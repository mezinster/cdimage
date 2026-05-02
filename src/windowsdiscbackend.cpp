#include "windowsdiscbackend.h"

#include <QProcess>
#include <QDebug>

#include <windows.h>
#include <ntddscsi.h>
#include <ntddcdrm.h>
#include <cstring>
#include <stdexcept>

#include <imapi2.h>
#include <imapi2error.h>
#include <shlwapi.h>
#include <comdef.h>

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

BurnResult WindowsDiscBackend::burnTestPattern(const QString& devicePath,
                                                const QString& trackFile) {
    BurnResult r = burnViaImapi(devicePath, trackFile);
    if (r.errorMessage.startsWith(QStringLiteral("IMAPI unavailable")))
        return burnViaCdrecord(devicePath, trackFile);
    return r;
}

BurnResult WindowsDiscBackend::burnViaCdrecord(const QString& devicePath,
                                                const QString& trackFile) {
    BurnResult r;
    if (devicePath.size() < 5) {
        r.errorMessage = QStringLiteral("Bad device path: %1").arg(devicePath);
        return r;
    }
    const QChar driveLetter = devicePath.at(4);
    QProcess proc;
    proc.setProcessChannelMode(QProcess::MergedChannels);
    proc.start("cdrecord", {"-audio",
                            QString("dev=%1:").arg(driveLetter),
                            trackFile});
    if (!proc.waitForStarted(5000)) {
        r.errorMessage = QStringLiteral(
            "Failed to start cdrecord: %1. cdrecord (cdrtools) is not installed "
            "by default on Windows. Install it (e.g. via Cygwin/Cdrtools) and "
            "add it to PATH, or burn the generated WAV with Nero/ImgBurn manually.")
            .arg(proc.errorString());
        return r;
    }
    r.started = true;
    if (!proc.waitForFinished(600000)) {
        proc.kill();
        proc.waitForFinished(2000);
        r.stderrText   = QString::fromLocal8Bit(proc.readAll());
        r.errorMessage = QStringLiteral("cdrecord did not finish within 10 minutes.");
        return r;
    }
    r.stderrText = QString::fromLocal8Bit(proc.readAll());
    if (proc.exitStatus() != QProcess::NormalExit) {
        r.errorMessage = QStringLiteral("cdrecord crashed.");
        return r;
    }
    r.finished = true;
    r.exitCode = proc.exitCode();
    if (r.exitCode != 0)
        r.errorMessage = QStringLiteral("cdrecord exited with code %1").arg(r.exitCode);
    return r;
}

BurnResult WindowsDiscBackend::burnViaImapi(const QString& devicePath,
                                             const QString& trackFile) {
    BurnResult r;
    qInfo() << "IMAPI burnViaImapi begin device=" << devicePath << "track=" << trackFile;

    // ---- 1. Initialise COM ----
    HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    qInfo() << "IMAPI CoInitializeEx hr=0x" << QString::number(hr, 16);
    const bool comInited = SUCCEEDED(hr);
    if (FAILED(hr) && hr != RPC_E_CHANGED_MODE) {
        // RPC_E_CHANGED_MODE means COM was already initialised with a different
        // model — that's fine, we can still use it.
        r.errorMessage = QStringLiteral("IMAPI unavailable: CoInitializeEx failed: %1")
                         .arg(QString::fromWCharArray(_com_error(hr).ErrorMessage()));
        return r;
    }

    IDiscMaster2*           master   = nullptr;
    IDiscRecorder2*         recorder = nullptr;
    IDiscFormat2TrackAtOnce* format  = nullptr;
    IStream*                stream   = nullptr;

    auto cleanup = [&]() {
        if (stream)   { stream->Release();   stream   = nullptr; }
        if (format)   { format->Release();   format   = nullptr; }
        if (recorder) { recorder->Release(); recorder = nullptr; }
        if (master)   { master->Release();   master   = nullptr; }
        if (comInited) CoUninitialize();
    };

    // ---- 2. IDiscMaster2 ----
    hr = CoCreateInstance(CLSID_MsftDiscMaster2, nullptr, CLSCTX_INPROC_SERVER,
                          IID_IDiscMaster2, reinterpret_cast<void**>(&master));
    if (FAILED(hr)) {
        const QString msg = QString::fromWCharArray(_com_error(hr).ErrorMessage());
        cleanup();
        r.errorMessage = QStringLiteral("IMAPI unavailable: IDiscMaster2 creation failed: %1").arg(msg);
        return r;
    }

    // ---- 3. Find recorder matching devicePath ----
    // devicePath is like "\\\\.\\D:" — the drive letter is at index 4.
    if (devicePath.size() < 5) {
        cleanup();
        r.errorMessage = QStringLiteral("Bad device path: %1").arg(devicePath);
        return r;
    }
    const QChar wantedLetter = devicePath.at(4).toUpper();

    LONG recorderCount = 0;
    master->get_Count(&recorderCount);

    for (LONG i = 0; i < recorderCount && recorder == nullptr; ++i) {
        BSTR uid = nullptr;
        if (FAILED(master->get_Item(i, &uid))) continue;

        IDiscRecorder2* tmp = nullptr;
        hr = CoCreateInstance(CLSID_MsftDiscRecorder2, nullptr, CLSCTX_INPROC_SERVER,
                              IID_IDiscRecorder2, reinterpret_cast<void**>(&tmp));
        if (FAILED(hr)) { SysFreeString(uid); continue; }

        if (SUCCEEDED(tmp->InitializeDiscRecorder(uid))) {
            SAFEARRAY* paths = nullptr;
            if (SUCCEEDED(tmp->get_VolumePathNames(&paths)) && paths != nullptr) {
                LONG lBound = 0, uBound = -1;
                SafeArrayGetLBound(paths, 1, &lBound);
                SafeArrayGetUBound(paths, 1, &uBound);
                // IDiscRecorder2::get_VolumePathNames returns a SAFEARRAY of
                // VARIANTs (each VT_BSTR), NOT a SAFEARRAY of raw BSTRs.
                // Reading via &volPath (BSTR*) made SafeArrayGetElement copy
                // sizeof(VARIANT)=24 bytes into an 8-byte slot; the first 8
                // bytes are {vt, reserved...} = 0x0000000000000008, so
                // volPath ended up = 0x8 and wcslen(0x8) AV'd at address 8
                // inside ucrtbase. Use the proper VARIANT extraction.
                for (LONG j = lBound; j <= uBound && recorder == nullptr; ++j) {
                    VARIANT v;
                    VariantInit(&v);
                    if (SUCCEEDED(SafeArrayGetElement(paths, &j, &v))) {
                        if (v.vt == VT_BSTR && v.bstrVal != nullptr &&
                            SysStringLen(v.bstrVal) > 0) {
                            QChar volLetter = QChar(
                                static_cast<ushort>(v.bstrVal[0])).toUpper();
                            qInfo() << "IMAPI recorder candidate volPath letter="
                                    << volLetter;
                            if (volLetter == wantedLetter)
                                recorder = tmp;  // keep this recorder
                        }
                        VariantClear(&v);  // frees the contained BSTR
                    }
                }
                SafeArrayDestroy(paths);
            }
        }

        SysFreeString(uid);
        if (recorder == nullptr)
            tmp->Release();
    }

    if (recorder == nullptr) {
        cleanup();
        r.errorMessage = QStringLiteral("IMAPI: no recorder found for device %1").arg(devicePath);
        qWarning() << r.errorMessage << "(scanned" << recorderCount << "recorders)";
        return r;
    }
    qInfo() << "IMAPI recorder bound for letter" << wantedLetter;

    // ---- 5. IDiscFormat2TrackAtOnce ----
    hr = CoCreateInstance(CLSID_MsftDiscFormat2TrackAtOnce, nullptr, CLSCTX_INPROC_SERVER,
                          IID_IDiscFormat2TrackAtOnce, reinterpret_cast<void**>(&format));
    if (FAILED(hr)) {
        cleanup();
        r.errorMessage = QStringLiteral("IMAPI: IDiscFormat2TrackAtOnce creation failed: %1")
                         .arg(QString::fromWCharArray(_com_error(hr).ErrorMessage()));
        return r;
    }

    // ---- 6. Attach recorder and set client name ----
    hr = format->put_Recorder(recorder);
    if (FAILED(hr)) {
        cleanup();
        r.errorMessage = QStringLiteral("IMAPI: put_Recorder failed: %1")
                         .arg(QString::fromWCharArray(_com_error(hr).ErrorMessage()));
        return r;
    }
    BSTR clientName = SysAllocString(L"CDImage");
    if (clientName) {
        format->put_ClientName(clientName);
        SysFreeString(clientName);
    }

    // ---- 7. Open IStream over the WAV file ----
    const std::wstring wTrackFile = trackFile.toStdWString();
    hr = SHCreateStreamOnFileEx(wTrackFile.c_str(),
                                STGM_READ | STGM_SHARE_DENY_WRITE,
                                0, FALSE, nullptr, &stream);
    if (FAILED(hr)) {
        cleanup();
        r.errorMessage = QStringLiteral("IMAPI: cannot open track file '%1': %2")
                         .arg(trackFile,
                              QString::fromWCharArray(_com_error(hr).ErrorMessage()));
        return r;
    }

    // Seek past the 44-byte WAV header so IMAPI sees raw PCM
    LARGE_INTEGER headerOffset;
    headerOffset.QuadPart = 44;
    stream->Seek(headerOffset, STREAM_SEEK_SET, nullptr);

    // ---- 8. PrepareMedia ----
    qInfo() << "IMAPI PrepareMedia";
    hr = format->PrepareMedia();
    if (FAILED(hr)) {
        cleanup();
        r.errorMessage = QStringLiteral("IMAPI: PrepareMedia failed: %1")
                         .arg(QString::fromWCharArray(_com_error(hr).ErrorMessage()));
        qWarning() << r.errorMessage;
        return r;
    }

    // ---- 9. AddAudioTrack ----
    qInfo() << "IMAPI AddAudioTrack starting";
    r.started = true;
    hr = format->AddAudioTrack(stream);
    qInfo() << "IMAPI AddAudioTrack hr=0x" << QString::number(hr, 16);
    if (FAILED(hr)) {
        format->ReleaseMedia();
        cleanup();
        r.errorMessage = QStringLiteral("IMAPI: AddAudioTrack failed: %1")
                         .arg(QString::fromWCharArray(_com_error(hr).ErrorMessage()));
        qWarning() << r.errorMessage;
        return r;
    }

    // ---- 10. ReleaseMedia (finalise disc) ----
    qInfo() << "IMAPI ReleaseMedia (finalize)";
    hr = format->ReleaseMedia();
    if (FAILED(hr)) {
        cleanup();
        r.errorMessage = QStringLiteral("IMAPI: ReleaseMedia failed: %1")
                         .arg(QString::fromWCharArray(_com_error(hr).ErrorMessage()));
        qWarning() << r.errorMessage;
        return r;
    }

    // ---- Success ----
    r.finished = true;
    r.exitCode = 0;
    cleanup();
    qInfo() << "IMAPI burn succeeded";
    return r;
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
