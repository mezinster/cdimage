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
#include <QFutureWatcher>
#include <QPushButton>

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
    Q_OBJECT
public:
    WelcomePage(const RawDiscInfo& disc, QWidget* parent = nullptr);
protected:
    void changeEvent(QEvent* e) override;
private:
    void retranslateUi();
    RawDiscInfo m_disc;
    QLabel*     m_lbl;
};

class BurnPatternPage : public QWizardPage {
    Q_OBJECT
public:
    BurnPatternPage(IDiscBackend* backend, const RawDiscInfo& disc,
                    QWidget* parent = nullptr);
    void initializePage() override;
    bool isComplete() const override;
protected:
    void changeEvent(QEvent* e) override;
private slots:
    void doBurn();
    void onBurnFinished();
    void browseWavPath();
private:
    void retranslateUi();
    QLabel*                        m_pathLabel = nullptr;
    IDiscBackend*                  m_backend;
    RawDiscInfo                    m_disc;
    QLabel*                        m_status;
    QProgressBar*                  m_progress;
    QPushButton*                   m_burnBtn = nullptr;
    QLineEdit*                     m_lePath = nullptr;
    QPushButton*                   m_browseBtn = nullptr;
    QFutureWatcher<BurnResult>*    m_watcher = nullptr;
    bool                           m_done = false;
};

class MethodSelectPage : public QWizardPage {
    Q_OBJECT
public:
    MethodSelectPage(QWidget* parent = nullptr);
    int nextId() const override;
protected:
    void changeEvent(QEvent* e) override;
private:
    void retranslateUi();
    QRadioButton* m_rbPhoto;
    QRadioButton* m_rbReadback;
    QLabel*       m_lblPhotoHint;
    QLabel*       m_lblReadbackHint;
};

class PhotoPage : public QWizardPage {
    Q_OBJECT
public:
    PhotoPage(QWidget* parent = nullptr);
    int  nextId()    const override { return Page_Analysis; }
    bool isComplete() const override;
protected:
    void changeEvent(QEvent* e) override;
private:
    void retranslateUi();
    QLabel*    m_lblIntro;
    QLineEdit* m_path;
    QPushButton* m_browseBtn;
};

class ReadbackPage : public QWizardPage {
    Q_OBJECT
public:
    ReadbackPage(QWidget* parent = nullptr);
    void initializePage() override;
    int  nextId()    const override { return Page_Analysis; }
    bool isComplete() const override { return true; }
protected:
    void changeEvent(QEvent* e) override;
private:
    void retranslateUi();
    QLabel* m_lbl;
};

class AnalysisPage : public QWizardPage {
    Q_OBJECT
public:
    AnalysisPage(IDiscBackend* backend, const RawDiscInfo& disc,
                 QWidget* parent = nullptr);
    void initializePage() override;
    int  nextId()    const override { return Page_Result; }
    bool isComplete() const override { return m_done; }
protected:
    void changeEvent(QEvent* e) override;
private:
    void retranslateUi();
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
protected:
    void changeEvent(QEvent* e) override;
private:
    void retranslateUi();
    ProfileDatabase* m_db;
    QLabel*          m_lblTr0;
    QLabel*          m_lblDtr;
    QLabel*          m_lblR0;
    QLabel*          m_lblMediaType;
    QLineEdit*       m_leName;
    class QFormLayout* m_form = nullptr;   // re-label rows on retranslate
};

#endif
