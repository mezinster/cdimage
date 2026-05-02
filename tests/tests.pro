TEMPLATE = app
TARGET   = cdimage_tests
CONFIG  += testcase
QT      += testlib widgets concurrent

INCLUDEPATH += ..

HEADERS += test_discprofile.h \
           test_profiledatabase.h \
           test_discdetector.h \
           test_photocalibration.h \
           test_burnresult.h \
           test_converter_wav.h \
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
           test_burnresult.cpp \
           test_converter_wav.cpp \
           ../src/discprofile.cpp \
           ../src/profiledatabase.cpp \
           ../src/discdetector.cpp \
           ../src/photocalibration.cpp \
           ../src/testpatterngenerator.cpp \
           ../src/converter.cpp

unix:SOURCES  += ../src/linuxdiscbackend.cpp
unix:HEADERS  += ../src/linuxdiscbackend.h
win32:SOURCES += ../src/windowsdiscbackend.cpp
win32:HEADERS += ../src/windowsdiscbackend.h
win32:LIBS    += -lole32 -loleaut32 -lshlwapi -luuid

RESOURCES += ../resources/profiles.qrc
