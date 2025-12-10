# SMLinux - Station Mapper for Linux

QT += core widgets network charts printsupport
requires(qtConfig(combobox))

DEFINES += QT_MESSAGELOGCONTEXT

TARGET = StationMapper

HEADERS = renderarea.h \
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
    heatmap.h \
    highlighter.h \
    imageviewer.h \
    main.h \
    mainwindowtab.h \
    tabtraffic.h \
    updatecheck.h \
    version.h \
    window.h

SOURCES = main.cpp \
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

RESOURCES = images.qrc

FORMS += dlgterminal.ui

unix:!macx {
    LIBS += -lssl -lcrypto
}

QMAKE_CXXFLAGS += -Wno-unused-parameter
QMAKE_CXXFLAGS += -Wno-deprecated-declarations  
QMAKE_CXXFLAGS += -Wno-unused-variable
