#ifndef CREATETRACKDIALOG_H
#define CREATETRACKDIALOG_H

#include "ui_createtrackdialog.h"
#include "discprofile.h"
#include "profiledatabase.h"

class CreateTrackDialog: public QDialog, public Ui::CreateTrackDialog {
Q_OBJECT
public:
    explicit CreateTrackDialog(ProfileDatabase* db, QWidget* parent = nullptr);
    DiscProfile selectedProfile() const;

public slots:
    void selectFile();
    void loadPreset(int index);

private:
    ProfileDatabase* m_db;
};

#endif
