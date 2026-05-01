TEMPLATE = app
TARGET   = cdimage_tests
CONFIG  += testcase
QT      += testlib widgets concurrent

INCLUDEPATH += ..

HEADERS += test_discprofile.h \
           test_profiledatabase.h \
           test_discdetector.h \
           test_photocalibration.h \
           mockdiscbackend.h \
           ../src/converter.h \
           ../src/profiledatabase.h \
           ../src/icalibrationmethod.h \
           ../src/discdetector.h \
           ../src/photocalibration.h

SOURCES += main.cpp \
           test_discprofile.cpp \
           test_profiledatabase.cpp \
           test_discdetector.cpp \
           test_photocalibration.cpp \
           ../src/discprofile.cpp \
           ../src/profiledatabase.cpp \
           ../src/discdetector.cpp \
           ../src/photocalibration.cpp \
           ../src/testpatterngenerator.cpp \
           ../src/converter.cpp

RESOURCES += ../resources/profiles.qrc
