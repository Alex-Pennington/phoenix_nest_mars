/****************************************************************************
**
** Copyright (C) 2016 The Qt Company Ltd.
** Contact: https://www.qt.io/licensing/
**
** This file is part of the examples of the Qt Toolkit.
**
** $QT_BEGIN_LICENSE:BSD$
** Commercial License Usage
** Licensees holding valid commercial Qt licenses may use this file in
** accordance with the commercial license agreement provided with the
** Software or, alternatively, in accordance with the terms contained in
** a written agreement between you and The Qt Company. For licensing terms
** and conditions see https://www.qt.io/terms-conditions. For further
** information use the contact form at https://www.qt.io/contact-us.
**
** BSD License Usage
** Alternatively, you may use this file under the terms of the BSD license
** as follows:
**
** "Redistribution and use in source and binary forms, with or without
** modification, are permitted provided that the following conditions are
** met:
**   * Redistributions of source code must retain the above copyright
**     notice, this list of conditions and the following disclaimer.
**   * Redistributions in binary form must reproduce the above copyright
**     notice, this list of conditions and the following disclaimer in
**     the documentation and/or other materials provided with the
**     distribution.
**   * Neither the name of The Qt Company Ltd nor the names of its
**     contributors may be used to endorse or promote products derived
**     from this software without specific prior written permission.
**
**
** THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
** "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
** LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
** A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
** OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
** SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
** LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
** DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
** THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
** (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
** OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE."
**
** $QT_END_LICENSE$
**
****************************************************************************/

#ifndef WINDOW_H
#define WINDOW_H

#include "mainwindowtab.h"
//#include "classauthtable.h"
#include "dlgauthform.h"

#include <QWidget>
#include <QStatusBar>
#include <QTextEdit>
#include <QDialog>
#include <QFile>
#include <QTextStream>
#include <QLineEdit>
#include <QPushButton>
#include <QMenuBar>
#include <QGridLayout>
#include <QVBoxLayout>
#include <QtCharts>

//using namespace QtCharts;

//QT_BEGIN_NAMESPACE
//class QCheckBox;
//class QComboBox;
//class QLabel;
//class QSpinBox;
//class QAction;
//QT_END_NAMESPACE
//class RenderArea;
//class menuBar;



extern RenderArea *ptrRenderArea;
extern int varRelFreqSize;


class Window : public QWidget
{
    Q_OBJECT

public:
    Window();
    void updateStatusBar();
    void writeSettings();
    static QTextEdit * s_textEdit;
    static QTextEdit * r_textEdit;
    QLabel *statusLightningLbl;
    QLabel *statusSSNLbl;
    QPushButton *statusUpdateLbl;
    mainwindowtab * wMainWindowTab;
    void connectTcp();
    void readTcpData();

private:
    QThread *thread;
    RenderArea *renderArea;
    QGridLayout *mainLayout;
    QHBoxLayout *HboxToolsArea;
    QHBoxLayout *HboxToolbar;
    void readSettings();
    QStatusBar *statusBar;
    QLabel *statusLabel;
    void createStatusBar();
    QString SettingsFile;
    QMenuBar *mBar;
    QAction *selectAbout;
    QAction *selectUpdate;
    QAction *selectUpdateA;
    QAction *selectResetView;
    QAction *selectQTLicense;
    QAction *selectShowAll;
    QAction *selectShowLicense;
    QAction *selectFolders; //remove
    QAction *selectFont; //remove
    QAction *selectFontScalar2;
    QAction *selectFontScalar3;
    QAction *selectFontScalar4;
    QAction *selectFontScalar5;
    QAction *selectFontScalar6;
    QAction *selectFontScalar7;
    QAction *selectFontScalar8;
    QAction *selectFontScalar12;
    QAction *selectFontScalar16;
    QAction *selectFontScalar20;
    QAction *selectUsingZoom;
    QAction *selectDebug;
    QAction *selectDebugToFile;
    QAction *selectGotoMessages;
    QAction *selectGotoWiki;
    QAction *selectGotoPDF;
    QAction *selectVOACAP;
    QAction *selectBugReport;
    QAction *selectSetFreqALE;
    QAction *selectUploadFile;
    QTimer *timerWeather;
    QTimer *timer;
    QTimer *timerLightning;
    QLabel *lbllblDist;
    QLabel *lbllblAzi;
    QPushButton *pbChGraph;
    QPushButton *pbFqGraph;
    QPushButton *pbCoverageGraph;
    QToolBar *toolbar;
    QComboBox *cmboMap;
    QTcpSocket *pSocket;
    QAction *selectDebugAuth;
    dlgAuthForm *formAuthForm;


public slots:
    void restoreCursor();
    void callLogChecker(QString filename);
    void selectedAbout();
    void selectedUpdate();
    void selectedResetView();
    void selectedQTLicense();
    void selectedShowAll();
    void selectedShowLicense();
    void selectedFontScalar2();
    void selectedFontScalar3();
    void selectedFontScalar4();
    void selectedFontScalar5();
    void selectedFontScalar6();
    void selectedFontScalar7();
    void selectedFontScalar8();
    void selectedFontScalar12();
    void selectedFontScalar16();
    void selectedFontScalar20();
    void selectedUsingZoom();
    void selectedMAPS();
    void selectedDebug();
    void selectedDebugToFile();
    void selectedBugReport();
    void createToolsArea();
    void clickedDist();
    void selectedGotoMessages();
    void selectedGotoWiki();
    void selectedGotoPDF();
    void updateSolarTerminator();
    void updateWeather();
    void updateLightning();
    void selectedVOACAP();
    void selectedVOACAPFreq();
    void selectedVOACAPArea();
    void VOACAPAreaFinished(int, QProcess::ExitStatus);
    void selectedSettings();
    void swapHM();
    void ShowContextMenuAreaButton(const QPoint &pos);
    void closeAreaButton();
    void mapFontChanged(const QFont &font);
    void selectedMapFontScalarDec();
    void selectedMapFontScalarInc();
    void cmboMapCurrentIndexChanged(int);
    void selectedSetFreqALE();
    void selecedtDebugAuth();
    void selectedUploadFile();

public:
    void updateA(QString);
    QLabel *lblDist;
    QLabel *lblAzi;
    QComboBox* cmboStation1;
    QComboBox* cmboStation2;
    QHBoxLayout *ToolsArea2;
    void PBHighlite();
    void hideToolBars();
    void showToolBars();

protected:
    void closeEvent(QCloseEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    bool eventFilter(QObject *obj, QEvent *event) override;
};

class Dialog : public QDialog
{
    Q_OBJECT

public:
    Dialog();
    static QTextEdit * r_textEdit;

private:
    void readFile();

};

class Dialog2 : public QDialog
{
    Q_OBJECT

public:
    Dialog2();
    static QTextEdit * r_textEdit;

private:
    void readFile();

};

class ChanGraph : public QWidget
{
    Q_OBJECT

public:
    ChanGraph(QString Title);

public slots:
    void ShowContextMenu(const QPoint &pos);
    void closeChart();

private:

};

class FreqGraph : public QWidget
{
    Q_OBJECT

public:
    FreqGraph(QString Title);

public slots:
    void ShowContextMenu(const QPoint &pos);
    void closeChart();

private:

};

class clsDlgNotice : public QDialog
{
    Q_OBJECT

public:
    clsDlgNotice();
    static QTextEdit * r_textEdit;

private:
    void readFile();

};

#endif // WINDOW_H

