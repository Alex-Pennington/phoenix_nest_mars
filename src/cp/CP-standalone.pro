#-------------------------------------------------
# Communications Processor - Standalone Build
# Phoenix Nest LLC / GRSS GPL Code
# Stripped of external CUI dependencies
#-------------------------------------------------

QT += core gui network serialport concurrent
CONFIG += c++11

greaterThan(QT_MAJOR_VERSION, 4): QT += widgets

TARGET = communicationsprocessor
TEMPLATE = app

DEFINES += QT_DEPRECATED_WARNINGS

# Build without cipher library - plaintext only mode
DEFINES += NO_CIPHER_LIBRARY

SOURCES += main.cpp \
    classsignmessage.cpp \
    classipchatxml.cpp \
    dialogchat.cpp \
    mainwindow.cpp \
    dialoglog.cpp \
    dialogmodem.cpp \
    dialogsettings.cpp \
    dialogtcpip.cpp \
    dialogudp.cpp \
    tcpsocket.cpp \
    classmessage.cpp \
    classmessagequeue.cpp \
    classxml.cpp \
    dialogretryqueue.cpp \
    dialogprogressbar.cpp \
    dialogale.cpp \
    cipherlibraryclass_stub.cpp

HEADERS += mainwindow.h \
    classsignmessage.h \
    cipherlibraryclass.h \
    cipherlibraryclass_global.h \
    classipchatxml.h \
    dialogchat.h \
    dialoglog.h \
    dialogmodem.h \
    dialogsettings.h \
    dialogtcpip.h \
    dialogudp.h \
    globals.h \
    tcpsocket.h \
    classmessage.h \
    classmessagequeue.h \
    classxml.h \
    dialogretryqueue.h \
    dialogprogressbar.h \
    dialogale.h \
    version.h

FORMS += mainwindow.ui \
    dialogchat.ui \
    dialoglog.ui \
    dialogmodem.ui \
    dialogradio.ui \
    dialogsettings.ui \
    dialogtcpip.ui \
    dialogudp.ui \
    dialogretryqueue.ui \
    dialogprogressbar.ui \
    dialogale.ui

RC_ICONS = ./images/RMIIcon.ico

RESOURCES += rmiresources.qrc
