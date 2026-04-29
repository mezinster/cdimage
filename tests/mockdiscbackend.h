#ifndef MOCKDISCBACKEND_H
#define MOCKDISCBACKEND_H

#include "../src/idiscbackend.h"

class MockDiscBackend : public IDiscBackend {
public:
    RawDiscInfo     m_discInfo;
    QVector<qint64> m_seekTimes;
    bool            m_queryFails = false;

    QStringList availableDevices() override { return {"/dev/mock"}; }

    RawDiscInfo queryDisc(const QString&) override {
        if (m_queryFails) throw std::runtime_error("mock error");
        return m_discInfo;
    }

    bool burnTestPattern(const QString&, const QString&) override { return true; }

    QVector<qint64> measureSeekTimes(const QString&,
                                     const QVector<qint64>& sectors) override {
        return m_seekTimes.isEmpty()
               ? QVector<qint64>(sectors.size(), 100LL)
               : m_seekTimes;
    }
};

#endif
