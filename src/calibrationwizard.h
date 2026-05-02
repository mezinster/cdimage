#ifndef CALIBRATIONWIZARD_H
#define CALIBRATIONWIZARD_H

#include "discprofile.h"
#include "idiscbackend.h"
#include "profiledatabase.h"
#include <QWizard>
#include <QWizardPage>
#include <QLabel>
#include <QLineEdit>
#include <QRadioButton>
#include <QProgressBar>

enum CalibrationPageId {
    Page_Welcome = 0, Page_BurnPattern, Page_MethodSelect,
    Page_Photo, Page_Readback, Page_Analysis, Page_Result
};

class CalibrationWizard : public QWizard {
    Q_OBJECT
public:
    CalibrationWizard(IDiscBackend* backend, ProfileDatabase* db,
                      const RawDiscInfo& disc, QWidget* parent = nullptr);
    DiscProfile calibratedProfile() const { return m_result; }
    void setResult(const DiscProfile& p)  { m_result = p; }

private:
    IDiscBackend*    m_backend;
    ProfileDatabase* m_db;
    RawDiscInfo      m_disc;
    DiscProfile      m_result;
};

// ── Pages ─────────────────────────────────────────────────────────────────────

class WelcomePage : public QWizardPage {
public:
    WelcomePage(const RawDiscInfo& disc, QWidget* parent = nullptr);
};

class BurnPatternPage : public QWizardPage {
    Q_OBJECT
public:
    BurnPatternPage(IDiscBackend* backend, const RawDiscInfo& disc,
                    QWidget* parent = nullptr);
    void initializePage() override;
    bool isComplete() const override;
private slots:
    void doBurn();
private:
    IDiscBackend* m_backend;
    RawDiscInfo   m_disc;
    QLabel*       m_status;
    QProgressBar* m_progress;
    bool          m_done = false;
};

class MethodSelectPage : public QWizardPage {
public:
    MethodSelectPage(QWidget* parent = nullptr);
    int nextId() const override;
private:
    QRadioButton* m_rbPhoto;
    QRadioButton* m_rbReadback;
};

class PhotoPage : public QWizardPage {
public:
    PhotoPage(QWidget* parent = nullptr);
    int  nextId()    const override { return Page_Analysis; }
    bool isComplete() const override;
private:
    QLineEdit* m_path;
};

class ReadbackPage : public QWizardPage {
public:
    ReadbackPage(QWidget* parent = nullptr);
    void initializePage() override;
    int  nextId()    const override { return Page_Analysis; }
    bool isComplete() const override { return true; }
};

class AnalysisPage : public QWizardPage {
    Q_OBJECT
public:
    AnalysisPage(IDiscBackend* backend, const RawDiscInfo& disc,
                 QWidget* parent = nullptr);
    void initializePage() override;
    int  nextId()    const override { return Page_Result; }
    bool isComplete() const override { return m_done; }
private:
    IDiscBackend* m_backend;
    RawDiscInfo   m_disc;
    QProgressBar* m_progress;
    QLabel*       m_status;
    bool          m_done = false;
};

class ResultPage : public QWizardPage {
    Q_OBJECT
public:
    ResultPage(ProfileDatabase* db, QWidget* parent = nullptr);
    void initializePage() override;
    bool validatePage() override;
private:
    ProfileDatabase* m_db;
    QLabel*          m_lblTr0;
    QLabel*          m_lblDtr;
    QLabel*          m_lblR0;
    QLabel*          m_lblMediaType;
    QLineEdit*       m_leName;
};

#endif
