#-------------------------------------------------
#
# Project created by QtCreator 2017-05-14T12:57:44
#
#-------------------------------------------------

QT       += core gui network serialport concurrent texttospeech
CONFIG += c++11
#CONFIG += console

greaterThan(QT_MAJOR_VERSION, 4): QT += widgets

TARGET = communicationsprocessor

TEMPLATE = app

# The following define makes your compiler emit warnings if you use
# any feature of Qt which as been marked as deprecated (the exact warnings
# depend on your compiler). Please consult the documentation of the
# deprecated API in order to know how to port your code away from it.
DEFINES += QT_DEPRECATED_WARNINGS

# You can also make your code fail to compile if you use deprecated APIs.
# In order to do so, uncomment the following line.
# You can also select to disable deprecated APIs only up to a certain version of Qt.
#DEFINES += QT_DISABLE_DEPRECATED_BEFORE=0x060000    # disables all the APIs deprecated before Qt 6.0.0

SOURCES += main.cpp\
    ../ClassKeyLoader(CUI)/keyloader.cpp \
    ../ClassSignMessageGPL/classsignmessage.cpp \
    classipchatxml.cpp \
#    classlog.cpp \
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
    dialogale.cpp

HEADERS  += mainwindow.h \
    ../ClassKeyLoader(CUI)/keyloader.h \
    ../ClassSignMessageGPL/classsignmessage.h \
    ../MSCCipherLibraryClass(CUI)/cipherlibraryclass.h \
    ../MSCCipherLibraryClass(CUI)/cipherlibraryclass_global.h \
    classipchatxml.h \
#    classlog.h \
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

FORMS    += mainwindow.ui \
    ../ClassKeyLoader(CUI)/keyloader.ui \
    dialogchat.ui \
    dialoglog.ui \
    dialogmodem.ui \
    dialogradio.ui \
    dialogsettings.ui \
    dialogtcpip.ui \
    dialogudp.ui \
    dialogretryqueue.ui \
    dialogprogressbar.ui \
    dialogale.ui \

RC_ICONS = ./images/RMIIcon.ico

DISTFILES += \
    "Change Log.txt" \
    scratchFile.txt \
    temp \
    test.xml \
    RMI-GPL v3 Manual.pdf

RESOURCES += \
    rmiresources.qrc

INCLUDEPATH += $$PWD/../MSCCipherLibraryClass(CUI)
DEPENDPATH += $$PWD/../MSCCipherLibraryClass(CUI)



#Qt5
win32:CONFIG(release, debug|release): LIBS += -L$$PWD/../build-MSCCipherLibraryClass64-Desktop_Qt_5_15_2_MinGW_64_bit/release/ -lMSCCipherLibrary64
else:win32:CONFIG(debug, debug|release): LIBS += -L$$PWD/../build-MSCCipherLibraryClass64-Desktop_Qt_5_15_2_MinGW_64_bit/debug/ -lMSCCipherLibrary64

#Qt6
#win32:CONFIG(release, debug|release): LIBS += -L$$PWD/../build-MSCCipherLibraryClass64-Desktop_Qt_6_6_0_MinGW_64_bit/release/ -lMSCCipherLibrary64
#else:win32:CONFIG(debug, debug|release): LIBS += -L$$PWD/../build-MSCCipherLibraryClass64-Desktop_Qt_6_6_0_MinGW_64_bit/debug/ -lMSCCipherLibrary64


