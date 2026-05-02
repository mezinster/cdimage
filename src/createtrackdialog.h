#ifndef CREATETRACKDIALOG_H
#define CREATETRACKDIALOG_H

#include "ui_createtrackdialog.h"
#include "discprofile.h"
#include "profiledatabase.h"

class CreateTrackDialog: public QDialog, public Ui::CreateTrackDialog {
Q_OBJECT
public:
    explicit CreateTrackDialog(ProfileDatabase* db,
                               const DiscProfile& currentProfile = {},
                               QWidget* parent = nullptr);
    DiscProfile selectedProfile() const;

public slots:
    void selectFile();
    void loadPreset(int index);

private:
    void populatePresets();
    int  indexForProfile(const DiscProfile& p) const;

    ProfileDatabase*    m_db;
    QList<DiscProfile>  m_orderedProfiles;  // mirrors combo order: user first, then bundled
    DiscProfile         m_currentProfile;
};

#endif
