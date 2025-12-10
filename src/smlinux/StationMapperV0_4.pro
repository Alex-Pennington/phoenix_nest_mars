QT += core widgets network
QT += charts
QT += printsupport
requires(qtConfig(combobox))

DEFINES += QT_MESSAGELOGCONTEXT

HEADERS       = renderarea.h \
                classaddresslookup.h \
                classauthtable.h \
                classchan.h \
                classdb.h \
                classfile.h \
                classgetlightning.h \
                classmetar.h \
                classroster.h \
                classsunspot.h \
                classweather.h \
                classxml.h \
                currentinfo.h \
                dbcreator.h \
                debug.h \
                dlgauthform.h \
                dlgdatabaseeditor.h \
                dlgpoppler.h \
                dlgsettingstab.h \
                dlgterminal.h \
                globals.h \
                highlighter.h \
                imageviewer.h \
                main.h \
                mainwindowtab.h \
                tabtraffic.h \
                updatecheck.h \
                version.h \
                window.h
SOURCES       = main.cpp \
                classaddresslookup.cpp \
                classauthtable.cpp \
                classchan.cpp \
                classdb.cpp \
                classfile.cpp \
                classgetlightning.cpp \
                classmetar.cpp \
                classroster.cpp \
                classsunspot.cpp \
                classweather.cpp \
                classxml.cpp \
                currentinfo.cpp \
                dbcreator.cpp \
                dlgauthform.cpp \
                dlgdatabaseeditor.cpp \
                dlgpoppler.cpp \
                dlgsettingstab.cpp \
                dlgterminal.cpp \
                highlighter.cpp \
                imageviewer.cpp \
                mainwindowtab.cpp \
                renderarea.cpp \
                tabtraffic.cpp \
                updatecheck.cpp \
                window.cpp
RESOURCES     = \
    images.qrc

TARGET=StationMapper

# install
target.path = D:/StationMapper-Build/basicdrawing/builds/
INSTALLS += target

# win32: LIBS += -L$$PWD/3rdParty/lib/ -llibGeographic.dll
# win32: LIBS += -L$$PWD/3rdParty/lib/ -lproj_5_2
win32: LIBS += -L$$PWD/3rdParty/lib/ -lhunspell-1.7.dll
win32: LIBS += -LC:/OpenSSL-Win64/lib -llibeay32
win32: LIBS += -LC:/OpenSSL-Win64/lib -lssleay32

INCLUDEPATH += C:/OpenSSL-Win64/include
INCLUDEPATH += $$PWD/3rdParty/include
DEPENDPATH += $$PWD/3rdParty/include

DISTFILES +=

RC_FILE = $$PWD/resources/resources.rc

FORMS += \
    dlgterminal.ui
QMAKE_CXXFLAGS -= -Wunused-parameter
QMAKE_CXXFLAGS -= -Wdeprecated-declarations
QMAKE_CXXFLAGS -= -Wunused-variable
QMAKE_CXXFLAGS += -Wno-unused-parameter
QMAKE_CXXFLAGS += -Wno-deprecated-declarations
QMAKE_CXXFLAGS += -Wno-unused-variable
