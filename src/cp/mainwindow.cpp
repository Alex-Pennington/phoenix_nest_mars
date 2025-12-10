/****************************************************************************
**
** Copyright (C) 2016 - 2024 Timothy Millea <timothy.j.millea@gmail.com>
**
    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.

****************************************************************************/
#include "mainwindow.h"
#include "ui_mainwindow.h"
#include <tcpsocket.h>
#include "classxml.h"
#include "globals.h"
#include "cipherlibraryclass.h"
#include "dialogretryqueue.h"

//#include <QtConcurrent>
//#include <QStringList>


//globals
bool g_TCPServerChanged = false;
Settings globalConfigSettings;
//Settings localConfigSettings;
QStringList keyNameList;
bool g_ExitApplication = false;
bool g_ApplicationStart = false;
bool isFirstTimeRun = true;
bool g_IsWritingData = false;
bool g_IsRadioBusy = false;
bool g_IsTransmitting = false;
bool g_IsReceiving = false;
bool g_IsCipherInit = false;
bool g_IsDigitalVoice = false;
bool g_IsWaitingForModemResponse = false;
bool g_HaveReceivedModemResponse = false;
QString g_senderCallsign;

QString g_DVSpeed = "600";
//bool g_IsLinked = false;

QString BUILD_DATE = BUILDDATE;
QString APP_DIR;
QString MSC_DIR;
QString CIPHER_DIR;

//QSerialPort *radioSerialPort;
QSerialPort *modemSerialPort;
bool g_RadioSerialPortChanged = false;
bool g_ModemSerialPortChanged = false;
bool g_TCPSocketChanged = false;
bool g_ALEChanged = false;
bool g_MSDMTTCPChanged = false;
QString g_MyNewPositionID, g_MyOldPositionID;

//QTcpSocket *clientSocket, *MSDMTDataSocket, MSDMTCMDSocket;

MainWindow::MainWindow(QWidget *parent) :
    QMainWindow(parent),
    ui(new Ui::MainWindow)
{
    //    qDebug() << "Function Name: " << Q_FUNC_INFO;
    //    QString funcName = Q_FUNC_INFO;
    //    emit signalUpdateDebugLog("Function Name: " + funcName);
    ui->setupUi(this);

#ifdef CHECKAGE
    if(isTooOld()){
        exit(0);
    }
#endif

    emit signalUpdateApplicationLog("Starting application...");

    g_ApplicationStart = true;

    connect(QApplication::instance(), &QApplication::aboutToQuit, this, [this]() {
        if(globalConfigSettings.generalSettings.useSystemTray){

#ifdef Q_OS_OSX
            if (!event->spontaneous() || !isVisible()) {
                return;
            }
#endif

            if (trayIcon->isVisible()) {
                if(!g_ExitApplication){
                    emit signalUpdateDebugLog("Using system tray...");
                    qApp->setQuitOnLastWindowClosed(false);
                    QMessageBox::information(this, tr(APP_NAME),
                                             tr("The program will keep running in the "
                                                "system tray. To terminate the program, "
                                                "choose <b>Quit</b> in the context menu "
                                                "of the application icon in the "
                                                " system tray."));
                    writeSettings();
                    hide();
                    return;
                }//if not exiting application - system tray
                else {
                    qApp->setQuitOnLastWindowClosed(true);
                    QApplication::processEvents(QEventLoop::ExcludeSocketNotifiers);
                    writeSettings();
                    g_ExitApplication = true;
                    qDebug() << QDateTime::currentDateTimeUtc() << Q_FUNC_INFO << "exiting application";
                    QMutexLocker locker(&mainMessageQueueMutex);
                    while (MSCTCPServer->isListening()) {
                        static int counter = 0;
                        emit signalUpdateDebugLog("Closing TCPServer...");
                        slotBroadcastID("bye");
                        MSCTCPServer->close();
                        QApplication::processEvents();
                        QThread::msleep(10);
                        counter++;
                        if(counter > 100) break;
                    }

                    while(DMTProcess->state() == QProcess::Running){
                        emit signalUpdateApplicationLog("Closing DMT... - "+QString::number(DMTProcess->state()));
                        DMTProcess->terminate();
                        QApplication::processEvents();
                        DMTProcess->waitForFinished(5000);
                    }
                    emit signalUpdateApplicationLog("Application exited normally.");
                    emit signalUpdateDebugLog("Application exited normally.");
                    emit signalUpdateALELog("Application exited normally.");
                    emit signalUpdateTCPLog("Application exited normally.");
                    emit signalUpdateUDPLog("Application exited normally.");
                    emit signalUpdateModemLog("Application exited normally.",false,false);
                    emit signalUpdateRadioCommand("Application exited normally.");
                    QApplication::processEvents();
                }//if exiting application
            }//if trayIcon is visible
        }//if use system tray
        else{
            emit signalUpdateDebugLog("NOT Using system tray...");
            qApp->setQuitOnLastWindowClosed(true);
            QApplication::processEvents(QEventLoop::ExcludeSocketNotifiers);
            writeSettings();
            g_ExitApplication = true;

            qDebug() << QDateTime::currentDateTimeUtc() << Q_FUNC_INFO << "exiting application";
            QMutexLocker locker(&mainMessageQueueMutex);

            while (MSCTCPServer->isListening()) {
                static int counter = 0;
                emit signalUpdateDebugLog("Closing TCPServer...");
                slotBroadcastID("bye");
                MSCTCPServer->close();
                QApplication::processEvents();
                QThread::msleep(10);
                counter++;
                if(counter > 100) break;
            }

            while(DMTProcess->state() == QProcess::Running){
                emit signalUpdateApplicationLog("Closing DMT... - "+QString::number(DMTProcess->state()));
                DMTProcess->terminate();
                QApplication::processEvents();
                DMTProcess->waitForFinished(5000);
            }
            emit signalUpdateApplicationLog("DMT closed... OK - "+QString::number(DMTProcess->state()));

            emit signalUpdateApplicationLog("Application exited normally.");
            emit signalUpdateDebugLog("Application exited normally.");
            emit signalUpdateALELog("Application exited normally.");
            emit signalUpdateTCPLog("Application exited normally.");
            emit signalUpdateUDPLog("Application exited normally.");
            emit signalUpdateModemLog("Application exited normally.",false,false);
            emit signalUpdateRadioCommand("Application exited normally.");
            QApplication::processEvents();
        }
    });//end of about to quit lambda

    DMTProcess = new QProcess(this);
    connect(DMTProcess, &QProcess::started,
            this, &MainWindow::slotDMTProcessStarted);

    ui->groupBoxModem->hide();
    ui->groupBoxALE->hide();
    ui->lineEditSelectFrequency->hide();
    ui->groupBoxDelay->hide();
    ui->radioButtonLQA->hide();
    ui->radioButtonNet->hide();

    mainMessageQueue = new ClassMessageQueue(this);
    connect(mainMessageQueue,&ClassMessageQueue::signalUpdateDebugLog,
            this, &MainWindow::signalUpdateDebugLog);
    retryMessageQueue = new ClassMessageQueue(this);
    connect(retryMessageQueue,&ClassMessageQueue::signalUpdateDebugLog,
            this, &MainWindow::signalUpdateDebugLog);
    connect(this, &MainWindow::signalUpdateStatusBar,
            this, &MainWindow::slotUpdateStatusBar);
    connect(this, &MainWindow::signalUpdateProgressBar,
            this, &MainWindow::slotUpdateProgressBar);
    connect(this, &MainWindow::signalResetProgressBar,
            this, &MainWindow::slotResetProgressBar);
    connect(this, &MainWindow::signalUpdateRetryQueueButton,
            this, &MainWindow::slotUpdateRetryQueueButton);

    dialogProgressBar = new DialogProgressBar(this);

    connect(this, &MainWindow::signalUpdateProgressBar,
            dialogProgressBar, &DialogProgressBar::slotUpdateProgressBar);
    connect(this, &MainWindow::signalResetProgressBar,
            dialogProgressBar, &DialogProgressBar::slotResetProgressBar);
    connect(this, &MainWindow::signalUpdateStatusDisplay,
            dialogProgressBar, &DialogProgressBar::slotUpdateStatusLabel);

    modemSerialPort = new QSerialPort(this);
    connect(modemSerialPort, &QSerialPort::readyRead,
            this, &MainWindow::slotReadModemSerialData);

    readSettings();

    aleLinkTimeOutTimer = new QTimer(this);
    connect(aleLinkTimeOutTimer, &QTimer::timeout,
            this, &MainWindow::slotALELinkTimeout);
    aleLinkTimeOutTimer->setInterval(globalConfigSettings.aleSettings.aleLinkTimeOut.toInt()*1000);//milliseconds


    clientSocket = new QTcpSocket(this);
    connect(clientSocket, &QTcpSocket::readyRead,
            this, &MainWindow::slotReadClientSocket);
    connect(clientSocket, &QTcpSocket::disconnected,
            this, &MainWindow::slotClientSocketDisconnect);

    if(globalConfigSettings.dataComPortSettings.currentDevice == "TCPSocket")
        connectTCPClient();

    MSDMTDataSocket = new QTcpSocket(this);
    connect(MSDMTDataSocket, &QTcpSocket::readyRead,
            this, &MainWindow::slotReadMSDMTDataSocket);
    connect(MSDMTDataSocket, &QTcpSocket::disconnected,
            this, &MainWindow::slotMSDMTDataSocketDisconnect);

    MSDMTCMDSocket = new QTcpSocket(this);
    connect(MSDMTCMDSocket, &QTcpSocket::readyRead,
            this, &MainWindow::slotReadMSDMTCMDSocket);
    connect(MSDMTCMDSocket, &QTcpSocket::disconnected,
            this, &MainWindow::slotMSDMTCMDSocketDisconnect);

    aleSocket = new QTcpSocket(this);
    connect(aleSocket, &QTcpSocket::readyRead,
            this, &MainWindow::slotReadALESocket);
    connect(aleSocket, &QTcpSocket::disconnected,
            this, &MainWindow::slotALESocketDisconnected);
    telnetSockerTimer = new QTimer(this);
    connect(telnetSockerTimer, &QTimer::timeout,
            this, &MainWindow::slotHandleALEData);
    telnetSockerTimer->setInterval(500);
    
    g_MyNewPositionID = globalConfigSettings.generalSettings.positionIdentifier;
    g_MyOldPositionID = globalConfigSettings.generalSettings.positionIdentifier;

    modemSerialTimer = new QTimer(this);
    modemSerialTimer->setInterval(globalConfigSettings.dataComPortSettings.serialTimeout.toInt());
    connect(modemSerialTimer, &QTimer::timeout,
            this, &MainWindow::slotHandleModemReceivedlData);

    updateSettings();

    settingsDialog = new DialogSettings(this);
    connect(settingsDialog, &DialogSettings::signalDisconnectDMT,
            this, &MainWindow::slotDisconnectFromDMT);
    connect(settingsDialog, &DialogSettings::signalConnectDMT,
            this, &MainWindow::slotConnectToDMT);
    connect(settingsDialog, &DialogSettings::signalUpdateMainWindow,
            this, &MainWindow::slotUpdateMainWindow);
    connect(settingsDialog, &DialogSettings::signalBroadcastID,
            this, &MainWindow::slotBroadcastID);
    connect(settingsDialog, &DialogSettings::signalSendStatusChange,
            this, &MainWindow::sendStatusChange);
    connect(settingsDialog, &DialogSettings::signalUpdateStyleSheet,
            this, &MainWindow::slotUpdateStylesheet);
    connect(this, &MainWindow::signalUpdateDMTConnectionList,
            settingsDialog, &DialogSettings::slotUpdateDMTConnections);
    connect(settingsDialog, &DialogSettings::signalUpdateCriticalNotifications,
            this, &MainWindow::signalUpdateCriticalNotifications);

    dialogLog = new DialogLog(this);
    connect(this, &MainWindow::signalUpdateApplicationLog,
            dialogLog, &DialogLog::slotUpdateBrowser);
    connect(this, &MainWindow::signalUpdateCriticalNotifications,
            dialogLog, &DialogLog::slotUpdateCriticalNotifications);
    connect(this, &MainWindow::signalUpdateDebugLog,
            dialogLog, &DialogLog::slotUpdateDebug);

    connect(this, &MainWindow::signalUpdateStatusDisplay,
            this, &MainWindow::slotUpdateStatusDisplay);

    dialogALE = new DialogAle(this);
    connect(this, &MainWindow::signalUpdateALELog,
            dialogALE, &DialogAle::slotUpdateBrowser);
    connect(this, &MainWindow::signalUpdateAMDMessage,
            dialogALE, &DialogAle::slotUpdateAMDBrowser);
    connect(this, &MainWindow::signalUpdateALEAddressSelect,
            dialogALE, &DialogAle::slotUpdateALEAddressSelect);
    connect(this, &MainWindow::signalUpdateALEHeardList,
            dialogALE, &DialogAle::slotUpdateHeardListBrowser);

    connect(dialogALE, &DialogAle::signalSendALECommand,
            this, &MainWindow::slotSendAMDMessage);


    connect(this, &MainWindow::signalUpdateALEStatus,
            this, &MainWindow::slotUpdateALEStatus);

    //    settingsDialog->initializeCipher();

    //setup UDP listen socket and connections
    //this is used to auto configure TCP connections
    udpListenSocket = new QUdpSocket(this);
    udpListenSocket->bind(globalConfigSettings.ipSettings.applicationDiscoveryPort.toUShort(),
                          QUdpSocket::ReuseAddressHint);
    emit signalUpdateApplicationLog("Discovery: "+
                                    udpListenSocket->localAddress().toString()+":"+
                                    QString::number(udpListenSocket->localPort()));
    connect(udpListenSocket,&QUdpSocket::readyRead,
            this, &MainWindow::slotReadUdpData);

    //setup UDP listen socket and connections
    //this is used to auto configure TCP connections
    streamingUDPSocket = new QUdpSocket(this);
    streamingUDPSocket->bind(QHostAddress(globalConfigSettings.ipSettings.listenIPAddress),5001,
                             QUdpSocket::ReuseAddressHint);
    emit signalUpdateApplicationLog("Streaming: "+
                                    streamingUDPSocket->localAddress().toString()+":"+
                                    QString::number(streamingUDPSocket->localPort()));
    connect(streamingUDPSocket,&QUdpSocket::readyRead,
            this, &MainWindow::slotReadStreamingUDPData);

    //set up message queue timers
    //main queue
    timerCheckMainMessageQueue = new QTimer(this);
    connect(timerCheckMainMessageQueue, &QTimer::timeout,
            this, &MainWindow::slotCheckMainMessageQueue,Qt::DirectConnection);
    timerCheckMainMessageQueue->setInterval(1000);
    timerCheckMainMessageQueue->start();
    //retry queue
    timerCheckRetryMessageQueue = new QTimer(this);
    connect(timerCheckRetryMessageQueue, &QTimer::timeout,
            this, &MainWindow::slotCheckRetryMessageQueue,Qt::DirectConnection);
    timerCheckRetryMessageQueue->setInterval(1000*60*15);//15 minutes
    //    timerCheckRetryMessageQueue->setInterval(1000*60);//1 minutes
    timerCheckRetryMessageQueue->start();
    readRetryFile();

    //setup UDP to broadcast every 15 minutes
    udpTimer = new QTimer(this);
    connect(udpTimer, &QTimer::timeout,
            this, &MainWindow::slotUdpUpdate);
    udpTimer->setInterval(1000*60*5);//5 minutes in milliseconds
    udpTimer->start();

    dialogTCP = new DialogTCPIP(this);
    connect(this, &MainWindow::signalUpdateTCPLog,
            dialogTCP, &DialogTCPIP::slotUpdateBrowser);
    connect(dialogTCP, &DialogTCPIP::signalUpdateMainWindow,
            this, &MainWindow::slotUpdateMainWindow);
    connect(dialogTCP, &DialogTCPIP::signalWriteTCPLogFile,
            dialogLog, &DialogLog::slotWriteTCPLogFile);

    dialogUDP = new DialogUDP(this);
    connect(this, &MainWindow::signalUpdateUDPLog,
            dialogUDP, &DialogUDP::slotUpdateBrowser);
    connect(dialogUDP, &DialogUDP::signalWriteUDPLogFile,
            dialogLog, &DialogLog::slotWriteUDPLogFile);

    dialogModem = new DialogModem(this);
    connect(this, &MainWindow::signalUpdateModemLog,
            dialogModem, &DialogModem::slotUpdateBrowser);
    connect(this, &MainWindow::signalUpdateModemStatus,
            dialogModem, &DialogModem::slotUpdateModemStatus);
    connect(dialogModem, &DialogModem::signalSendDataToModem,
            this, &MainWindow::writeDataModem);
    connect(dialogModem, &DialogModem::signalWriteModemLogFile,
            dialogLog, &DialogLog::slotWriteModemLogFile);
    connect(this, &MainWindow::signalWriteModemData,
            this, &MainWindow::writeDataModem);
    if(globalConfigSettings.dataComPortSettings.showModemDialog){
        dialogModem->show();
        dialogModem->raise();
    }

    dialogChat = new DialogChat(this);
    connect(dialogChat, &DialogChat::signalSendMessage,
            this, &MainWindow::slotSendChatMessage);
    connect(this, &MainWindow::signalUpdateChatDialogConfig,
            dialogChat, &DialogChat::slotUpdateConfig);
    connect(this, &MainWindow::signalUpdateChatDialogMessage,
            dialogChat, &DialogChat::slotMessageReceived);
    if(globalConfigSettings.chat_Dialog.showChatDialog){
        dialogChat->show();
        dialogChat->raise();
    }

    MSCCipherLibraryClass MSC_Cipher;
    keyNameList = MSC_Cipher.QT_GetKeys();
    if(globalConfigSettings.cipherSettings.allowPT){
        keyNameList.append("PLAINTEXT");
    }
    //    qDebug() << "Zeroizing cipher..." << MSC_Cipher.QT_Zeroize();
    emit signalUpdateChatDialogConfig(xmlWrapper("config","",true));
    emit signalUpdateChatDialogConfig(xmlWrapper("status","",true));


    //MSC TCP Server
    MSCTCPServer = new QTcpServer(this);
    connect(MSCTCPServer, &QTcpServer::newConnection,
            this, &MainWindow::slotMakeNewMSCTCPConnection);

    rawTCPServer = new QTcpServer(this);
    connect(rawTCPServer, &QTcpServer::newConnection,
            this, &MainWindow::slotMakeNewRawTCPConnection);

    if(!isFirstTimeRun){
        //start the server in 1000ms
        QTimer::singleShot(1000, [=] {
            // must have CONFIG += c++11 in .pro file
            startTCPServer();
            purgeOldReceivedFiles();
            purgeOldSentFiles();
        });
        //close/open serial ports
        closeSerialPorts();
        openSerialPorts();
        /*     if(configSettings.dataComPortSettings.currentDevice=="MS-DMT Serial"){
            writeCommandModem("<<CMD:DATA RATE?>>");
        }//if MSDMT Serial
        else if (configSettings.dataComPortSettings.currentDevice=="MS-DMT TCP") {
            if(MSDMTCMDSocket->isOpen()){
                writeCommandModem("CMD:DATA RATE:?");
            }
        }*///else tcp
    }//if this is not the first time running application

    if(isFirstTimeRun){
        settingsDialog->show();
        settingsDialog->raise();
    }

    //wait 5 seconds for application to settle
    QTimer::singleShot(5000, [=] {
        // must have CONFIG += c++11 in .pro file
        g_ApplicationStart = false;
//        if(globalConfigSettings.dataComPortSettings.currentDevice == "MS-DMT TCP" ||
//            globalConfigSettings.dataComPortSettings.currentDevice == "MS-DMT Serial"){
//            if(!isFirstTimeRun){
//                on_comboBoxSelectModemBaud_currentTextChanged(ui->comboBoxSelectModemBaud->currentText());
//            }
//        }
    });

    emit signalUpdateApplicationLog("Finished setting up UI...");

#if defined INTERNETCHECK || defined INTERNETWARNING
    emit signalUpdateApplicationLog("Checking for Internet access...");
    QTimer::singleShot(0, [=] {
        if(checkInternet()){
#ifdef INTERNETCHECK
            QMessageBox msgBox;
            msgBox.setText("ERROR: Internet detected!\n\n"
                           "Disable Internet access and restart application.\n\n"
                           "Application will now close!");
            msgBox.exec();
            this->close();
#elif defined INTERNETWARNING
                QMessageBox msgBox;
                msgBox.setText("WARNING: Internet detected!\n\n"
                               "Disable Internet access and restart application.\n\n");
                msgBox.exec();
#endif
        }
    });//
#endif
    systemTray();
}//MainWindow

void MainWindow::purgeOldSentFiles()
{
    //purge files after 7 days
    QDir logDir(QApplication::applicationDirPath()+"/SENT");

    logDir.setFilter(QDir::Files);
    logDir.setNameFilters(QStringList() << "*.xml" << "*.XML");

    if(logDir.exists()){
        if(logDir.count() > 0){
            for(int i = 0; i < logDir.entryInfoList().size();i++)//for each file in the directory
            {
                QFile readFile(logDir.entryInfoList().at(i).absoluteFilePath());
                QFileInfo fileInfo(readFile);
                if(fileInfo.birthTime().toTimeSpec(Qt::UTC) < QDateTime::currentDateTimeUtc().addDays(-7)){
                    if(readFile.remove()){
                        emit signalUpdateApplicationLog("Purging xml file: " + fileInfo.fileName());
                    }
                    else{
                        emit signalUpdateApplicationLog("ERROR: Purging xml file: " + readFile.fileName());
                    }
                }//if file exists
                else {
                    continue;
                }
            }//for each file in folder
        }//if there are files in folder
    }//if folder exists
}//purgeOldSentFiles

void MainWindow::purgeOldReceivedFiles()
{
    //purge files after 7 days
    QDir logDir(QApplication::applicationDirPath()+"/RECEIVED");

    logDir.setFilter(QDir::Files);
    logDir.setNameFilters(QStringList() << "*.xml" << "*.XML");

    if(logDir.exists()){
        if(logDir.count() > 0){
            for(int i = 0; i < logDir.entryInfoList().size();i++)//for each file in the directory
            {
                QFile readFile(logDir.entryInfoList().at(i).absoluteFilePath());
                QFileInfo fileInfo(readFile);
                if(fileInfo.birthTime().toTimeSpec(Qt::UTC) < QDateTime::currentDateTimeUtc().addDays(-7)){
                    if(readFile.remove()){
                        emit signalUpdateApplicationLog("Purging xml file: " + fileInfo.fileName());
                    }
                    else{
                        emit signalUpdateApplicationLog("ERROR: Purging xml file: " + readFile.fileName());
                    }
                }//if file exists
                else {
                    continue;
                }
            }//for each file in folder
        }//if there are files in folder
    }//if folder exists
}//purgeOldReceivedFiles

void MainWindow::slot_SetModem75L(){
    ui->comboBoxSelectModemBaud->setCurrentText("75");
    ui->comboBoxSelectModemInterleave->setCurrentText("L");
}//setModem75L

void MainWindow::slot_SetModem150L(){
    ui->comboBoxSelectModemBaud->setCurrentText("150");
    ui->comboBoxSelectModemInterleave->setCurrentText("L");
}//setModem75L

void MainWindow::slot_SetModem300L(){
    ui->comboBoxSelectModemBaud->setCurrentText("300");
    ui->comboBoxSelectModemInterleave->setCurrentText("L");
}//setModem75L

void MainWindow::slot_SetModem600L(){
    ui->comboBoxSelectModemBaud->setCurrentText("600");
    ui->comboBoxSelectModemInterleave->setCurrentText("L");
}//setModem75L

void MainWindow::slot_SetModem1200L(){
    ui->comboBoxSelectModemBaud->setCurrentText("1200");
    ui->comboBoxSelectModemInterleave->setCurrentText("L");
}//setModem75L

void MainWindow::systemTray(){
    //system tray stuff
    trayIcon = new QSystemTrayIcon(this);
    //    qDebug() << "System Tray Available: " << trayIcon->isSystemTrayAvailable();
    emit signalUpdateApplicationLog("System Tray Available: " +QString::number(static_cast<int>(trayIcon->isSystemTrayAvailable())));
    trayIconMenu = new QMenu(this);

    restoreAction = new QAction(tr("&Show"), this);
    connect(restoreAction, &QAction::triggered, this, &QWidget::showNormal);

    quitAction = new QAction(tr("&Quit"), this);
    connect(quitAction, &QAction::triggered, this, &MainWindow::quitApplication);

    openLogDialog = new QAction(tr("Log"),this);
    connect(openLogDialog, &QAction::triggered, this, &MainWindow::on_pushButtonLog_clicked);

    openALEDialog = new QAction(tr("ALE"),this);
    connect(openALEDialog, &QAction::triggered, this, &MainWindow::on_pushButtonALE_clicked);

    openModemDialog = new QAction(tr("Modem"),this);
    connect(openModemDialog, &QAction::triggered, this, &MainWindow::on_pushButtonModem_clicked);

    openTCPDialog = new QAction(tr("TCP"),this);
    connect(openTCPDialog, &QAction::triggered, this, &MainWindow::on_pushButtonTCP_clicked);

    openUDPDialog = new QAction(tr("UDP"),this);
    connect(openUDPDialog, &QAction::triggered, this, &MainWindow::on_pushButtonUDP_clicked);

    modem75L = new QAction(tr("75L"),this);
    connect(modem75L, &QAction::triggered, this, &MainWindow::slot_SetModem75L);

    modem150L = new QAction(tr("150L"),this);
    connect(modem150L, &QAction::triggered, this, &MainWindow::slot_SetModem150L);

    modem300L = new QAction(tr("300L"),this);
    connect(modem300L, &QAction::triggered, this, &MainWindow::slot_SetModem300L);

    modem600L = new QAction(tr("600L"),this);
    connect(modem600L, &QAction::triggered, this, &MainWindow::slot_SetModem600L);

    modem1200L = new QAction(tr("1200L"),this);
    connect(modem1200L, &QAction::triggered, this, &MainWindow::slot_SetModem1200L);

    trayIconMenu->addAction(restoreAction);
    trayIconMenu->addSeparator();
    trayIconMenu->addAction(openLogDialog);
    trayIconMenu->addAction(openALEDialog);
    trayIconMenu->addAction(openModemDialog);
    trayIconMenu->addAction(openTCPDialog);
    trayIconMenu->addAction(openUDPDialog);
    trayIconMenu->addSeparator();
    trayIconMenu->addAction(modem75L);
    trayIconMenu->addAction(modem150L);
    trayIconMenu->addAction(modem300L);
    trayIconMenu->addAction(modem600L);
    trayIconMenu->addAction(modem1200L);
    trayIconMenu->addSeparator();
    trayIconMenu->addAction(quitAction);

    trayIcon->setContextMenu(trayIconMenu);

    QIcon appIcon;
    //    appIcon
    appIcon.addFile(":/images/RMIIcon.png");
    trayIcon->setIcon(appIcon);

    QString windowTitleString;
    windowTitleString.append(globalConfigSettings.generalSettings.positionIdentifier+" - ");
    windowTitleString.append(tr(APP_NAME_ABBV) + " - " +
                             globalConfigSettings.dataComPortSettings.currentDevice);
    QString version = VERSION;
    if(version.contains("alpha") || version.contains("beta")){
        windowTitleString.append(" - " +tr(VERSION));
        windowTitleString.append(" - " + tr(RELEASEDATE));
    }
    else {
        windowTitleString.append(" - " + tr(RELEASEDATE));
    }
    trayIcon->setToolTip(windowTitleString);

    setWindowIcon(appIcon);

    trayIcon->setVisible(true);
    QTimer::singleShot(0, [=] {
        // must have CONFIG += c++11 in .pro file
        if(globalConfigSettings.generalSettings.useSystemTray){
            //            qDebug() << "TrayIcon isVisible: " << trayIcon->isVisible();
            if (trayIcon->isVisible()) {
                qApp->setQuitOnLastWindowClosed(false);
                //                QMessageBox::information(this, tr(APP_NAME),
                //                                         tr("The program will keep running in the "
                //                                            "system tray. To terminate the program, "
                //                                            "choose <b>Quit</b> in the context menu "
                //                                            "of the application icon in the "
                //                                            " system tray."));
                emit signalUpdateApplicationLog("Minimizing to system tray...");

                hide();
            }//if trayIcon is visible        }
            else {
                show();
            }//else show
        }//if use system tray
        else {
            qApp->setQuitOnLastWindowClosed(true);
        }
    });//singleshot for minimize to system tray
}//systemTray

void MainWindow::quitApplication()
{
    if(globalConfigSettings.generalSettings.confirmApplicationExit){
        QMessageBox msgBox(this);
        msgBox.setText("Closing  Application!");
        msgBox.setInformativeText("Are you sure you want to close the application?");
        msgBox.setStandardButtons(QMessageBox::Yes | QMessageBox::No);
        msgBox.setDefaultButton(QMessageBox::No);
        int result = msgBox.exec();

        switch (result) {
        case QMessageBox::No:
            return;
            break;
        default:
            break;
        }
    }//if confirm exit
    g_ExitApplication = true;
    QCoreApplication::quit();
}//quitApplication

#if defined INTERNETCHECK || defined INTERNETWARNING
bool MainWindow::checkInternet() {
    //    return false;
    QTcpSocket socket;

    socket.connectToHost("172.217.11.174", 80);
    if (socket.waitForConnected(1000)){
        qDebug() << "ERROR: INTERNET DETECTED!";
        return true;
    }
    socket.connectToHost("72.30.35.10", 80);
    if (socket.waitForConnected(1000)){
        qDebug() << "ERROR: INTERNET DETECTED!";
        socket.disconnectFromHost();
        return true;
    }
    socket.connectToHost("23.100.122.175", 80);
    if (socket.waitForConnected(1000)){
        qDebug() << "ERROR: INTERNET DETECTED!";
        socket.disconnectFromHost();
        return true;
    }
    return false;
}//checkInternet
#endif

void MainWindow::slotDSRTestTimeout()
{
    if(!isDSR()){
        emit signalUpdateCriticalNotifications("ERROR: DSR not present - is your modem connected?");
    }
}//slotDSRTestTimeout

void MainWindow::slotDMTProcessStarted()
{
    QTimer::singleShot(1000, [=] {
        on_comboBoxSelectModemBaud_currentTextChanged(ui->comboBoxSelectModemBaud->currentText());
    });
}//slotDMTProcessStarted

void MainWindow::slotUpdateStylesheet(QString styleSheetString)
{
    this->setStyleSheet(styleSheetString);
    dialogALE->setStyleSheet(styleSheetString);
    dialogLog->setStyleSheet(styleSheetString);
    dialogTCP->setStyleSheet(styleSheetString);
    dialogUDP->setStyleSheet(styleSheetString);
    dialogModem->setStyleSheet(styleSheetString);
    dialogProgressBar->setStyleSheet(styleSheetString);

}//slotUpdateStylesheet

void MainWindow::slotCheckMainMessageQueue()
{
    ui->pushButtonDumpQueue->setEnabled(!mainMessageQueue->isEmpty());

    if(mainMessageQueue->getB_isQueueCurrentlyProcessing())
        return;

    if(mainMessageQueue->isEmpty()){
        b_delaySet = false;
        return;
    }

    mainMessageQueue->setB_isQueueCurrentlyProcessing(true);
    
    aleLinkTimeOutTimer->start(globalConfigSettings.aleSettings.aleLinkTimeOut.toInt()*1000);

    //futureCheckMainMessageQueue = QtConcurrent::run(this, &MainWindow::checkMainMessageQueue); //qt 5.
    futureCheckMainMessageQueue = QtConcurrent::run(&MainWindow::checkMainMessageQueue,this); //qt 6.
}//slotCheckMessageQueue

void MainWindow::checkMainMessageQueue(){
    emit signalUpdateDebugLog("Locking mainMessageQueueMutex");
    QMutexLocker mutexLocker(&mainMessageQueueMutex);
    emit signalUpdateDebugLog("Locked mainMessageQueueMutex");
    bool processedMessages = false;
    while (!mainMessageQueue->isEmpty()) {
        qDebug() << QDateTime::currentDateTimeUtc() << Q_FUNC_INFO << "main message queue loop" << mainMessageQueue->getMessageCount();
        if(g_ExitApplication){
            mainMessageQueue->clearMessageQueue();
            mainMessageQueue->setB_isQueueCurrentlyProcessing(false);
            currentMessage->deleteLater();
            emit signalUpdateDebugLog("Exiting function - UNLocking mainMessageQueueMutex");
            return;
        }

        bool needDelay = false;
        while (isRMIBusy()) {
            //            qDebug() << Q_FUNC_INFO << "1: RMI is busy...";
            QThread::msleep(1000);
            QApplication::processEvents();
            if(g_ExitApplication){
                mainMessageQueue->clearMessageQueue();
                mainMessageQueue->setB_isQueueCurrentlyProcessing(false);
                currentMessage->deleteLater();
                emit signalUpdateDebugLog("Exiting function - UNLocking mainMessageQueueMutex");
                return;
            }
            needDelay = true;
        }//while CP is busy
        if(needDelay || b_delaySet){
            //            qDebug() << Q_FUNC_INFO << "1: Delay before next transmission..." << configSettings.dataComPortSettings.delayBeforeNextTX*1000;
            QThread::msleep(globalConfigSettings.dataComPortSettings.delayBeforeNextTX*1000);
            needDelay = false;
        }

        emit signalUpdateDebugLog("******** - LOCKING mainQueueLoopMutex");
        QMutexLocker mutexLocker(&mainQueueLoopMutex);
        //        qDebug() << "******** - Locked mainQueueLoopMutex";
        emit signalUpdateDebugLog("******** - Locked mainQueueLoopMutex");
        processedMessages = true;
        //        qDebug() << QDateTime::currentDateTimeUtc() << "1" << Q_FUNC_INFO << mainMessageQueue->getMessageCount();
        currentMessage = mainMessageQueue->getNextMessage();
        //        qDebug() << QDateTime::currentDateTimeUtc() << "2" << Q_FUNC_INFO << mainMessageQueue->getMessageCount();
        int messageSerialNumber = currentMessage->getMessageSerialNumber();
        TXsourceStation = currentMessage->getSourceStation();
        TXdestinationStation = currentMessage->getDestinationStation();
        QByteArray xmlDocument = currentMessage->getXMLDocument();
        //        qDebug() << xmlDocument;
        if(xmlDocument.size() > 0)
        {
            //check for dump message queue
            if(dumpMessageQueue){
                //display or log error and add message to retry queue
                if(globalConfigSettings.generalSettings.useRetryQueue){
                    emit signalUpdateApplicationLog("Dumping message queue: Adding current message to retry queue...");
                    retryMessageQueue->insertMessage(currentMessage);
                    writeTcpSockets(xmlWrapper("error","Dumping message queue: Adding current message to retry queue...",true),"");
                }//if use retry queue
                else {
                    emit signalUpdateApplicationLog("Dumping message queue...");
                    writeTcpSockets(xmlWrapper("error","Dumping message queue...",true),"");
                }
                emit signalUpdateRetryQueueButton();
                emit signalUpdateRetryQueueButton();
                emit signalUpdateDebugLog("Dumping queue - unlocking mainQueueLoopMutex");
                emit signalUpdateStatusBar();
                //                currentMessage->deleteLater();
                continue;
            }//if dump message queue

            //check for EMCOM
            if(globalConfigSettings.dataComPortSettings.EMCON){
                if(globalConfigSettings.generalSettings.useRetryQueue){
                    retryMessageQueue->insertMessage(currentMessage);
                    emit signalUpdateRetryQueueButton();
                    writeTcpSockets(xmlWrapper("error","EMCOM condition: Disable EMCOM condition in CP.\n"
                                                        "Message added to CP retry queue.",true),"");
                }//if use retry queue
                else {
                    writeTcpSockets(xmlWrapper("error","EMCOM condition: Disable EMCOM condition in CP.",true),"");
                }//else not using retry queue
                emit signalUpdateDebugLog("EMCOM - UNLocking mainQueueLoopMutex");
                emit signalUpdateStatusBar();
                //                currentMessage->deleteLater();
                continue;
            }//if EMCOM

            ClassXML xmlClass(xmlDocument);
            if(xmlClass.getParsingError()){
                emit signalUpdateDebugLog("XML parsing error: Un-Locking mainMessageQueueMutex");
                emit signalUpdateDebugLog("XML Parsing error - UNLocking mainQueueLoopMutex");
                emit signalUpdateStatusBar();
                currentMessage->deleteLater();
                continue;
            }//if parsing error

            if(!setupCipher(xmlDocument))
            {
                //display or log error and add message to retry queue
                if(globalConfigSettings.generalSettings.useRetryQueue){
                    emit signalUpdateApplicationLog("Cipher error: Adding current message to retry queue...");
                    retryMessageQueue->insertMessage(currentMessage);
                    writeTcpSockets(xmlWrapper("error","Cipher setup error: Adding current message to retry queue...",true),"");
                }//if use retry queue
                else {
                    emit signalUpdateRetryQueueButton();
                    emit signalUpdateStatusBar();
                    emit signalUpdateDebugLog("Cipher error: Un-Locking mainMessageQueueMutex");
                    emit signalUpdateDebugLog("Cipher error - UNLocking mainQueueLoopMutex");
                    emit signalUpdateStatusBar();
                    writeTcpSockets(xmlWrapper("error","Cipher setup error: Message discarded.",true),"");
                    emit signalUpdateCriticalNotifications("Cipher setup error: Message discarded.");
                }
                //                currentMessage->deleteLater();
                continue;
            }//if cipher setup failed

            if(!setupRadio(xmlDocument))
            {
                //display or log error and add message to retry queue
                if(globalConfigSettings.generalSettings.useRetryQueue){
                    retryMessageQueue->insertMessage(currentMessage);
                    emit signalUpdateApplicationLog(ALEErrorMessage+" - Message added to retry queue.");
                    writeTcpSockets(xmlWrapper("error", ALEErrorMessage.toLatin1()+" - Message added to retry queue.", true),"");
                    emit signalUpdateRetryQueueButton();
                }//if use retry queue
                else {
                    emit signalUpdateApplicationLog(ALEErrorMessage);
                    writeTcpSockets(xmlWrapper("error", ALEErrorMessage.toLatin1(), true),"");
                }
                //send error message to clients
                b_linkError = false;
                ALEErrorMessage.clear();

                emit signalUpdateStatusBar();
                emit signalUpdateDebugLog("Radio error - UNLocking mainQueueLoopMutex");
                emit signalUpdateStatusBar();
                //                currentMessage->deleteLater();
                continue; //we failed to set up radio per xmlDocument parameters might as well quit
            }//failed to set up ALE

            setupModem(xmlDocument);
            QApplication::processEvents();
            QThread::msleep(250);
            if(g_ExitApplication){
                mainMessageQueue->clearMessageQueue();
                mainMessageQueue->setB_isQueueCurrentlyProcessing(false);
                emit signalUpdateDebugLog("Exiting function - UNLocking mainMessageQueueMutex");
                currentMessage->deleteLater();
                return;
            }
            setupCompression(xmlDocument);
            QList<QByteArray> messageList;
            messageList = xmlParseMessageList(xmlDocument);

            //            int messagesSent = 0;
            if(messageList.size() > 0)
            {
                //send messagList
                emit signalUpdateApplicationLog("Sending: " +
                                                QString::number(messageList.size()) + " message(s)...");
                int messagesSent = sendMessages(messageList,messageSerialNumber);

                if(messagesSent > 0){
                    emit signalUpdateApplicationLog("Sent: " +
                                                    QString::number(messagesSent) + " message(s)...");
                    deleteRetryFile(xmlDocument);
                    writeSendFile(xmlDocument);

                    currentMessage->deleteLater();
                }
                else {
                    if(globalConfigSettings.generalSettings.useRetryQueue){
                        retryMessageQueue->insertMessage(currentMessage);
                        emit signalUpdateRetryQueueButton();
                        writeTcpSockets(xmlWrapper("error","ERROR: Cipher error! - "
                                                            "Message added to CP retry queue.",true),"");
                        emit signalUpdateApplicationLog("ERROR: Cipher error! - "
                                                        "Message added to CP retry queue.");
                    }
                    else {
                        writeTcpSockets(xmlWrapper("error","ERROR: Cipher error. - "
                                                            "Message discarded.",true),"");
                        emit signalUpdateApplicationLog("ERROR: Cipher error. - "
                                                        "Message discarded.");
                    }
                    emit signalUpdateStatusBar();
                    //                    currentMessage->deleteLater();
                }
                if(b_messageInitiatedALECall){
                    writeControlRadio("CMD CLEAR "+linkedAddress.toLatin1());
                    b_isLinked = false;
                    b_messageInitiatedALECall = false;
                    QThread::msleep(5000);
                    if(g_ExitApplication){
                        mainMessageQueue->clearMessageQueue();
                        mainMessageQueue->setB_isQueueCurrentlyProcessing(false);
                        emit signalUpdateDebugLog("Exiting function - UNLocking mainMessageQueueMutex");
                        currentMessage->deleteLater();
                        return;
                    }
                }
            }//if we have messages to send
            else {
                deleteRetryFile(xmlDocument);
                writeSendFile(xmlDocument);
                currentMessage->deleteLater();
            }
            //            QApplication::processEvents();
        }//if we have content in our xmlDOcument
    }//while message queue contains messages
    emit signalUpdateRetryQueueButton();
    dumpMessageQueue = false;

    if(processedMessages)
        emit signalUpdateDebugLog("Un-Locking mainMessageQueueMutex - processed message(s)");
    else
        emit signalUpdateDebugLog("Un-Locking mainMessageQueueMutex - no messages processed");

    processedMessages = false;
    mainMessageQueue->setB_isQueueCurrentlyProcessing(false);
    //    isBusy = false;
    emit signalUpdateDebugLog("Exiting function - UNLocking mainMessageQueueMutex");
    emit signalUpdateStatusBar();
    return;
}//checkMainMessageQueue

void MainWindow::slotCheckRetryMessageQueue()
{
    if(!globalConfigSettings.generalSettings.autoTXRetryQueue)
        return;

    if(isRMIBusy() || isALEBusy())
        return;

    else {
        while (!retryMessageQueue->isEmpty()) {
            emit signalUpdateDebugLog("Moving message from retry queue to main queue...");
            mainMessageQueue->insertMessage(retryMessageQueue->getNextMessage());
            emit signalUpdateStatusBar();

        }
        emit signalUpdateRetryQueueButton();
    }
}

void MainWindow::slotUpdateStatusDisplay(QString status)
{
    if(status.isEmpty())
        return;

    //    qDebug() << Q_FUNC_INFO << status;

    ui->statusDisplay->setText(status);
    if(status.contains("RX"))
        ui->statusDisplay->setStyleSheet("QLabel {color : orange }");
    else if(status.contains("Receiving data..."))
        ui->statusDisplay->setStyleSheet("QLabel {color : orange }");
    else if(status.contains("Detected"))
        ui->statusDisplay->setStyleSheet("QLabel {color : orange }");
    else if(status.contains("CTS"))
        ui->statusDisplay->setStyleSheet("QLabel {color : orange }");
    else if(status.contains("Buffering"))
        ui->statusDisplay->setStyleSheet("QLabel {color : orange }");
    else if(status.contains("Writing data"))
        ui->statusDisplay->setStyleSheet("QLabel {color : red }");
    else if(status.contains("Transmitting"))
        ui->statusDisplay->setStyleSheet("QLabel {color : red }");
    else if(status.contains("Calling"))
        ui->statusDisplay->setStyleSheet("QLabel {color : red }");
    else if(status.contains("Performing LQ"))
        ui->statusDisplay->setStyleSheet("QLabel {color : red }");
    else if(status.contains("Dropping link"))
        ui->statusDisplay->setStyleSheet("QLabel {color : red }");
    else if(status.contains("Scanning..."))
        ui->statusDisplay->setStyleSheet("QLabel {color : blue }");
    else if(status.contains("Sounding"))
        ui->statusDisplay->setStyleSheet("QLabel {color : orange }");
    else if(status.contains("Idle"))
        ui->statusDisplay->setStyleSheet("QLabel {color : lightgrey }");
    else
        ui->statusDisplay->setStyleSheet("QLabel {color : lightgrey }");

}//slotUpdateStatusDisplay

void MainWindow::slotUpdateALEStatus(QString status)
{
    if(status.isEmpty())
        return;

    ui->labelALEStatus->setText(status);
    if(status.contains("Receiving data..."))
        ui->labelALEStatus->setStyleSheet("QLabel {color : black }");
    else if(status.contains("Call failed:"))
        ui->labelALEStatus->setStyleSheet("QLabel {color : red }");
    else if(status.contains("Failed:"))
        ui->labelALEStatus->setStyleSheet("QLabel {color : red }");
    else if(status.contains("Disconnected"))
        ui->labelALEStatus->setStyleSheet("QLabel {color : red }");
    else if(status.contains("Clearing with:"))
        ui->labelALEStatus->setStyleSheet("QLabel {color : orange }");
    else if(status.contains("Syncing..."))
        ui->labelALEStatus->setStyleSheet("QLabel {color : orange }");
    else if(status.contains("Clearing..."))
        ui->labelALEStatus->setStyleSheet("QLabel {color : orange }");
    else if(status.contains("Dropping "))
        ui->labelALEStatus->setStyleSheet("QLabel {color : orange }");
    else if(status.contains("Linked: "))
        ui->labelALEStatus->setStyleSheet("QLabel {color : red }");
    else if(status.contains("Calling..."))
        ui->labelALEStatus->setStyleSheet("QLabel {color : orange }");
    else if(status.contains("Listening..."))
        ui->labelALEStatus->setStyleSheet("QLabel {color : orange }");
    else if(status.contains("Performing LQ "))
        ui->labelALEStatus->setStyleSheet("QLabel {color : orange }");
    else if(status.contains("Cleared: "))
        ui->labelALEStatus->setStyleSheet("QLabel {color : orange }");
    else if(status.contains("Scanning..."))
        ui->labelALEStatus->setStyleSheet("QLabel {color : blue }");
    else if(status.contains("Sounding..."))
        ui->labelALEStatus->setStyleSheet("QLabel {color : orange }");
    else if(!status.contains("Idle..."))
        ui->labelALEStatus->setStyleSheet("QLabel {color : black }");
    else
        ui->labelALEStatus->setStyleSheet("QLabel {color : black }");
}//slotUpdateALEStatus

void MainWindow::slotReadUdpData()
{
    //    QMutexLocker mutexlocker(&slotReadUDPSocketMutex);

    if(g_ExitApplication)
        return;

    //    qDebug() << "Function Name: " << Q_FUNC_INFO;
    //    QString funcName = Q_FUNC_INFO;
    //    emit signalUpdateDebugLog("Function Name: " + funcName);

    QByteArray udpData;
    //    while (udpListenSocket->hasPendingDatagrams()) {
    udpData.resize(static_cast<int>(udpListenSocket->pendingDatagramSize()));
    udpListenSocket->readDatagram(udpData.data(), udpData.size());
    //    }//while we have datagrams
    //    qDebug() << QDateTime::currentDateTimeUtc() << udpData;
    handleUDPData(udpData);
}//slotReadUdpData

void MainWindow::slotReadStreamingUDPData()
{
    if(g_ExitApplication)
        return;

    //    qDebug() << "Function Name: " << Q_FUNC_INFO;
    //    QString funcName = Q_FUNC_INFO;
    //    emit signalUpdateDebugLog("Function Name: " + funcName);

    QByteArray udpData;
    //    while (udpListenSocket->hasPendingDatagrams()) {
    udpData.resize(static_cast<int>(streamingUDPSocket->pendingDatagramSize()));
    streamingUDPSocket->readDatagram(udpData.data(), udpData.size());
    //    }//while we have datagrams
    //    qDebug() << QDateTime::currentMSecsSinceEpoch() << udpData;
    handleStreamingUDPData(udpData);
}//slotReadStreamingUDPData

void MainWindow::slotMakeNewMSCTCPConnection()
{
    //    qDebug() << "Function Name: " << Q_FUNC_INFO;
    //    QString funcName = Q_FUNC_INFO;
    //    emit signalUpdateDebugLog("Function Name: " + funcName);

    //    QObject *senderObject = sender();

    QTcpServer * tcpServer = dynamic_cast<QTcpServer *>(sender());
    if(!tcpServer->hasPendingConnections())
        return;

    MyTcpSocketClass *myTCPSocketObject = new MyTcpSocketClass(this);
    myTCPSocketObject->createSocket(tcpServer->nextPendingConnection());

    //    myTCPSocketObject->tcpSocket->bind(myTCPSocketObject->tcpSocket->peerPort(),QAbstractSocket::ReuseAddressHint);
    MSCTCPSocketList.append(myTCPSocketObject->tcpSocket);
    connect(myTCPSocketObject->tcpSocket, &QTcpSocket::readyRead,
            this, &MainWindow::slotReadMSCTcpSocket,Qt::QueuedConnection);
    connect(myTCPSocketObject->tcpSocket, &QTcpSocket::disconnected,
            this, &MainWindow::slotMSCSocketDisconnected);

    MSCCipherLibraryClass MSC_Cipher;
    keyNameList = MSC_Cipher.QT_GetKeys();
    if(globalConfigSettings.cipherSettings.allowPT){
        keyNameList.append("PLAINTEXT");
    }
    //    qDebug() << "Zeroizing cipher..." << MSC_Cipher.QT_Zeroize();
    emit signalUpdateApplicationLog("Sending TCP config document...");
    if(!globalConfigSettings.generalSettings.hideHandShakePackets){
        emit signalUpdateTCPLog(xmlWrapper("config","",true));
    }
    myTCPSocketObject->tcpSocket->write(xmlWrapper("config","",true));
    if(!globalConfigSettings.generalSettings.hideHandShakePackets){
        emit signalUpdateTCPLog(xmlWrapper("status","",true));
    }
    myTCPSocketObject->tcpSocket->write(xmlWrapper("status","",true));

    numberConnections++;
    ui->pushButtonTCP->setEnabled(numberConnections > 0);
    emit signalUpdateStatusBar();
}//slotMakeNewTCPConnection

void MainWindow::slotMakeNewRawTCPConnection()
{
    //    qDebug() << "Function Name: " << Q_FUNC_INFO;
    //    QString funcName = Q_FUNC_INFO;
    //    emit signalUpdateDebugLog("Function Name: " + funcName);

    //    QObject *senderObject = sender();

    QTcpServer * tcpServer = dynamic_cast<QTcpServer *>(sender());
    if(!tcpServer->hasPendingConnections())
        return;

    MyTcpSocketClass *myTCPSocketObject = new MyTcpSocketClass(this);
    myTCPSocketObject->createSocket(tcpServer->nextPendingConnection());

    //    myTCPSocketObject->tcpSocket->bind(myTCPSocketObject->tcpSocket->peerPort(),QAbstractSocket::ReuseAddressHint);
    rawTCPSocketList.append(myTCPSocketObject->tcpSocket);
    connect(myTCPSocketObject->tcpSocket, &QTcpSocket::readyRead,
            this, &MainWindow::slotReadRawTcpSocket,Qt::QueuedConnection);
    connect(myTCPSocketObject->tcpSocket, &QTcpSocket::disconnected,
            this, &MainWindow::slotRawSocketDisconnected);
    
    myTCPSocketObject->tcpSocket->write("Hello, you are connected to "+globalConfigSettings.generalSettings.positionIdentifier.toLatin1());
}//slotMakeNewTCPConnection

void MainWindow::slotReadMSCTcpSocket()
{
    //    qDebug() << Q_FUNC_INFO;
    //    QString funcName = Q_FUNC_INFO;
    //    emit signalUpdateDebugLog("Function Name: " + funcName);

    emit signalUpdateDebugLog("Locking slotReadTCPSocketMutex");
    QMutexLocker mutexLocker(&slotReadTCPSocketMutex);
    emit signalUpdateDebugLog("Locked slotReadTcpSocket");

    //    if(configSettings.dataComPortSettings.currentDevice == "MS-DMT Serial" ||
    //            configSettings.dataComPortSettings.currentDevice == "MS-DMT TCP"){
    ////        modemPingTimer->stop();
    //        modemResponseTimer->stop();
    //    }

    QByteArray localDataByteArray;
    static QByteArray staticLocalByteArray;
    QTcpSocket *tcpSocket = static_cast<QTcpSocket*>(sender());//get the object which raised the signal

    localDataByteArray.append(staticLocalByteArray);//add any residual elements from last iteration
    if(localDataByteArray.contains("<?xml version=") &&
        !localDataByteArray.startsWith("<?xml version=")){
        while (!localDataByteArray.startsWith("<?xml version=") &&
               localDataByteArray.size() > 0) {
            emit signalUpdateDebugLog("trimming garbage...");
            localDataByteArray.remove(0,1);
        }
    }
    staticLocalByteArray.clear();
    while (tcpSocket->bytesAvailable() > 0) {

        if(g_ExitApplication){
            return;
        }
        emit signalUpdateDebugLog("Reading MSC TCP socket...");
        localDataByteArray.append(tcpSocket->readAll());
    }

    while (localDataByteArray.size() > 0) {
        if(g_ExitApplication){
            //            isBusy=false;
            return;
        }
        emit signalUpdateDebugLog("parsing XML packets...");

        if(localDataByteArray.contains("<?xml version=") &&
            localDataByteArray.contains("</V3PROTOCOL>"))
        {
            while (!localDataByteArray.startsWith("<?xml version=") &&
                   localDataByteArray.size() > 0) {
                emit signalUpdateDebugLog("trimming garbage...");
                localDataByteArray.remove(0,1);
            }
            QByteArray sendDataByteArray =
                localDataByteArray.mid(
                    localDataByteArray.indexOf("<?xml version="),
                    localDataByteArray.indexOf("</V3PROTOCOL>")-
                        localDataByteArray.indexOf("<?xml version=")+QString("/V3PROTOCOL>").size()+1);
            localDataByteArray.remove(localDataByteArray.indexOf(sendDataByteArray),sendDataByteArray.size());
            emit signalUpdateDebugLog(localDataByteArray);
            emit signalUpdateDebugLog("Calling handleTcpData function directly...");
            slotHandleTcpData(sendDataByteArray, tcpSocket);  //call slot directly
        }//if we have a full packet
        else {
            emit signalUpdateDebugLog(localDataByteArray);
            break;
        }
    }//while locaDataByteArray has data
    if(localDataByteArray.size() > 0){//this should never happen
        staticLocalByteArray = localDataByteArray;
        emit signalUpdateDebugLog(staticLocalByteArray);
    }
    emit signalUpdateDebugLog("Un-Locking slotReadTCPSocketMutex");
    //    emit signalUpdateDebugLog("Leaving Function: " + funcName);
}//slotReadTcpSocket

void MainWindow::slotReadRawTcpSocket()
{
    //    qDebug() << Q_FUNC_INFO;
    //    QString funcName = Q_FUNC_INFO;
    //    emit signalUpdateDebugLog("Function Name: " + funcName);

    emit signalUpdateDebugLog("Locking slotReadTCPSocketMutex");
    QMutexLocker mutexLocker(&slotReadTCPSocketMutex);
    emit signalUpdateDebugLog("Locked slotReadTcpSocket");

    //    if(configSettings.dataComPortSettings.currentDevice == "MS-DMT Serial" ||
    //            configSettings.dataComPortSettings.currentDevice == "MS-DMT TCP"){
    ////        modemPingTimer->stop();
    //        modemResponseTimer->stop();
    //    }

    QByteArray localDataByteArray;
    static QByteArray staticLocalByteArray;
    QTcpSocket *tcpSocket = static_cast<QTcpSocket*>(sender());//get the object which raised the signal

    //Raw TCP Data
    localDataByteArray.append(staticLocalByteArray);//add any residual data from last read'
    staticLocalByteArray.clear();
    while (tcpSocket->bytesAvailable() > 0) {
        if(g_ExitApplication){
            return;
        }
        emit signalUpdateDebugLog("Reading raw socket...");
        localDataByteArray.append(tcpSocket->readAll());
    }
    slotHandleRawTcpData(localDataByteArray);  //call slot directly

}//slotReadTcpSocket

MainWindow::~MainWindow()
{
    //delete message queues
    mainMessageQueue->deleteLater();
    retryMessageQueue->deleteLater();
    //delete serial ports
    delete modemSerialPort;
    //delete timers
    modemSerialTimer->deleteLater();
    timerCheckMainMessageQueue->deleteLater();
    timerCheckRetryMessageQueue->deleteLater();
    udpTimer->deleteLater();
    //delete sockets/servers
    udpListenSocket->deleteLater();
    MSCTCPServer->deleteLater();
    //delete user interface
    delete ui;
}//~MainWindow()

bool MainWindow::setupRadio(QByteArray xmlDocument)
{
    //    qDebug() << Q_FUNC_INFO;
    //    QString funcName = Q_FUNC_INFO;
    //    emit signalUpdateDebugLog("Function Name: " + funcName);
    //    qDebug() << funcName;
    if(xmlDocument.size() > 0)
    {
        if(!globalConfigSettings.aleSettings.enableALE){
            return true;
        }
        QByteArray channel = ui->comboBoxSelectChannel->currentText().toLocal8Bit();
        if(channel.startsWith('0'))
            channel.remove(0,1);

        ClassXML xmlClass(xmlDocument);
        if(!xmlClass.getParsingError()){
            int loopCounter = 0;
            while (isALEBusy() || isRMIBusy()) {
                if(g_ExitApplication)
                    return false;

                if(loopCounter++ % 20 == 0)
                    emit signalUpdateDebugLog("Waiting... radio is busy"+QString::number(isALEBusy())+" "+QString::number(isRMIBusy()));

                QApplication::processEvents();
                QThread::msleep(100);
            }//while radio is busy
            //check for ALE ALL CALL
            if(!xmlClass.getALEAddres().isEmpty()){
                if(b_isLinked && xmlClass.getALEAddres() == linkedAddress){
                    return true;
                }//if we're already linked to the address
                else if(b_isLinked && xmlClass.getALEAddres() != linkedAddress){
                    //                    if(xmlClass.getALEAddres() != linkedAddress){
                    while (b_isLinked) {
                        if(loopCounter++ % 20 == 0)
                            emit signalUpdateDebugLog("Waiting... radio is linked to "+ALEAddress);

                        QApplication::processEvents();
                        QThread::msleep(100);
                        if(g_ExitApplication)
                            return false;
                    }//while linked
                    QThread::sleep(5);
                    //                    }//if ALE Addresses don't match
                }//if already linked
                //make ale call
                callThisAddress = xmlClass.getALEAddres();
                if(callThisAddress.toUpper() == "ALL"){
                    //if ALL CALL check for channel
                    if(!xmlClass.getRadioChannel().isEmpty()){
                        //GLOBAL ALLCALL [Address, Channel, {message}]
                        writeControlRadio("CMD GLOBAL ALLCALL "+xmlClass.getRadioChannel().toLatin1());
                        b_isCalling = true;
                    }//if we have a channel designation
                    else{
                        ALEErrorMessage = "ERROR: ALL CALL without channel specified!";
                        emit signalUpdateDebugLog("ERROR: ALL CALL without channel specified!");
                        //                        qDebug() << "ERROR: ALL CALL without channel specified!";
                        return false;
                    }//else error ALE ALL CALL with no channel
                }//if ALL address
                else {
                    writeControlRadio("CMD CALL "+callThisAddress.toLatin1()+" "+channel+ " 0");
                    b_isCalling = true;
                }
                //wait for link
                while(!b_isLinked){
                    b_isWaitingLink = true;
                    //                    isALEBusy();
                    if(b_linkError || b_linkTimeOut || b_linkKilled || !b_isCalling || dumpMessageQueue){
                        if(b_linkError){
                            //                            emit signalUpdateApplicationLog("Call failed: "+xmlClass.getALEAddres());
                            ALEErrorMessage = "Call failed: "+callThisAddress;
                            b_linkError = false;
                            b_isCalling = false;
                            b_isWaitingLink = false;
                            b_isLinked = false;
                            b_isClearing = false;
                        }
                        else if (b_linkTimeOut) {
                            //                            emit signalUpdateApplicationLog("Call timed out: "+xmlClass.getALEAddres());
                            ALEErrorMessage = "Call timed out: "+callThisAddress;
//                            writeControlRadio("CMD KILL");
                            b_isClearing = false;
                            b_linkTimeOut = false;
                            b_linkError = false;
                            b_isCalling = false;
                            b_isWaitingLink = false;
                            b_isLinked = false;
                        }
                        else if (b_linkKilled) {
                            ALEErrorMessage = "Call terminated: "+callThisAddress;
                            //                            emit signalUpdateApplicationLog("Call terminated: "+xmlClass.getALEAddres());
                            b_linkKilled = false;
                            b_isCalling = false;
                            b_isWaitingLink = false;
                            b_isLinked = false;
                            b_isClearing = false;
                        }
                        else if (!b_isCalling) {
                            b_isWaitingLink = false;
                            ALEErrorMessage = "Call terminated: "+callThisAddress;
                            //                            emit signalUpdateApplicationLog("Call terminated: "+xmlClass.getALEAddres());
                            b_isCalling = false;
                            b_isWaitingLink = false;
                            b_isLinked = false;
                            b_isClearing = false;
                        }
                        else if (dumpMessageQueue) {
                            ALEErrorMessage = "Dumping message queue...";
                        }
//                        writeControlRadio("CMD KILL");
//                        b_isCalling = false;
//                        b_isClearing = true;
//                        b_linkTimeOut = false;
//                        b_isWaitingLink = false;
                        return false;
                    }
                    if(g_ExitApplication)
                        return false;

                    if(loopCounter++ % 20 == 0)
                        emit signalUpdateApplicationLog("Waiting for ALE link...");

                    QApplication::processEvents();
                    QThread::msleep(100);
                }//while not linked
                b_isWaitingLink = false;
                b_messageInitiatedALECall = true;
                //                isALEBusy();
                return true;
            }//if we have an ALE address
            else if (xmlClass.getALEAddres().isEmpty())
            {
                if(xmlClass.getRadioChannel().isEmpty() &&
                    xmlClass.getRadioFrequency().isEmpty())
                {
                    if(b_isScanning){
                        ALEErrorMessage = "ERROR - ALE is scanning!";
                        emit signalUpdateApplicationLog("ERROR - ALE is scanning!");
                        return false;
                    }
                    else if(!b_isLinked  &&
                             globalConfigSettings.aleSettings.opMode == "ALE"){
                        ALEErrorMessage = "ERROR - ALE is not linked!";
                        emit signalUpdateApplicationLog("ERROR - ALE is not linked!");
                        return false;
                    }
                    else {
                        return true;
                    }
                }//if freq or channel is empty
                if(!xmlClass.getRadioTXMode().isEmpty()){
                    //set radio TX mode
                    ALEErrorMessage = "ERROR - TXMode not supported!";
                    emit signalUpdateApplicationLog("ERROR - TXMode not supported!");
                }//set radio TX mode

                if (!xmlClass.getRadioOPMode().isEmpty()) {
                    //set Op Mode
                    ALEErrorMessage = "ERROR - OpMode not supported!";
                    emit signalUpdateApplicationLog("ERROR - OpMode not supported!");
                }//radioOPMode

                if(!xmlClass.getRadioChannel().isEmpty()){
                    //change radio channel
                    ALEErrorMessage = "ERROR - Change channel not supported!";
                    emit signalUpdateApplicationLog("ERROR - Change channel not supported!");
                    return false;
                }//change radio channel
                else if(!xmlClass.getRadioFrequency().isEmpty()){
                    //change radio frequency
                    ALEErrorMessage = "ERROR - Change freq not supported!";
                    emit signalUpdateApplicationLog("ERROR - Change freq not supported!");
                    return false;
                }//change radio frequency
                else {
                    ALEErrorMessage = "ERROR - Unknown ALE error!";
                    emit signalUpdateApplicationLog("ERROR - Unknown ALE error!");
                    return false;
                }
            }//if not an ALE Call
            else{
                ALEErrorMessage = "ERROR - Unknown ALE error!";
                emit signalUpdateApplicationLog("ERROR - Unknown ALE error!");
                return false;
            }
        }//if no parsing error
        else {
            ALEErrorMessage = "ERROR - Parsing XML document!";
            emit signalUpdateApplicationLog("ERROR - Parsing XML document!");
            return false;
        }//else return false
    }//if we have data
    else{
        ALEErrorMessage = "ERROR - No data in XML Document!";
        emit signalUpdateApplicationLog("ERROR - No data in XML Document!");
        return false;
    }//else return false
}//setupRadio

bool MainWindow::setupModem(QByteArray xmlDocument)
{
    //    qDebug() << Q_FUNC_INFO;
    if(xmlDocument.size() > 0)
    {
        QStringList modemParameters = xmlParseModemParameters(xmlDocument);
        if(modemParameters.count() > 0){
            //            QString modemWaveform = modemParameters.at(0);
            QString modemBaud = modemParameters.at(1);
            QString modemInterleave = modemParameters.at(2);
            if(modemBaud.isEmpty()){
                modemBaud = ui->comboBoxSelectModemBaud->currentText();
            }
            if(modemInterleave.isEmpty()){
                modemInterleave = ui->comboBoxSelectModemInterleave->currentText();
            }
            if (globalConfigSettings.dataComPortSettings.currentDevice == "MS-DMT Serial") {
                isReconfiguring = true;
                emit signalUpdateDebugLog("Setting modem baud...");
                //                ui->comboBoxSelectModemBaud->setCurrentText(modemBaud);
                emit signalUpdateDebugLog("Setting modem interleave...");
                //                ui->comboBoxSelectModemInterleave->setCurrentText(modemInterleave);

                if(modemBaud == "4800"){
                    writeCommandModem("<<CMD:DATA RATE>>4800U<>");
                }
                else {
                    writeCommandModem("<<CMD:DATA RATE>>"+modemBaud.toLatin1()+
                                      modemInterleave.toLatin1()+"<>");
                }
                QApplication::processEvents();
                QThread::msleep(1500);
            }//if MS-DMT Serial
            else if (globalConfigSettings.dataComPortSettings.currentDevice == "MS-DMT TCP") {
                isReconfiguring = true;
                emit signalUpdateDebugLog("Setting modem baud...");
                //                ui->comboBoxSelectModemBaud->setCurrentText(modemBaud);
                emit signalUpdateDebugLog("Setting modem interleave...");
                //                ui->comboBoxSelectModemInterleave->setCurrentText(modemInterleave);

                if(modemBaud == "4800"){
                    writeCommandModem("CMD:DATA RATE:4800U");
                }
                else if (modemBaud == "75") {
                    writeCommandModem("CMD:DATA RATE:75L");
                }
                else {
                    writeCommandModem("CMD:DATA RATE:"+modemBaud.toLatin1()+
                                      modemInterleave.toLatin1());
                }
                QApplication::processEvents();
                //                QThread::msleep(100);
                g_IsWaitingForModemResponse = true;
                g_HaveReceivedModemResponse = false;
                int counter = 0;
                while (g_IsWaitingForModemResponse && !g_HaveReceivedModemResponse) {
                    emit signalUpdateDebugLog("Waiting for modem response...");
                    QApplication::processEvents();
                    QThread::msleep(100);
                    if(counter++ > 10){
                        emit signalUpdateDebugLog("Timeout - waiting for modem response...");
                        break;
                    }
                }//while
                g_IsWaitingForModemResponse = false;
                g_HaveReceivedModemResponse = false;
                isReconfiguring = false;
            }//if MS-DMT TCP
            return true;
        }//if we have modem parameters
        else {
            return false;
        }
    }//if XML Document
    else {
        return false;
    }
}//setupModem

bool MainWindow::setupCipher(QByteArray xmlDocument)
{
    //    qDebug() << Q_FUNC_INFO;

    if(xmlDocument.size() > 0)
    {
        ClassXML xmlClass(xmlDocument);
        if(xmlClass.getEncrypt() == "0"){
            encryptionkey.clear();
            useEncryption = false;
            return true;
        }
        else {
            encryptionkey = xmlClass.getEncryptionKey();
            if(!encryptionkey.isEmpty()){
                useEncryption = true;
                return true;
            }
            else
                return false;
        }
    }//if we have data
    else
        return false;
}//setupCipher

void MainWindow::setupCompression(QByteArray xmlDocument)
{
    //    qDebug() << Q_FUNC_INFO;
    ClassXML xmlClass(xmlDocument);
    if(xmlClass.getCompress() == "1"){
        useCompression = true;
    }
    else {
        useCompression = false;
    }
}//setupCompression

int MainWindow::sendMessages(QList<QByteArray> messageList, int messageSerialNumber)
{
    Q_UNUSED(messageSerialNumber)
    qDebug() << Q_FUNC_INFO;
    //    qDebug() << g_IsTransmitting << g_IsWritingData << g_IsRadioBusy << g_IsReceiving;
    //TODO messageSerialNumber is for future extended checksum header when enabled
    int numMessagesSent = 0;
    if(messageList.count() > 0)
    {
        //        QString funcName = Q_FUNC_INFO;
        //        emit signalUpdateDebugLog("Function Name: " + funcName);

        QListIterator <QByteArray>listIterator(messageList);
        while (listIterator.hasNext()){
            if(g_ExitApplication || b_isResetting)
                return numMessagesSent;
            bool needDelay = false;
            while (isRMIBusy()) {
                //                qDebug() << Q_FUNC_INFO << "?: RMI is busy...";
                if(g_ExitApplication)
                    return numMessagesSent;
                QThread::msleep(1000);
                QApplication::processEvents();
                needDelay = true;
            }//while radio is occupied
            if(needDelay){
                //                qDebug() << Q_FUNC_INFO << "?: Delay before next transmission..." << configSettings.dataComPortSettings.delayBeforeNextTX*1000;
                QThread::msleep(globalConfigSettings.dataComPortSettings.delayBeforeNextTX*1000);
                needDelay = false;
            }
            QByteArray plaintext = listIterator.next();
            if(plaintext.endsWith("==\r\n")){
                plaintext = QByteArray::fromBase64(plaintext);
            }
            QByteArray cipherText;
            if(!g_IsCipherInit && useEncryption) {
                emit signalUpdateApplicationLog("ERROR - Cipher not initialized!");
                ALEErrorMessage = "ERROR - Cipher not initialized!";
                g_IsTransmitting = false;
                g_IsWritingData = false;
                return numMessagesSent;
            }//if cipher is not initialized
            else if(!useEncryption && !globalConfigSettings.cipherSettings.allowPT) {
                emit signalUpdateApplicationLog("ERROR - Plaintext not allowed!");
                ALEErrorMessage = "ERROR - Plaintext not allowed!";
                g_IsTransmitting = false;
                g_IsWritingData = false;
                return numMessagesSent;
            }//else if PT not allowed
            else {
                //                cipherText = checkCompressData(plaintext);
                //encrypt the data
                //if cipher is initialized and we want to encrypt the data
                //                if((g_IsCipherInit && useEncryption)){
                cipherText = encryptData(plaintext);
                if(cipherText.size() == 0){
                    g_IsTransmitting = false;
                    g_IsWritingData = false;
                    ALEErrorMessage = "Unknown error - Failed to produce ciphertext!";
                    return numMessagesSent;
                }
                if(cipherText.startsWith("ERROR: ")){
                    emit signalUpdateApplicationLog(printables(cipherText));
                    emit signalUpdateApplicationLog("Messages in queue: "+QString::number(mainMessageQueue->getMessageCount()));
                    emit signalUpdateStatusBar();
                    g_IsTransmitting = false;
                    g_IsWritingData = false;
                    return numMessagesSent;
                }
                emit signalUpdateModemLog(printables(plaintext), false, false);
                emit signalUpdateModemLog(cipherText, false, true);
            }//else add checksum header and check to see if we need to encrypt the data

            g_IsTransmitting = true;
            emit signalWriteModemData(cipherText);

            //echo sucess to all connected clients
            writeTcpSockets(xmlWrapper("ack",printables(plaintext),true),"");
            emit signalUpdateStatusBar();
            numMessagesSent++;

            needDelay = false;
            if(globalConfigSettings.dataComPortSettings.currentDevice == "MS-DMT TCP"){
                while (isRMIBusy()) {
                    //                    qDebug() << Q_FUNC_INFO << "2: RMI is busy...";
                    QThread::msleep(1000);
                    QApplication::processEvents();
                    if(g_ExitApplication){
                        return numMessagesSent;
                    }
                    needDelay = true;
                }
                if(needDelay && !b_delaySet){
                    //                    qDebug() << Q_FUNC_INFO << "2: Delay before next transmission..." << configSettings.dataComPortSettings.delayBeforeNextTX*1000;
                    b_delaySet = true;
                    QThread::msleep(globalConfigSettings.dataComPortSettings.delayBeforeNextTX*1000);
                    needDelay = false;
                }
            }//if MS-DMT TCP
            else {
                //TODO Need a better way to set delay between message TX
                int delay = calculateDelayBetweenMessages(cipherText.size()+globalConfigSettings.dataComPortSettings.delayBeforeNextTX);
                emit signalUpdateDebugLog("Calculated TX time: "+
                                          QString::number(calculateDelayBetweenMessages(cipherText.size())));
                emit signalUpdateDebugLog("Configured delay: "+
                                          QString::number(globalConfigSettings.dataComPortSettings.delayBeforeNextTX));
                emit signalUpdateDebugLog("Total time before sending next message: "+
                                          QString::number(delay+globalConfigSettings.dataComPortSettings.delayBeforeNextTX));
                for(int i=0; i<delay*4; i++)
                {
                    if(g_ExitApplication){
                        g_IsTransmitting = false;
                        g_IsWritingData = false;
                        return numMessagesSent;
                    }
                    //                    emit signalUpdateDebugLog("Calculated delay...");
                    QApplication::processEvents();
                    QThread::msleep(250);
                }//pause before sending next message
                g_IsTransmitting = false;
                g_IsWritingData = false;
                g_IsRadioBusy = false;
                emit signalUpdateStatusDisplay("Idle...");

                //                qDebug() << Q_FUNC_INFO << "3: Delay before next transmission..." << configSettings.dataComPortSettings.delayBeforeNextTX*1000;
                QThread::sleep(static_cast<ulong>(globalConfigSettings.dataComPortSettings.delayBeforeNextTX));

                //MS-DMT error messages
                if(modemReceivedDataByteArray.startsWith("<<CMD:ERROR: SEND FILE TERMINATOR ERROR>>")){
                    emit signalUpdateApplicationLog("Detected MS-DMT CMD:ERROR...");
                    modemReceivedDataByteArray.clear();
                    emit signalUpdateApplicationLog("Resending message again...");
                    writeTcpSockets(xmlWrapper("error","Resending message...",true),"");
                    listIterator.previous();//rewind list iterator
                    numMessagesSent--;
                }//if detected error rewind iterator to send it again
                emit signalUpdateApplicationLog("Messages in queue: "+QString::number(mainMessageQueue->getMessageCount()));
                emit signalUpdateStatusBar();
                g_IsTransmitting = false;
                //            QApplication::processEvents();
                QThread::msleep(10);
            }//else NOT MS-DMT TCP

        }//foreach message in message list
        g_IsTransmitting = false;
        g_IsWritingData = false;
        return numMessagesSent;
    }//if we have messages
    else{
        emit signalUpdateStatusBar();
        g_IsTransmitting = false;
        g_IsWritingData = false;
        return numMessagesSent;
    }
}//sendMessages

bool MainWindow::xmlParseRadioParameters(QByteArray xmlDocument)
{
    if(xmlDocument.size() > 0)
    {
        return true;
    }
    else
        return false;
}//xmlParseRadioParameters

QStringList MainWindow::xmlParseModemParameters(QByteArray xmlDocument)
{
    QStringList modemParameters;
    if(xmlDocument.size() > 0)
    {
        ClassXML xmlClass(xmlDocument);
        modemParameters.append(xmlClass.getModemWaveform());
        modemParameters.append(xmlClass.getModemDataRate());
        modemParameters.append(xmlClass.getModemInterleave());
        //        ui->comboBoxSelectModemMode->setCurrentText(xmlClass.getModemInterleave());
        //        sendStatusChange();
    }
    return modemParameters;
}//xmlParseModemParameters

bool MainWindow::xmlParseCipherParameters(QByteArray xmlDocument)
{
    //    QString funcName = Q_FUNC_INFO;
    //    emit signalUpdateDebugLog("Function Name: " + funcName);

    bool parsedCipher = false;
    QXmlStreamReader xmlReader(xmlDocument);
    while (!xmlReader.isStartDocument()) {
        xmlReader.readNext();
        //        qDebug() << xmlReader.tokenString() << xmlReader.name();
    }
    while (!xmlReader.atEnd()) {
        xmlReader.readNextStartElement();
        //        qDebug() << xmlReader.tokenString() << xmlReader.name();
        if(xmlReader.atEnd())
        {
            break;
        }
        if(xmlReader.name().toString() == "V3PROTOCOL"){
            bool isV3PROTOCOL = true;
            while (isV3PROTOCOL) {
                xmlReader.readNextStartElement();
                //                qDebug() << xmlReader.tokenString() << xmlReader.name();
                if(xmlReader.isEndElement() && xmlReader.name().toString() == "V3PROTOCOL"){
                    isV3PROTOCOL = false;
                }//end RIMPACKET
                else if (xmlReader.name().toString() == "HEADER") {
                    bool isHEADER = true;
                    while (isHEADER) {
                        xmlReader.readNextStartElement();
                        //                        qDebug() << xmlReader.tokenString() << xmlReader.name();
                        if(xmlReader.isEndElement() && xmlReader.name().toString() == "HEADER"){
                            isHEADER = false;
                        }//end HEADER
                        else if (xmlReader.name().toString() == "COMPRESS") {
                            compress = xmlReader.readElementText();
                        }
                        else if (xmlReader.name().toString() == "ENCRYPT") {
                            encrypt = xmlReader.readElementText();
                        }
                        else if (xmlReader.name().toString() == "ENCRYPTIONKEY") {
                            encryptionkey = xmlReader.readElementText();
                            parsedCipher = true;
                        }
                    }//while is HEADER
                }//HEADER
            }//while iSV3PROTOCOL
        }//if V3PROTOCOL Document
    }//while not end of document

    if(encrypt == "1"){
        useEncryption = true;
    }
    else{
        useEncryption = false;
    }
    if(compress == "1"){
        useCompression = true;
    }
    else{
        useCompression = false;
    }
    return parsedCipher;
}//xmlParseCipherParameters

bool MainWindow::xmlParseHeader(QByteArray xmlDocument)
{
    //    QString funcName = Q_FUNC_INFO;
    //    emit signalUpdateDebugLog("Function Name: " + funcName);

    /* The new client-CP packet format allows a client such as BS and MM to instruct the CP on how to proceed.
         * Not all fields will be used in all cases (or maybe ever) but we include those fields for possible future expansion.
         * A future CP may also include a serial connection to an antenna rotator controller so we include a beam heading. (azimuth)
         * Some radios may not use channels or a desired frequency is not stored in a channel so we include frequency.
         * Some message recipients can only receive in certain modem waveforms so we include that also.
         * The packet priority contains the priority of the highest priority message in the packet.
         * The messages must be stored in priority format with the highest priority message in message slot 1.
         * This way the CP does not need to concern itself with sorting messages and just sends them in the order given.
         * The CP will not parse or use any 126/127 procedure indicators so the CP may continue to be used in the future if 126/127 are discontinued.
         * Only Message Machine and Message Editor(s) should be concerned with 126/127 procedure.
         *
         * In any descrepencies between code examples and the packet format below the code examples are considered correct
         *
         * XML packet format:
         *
         * <V3PROTOCOL>
         *      <HEADER>
         *          <POSITIONID>positionID</POSITIONID>
         *          <PRIORITY>priority</PRIORITY>
         *          <COMMAND>handshake</COMMAND>
         *          <KEYLIST>keylist</KEYLIST>
         *          <LISTENIPADDRESS>ipaddress</LISTENIPADDRESS>
         *          <LISTENTCPPORT>ipport<LISTENTCPPORT>
         *          <COMPRESS>compress</COMPRESS>
         *          <ENCRYPT>encrypt</ENCRYPT>
         *          <ENCRYPTIONKEY>encryptionKey</ENCRYPTIONKEY>
         *          <SOURCESTATION>sourceStation</SOURCESTATION>
         *          <DESTINATIONSTATION>destinationStation</DESTINATIONSTATION>
         *          <ALEADDRESS>ALEAddress</ALEADDRESS>
         *          <ANTENNABEAMHEADING>beamHeading</ANTENNABEAMHEADING>
         *          <RADIOCHANNEL>channel</RADIOCHANNEL>
         *          <RADIOFREQUENCY>frequency</RADIOFREQUENCY>
         *          <RADIOOPMODE>OPMode</RADIOOPMODE>
         *          <RADIOTXMODE>TXMode</RADIOTXMODE>
         *          <MODEMDATARATE>baud</MODEMDATARATE>
         *          <MODEMINTERLEAVE>interleave</MODEMINTERLEAVE>
         *          <MODEMWAVEFORM>waveform</MODEMWAVEFORM>
         *          <CHECKSUM>checksum</CHECKSUM>
         *      </HEADER>
         *      <PAYLOAD>
         *          <DATA>
         *              <LINE>This is message number 1</LINE>
         *              <LINE>Message line 1</LINE>
         *              <LINE>Message line 2</LINE>
         *          </DATA>
         *          <DATA>
         *              <LINE>This is message number 2</LINE>
         *              <LINE>Message line 1</LINE>
         *              <LINE>Message line 2</LINE>
         *          </DATA>
         *          <DATA>
         *              <LINE>This is message number 3</LINE>
         *              <LINE>Message line 1</LINE>
         *              <LINE>Message line 2</LINE>
         *          </DATA>
         *      <PAYLOAD>
         * </V3PROTOCOL>
         *
         * where:
         *
         * positionID = position identifier of client application sending data (NOT BLANK)
         * priority = highest prioity of message(s) (X) 1=Z, 2=O, 3=P, 4=R
         * handshake = handshake message (helo, update, bye)
         * keylist = keylist of comma separated keyfile names
         * listenipaddress = address client/interface is listening on
         * listenipport = port client/interface is listening on
         * compress = whether to compress or not - 0 = no 1 = yes
         * encrypt = whether to encrypt or not - 0 = no 1 = yes
         * encryptionKey = name of part 1 key file to use
         * sourceStation = callsign of station client application sending data (if blank callsign in CP will be used)
         * destinationStation = callsign of receiving station (ALL or CALLSIGN)
         * ALEAddress = ALE address of receiving station (ALEADD or blank)
         * beamHeading = azimuth bearing (XXX or blank)
         * channel = channel number on radio (XXX or blank)
         * frequency = frequency (XX.XXX.XX or blank)
         * OPMode = operational mode of radio (AAA or blank) (ALE, 3G, SSB, HOP)
         * TXMode = TX mode on radio (AAA or blank) (SSB, LSB, AME, CW)
         * baud = modem BAUD (XXXX or blank)
         * interleave = modem interleave (AA or blank)
         * waveform = modem wave form (AAAA or blank)
         * checksum = PASS, FAIL, UNKN
         *
         * */

    bool isHeader = false;

    positionID.clear(); ALEAddress.clear(); ALEAddresses.clear(); TXsourceStation.clear();
    TXdestinationStation.clear(); encrypt.clear();
    priority.clear(); compress.clear(); encryptionkey.clear();
    antennaBeamHeading.clear(); radioChannel.clear(); radioFrequency.clear();
    radioOPMode.clear(); radioTXMode.clear(); modemBaud.clear();
    modemInterleave.clear(); modemWaveform.clear();



    QXmlStreamReader xmlReader(xmlDocument);
    while (!xmlReader.isStartDocument()) {
        xmlReader.readNext();
        //            qDebug() << xmlReader.tokenString() << xmlReader.name();
    }
    while (!xmlReader.atEnd()) {
        xmlReader.readNextStartElement();
        //            qDebug() << xmlReader.tokenString() << xmlReader.name();
        if(xmlReader.atEnd())
        {
            break;
        }
        if(xmlReader.name().toString() == "V3PROTOCOL"){
            bool isV3PROTOCOL = true;
            while (isV3PROTOCOL) {
                xmlReader.readNextStartElement();
                //                    qDebug() << xmlReader.tokenString() << xmlReader.name();
                if(xmlReader.isEndElement() && xmlReader.name().toString() == "V3PROTOCOL"){
                    isV3PROTOCOL = false;
                }//end RIMPACKET
                else if (xmlReader.name().toString() == "HEADER") {
                    bool isHEADER = true;
                    isHeader = true;
                    while (isHEADER) {
                        xmlReader.readNextStartElement();
                        //                            qDebug() << xmlReader.tokenString() << xmlReader.name();
                        if(xmlReader.isEndElement() && xmlReader.name().toString() == "HEADER"){
                            isHEADER = false;
                        }//end HEADER
                        //else grab all the header info
                        else if (xmlReader.name().toString() == "POSITIONID") {
                            //assign your own variables
                            positionID = xmlReader.readElementText();
                            if(positionID == globalConfigSettings.generalSettings.positionIdentifier){
                                return false;//ignore my own packets
                            }
                        }
                        else if (xmlReader.name().toString() == "COMMAND") {
                            QString handshake = xmlReader.readElementText();
                            //if introduction
                            if(handshake == "helo"){
                                //introduce ourselves
                                if(MSCTCPServer->isListening()){
                                    QRandomGenerator random(static_cast<uint>(QTime::currentTime().msec()));
                                    //                                    qsrand(static_cast<uint>(QTime::currentTime().msec()));
                                    QTimer::singleShot(random.generate() % ((2000 + 1) - 10) + 10, [=] {
                                        // must have CONFIG += c++11 in .pro file
                                        //start the server
                                        slotBroadcastID("update");
                                    });
                                    //                                    QTimer::singleShot(qrand() % ((2000 + 1) - 10) + 10, this, SLOT(slotBroadcastID("update")));
                                    //                                    slotBroadcastID("update");
                                }
                            }
                        }
                        else if (xmlReader.name().toString() == "KEYLIST") {
                            xmlReader.readElementText();
                        }
                        else if (xmlReader.name().toString() == "LISTENIPADDRESS") {
                            xmlReader.readElementText();
                        }
                        else if (xmlReader.name().toString() == "LISTENTCPPORT") {
                            xmlReader.readElementText();
                        }
                        else if (xmlReader.name().toString() == "PRIORITY") {
                            priority = xmlReader.readElementText();
                        }
                        else if (xmlReader.name().toString() == "COMPRESS") {
                            compress = xmlReader.readElementText();
                        }
                        else if (xmlReader.name().toString() == "ENCRYPT") {
                            encrypt = xmlReader.readElementText();
                        }
                        else if (xmlReader.name().toString() == "ENCRYPTIONKEY") {
                            encryptionkey = xmlReader.readElementText();
                        }
                        else if (xmlReader.name().toString() == "SOURCESTATION") {
                            TXsourceStation = xmlReader.readElementText();
                        }
                        else if (xmlReader.name().toString() == "DESTINATIONSTATION") {
                            TXdestinationStation = xmlReader.readElementText();
                        }
                        else if (xmlReader.name().toString() == "ALEADDRESS") {
                            ALEAddress = xmlReader.readElementText();
                        }
                        else if (xmlReader.name().toString() == "ALEADDRESSLIST") {
                            ALEAddresses = xmlReader.readElementText();
                        }
                        else if (xmlReader.name().toString() == "ANTENNABEAMHEADING") {
                            antennaBeamHeading = xmlReader.readElementText();
                        }
                        else if (xmlReader.name().toString() == "RADIOCHANNEL") {
                            radioChannel = xmlReader.readElementText();
                        }
                        else if (xmlReader.name().toString() == "RADIOFREQUENCY") {
                            radioFrequency = xmlReader.readElementText();
                        }
                        else if (xmlReader.name().toString() == "RADIOOPMODE") {
                            radioOPMode = xmlReader.readElementText();
                        }
                        else if (xmlReader.name().toString() == "RADIOTXMODE") {
                            radioTXMode = xmlReader.readElementText();
                        }
                        else if (xmlReader.name().toString() == "MODEMDATARATE") {
                            modemBaud = xmlReader.readElementText();
                        }
                        else if (xmlReader.name().toString() == "MODEMINTERLEAVE") {
                            modemInterleave = xmlReader.readElementText();
                        }
                        else if (xmlReader.name().toString() == "MODEMWAVEFORM") {
                            modemWaveform = xmlReader.readElementText();
                        }
                        else if (xmlReader.name().toString() == "CHECKSUM") {
                            xmlReader.readElementText();
                        }
                        else {
                            emit signalUpdateApplicationLog("ERROR parsing xml header: " +
                                                            xmlReader.tokenString() + xmlReader.name().toString() +
                                                            QString::number(xmlReader.lineNumber()));
                            //                            qDebug() << "ERROR parsing xml header: " << xmlReader.tokenString() << xmlReader.name() << xmlReader.lineNumber();
                        }
                    }//while is HEADER
                }//HEADER
            }//while iSV3PROTOCOL
        }//if CP PACKET
    }//while not end of document


    //    cipherKey = encryptionkey;
    if(encrypt == "1"){
        useEncryption = true;
    }
    else{
        useEncryption = false;
    }
    if(compress == "1"){
        useCompression = true;
    }
    else{
        useCompression = false;
    }

    return isHeader;
}//xmlParseHeader

bool xmlParseCipherParameters(QByteArray xmlDocument)
{
    if(xmlDocument.size() > 0)
    {

        return true;
    }
    else
        return false;
}//xmlParseCipherParameters

QList<QByteArray> MainWindow::xmlParseMessageList(QByteArray xmlDocument)
{
    //    QString funcName = Q_FUNC_INFO;
    //    emit signalUpdateDebugLog("Function Name: " + funcName);

    QList<QByteArray> messageList;
    if(xmlDocument.size() > 0)
    {
        //        emit signalUpdateTCPLog("RECEIVED: \r\n"+xmlDocument);

        QXmlStreamReader xmlReader(xmlDocument);
        while (!xmlReader.isStartDocument()) {
            xmlReader.readNext();
            //            qDebug() << xmlReader.tokenString() << xmlReader.name();
        }
        while (!xmlReader.atEnd()) {
            xmlReader.readNextStartElement();
            //            qDebug() << xmlReader.tokenString() << xmlReader.name();
            if(xmlReader.atEnd())
            {
                break;
            }
            if(xmlReader.name().toString() == "V3PROTOCOL"){
                bool isV3PROTOCOL = true;
                while (isV3PROTOCOL) {
                    xmlReader.readNextStartElement();
                    //                    qDebug() << xmlReader.tokenString() << xmlReader.name();
                    if(xmlReader.isEndElement() && xmlReader.name().toString() == "V3PROTOCOL"){
                        isV3PROTOCOL = false;
                    }//end V3PROTOCOL
                    else if (xmlReader.name().toString() == "PAYLOAD") {
                        bool isPAYLOAD = true;
                        while (isPAYLOAD) {
                            xmlReader.readNextStartElement();
                            //                            qDebug() << xmlReader.tokenString() << xmlReader.name();
                            if(xmlReader.isEndElement() && xmlReader.name().toString() == "PAYLOAD"){
                                isPAYLOAD = false;
                            }//end PAYLOAD
                            else if (xmlReader.name().toString() == "DATA") {
                                //                                bool isMESSAGE = true;
                                QByteArray message;
                                message.append(xmlReader.readElementText().toLatin1());
                                QByteArrayList tempList = message.split('\n');
                                message.clear();
                                foreach (QByteArray tempArray, tempList) {
                                    message.append(tempArray+"\r\n");
                                }


                                messageList.append(message);
                            }//if DATA
                        }//while isPAYLOAD
                    }//else if PAYLOAD
                }//while iSV3PROTOCOL
            }//if CP PACKET
        }//while not end of document

        return messageList;
    }//if we have content in our xmlDocument

    return messageList;
}//xmlParseMessageList

//QByteArray MainWindow::printables(QByteArray byteArray)
//{
//    //    qDebug() << "Function Name: " << Q_FUNC_INFO;
//    //    QString funcName = Q_FUNC_INFO;
//    //    emit signalUpdateDebugLog("Function Name: " + funcName);

//    for(int i=0;i<byteArray.size();i++){
//        if(byteArray.at(i) == '\r' ||
//            byteArray.at(i) == '\n')
//            continue;
//        else if(byteArray.at(i) < 32 || byteArray.at(i) > 126)
//            byteArray[i] = '.';
//    }
//    return byteArray;
//}//printables

void MainWindow::slotHandleModemReceivedlData()
{
    if(modemReceivedDataByteArray.isEmpty()){
        return;
    }

    static bool isBusy=false;
    if(isBusy){
        emit signalUpdateDebugLog("slotHandleModemSerialData busy - returning: "+modemReceivedDataByteArray);
        return;
    }
    isBusy = true;

    //    QString funcName = Q_FUNC_INFO;
    //    emit signalUpdateDebugLog("Function Name: " + funcName);
    //    QApplication::processEvents(QEventLoop::ExcludeSocketNotifiers);


    if(modemSerialPort->isOpen()){
        while (modemSerialPort->bytesAvailable() > 0) {
            //        qDebug() << "Final read of modem serial data...";
            modemReceivedDataByteArray.append(modemSerialPort->readAll());
        }
    }
    else if(MSDMTDataSocket->isOpen()){
        //        while (MSDMTDataSocket->bytesAvailable() > 0) {
        modemReceivedDataByteArray.append(MSDMTDataSocket->readAll());
        //        }
    }

    if(modemReceivedDataByteArray.startsWith("<<CMD:ERROR")){
        //        qDebug() << "Handle mode data detected modem error response";
        modemSerialTimer->setInterval(globalConfigSettings.dataComPortSettings.serialTimeout.toInt());
        writeTcpSockets(xmlWrapper("error",printables(modemReceivedDataByteArray),true),"");

        modemReceivedDataByteArray.clear();
        isBusy=false;
        g_IsRadioBusy = false;
        g_IsReceiving= false;
        //        if(configSettings.dataComPortSettings.currentDevice == "MS-DMT Serial"){
        //            modemResponseTimer->start();
        //            //            radioPingTimer->start();
        //        }
        return;
    }
    else if (modemReceivedDataByteArray.startsWith("<<CMD:DATA RATE?>>")) {
        QByteArrayList dataList = modemReceivedDataByteArray.split('?');
        dataList.removeFirst();
        QString modemData = dataList.at(0);
        modemData.remove(">>");
        modemData.remove("\r\n");
        if(!modemData.isEmpty()){
            QString interleave = modemData.at(modemData.size()-1);
            modemData.remove(interleave);
            ui->comboBoxSelectModemBaud->setCurrentText(modemData);
            ui->comboBoxSelectModemInterleave->setCurrentText(interleave);
            ui->comboBoxConfiguredModemSpeed->setCurrentText(modemData);
            ui->comboBoxConfiguredModemInterleave->setCurrentText(interleave);
            //        QApplication::processEvents();
            sendStatusChange();
            emit signalUpdateChatDialogConfig(xmlWrapper("status","",true));
        }

        modemReceivedDataByteArray.clear();
        isBusy=false;
        g_IsRadioBusy = false;
        g_IsReceiving= false;
        //        if(configSettings.dataComPortSettings.currentDevice == "MS-DMT Serial"){
        //            modemResponseTimer->start();
        //        }
        return;
    }//if MS-DMT sends data rate

    wasEncrypted = false;
    wasCompressed = false;
    decryptionkey.clear();
    RXdestinationStation = "ALL";
    RXsourceStation = "UNKN";

    emit signalUpdateProgressBar(100,100);
    emit signalUpdateModemLog(modemReceivedDataByteArray, true, true);

    QFile hexDump(QApplication::applicationDirPath()+"/lastReceived.hex");
    QTextStream out(&hexDump);
    hexDump.open(QFile::WriteOnly);
    out << modemReceivedDataByteArray.toHex().toUpper();
    hexDump.flush();
    hexDump.close();

    QByteArray rawByteArray = modemReceivedDataByteArray;

    if(rawByteArray.endsWith("==")){
        QByteArray uuEncode = QByteArray::fromBase64(rawByteArray);
        if(!uuEncode.isEmpty()){
            rawByteArray = uuEncode;
        }
    }//uuencoded data

    //decrypt the data
    rawByteArray = decryptData(rawByteArray);
    if(rawByteArray.endsWith("==")){
        QByteArray uuEncode = QByteArray::fromBase64(rawByteArray);
        if(!uuEncode.isEmpty()){
            rawByteArray = uuEncode;
        }
    }//uuencoded data
    emit signalUpdateModemLog(rawByteArray, true, false);
    if(rawByteArray.startsWith("ERROR: ")){
        rawByteArray = xmlWrapper("error", printables(rawByteArray), true);
    }
    else {
        rawByteArray = xmlWrapper("data", printables(rawByteArray), false);
        if(checksumBool == PASS){
            emit signalUpdateChatDialogMessage(rawByteArray);
        }
        else {
            if(globalConfigSettings.modem_Dialog.displayModemOnRecvError){
                dialogModem->show();
                dialogModem->raise();
            }
        }
    }
    //TODO mode to else bracket after testing/debugging
    writeTcpSockets(rawByteArray,rawByteArray);
    writeReceivedFile(rawByteArray);

    modemReceivedDataByteArray.clear();
    modemSerialTimer->setInterval(globalConfigSettings.dataComPortSettings.serialTimeout.toInt());

    QTimer::singleShot(100, [=] {
        // must have CONFIG += c++11 in .pro file
        emit signalUpdateStatusDisplay("Idle...");
        if(globalConfigSettings.generalSettings.useProgressBar){
            emit signalResetProgressBar();
            dialogProgressBar->hide();
        }
    });

    isBusy=false;
    foundKey = false;
    foundChecksum = false;
    predictedByteCountSize = 0;
    g_IsRadioBusy = false;
    g_IsReceiving= false;
}//slotHandleModemSerialData

void MainWindow::sendStatusChange()
{
    if(!MSCTCPSocketList.isEmpty()){
        if(!globalConfigSettings.generalSettings.hideHandShakePackets)
            emit signalUpdateTCPLog(xmlWrapper("config","",true));
        writeTcpSockets(xmlWrapper("config","",true),"");
        if(!globalConfigSettings.generalSettings.hideHandShakePackets)
            emit signalUpdateTCPLog(xmlWrapper("status","",true));
        writeTcpSockets(xmlWrapper("status","",true),"");
    }
}//slotStatChanged

bool MainWindow::isPlaintext(QByteArray byteArray)
{
    //    qDebug() << "Function Name: " << Q_FUNC_INFO;
    //    QString funcName = Q_FUNC_INFO;
    //    emit signalUpdateDebugLog("Function Name: " + funcName);
    if(byteArray.size() == 0)
        return false;

    //remove pre/post NULLs
    while (byteArray.startsWith((char)0x00)) {
        byteArray.remove(0,1);
    }
    while (byteArray.endsWith((char)0x00)) {
        byteArray.chop(1);
    }
    QRegularExpression nonPrintRegX("[^ -~\r\n]");//regexp for non-printable character
    QString tempString = QString::fromStdString(byteArray.toStdString());

    //if non printable count == 0 it's all plaintext
    if(tempString.count(nonPrintRegX) == 0){
        //        qDebug() << "isPlaintext found no non-printable characters";
        return true;
    }
    //if string contains any 127 proceedure it must be plaintext - add more as needed
    else if(byteArray.toUpper().contains("ZCZC") ||
             byteArray.toUpper().contains("UUUU") ||
             byteArray.toUpper().contains("UNCLAS") ||
             byteArray.toUpper().contains("EEEE") ||
             byteArray.toUpper().contains("[DIGEST:") ||
             byteArray.toUpper().contains("RYRY") ||
             byteArray.contains("     \r\n") ||
             byteArray.toUpper().contains("NNNN") ||
             byteArray.toUpper().contains(" DE ") ||
             byteArray.toUpper().contains("DE ") ||
             byteArray.toUpper().contains("BT ") ||
             byteArray.contains(" // ")
             ){
        //        qDebug() << "isPlaintext found 127 or 201 proceedure markers";
        return true;
    }
    //else if the number of nonprint is less than one 5th of all the data - adjust as needed
    else if(tempString.count(nonPrintRegX) < tempString.size()/20){
        //        qDebug() << "isPlaintext found a few non-printable characters";
        return true;
    }
    //else must NOT be plaintext
    else{
        //        qDebug() << "isPlaintext appears to not be plaintext";
        //        qDebug() << tempString;
        //        qDebug() << tempString.count(nonPrintRegX) << tempString.size() << tempString.size()/20;
        return false;
    }
}//isPlaintext

void MainWindow::slotHandleTcpData(QByteArray dataByteArray, QTcpSocket *tcpSocket)
{
    if(dataByteArray.isEmpty()){
        return;
    }
    //    QString funcName = Q_FUNC_INFO;
    //    emit signalUpdateDebugLog("Function Name: " + funcName);
    aleLinkTimeOutTimer->start(globalConfigSettings.aleSettings.aleLinkTimeOut.toInt()*1000);
    handleTcpData(dataByteArray, tcpSocket);
}//slotHandleTcpData

void MainWindow::slotHandleRawTcpData(QByteArray dataByteArray)
{
    if(dataByteArray.isEmpty()){
        //        qDebug() << "Data empty...";
        return;
    }
    //    qDebug() << Q_FUNC_INFO;
    aleLinkTimeOutTimer->start(globalConfigSettings.aleSettings.aleLinkTimeOut.toInt()*1000);

    //    futurehandleTCPData = QtConcurrent::run(this, &MainWindow::handleTcpData, dataByteArray);
    handleRawTcpData(dataByteArray);
}//slotHandleRawTcpData

void MainWindow::handleTcpData(QByteArray dataByteArray, QTcpSocket *tcpSocket)
{
    if(dataByteArray.isEmpty()){
        return;
    }

    QMutexLocker mutexLocker(&slotHandleTCPDataMutex);

    if(ui->pushButtonTCP->isEnabled()){
        emit signalUpdateTCPLog(QDateTime::currentDateTimeUtc().toString("ddMMMyy-hh:mm:ss.zzz - ").toLatin1()+"RECEIVED TCP: \r\n"+dataByteArray);
    }
    ClassMessage * newMessage = new ClassMessage(dataByteArray, this);
    connect(newMessage, &ClassMessage::signalUpdateDebugLog,
            this, &MainWindow::signalUpdateDebugLog);
    connect(newMessage, &ClassMessage::signalUpdateApplicationLog,
            this, &MainWindow::signalUpdateApplicationLog);
    //write settings because message serial number incremented
    writeSettings();

    if (newMessage->getMessageText().size() > 0) {
        if(globalConfigSettings.generalSettings.useRetryQueue){
            writeRetryFile(newMessage->getXMLDocument());
        }
        emit signalUpdateApplicationLog("Message(s) added to queue: " +
                                        QString::number(static_cast<int>(mainMessageQueue->insertMessage(newMessage))));

        emit signalUpdateStatusBar();
    }//if we have a message
    else {
        delete newMessage;
        newMessage = nullptr;
        ClassXML xmlClass(dataByteArray);
        if(!xmlClass.getParsingError()){
            //            positionID = xmlClass.getPositionID();
            //            if(positionID == globalConfigSettings.generalSettings.positionIdentifier){
            //                return;
            //            }//if positionID is ours ignore it
            if (xmlClass.getCommand().toUpper() == "ALE CALL" &&
                !xmlClass.getALEAddres().isEmpty()) {
                //                qDebug() << "Calling: " + xmlClass.getALEAddres().toLatin1();
                emit signalUpdateApplicationLog("Calling: " + xmlClass.getALEAddres().toLatin1());
                callThisAddress = xmlClass.getALEAddres();
                if(!setupRadio(dataByteArray))
                {
                    //display or log error
                    emit signalUpdateApplicationLog(ALEErrorMessage);
                    //send error message to clients
                    writeTcpSockets(xmlWrapper("error", ALEErrorMessage.toLatin1(), true),"");
                    ALEErrorMessage.clear();
                }//failed to set up ALE
                //                writeControlRadio("CMD CALL "+callThisAddress.toLatin1()+" 0");
                //                isCalling = true;
            }//else if command to call ALE address and we have an ale address
            else if (xmlClass.getCommand() == "ping") {
                //send a pong back
                //                qDebug() << "Sending pong to " + xmlClass.getPositionID();
                if(tcpSocket != nullptr)
                {
                    tcpSocket->write(xmlWrapper("pong","",true));
                    tcpSocket->flush();
                    emit signalUpdateTCPLog(QDateTime::currentDateTimeUtc().toString("ddMMMyy-hh:mm:ss.zzz - ").toLatin1()+"SENT TCP: \r\n"+xmlWrapper("pong","",true));
                }
                return;
            }

            /**********************************************************************************
            * add more getCommands here
            * ********************************************************************************/

        }//if no xml parsing error
    }//else not message
}//HandleTcpData

void MainWindow::handleRawTcpData(QByteArray dataByteArray)
{
    //    qDebug() << dataByteArray;
    //    QByteArray localByteArray = xmlWrapper("data",dataByteArray,false);

    while (isRMIBusy()) {
        //        qDebug() << Q_FUNC_INFO << "??: RMI is busy...";
        if(g_ExitApplication){
            return;
        }
        //        qDebug() << "Waiting... CP is busy!";
        QApplication::processEvents(QEventLoop::ExcludeSocketNotifiers);
        QThread::sleep(1000);
    }//while CP is busy
    if(globalConfigSettings.ipSettings.rawUseRMICipher){
        if(isPlaintext(dataByteArray)){
            encryptionkey = globalConfigSettings.ipSettings.rawDefaultCipherKey;
            TXsourceStation = globalConfigSettings.generalSettings.callSign;
            TXdestinationStation = "ALL";
            useEncryption = true;
            useCompression = false;
            dataByteArray = encryptData(dataByteArray);
            writeDataModem(dataByteArray);
        }
        else {
            foreach (QTcpSocket *tcpSocket, rawTCPSocketList) {
                tcpSocket->write(globalConfigSettings.generalSettings.positionIdentifier.toLatin1()+
                                 " - ERROR: Trying to double encrypt data!");
                tcpSocket->flush();
            }//foreach TCP Socket
        }
    }
    else {
        writeDataModem(dataByteArray);
    }
    QTimer::singleShot(calculateDelayBetweenMessages(dataByteArray.size())*1000, [=] {
        // must have CONFIG += c++11 in .pro file
        emit signalUpdateStatusDisplay("Idle...");
        g_IsTransmitting = false;
        g_IsRadioBusy = false;
    });

}//handleRawTcpData

void MainWindow::slotReadClientSocket()
{
    modemSerialTimer->setInterval(globalConfigSettings.dataComPortSettings.serialTimeout.toInt());
    while (clientSocket->bytesAvailable() > 0) {
        if(g_ExitApplication){
            g_IsRadioBusy = false;
            g_IsReceiving= false;
            return;
        }
        modemReceivedDataByteArray.append(clientSocket->readAll());
    }
    if(modemReceivedDataByteArray.size() > 3)
        emit signalUpdateStatusDisplay("Receiving data...");
}//slotReadClientSocket

void MainWindow::slotReadMSDMTDataSocket()
{
    //    qDebug() << Q_FUNC_INFO;
    //    QString funcName = Q_FUNC_INFO;
    //    emit signalUpdateDebugLog("Function Name: " + funcName);
    //TODO - Remove from production code
    //    if(g_IsTransmitting || !g_IsReceiving){
    //    if(g_IsTransmitting){
    //        modemReceivedDataByteArray = MSDMTDataSocket->readAll();
    //        //        qDebug() << Q_FUNC_INFO << "Dumping modem received data: " << modemReceivedDataByteArray.toHex(' ').toUpper();
    //        modemReceivedDataByteArray.clear();
    //        //        qDebug() << Q_FUNC_INFO << "Modem received data dumped: " << modemReceivedDataByteArray;
    //        return;
    //    }

    while (MSDMTDataSocket->bytesAvailable() > 0) {
        if(g_ExitApplication){
            g_IsRadioBusy = false;
            g_IsReceiving= false;
            return;
        }
        modemReceivedDataByteArray.append(MSDMTDataSocket->readAll());
        //        qDebug() << Q_FUNC_INFO << modemReceivedDataByteArray.toHex(' ').toUpper();
    }
    emit signalUpdateStatusDisplay("Receiving data...");
    g_IsReceiving = true;
    g_IsTransmitting = false;

    if(modemReceivedDataByteArray.size() > 64 && !foundKey){
        if(testDecrypt(modemReceivedDataByteArray)){
            foundKey = true;
        }//if testDecrypt
    }//if data is larger than 64 bytes
    
    if(predictedByteCountSize > 0 && globalConfigSettings.generalSettings.useProgressBar){
        if(globalConfigSettings.generalSettings.useSystemTray){
            if(trayIcon->isVisible()){
                dialogProgressBar->show();
                dialogProgressBar->raise();
            }
        }
        emit signalUpdateProgressBar(modemReceivedDataByteArray.size(), predictedByteCountSize);
    }
}//slotReadMSDMTDataSocket

void MainWindow::slotReadMSDMTCMDSocket()
{
    //    qDebug() << Q_FUNC_INFO;
    //    QString funcName = Q_FUNC_INFO;
    //    emit signalUpdateDebugLog("Function Name: " + funcName);

    QByteArray statusMSDMTArray;
    modemSerialTimer->setInterval(globalConfigSettings.dataComPortSettings.serialTimeout.toInt());
    while (MSDMTCMDSocket->bytesAvailable() > 0) {
        if(g_ExitApplication){
            g_IsRadioBusy = false;
            g_IsReceiving= false;
            return;
        }//if application is exiting
        statusMSDMTArray.append(MSDMTCMDSocket->readAll());
    }//while byte available

    //    qDebug() << Q_FUNC_INFO << "1:" << statusMSDMTArray;

    //split them apart into a list
    QString statusString = statusMSDMTArray.replace("\r\n","\n");
    QStringList statusList = statusString.split('\n',Qt::SkipEmptyParts);
    //    qDebug() << Q_FUNC_INFO << "2:" << statusString;

    //for each event status string
    foreach (QString eventStatus, statusList) {
        //        qDebug() << Q_FUNC_INFO << "3:" << eventStatus;
        emit signalUpdateModemStatus(eventStatus.toLatin1());

        if(g_IsWaitingForModemResponse && eventStatus.startsWith("OK")){
            emit signalUpdateModemStatus("Received OK modem status...");
            g_HaveReceivedModemResponse = true;
            continue;
        }
        //if it's a status message
        if(eventStatus.startsWith("STATUS:")){
            //if it's a TX message
            if (eventStatus.startsWith("STATUS:TX:")) {
                //            qDebug() << "STATUS:TX:";
                if(eventStatus.contains(":TRANSMIT")){
                    //we're transmitting
                    g_IsTransmitting = true;
                    g_IsReceiving = false;
                    emit signalUpdateStatusDisplay("Transmitting data...");
                }
                else if (eventStatus.contains(":IDLE")) {
                    //we're no longer transmitting
                    g_IsTransmitting = false;
                    emit signalUpdateStatusDisplay("Idle...");
                }
            }//if STATUS:TX
            //else if RX
            else if (eventStatus.startsWith("STATUS:RX:")) {
                QString status = eventStatus;
                status.remove("STATUS:RX:");

                /******************************************************************
                 * modem is done receiving
                 * ***************************************************************/
                if(status == "NO DCD"){
                    QTimer::singleShot(200, [this](){
                        while (MSDMTDataSocket->bytesAvailable()) {
                            modemReceivedDataByteArray.append(MSDMTDataSocket->readAll());
                        }
                        if(!modemReceivedDataByteArray.isEmpty())
                            slotHandleModemReceivedlData();
                        g_IsReceiving = false;
                        emit signalUpdateStatusDisplay("Idle...");
                    });
                }//NO DCD
                else {
                    emit signalUpdateStatusDisplay("RX: "+status);
                    g_IsReceiving = true;
                }
            }//if STATUS:RX
        }//if STATUS
        else {
            if(eventStatus.endsWith('S') || eventStatus.endsWith('L')){
                QString status(eventStatus);
                QString speed = status;
                QString interleave = status.right(1);
                speed.remove('S');
                speed.remove('L');
                //            qDebug() << Q_FUNC_INFO << "Speed/Interleave:" << speed << interleave;
                ui->comboBoxSelectModemBaud->setCurrentText(speed);
                ui->comboBoxSelectModemInterleave->setCurrentText(interleave);
                ui->comboBoxConfiguredModemSpeed->setCurrentText(speed);
                ui->comboBoxConfiguredModemInterleave->setCurrentText(interleave);
                //        QApplication::processEvents();
                //                emit signalUpdateModemStatus(eventStatus.toLatin1());
                sendStatusChange();
                emit signalUpdateChatDialogConfig(xmlWrapper("status","",true));
            }//if modem speed/interleave
            else {
                //            qDebug() << Q_FUNC_INFO << eventStatus;
                emit signalUpdateModemStatus("Could not parse: "+eventStatus.toLatin1());
            }
        }//else not STATUS
    }//foreach status string
}//slotReadMSDMTCMDSocket

void MainWindow::slotMSDMTCMDSocketDisconnect()
{
    //    qDebug() << MSDMTCMDSocket->errorString();
    if(g_ExitApplication)
        return;
    ui->labelModemConnectionStatus->setText("DMT Modem: Disconnected!");
    emit signalUpdateCriticalNotifications("DMT Modem: CMD port disconnected - " +
                                           MSDMTCMDSocket->errorString());
    //    emit signalUpdateDebugLog("status_label - DMT Modem: CMD port disconnected - " +
    //                              MSDMTCMDSocket->errorString());
}//slotMSDMTCMDSocketDisconnect

void MainWindow::slotMSDMTDataSocketDisconnect()
{
    if(g_ExitApplication)
        return;
    //    qDebug() << MSDMTCMDSocket->errorString();
    ui->labelModemConnectionStatus->setText("DMT Modem: Disconnected!");
    emit signalUpdateCriticalNotifications("DMT Modem: Data port disconnected - " +
                                           MSDMTDataSocket->errorString());
    //    emit signalUpdateDebugLog("status_label - DMT Modem: Data port disconnected - " +
    //                              MSDMTDataSocket->errorString());
}//slotMSDMTCMDSocketDisconnect

void MainWindow::slotClientSocketDisconnect()
{
    //    qDebug() << clientSocket->errorString();
    if(!g_ExitApplication){
        ui->labelModemConnectionStatus->setText("TCP Socket: Disconnected!");
        emit signalUpdateCriticalNotifications("TCP Socket: Disconnected - " +
                                               clientSocket->errorString());
        //        emit signalUpdateDebugLog("status_label - TCP Socket: Disconnected - " +
        //                                  clientSocket->errorString());
    }
}//slotClientSocketDisconnect

//void MainWindow::slotStartModemResponseTimer()
//{
//    modemResponseTimer->start();
//}//slotStartModemResponseTimer

//void MainWindow::slotStartModemPingTimer()
//{
//    modemPingTimer->start();
//}//slotStartModemPingTimer

void MainWindow::slotReadALESocket()
{
    telnetSockerTimer->start();
    QByteArray localByteArray;
    while (aleSocket->bytesAvailable() > 0) {
        if(g_ExitApplication){
            g_IsRadioBusy = false;
            g_IsReceiving= false;
            return;
        }
        //        while (aleSocket->bytesAvailable() > 0) {
        emit signalUpdateDebugLog("Reading ALE socket...");
        localByteArray.append(aleSocket->readAll());
        //        }
        emit signalUpdateALELog(localByteArray.trimmed());
        ALEDataByteArray.append(localByteArray);
    }//while bytes available at ALE socket
}//slotReadALESocket

void MainWindow::slotALELinkTimeout()
{
    if(!b_isCalling){
        aleLinkTimeOutTimer->setInterval(globalConfigSettings.aleSettings.aleLinkTimeOut.toInt()*1000);
        return;
    }//if we're not calling reset the interval and return.  Happens if interval is less than time it takes to initiate the call
    else {
        //        qDebug() << Q_FUNC_INFO << QThread::currentThreadId();
        //weird issues here with timing, what if call times out right as ALE is making link
        //or ALE was linked initially and could not drop link
        //then we try to drop link again and send time out signal
        if(b_isLinked){
            writeControlRadio("CMD CLEAR "+linkedAddress.toLatin1());
            b_isLinked = false;
        }
        aleLinkTimeOutTimer->setInterval(globalConfigSettings.aleSettings.aleLinkTimeOut.toInt()*1000);
        b_linkTimeOut = true;
    }//else we're calling or linked
}//slotALELinkTimeout

void MainWindow::slotHandleALEData(){
    if(ALEDataByteArray.isEmpty()){
        return;
    }
    //    QString funcName = Q_FUNC_INFO;
    //    emit signalUpdateDebugLog("Function Name: " + funcName);
    QMutexLocker mutexLocker(&handleALEDataMutex);
    emit signalUpdateDebugLog("Passed mutex locker" +tr(Q_FUNC_INFO) );
    //    isALEBusy();
    QString responseString = ALEDataByteArray;
    QStringList responseStringList = responseString.split("\n",Qt::SkipEmptyParts);
    //    qDebug() << responseStringList;

    foreach (QString responseLine, responseStringList) {
        if(responseLine.startsWith("ID RX FREQ ")){
            b_isLoadingChannels = true;
            //            isALEBusy();
            break;
        }
        else if (responseLine.startsWith("OTHER IDs")) {
            b_isLoadingAddresses = true;
            break;
        }
    }//check for channel list
    if(b_isLoadingChannels){
        ui->comboBoxSelectChannel->clear();
        ui->comboBoxSelectChannel->addItem("");
        emit signalUpdateApplicationLog("Loading ALE channels...");
        foreach (QString responseLine, responseStringList) {
            if(responseLine.trimmed().startsWith("ID ") ||
                responseLine.trimmed().startsWith("-- ")||
                responseLine.trimmed().startsWith("OK") ||
                responseLine.isEmpty())
            {
                continue;
            }
            else {
                QStringList channelStringList = responseLine.trimmed().split(" ");
                QString chan = channelStringList.at(0);
                if(chan.toInt() > 0){
                    ui->comboBoxSelectChannel->addItem(chan);
                    channelMap.insert(chan,channelStringList.at(1).trimmed());
                }
            }///else load channel number into comboBox
        }
        b_isLoadingChannels = false;
        globalConfigSettings.aleSettings.recommendedTimeout = (channelMap.count() * 15)+15;
        writeControlRadio("CMD OTHER");
        b_isLoadingAddresses = true;
    }//if loading channels
    else if (b_isLoadingAddresses) {
        ui->comboBoxSelectALEAddress->clear();
        emit signalUpdateApplicationLog("Loading ALE addresses...");

        foreach (QString address, responseStringList) {
            if(address.startsWith("OTHER ")){
                continue;
            }
            else {
                address = address.trimmed();
                if(!address.isEmpty() && !globalConfigSettings.aleSettings.knownALEAddressList.contains(address)){
                    globalConfigSettings.aleSettings.knownALEAddressList.append(address);
                }
            }
        }//fore each line in the response list add address to combo box
        //        qDebug() << "stop";
        globalConfigSettings.aleSettings.knownALEAddressList.sort(Qt::CaseInsensitive);
        ui->comboBoxSelectALEAddress->addItems(globalConfigSettings.aleSettings.knownALEAddressList);
        emit signalUpdateALEAddressSelect(globalConfigSettings.aleSettings.knownALEAddressList);
        b_isLoadingAddresses = false;
        this->sendStatusChange();
        emit signalUpdateChatDialogConfig(xmlWrapper("status","",true));
        
        if(globalConfigSettings.aleSettings.startScan){
            writeControlRadio("CMD START");
            emit signalUpdateALEStatus("Syncing...");
        }
        else {
            writeControlRadio("CMD STATUS");
            emit signalUpdateALEStatus("Syncing...");
        }
    }
    else {
        QString chan;
        QString freq;
        foreach (QString responseLine, responseStringList) {
            QStringList responseList = responseLine.trimmed().split("]",Qt::SkipEmptyParts);
            foreach (QString response, responseList) {
                if(response.startsWith("[FRQ "))
                {
                    freq = response.right(response.size()-QString("[FRQ ").size());
                    if(freq != "00000000"){
                        ui->FrequencyDisplay->setText(freq);
                        QStringList freqList = channelMap.values();
                        foreach (QString freq1, freqList) {
                            if(freq.contains(freq1)){
                                chan = channelMap.keys().at(channelMap.values().indexOf(freq1));
                                //                                ui->comboBoxSelectChannel->setCurrentText(chan);
                                ui->ChannelDisplay->setText(chan);
                            }
                        }
                    }
                }//FRQ
                else if(response.startsWith("[CHN "))
                {
                    QString chan = response.right(response.size()-QString("[CHN ").size());

                    //                    ui->comboBoxSelectChannel->setCurrentText(chan);
                    ui->ChannelDisplay->setText(chan);
                    ui->FrequencyDisplay->setText(channelMap.value(chan));

                }//[CHN
                //[MMI CHANGE ERROR: NOT ENOUGH PARAMETERS]
                else if (response.startsWith("[MMI CHANGE ERROR:")) {
                    writeControlRadio("CMD STATUS");
                }
                else if (response.startsWith("[RESUMED SCANNING") ||
                         response.startsWith("SCANNING") ||
                         response.contains("SCANNING STARTED"))
                {
                    emit signalUpdateApplicationLog("Scanning...");
                    QTimer::singleShot(1500, [=] {
                        // must have CONFIG += c++11 in .pro file
                        ui->FrequencyDisplay->setText("SCANNING");
                        ui->ChannelDisplay->setText(" - ");
                        emit signalUpdateALEStatus("Scanning...");
                    });

                    b_isScanning = true;
                    b_isCalling = false;
                    b_isWaitingLink = false;
                    b_isLinked = false;
                    b_isClearing = false;
                    b_isLoadingChannels = false;
                    b_isLoadingAddresses = false;

                }//SCANNING
                else if (response.startsWith("[SCANNING: SCANNING STOPPED"))
                {
                    b_isScanning = false;
                    b_isCalling = false;
                    b_isWaitingLink = false;
                    b_isLinked = false;
                    b_isClearing = false;
                    b_isLoadingChannels = false;
                    b_isLoadingAddresses = false;
                    emit signalUpdateApplicationLog("Scanning stopped...");
                    emit signalUpdateALEStatus("Idle...");
                    //                    writeControlRadio("CMD FFFF");
                }//[SCANNING: SCANNING STOPPED
                else if (response.startsWith("STOPPED"))
                {
                    b_isScanning = false;
                    b_isCalling = false;
                    b_isWaitingLink = false;
                    b_isLinked = false;
                    b_isClearing = false;
                    b_isLoadingChannels = false;
                    b_isLoadingAddresses = false;
                    emit signalUpdateApplicationLog("Scanning stopped...");
                    emit signalUpdateALEStatus("Idle...");
                }//STOPPED
                //[ALE SCANNING TIS CALL INITIATED
                else if(response.startsWith("[ALE SCANNING TIS CALL INITIATED"))
                {
                    emit signalUpdateApplicationLog("Calling... "+callThisAddress.toLatin1());
                    emit signalUpdateALEStatus("Calling... "+callThisAddress.toLatin1());
                    b_isScanning = false;
                    b_isCalling = true;
                    b_isWaitingLink = false;
                    b_isLinked = false;
                    b_isClearing = false;
                    b_isLoadingChannels = false;
                    b_isLoadingAddresses = false;
                    //                    qDebug() << "STATUS - Calling..." << callThisAddress;
                }//[ALE SCANNING TIS CALL INITIATED
                else if(response.startsWith("[ALE INDIVIDUAL TIS CALL INITIATED"))
                {
                    emit signalUpdateApplicationLog("Calling... "+callThisAddress.toLatin1()+" on "+chan);
                    emit signalUpdateALEStatus("Calling... "+callThisAddress.toLatin1()+" on "+chan);
                    b_isScanning = false;
                    b_isCalling = true;
                    b_isWaitingLink = false;
                    b_isLinked = false;
                    b_isClearing = false;
                    b_isLoadingChannels = false;
                    b_isLoadingAddresses = false;
                    //                    qDebug() << "STATUS - Calling..." << callThisAddress;
                }//[ALE INDIVIDUAL TIS CALL INITIATED
                else if(response.startsWith("[ALE GLOBAL ALLCALL LINK"))
                {
                    emit signalUpdateApplicationLog("Calling... "+callThisAddress.toLatin1());
                    emit signalUpdateALEStatus("Calling... "+callThisAddress.toLatin1());
                    b_isScanning = false;
                    b_isCalling = true;
                    b_isWaitingLink = false;
                    b_isLinked = false;
                    b_isClearing = false;
                    b_isLoadingChannels = false;
                    b_isLoadingAddresses = false;
                    //                    qDebug() << "STATUS - Calling..." << callThisAddress;
                }//[ALE GLOBAL ALLCALL LINK
                else if(response.startsWith("LINKED"))
                {
                    emit signalUpdateApplicationLog("Linked: "+linkedAddress.toLatin1());
                    emit signalUpdateALEStatus("Linked: "+linkedAddress.toLatin1());
                    writeTcpSockets(xmlWrapper("error","Linked: "+linkedAddress.toLatin1(),true),"");
                    b_isScanning = false;
                    b_isCalling = false;
                    b_isWaitingLink = false;
                    b_isLinked = true;
                    b_isClearing = false;
                    b_isLoadingChannels = false;
                    b_isLoadingAddresses = false;
                    //                    qDebug() << "STATUS - Linked...";
                    //                    qDebug() << "Linked to: " << linkedAddress;
                }//LINKED
                //[CLEARED
                else if(response.startsWith("[CLEARED "))
                {
                    linkedAddress = responseList.at(3).trimmed();
                    linkedAddress = linkedAddress.right(linkedAddress.size()-1);
                    if(linkedAddress == "@?@") linkedAddress = "ALL";
                    emit signalUpdateApplicationLog("Cleared: "+linkedAddress.toLatin1());
                    emit signalUpdateALEStatus("Cleared: "+linkedAddress.toLatin1());
                    writeTcpSockets(xmlWrapper("error","Cleared: "+linkedAddress.toLatin1(),true),"");
                    b_isScanning = false;
                    b_isCalling = false;
                    b_isWaitingLink = false;
                    b_isLinked = false;
                    b_isClearing = false;
                    b_isLoadingChannels = false;
                    b_isLoadingAddresses = false;
                    //                    qDebug() << "Cleared with:" << linkedAddress;
                    linkedAddress.clear();
                }//[CLEARED
                else if(response.startsWith("[LINKED "))
                {
                    QString addr = responseList.last().trimmed();
                    if(!addr.isEmpty()){
                        linkedAddress = addr.right(addr.size()-1);
                        if(linkedAddress == "@?@"){
                            linkedAddress = "ALL";
                        }
                    }
                    emit signalUpdateApplicationLog("Linked: "+linkedAddress.toLatin1());
                    emit signalUpdateALEStatus("Linked: "+linkedAddress.toLatin1());
                    //                    writeTcpSockets(xmlWrapper("error","Linked: "+linkedAddress.toLatin1(),false));
                    b_isScanning = false;
                    b_isCalling = false;
                    b_isWaitingLink = false;
                    b_isLinked = true;
                    b_isClearing = false;
                    b_isLoadingChannels = false;
                    b_isLoadingAddresses = false;
                    //                    qDebug() << "Linked to:" << linkedAddress;
                }//[LINKED
                else if(response.startsWith("[RADIO: ALE INDIVIUAL CLEAR LINK"))
                {
                    emit signalUpdateApplicationLog("Clearing link...");
                    emit signalUpdateALEStatus("Clearing...");
                    b_isScanning = false;
                    b_isCalling = false;
                    b_isWaitingLink = false;
                    b_isLinked = false;
                    b_isClearing = true;
                    b_isLoadingChannels = false;
                    b_isLoadingAddresses = false;
                    //                    qDebug() << "Clearing with: " << linkedAddress;
                }//[RADIO: ALE INDIVIUAL CLEAR LINK
                else if(response.contains("PTT now ACTIVE"))
                {
                    if(b_isCalling){
                        emit signalUpdateALEStatus("Calling... "+callThisAddress);
                    }
                    if(b_isClearing){
                        emit signalUpdateALEStatus("Clearing...");
                    }
                }
                else if(response.contains("PTT now RELEASED"))
                {
                    if(b_isCalling){
                        emit signalUpdateALEStatus("Listening...");
                    }
                }
                // [22:03:36][CHN 02][AMD][R936           ][TEST AMD MESSAGE  ]
                if(response.startsWith("[AMD")){
                    if(responseList.at(0).startsWith("[AMD"))
                        continue;
                    QString ALEAddr = responseList.at(3).right(responseList.at(3).size()-1).trimmed();
                    QString AMDMessage = responseList.at(4).right(responseList.at(4).size()-1).trimmed();
                    emit signalUpdateAMDMessage(ALEAddr.toLatin1()+" - "+AMDMessage.toLatin1());
                    writeTcpSockets(xmlWrapper("error","[AMD] "+ALEAddr.toLatin1()+" - "+AMDMessage.toLatin1(),true),"");
                    dialogALE->show();
                    dialogALE->raise();
                }
                else if(response.startsWith("[FAILED "))
                {
                    if(responseLine.contains(" - UNKNOWN ADDRESS")){
                        //                        QString ALEAddr = responseList.at(2).right(responseList.at(2).size()-1).trimmed();
                        QStringList ALEAddrList = responseList.at(2).split(' ');
                        QString ALEAddr = ALEAddrList.last();
                        //                        qDebug() << "Call failed: " << "UNKNOWN ADDRESS" << ALEAddr;
                        //                        qDebug() << "Call failed: " << "UNKNOWN ADDRESS";
                        emit signalUpdateApplicationLog("Call failed: UNKNOWN ADDRESS");
                        emit signalUpdateALEStatus("Call failed: UNK ADDR");
                        writeTcpSockets(xmlWrapper("error","Call failed: UNK ADDR",true),"");
                        b_linkError = true;
                        //                        isScanning = false;
                        b_isCalling = false;
                        b_isWaitingLink = false;
                        b_isLinked = false;
                        b_isClearing = false;
                        b_isLoadingChannels = false;
                        b_isLoadingAddresses = false;
                        //                        ALEErrorMessage = "UNKNOWN ADDRESS";
                        writeControlRadio("CMD STATUS");
                        emit signalUpdateALEStatus("Syncing...");
                    }
                    else {
                        QStringList ALEAddrList = responseList.at(2).split(' ');
                        QString ALEAddr = ALEAddrList.last();
                        //                        qDebug() << "Call failed: " << ALEAddr;
                        //                        emit signalUpdateApplicationLog("Call failed: "+ALEAddr);
                        emit signalUpdateALEStatus("Call failed: "+ALEAddr);
                        //                        writeTcpSockets(xmlWrapper("error","Call failed: "+ALEAddr.toLatin1(),true));
                        b_linkError = true;
                        //                        isScanning = false;
                        b_isCalling = false;
                        b_isWaitingLink = false;
                        b_isLinked = false;
                        b_isClearing = false;
                        b_isLoadingChannels = false;
                        b_isLoadingAddresses = false;
                    }
                }
                /*status = "NULL        ";
status = "SCANNING    ";
status = "PAUSING 1   ";
status = "PAUSING 2   ";
status = "LISTEN CHAN ";
status = "RESPONDING  ";
status = "CALLING 1   ";
status = "WAIT ACK 1  ";
status = "WAIT ACK 2  ";
status = "WAIT ACK 3  ";
status = "WAIT ACK 4  ";
status = "WAIT RESP 1 ";
status = "WAIT RESP 2 ";
status = "WAIT RESP 3 ";
status = "CALLING 2   ";
status = "LINKED      ";
status = "CLEARING    ";
status = "HANDSHAKING ";
status = "SOUNDING LAS";
status = "SOUNDING LIS";
status = "SOUNDING TWS";
status = "SOUNDING TIS";
status = "ERROR       ";
*/
                else if (response.startsWith("NULL") ||
                         //                         response.startsWith("SCANNING") ||
                         //                         response.startsWith("PAUSING 1") ||
                         response.startsWith("PAUSING 2") ||
                         response.startsWith("LISTEN CHAN") ||
                         response.startsWith("RESPONDING") ||
                         response.startsWith("CALLING 1") ||
                         response.startsWith("WAIT ACK 1") ||
                         response.startsWith("WAIT ACK 2") ||
                         response.startsWith("WAIT ACK 3") ||
                         response.startsWith("WAIT ACK 4") ||
                         response.startsWith("WAIT RESP 1") ||
                         response.startsWith("WAIT RESP 2") ||
                         response.startsWith("CALLING 2") ||
                         //                         response.startsWith("LINKED") ||
                         //                         response.startsWith("CLEARING") ||
                         response.startsWith("HANDSHAKING") ||
                         response.startsWith("SOUNDING LAS") ||
                         response.startsWith("SOUNDING LIS") ||
                         response.startsWith("SOUNDING TWS") ||
                         response.startsWith("SOUNDING TIS") ||
                         response.startsWith("ERROR") ||
                         response.startsWith("WAIT RESP 3"))
                {
                    writeControlRadio("CMD STATUS");
                }
                else if (response.startsWith("PAUSING 1")) {
                    emit signalUpdateALEStatus("Listening...");
                    writeControlRadio("CMD STATUS");
                }
                else {
                    //                    if(!response.isEmpty())
                    //                        qDebug() << funcName << "-" << response << "not parsed!";
                }//we have no parser for this response yet
            }//for each element in the response line
        }//for each line in the response list
    }//else not loading channels
    ALEDataByteArray.clear();
    //    isALEBusy();
}//handleALEData

void MainWindow::slotSendAMDMessage(QByteArray dataByteArray)
{
    writeControlRadio(dataByteArray);
    //    emit signalUpdateALELog(dataByteArray);
}//slotSendAMDMessage

void MainWindow::handleUDPData(QByteArray data)
{
    //    qDebug() << "Function Name: " << Q_FUNC_INFO;
    if(data.isEmpty())
        return;

    //    QString funcName = Q_FUNC_INFO;
    //    emit signalUpdateDebugLog("Function Name: " + funcName);
    //        emit signalUpdateDebugLog("Locking slotHandleUDPDataMutex");
    //    QMutexLocker mutexLocker(&slotHandleUDPDataMutex);

    emit signalUpdateUDPLog(QDateTime::currentDateTimeUtc().toString("ddMMMyy-hh:mm:ss.zzz - ").toLatin1()+"RECEIVED UDP: \r\n"+data);

    ClassXML xmlClass(data);

    if(!xmlClass.getParsingError()){
        positionID = xmlClass.getPositionID();
        if(positionID == globalConfigSettings.generalSettings.positionIdentifier){
            return;
        }//if it's our own packet ignore it

        if(xmlClass.getCommand() == "helo" && xmlClass.getProtocol() == "V3PROTOCOL"){
            if(MSCTCPServer->isListening()){
                QRandomGenerator random(static_cast<uint>(QTime::currentTime().msec()));
                //                qsrand(static_cast<uint>(QTime::currentTime().msec()));
                QTimer::singleShot(random.generate() % ((2000 + 1) - 10) + 10, [=] {
                    // must have CONFIG += c++11 in .pro file
                    slotBroadcastID("update");
                });//send an update at a random time
            }//if tcpServer is online
        }//if helo
        else if ((xmlClass.getCommand() == "update" || xmlClass.getCommand() == "helo") &&
                 xmlClass.getProtocol() == "MSDMT") {
            //            qDebug() << "Received MS-DMT UDP Datagram...";
            DMTDataConnections.insert(xmlClass.getPositionID(),
                                      xmlClass.getListenDMTAddress()+":"+
                                          xmlClass.getListenDMTDataPort());
            DMTCMDConnections.insert(xmlClass.getPositionID(),
                                     xmlClass.getListenDMTAddress()+":"+
                                         xmlClass.getListenDMTCmdPort());
            emit signalUpdateDMTConnectionList(DMTDataConnections.keys());
            if(globalConfigSettings.dataComPortSettings.currentDevice == "MS-DMT TCP"){
                //parse connection info
                connectMSDMTCMD(xmlClass.getPositionID(),
                                xmlClass.getListenDMTAddress(),
                                xmlClass.getListenDMTCmdPort(), true);
                connectMSDMTData(xmlClass.getPositionID(),
                                 xmlClass.getListenDMTAddress(),
                                 xmlClass.getListenDMTDataPort(), true);
            }//if we're configured to use MSDMT TCP
        }//if MSDMT && update
        else if (xmlClass.getCommand() == "bye" && xmlClass.getProtocol() == "MSDMT") {
            DMTDataConnections.remove(xmlClass.getPositionID());
            DMTCMDConnections.remove(xmlClass.getPositionID());
            emit signalUpdateDMTConnectionList(DMTDataConnections.keys());
        }
    }//if no parsing errors
}//handleUDPData

void MainWindow::handleStreamingUDPData(QByteArray data)
{
    //    qDebug() << "Function Name: " << Q_FUNC_INFO;
    if(data.isEmpty())
        return;

    //    QString funcName = Q_FUNC_INFO;
    //    emit signalUpdateDebugLog("Function Name: " + funcName);
    //        emit signalUpdateDebugLog("Locking slotHandleUDPDataMutex");
    //    QMutexLocker mutexLocker(&slotHandleUDPDataMutex);

    emit signalUpdateUDPLog(QDateTime::currentDateTimeUtc().toString("ddMMMyy-hh:mm:ss.zzz - ").toLatin1()+"RECEIVED UDP: \r\n"+data);

    ClassXML xmlClass(data);

    if(!xmlClass.getParsingError()){
        positionID = xmlClass.getPositionID();
        if(positionID == globalConfigSettings.generalSettings.positionIdentifier){
            return;
        }//if it's our own packet ignore it

        if(xmlClass.getCommand() == "data" && xmlClass.getProtocol() == "V3PROTOCOL"){
            if(MSDMTCMDSocket->isOpen()){
                //                qDebug() << xmlClass.getModemDataRate();
                //                qDebug() << xmlClass.getModemInterleave();
                writeCommandModem("CMD:DATA RATE:"+xmlClass.getModemDataRate().toLatin1()+xmlClass.getModemInterleave().toLatin1().toLower());
                QApplication::processEvents();
                QThread::msleep(1500);
                QApplication::processEvents();
            }//send modem commands

            if(MSDMTDataSocket->isOpen()){
                QString dataStreamString = xmlClass.getMessageText().first();
                dataStreamString.remove("\r\n");
                //                qDebug() << dataStreamString;
                QByteArray dataStreamByteArray = QByteArray::fromHex(dataStreamString.toLatin1());
                writeMSDMTDataPort(dataStreamByteArray);
                writeMSDMTCMDPort("CMD:SENDBUFFER");
                QApplication::processEvents();
            }//send data
        }//if data
    }//if no parsing errors
}//handleStreamingUDPData

void MainWindow::slotUdpUpdate()
{
    //    qDebug() << "Function Name: " << Q_FUNC_INFO;
    //    QString funcName = Q_FUNC_INFO;
    //    emit signalUpdateDebugLog("Function Name: " + funcName);
    slotBroadcastID("update");
}//slotUdpUpdate

void MainWindow::slotUpdateStatusBar()
{
    //    this->readRetryFile();
    updateStatusBar();
}//slotUpdateStatusBar

void MainWindow::slotBroadcastID(QByteArray type)
{
    QUdpSocket localUdpSocket;
    QByteArray datagram = xmlWrapper(type, "", true);
    emit signalUpdateUDPLog(QDateTime::currentDateTimeUtc().toString("ddMMMyy-hh:mm:ss.zzz - ").toLatin1()+"SENT UDP: \r\n"+datagram);
    //    if(configSettings.ipSettings.listenIPAddress == "127.0.0.1" ||
    //            configSettings.ipSettings.listenIPAddress == "::1"){
    QHostAddress udpAddress;
    udpAddress.setAddress("127.255.255.255");
    localUdpSocket.writeDatagram(datagram.data(), datagram.size(),
                                 udpAddress, globalConfigSettings.ipSettings.applicationDiscoveryPort.toUShort());
    //    }//if localhost address
    //    else {
    QList<QNetworkInterface> interfaceList = QNetworkInterface::allInterfaces();
    foreach (QNetworkInterface interface1, interfaceList) {
        foreach (QNetworkAddressEntry address, interface1.addressEntries()) {
            localUdpSocket.writeDatagram(datagram.data(), datagram.size(),
                                         address.broadcast(), globalConfigSettings.ipSettings.applicationDiscoveryPort.toUShort());
        }
    }
    //    }//else not localhost
}//slotBroadcastID

void MainWindow::slotMSCSocketDisconnected()
{
    //    qDebug() << "Function Name: " << Q_FUNC_INFO;
    //    QString funcName = Q_FUNC_INFO;
    //    emit signalUpdateDebugLog("Function Name: " + funcName);

    if(!g_ExitApplication){
        numberConnections--;
        //    qDebug() << tcpSocket->isValid() << tcpSocket->isOpen();

        QTcpSocket *tcpSocket = static_cast<MyTcpSocketClass*>(sender());
        MSCTCPSocketList.removeAll(tcpSocket);
        tcpSocket->deleteLater();
        //    tcpSocket->deleteLater();
        ui->pushButtonTCP->setEnabled(numberConnections > 0);
        emit signalUpdateStatusBar();
    }//if not quitting - since UI is not available after destructor is called
}//slotSocketDisconnected

void MainWindow::slotRawSocketDisconnected()
{
    //    qDebug() << "Function Name: " << Q_FUNC_INFO;
    //    QString funcName = Q_FUNC_INFO;
    //    emit signalUpdateDebugLog("Function Name: " + funcName);

    if(!g_ExitApplication){
        //        numberConnections--;
        //    qDebug() << tcpSocket->isValid() << tcpSocket->isOpen();

        QTcpSocket *tcpSocket = static_cast<MyTcpSocketClass*>(sender());
        rawTCPSocketList.removeAll(tcpSocket);
        tcpSocket->deleteLater();
        //    tcpSocket->deleteLater();
        //        ui->pushButtonTCP->setEnabled(numberConnections > 0);
        //        emit signalUpdateStatusBar();
    }//if not quitting - since UI is not available after destructor is called
}//slotSocketDisconnected

void MainWindow::slotALESocketDisconnected()
{
    //    qDebug() << "Function Name: " << Q_FUNC_INFO;
    //    QString funcName = Q_FUNC_INFO;
    //    emit signalUpdateDebugLog("Function Name: " + funcName);

    if(!g_ExitApplication){
        QTcpSocket *aleSocket = static_cast<QTcpSocket*>(sender());
        //        qDebug() << aleSocket->errorString();
        aleSocket->close();
        ui->pushButtonALE->setEnabled(aleSocket->isOpen());
        emit signalUpdateALEStatus("Disconnected!");
    }//if not quitting - since UI is not available after destructor is called
}//slotALESocketDisconnected

void MainWindow::slotReadModemSerialData()
{
    //    qDebug() << "Function Name: " << Q_FUNC_INFO;
    //    QString funcName = Q_FUNC_INFO;
    //    emit signalUpdateDebugLog("Function Name: " + funcName);

    //    qDebug() << modemSerialTimer->remainingTime();
    modemSerialTimer->setInterval(globalConfigSettings.dataComPortSettings.serialTimeout.toInt());
    //    if(configSettings.dataComPortSettings.currentDevice == "MS-DMT Serial"){
    //    modemPingTimer->stop();
    //    modemResponseTimer->stop();
    //    }

    QByteArray localDataByteArray;
    g_IsRadioBusy = true;
    g_IsReceiving= true;
    //    while(modemSerialPort->bytesAvailable() > 0){
    if(g_ExitApplication){
        g_IsRadioBusy = false;
        g_IsReceiving= false;
        return;
    }
    localDataByteArray.append(modemSerialPort->readAll());
    //    qDebug() << "Bytes detected at the serial port";
    //    qDebug() << localDataByteArray.toHex();
    //    emit signalUpdateDebugLog("Reading bytes from the serial port...");
    //        QApplication::processEvents(QEventLoop::ExcludeSocketNotifiers);
    //    }//while data to read
    /*    while(serial->bytesAvailable()){
        localByteArray.append(serial->readAll());
    }
*/
    //do something with the data
    //    qDebug() << modemSerialPort->portName() << localDataByteArray;
    modemReceivedDataByteArray.append(localDataByteArray);
    //    qDebug() << modemReceivedDataByteArray;
    if(!modemReceivedDataByteArray.startsWith("<<CMD:"))
        emit signalUpdateStatusDisplay("Receiving data...");

    if(modemReceivedDataByteArray.size() > 64 && !foundKey){
        if(isPlaintext(modemReceivedDataByteArray)){
            foundKey = true;
        }
        else if(testDecrypt(modemReceivedDataByteArray)){
            foundKey = true;
        }//if testDecrypt
    }//if data is larger than 64 bytes
    
    if(predictedByteCountSize > 0 && globalConfigSettings.generalSettings.useProgressBar){
        //        qDebug() << "data size" << modemReceivedDataByteArray.size() << predictedByteCountSize;
        if(globalConfigSettings.generalSettings.useSystemTray){
            if(trayIcon->isVisible()){
                dialogProgressBar->show();
                dialogProgressBar->raise();
            }
        }
        emit signalUpdateProgressBar(modemReceivedDataByteArray.size(), predictedByteCountSize);
    }
    
    if(globalConfigSettings.dataComPortSettings.currentDevice == "MS-DMT TCP"){
        //        modemResponseTimer->start();
        //        modemPingTimer->start();
        if(predictedByteCountSize > 0 && globalConfigSettings.dataComPortSettings.resetSoftwareModem)
        {
            if(modemReceivedDataByteArray.size() >
                (predictedByteCountSize * (1+(static_cast<double>(globalConfigSettings.dataComPortSettings.percentRunOn/100.0)))+55))
            {
                emit signalUpdateDebugLog("Predicted byte count: "+QString::number(predictedByteCountSize));
                emit signalUpdateDebugLog("Received data byte count: "+QString::number(modemReceivedDataByteArray.size()));
                emit signalUpdateDebugLog("Data byte count limit: "+QString::number((predictedByteCountSize * (1+(static_cast<double>(globalConfigSettings.dataComPortSettings.percentRunOn/100.0)))+55)));
                emit signalUpdateApplicationLog("Detected modem run-on - sending modem reset...");
                writeCommandModem("CMD:RESET MDM");
                g_IsWaitingForModemResponse = true;
                g_HaveReceivedModemResponse = false;
                int counter = 0;
                while (g_IsWaitingForModemResponse && !g_HaveReceivedModemResponse) {
                    emit signalUpdateDebugLog("Waiting for modem response...");
                    QApplication::processEvents();
                    QThread::msleep(100);
                    if(counter++ > 10){
                        emit signalUpdateDebugLog("Timeout - waiting for modem response...");
                        break;
                    }
                }//while
                g_IsWaitingForModemResponse = false;
                g_HaveReceivedModemResponse = false;

                slotHandleModemReceivedlData();
            }//if received data is more than percent run on
        }//if we have TC Header byte count and configured to reset modem
    }//if using MS-DMT TCP
}//slotReadModemSerialData

void MainWindow::slotUpdateMainWindow()
{
    //    qDebug() << "Function Name: " << Q_FUNC_INFO;
    //    QString funcName = Q_FUNC_INFO;
    //    emit signalUpdateDebugLog("Function Name: " + funcName);
    writeSettings();
    updateSettings();

    if(g_TCPServerChanged || isFirstTimeRun){
        startTCPServer();
        g_TCPServerChanged = false;
    }//g_TCPServerChanged

    if(g_TCPSocketChanged){
        if(globalConfigSettings.dataComPortSettings.currentDevice == "TCPSocket")
            connectTCPClient();
        g_TCPSocketChanged = false;
    }//g_TCPSocketChanged

    if(g_ALEChanged){
        if(globalConfigSettings.aleSettings.enableALE){
            connectALEClient();
            aleLinkTimeOutTimer->setInterval(globalConfigSettings.aleSettings.aleLinkTimeOut.toInt()*1000);//milliseconds
        }
        else {

        }
        g_ALEChanged = false;
    }
    updateSerialPorts();
    if(globalConfigSettings.generalSettings.useSystemTray){
#ifdef Q_OS_OSX
        if (!event->spontaneous() || !isVisible()) {
            return;
        }
#endif \
        //        qDebug() << "TrayIcon isVisible: " << trayIcon->isVisible();
        if (trayIcon->isVisible()) {
            qApp->setQuitOnLastWindowClosed(false);
            QMessageBox::information(this, tr(APP_NAME),
                                     tr("The program will keep running in the "
                                        "system tray. To terminate the program, "
                                        "choose <b>Quit</b> in the context menu "
                                        "of the application icon in the "
                                        " system tray."));
            writeSettings();
            hide();
        }//if trayIcon is visible
        return;
    }//if use system tray
    else {
        qApp->setQuitOnLastWindowClosed(true);
    }
    sendStatusChange();
    emit signalUpdateChatDialogConfig(xmlWrapper("config","",true));
    emit signalUpdateChatDialogConfig(xmlWrapper("status","",true));

}//slotUpdateMainWindow

void MainWindow::writeSettings()
{
    //    emit signalUpdateDebugLog(Q_FUNC_INFO);
    QString fileName = QCoreApplication::applicationDirPath()+"/"+qAppName()+".ini";
    QSettings settings(fileName,QSettings::IniFormat, this);
    settings.clear();
    settings.beginGroup("Application");
    {
        //save MainWindow size and position
        //        qDebug() << Q_FUNC_INFO << this->size() << this->pos();
        settings.setValue("size", this->size());
        settings.setValue("pos", this->pos());
    }//Application
    settings.endGroup();
    settings.beginGroup("General");
    {
        settings.setValue("useProgressBar", globalConfigSettings.generalSettings.useProgressBar);
        //        settings.setValue("showModemControl", configSettings.generalSettings.showModemControl);
        settings.setValue("hideWindowFrame", globalConfigSettings.generalSettings.hideWindowFrame);
        settings.setValue("positionID", globalConfigSettings.generalSettings.positionIdentifier);
        settings.setValue("callSign", globalConfigSettings.generalSettings.callSign);
        settings.setValue("showDebugInfo", globalConfigSettings.generalSettings.showDebugInfo);
        settings.setValue("extendedChecksumSerialNumber", globalConfigSettings.generalSettings.messageSerialNumber);
        settings.setValue("hideHandShakePackets", globalConfigSettings.generalSettings.hideHandShakePackets);
        settings.setValue("protectConfig", globalConfigSettings.generalSettings.protectConfig);
        settings.setValue("confirmApplicationExit", globalConfigSettings.generalSettings.confirmApplicationExit);
        settings.setValue("hashCheck", globalConfigSettings.generalSettings.hashCheck);
        settings.setValue("useRetryQueue", globalConfigSettings.generalSettings.useRetryQueue);
        settings.setValue("autoTXRetryQueue", globalConfigSettings.generalSettings.autoTXRetryQueue);
        settings.setValue("useSystemTray", globalConfigSettings.generalSettings.useSystemTray);
        settings.setValue("styleSheetFilename", globalConfigSettings.generalSettings.styleSheetFilename);
        settings.setValue("loadStyleSheet", globalConfigSettings.generalSettings.loadStyleSheet);
        settings.setValue("modemSpeed", globalConfigSettings.generalSettings.modemSpeed);
        settings.setValue("modemInterleave", globalConfigSettings.generalSettings.modemInterleave);
        settings.setValue("machineAddressList", globalConfigSettings.generalSettings.machineAddressList);
        settings.setValue("savedQRUParameters", globalConfigSettings.generalSettings.savedQRUParameters);
    }//General
    settings.endGroup();

    settings.beginGroup("SettingsDialog");
    {
        settings.setValue("pos", globalConfigSettings.settings_Dialog.pos);
        settings.setValue("size", globalConfigSettings.settings_Dialog.size);

    }
    settings.endGroup();

    settings.beginGroup("LogDialog");
    {
        settings.setValue("pos", globalConfigSettings.log_Dialog.pos);
        settings.setValue("size", globalConfigSettings.log_Dialog.size);
    }
    settings.endGroup();

    settings.beginGroup("ModemDialog");
    {
        settings.setValue("pos", globalConfigSettings.modem_Dialog.pos);
        settings.setValue("size", globalConfigSettings.modem_Dialog.size);
        settings.setValue("splitterState", globalConfigSettings.modem_Dialog.splitterState);
        settings.setValue("displayModemOnRecvError", globalConfigSettings.modem_Dialog.displayModemOnRecvError);
    }
    settings.endGroup();

    settings.beginGroup("KeyLoaderDialog");
    {
        settings.setValue("pos", globalConfigSettings.keyLoaderSettings.pos);
        settings.setValue("size", globalConfigSettings.keyLoaderSettings.size);
        settings.setValue("splitterState", globalConfigSettings.keyLoaderSettings.splitterState);
    }
    settings.endGroup();

    settings.beginGroup("ChatDialog");
    {
        settings.setValue("pos", globalConfigSettings.chat_Dialog.pos);
        settings.setValue("size", globalConfigSettings.chat_Dialog.size);
        settings.setValue("chatSplitterState", globalConfigSettings.chat_Dialog.chatSplitterState);
        settings.setValue("showChatDialog", globalConfigSettings.chat_Dialog.showChatDialog);
        settings.setValue("chatMessageListMap", globalConfigSettings.chat_Dialog.chatMessageListMap);
        settings.setValue("messageRetention", globalConfigSettings.chat_Dialog.messageRetention);
        settings.setValue("delayBetweenRetries", globalConfigSettings.chat_Dialog.delayBetweenRetries);
        settings.setValue("allowRetrieveMessagesLater", globalConfigSettings.chat_Dialog.allowRetrieveMessagesLater);
        settings.setValue("numRetries", globalConfigSettings.chat_Dialog.numRetries);
        settings.setValue("keyName", globalConfigSettings.chat_Dialog.keyName);
        settings.setValue("modemSpeed", globalConfigSettings.chat_Dialog.modemSpeed);
        settings.setValue("shortInterleave", globalConfigSettings.chat_Dialog.shortInterleave);
        settings.setValue("useLocalModemSettings", globalConfigSettings.chat_Dialog.useLocalModemSettings);
    }
    settings.endGroup();

    settings.beginGroup("TCPDialog");
    {
        settings.setValue("pos", globalConfigSettings.tcp_Dialog.pos);
        settings.setValue("size", globalConfigSettings.tcp_Dialog.size);
    }
    settings.endGroup();

    settings.beginGroup("UDPDialog");
    {
        settings.setValue("pos", globalConfigSettings.udp_Dialog.pos);
        settings.setValue("size", globalConfigSettings.udp_Dialog.size);
    }
    settings.endGroup();

    settings.beginGroup("ALEDialog");
    {
        settings.setValue("pos", globalConfigSettings.ale_Dialog.pos);
        settings.setValue("size", globalConfigSettings.ale_Dialog.size);
        settings.setValue("ALEsplitterState", globalConfigSettings.ale_Dialog.splitterState);
        settings.setValue("ALEsplitterState2", globalConfigSettings.ale_Dialog.splitterState2);
    }
    settings.endGroup();

    settings.beginGroup("QueueDialog");
    {
        settings.setValue("pos", globalConfigSettings.queue_Dialog.pos);
        settings.setValue("size", globalConfigSettings.queue_Dialog.size);
    }
    settings.endGroup();

    settings.beginGroup("IP");
    {
        //MSC  TCP Server
        settings.setValue("maxPorts", globalConfigSettings.ipSettings.maxPorts);
        settings.setValue("applicationDiscoverPort", globalConfigSettings.ipSettings.applicationDiscoveryPort);
        settings.setValue("ipListenAddress", globalConfigSettings.ipSettings.listenIPAddress);
        settings.setValue("ipListenPort", globalConfigSettings.ipSettings.listenTCPPort);
        settings.setValue("onlyShowIP4", globalConfigSettings.ipSettings.onlyShowIP4);

        //raw TCP Server
        settings.setValue("rawListenIPAddress", globalConfigSettings.ipSettings.rawListenIPAddress);
        settings.setValue("rawListenTCPPort", globalConfigSettings.ipSettings.rawListenTCPPort);
        settings.setValue("rawOnlyShowIP4", globalConfigSettings.ipSettings.rawOnlyShowIP4);
        settings.setValue("rawUseRMICipher",globalConfigSettings.ipSettings.rawUseRMICipher);
        settings.setValue("rawDefaultCipherKey",globalConfigSettings.ipSettings.rawDefaultCipherKey);
    }//IP
    settings.endGroup();
    settings.beginGroup("ALE");
    {
        //        for(int i=0;i<ui->comboBoxSelectALEAddress->count();i++){
        //            if(!configSettings.aleSettings.heardCallList.contains(ui->comboBoxSelectALEAddress->itemText(i))){
        //                configSettings.aleSettings.heardCallList.append(ui->comboBoxSelectALEAddress->itemText(i));
        //            }
        //        }
        settings.setValue("ALETCPPort", globalConfigSettings.aleSettings.ALEIPAddress);
        settings.setValue("ALETCPPort", globalConfigSettings.aleSettings.ALETCPPort);
        settings.setValue("heardCallList", globalConfigSettings.aleSettings.heardCallList);
        settings.setValue("knownALEAddressList", globalConfigSettings.aleSettings.knownALEAddressList);
        settings.setValue("enableALE", globalConfigSettings.aleSettings.enableALE);
        settings.setValue("startScan", globalConfigSettings.aleSettings.startScan);
        settings.setValue("aleLinkTimeOut", globalConfigSettings.aleSettings.aleLinkTimeOut);
    }//ALE
    settings.endGroup();
    settings.beginGroup("TCPClient");
    {
        settings.setValue("IPAddress", globalConfigSettings.clientSettings.IPAddress);
        settings.setValue("TCPPort", globalConfigSettings.clientSettings.TCPPort);
        settings.setValue("useUuencode", globalConfigSettings.clientSettings.useUuencode);
    }
    settings.endGroup();
    settings.beginGroup("dataPort");
    {
        settings.setValue("name", globalConfigSettings.dataComPortSettings.portName);
        settings.setValue("baud", globalConfigSettings.dataComPortSettings.stringBaudRate);
        settings.setValue("data", globalConfigSettings.dataComPortSettings.stringDataBits);
        settings.setValue("parity", globalConfigSettings.dataComPortSettings.stringParity);
        settings.setValue("stop", globalConfigSettings.dataComPortSettings.stringStopBits);
        settings.setValue("flow", globalConfigSettings.dataComPortSettings.stringFlowControl);
        settings.setValue("useRadioComPort", globalConfigSettings.dataComPortSettings.useRadioComPort);
        settings.setValue("useRTSforPTT", globalConfigSettings.dataComPortSettings.useRTSforPTT);
        //        settings.setValue("useRTSDelay", configSettings.dataComPortSettings.useRTSDelay);
        settings.setValue("msRTSDelay", globalConfigSettings.dataComPortSettings.msDelayBeforeDroppingRTS);
        settings.setValue("useDTR", globalConfigSettings.dataComPortSettings.useDTR);
        settings.setValue("waitForCTS", globalConfigSettings.dataComPortSettings.waitForCTS);
        settings.setValue("msDelayAfterCTS", globalConfigSettings.dataComPortSettings.msDelayAfterCTS);
        settings.setValue("waitForDCD", globalConfigSettings.dataComPortSettings.waitForDCD);
        settings.setValue("serialTimeout", globalConfigSettings.dataComPortSettings.serialTimeout);
        settings.setValue("compressionIsDefault", globalConfigSettings.dataComPortSettings.compressionIsDefault);
        settings.setValue("currentDevice", globalConfigSettings.dataComPortSettings.currentDevice);
        settings.setValue("autoStartDMT", globalConfigSettings.dataComPortSettings.autoStartSoftwareModem);
        settings.setValue("delayBeforeNextTX", globalConfigSettings.dataComPortSettings.delayBeforeNextTX);
        //        settings.setValue("configuredModemInterleave", configSettings.dataComPortSettings.configuredModemInterleave);
        //        settings.setValue("configuredModemSpeed", configSettings.dataComPortSettings.configuredModemSpeed);
        //        settings.setValue("pingMSDMT", configSettings.dataComPortSettings.pingMSDMT);
        settings.setValue("resetSoftwareModem", globalConfigSettings.dataComPortSettings.resetSoftwareModem);
        settings.setValue("percentRunOn", globalConfigSettings.dataComPortSettings.percentRunOn);
        settings.setValue("EMCON",globalConfigSettings.dataComPortSettings.EMCON);
        settings.setValue("showModemDialog",globalConfigSettings.dataComPortSettings.showModemDialog);
    }//dataport
    settings.endGroup();

    settings.beginGroup("MSDMTSettings");
    {
        settings.setValue("useMSDMTFile", globalConfigSettings.MSDMTclientSettings.useMSDMTFile);
        settings.setValue("DMTFilePath", globalConfigSettings.MSDMTclientSettings.DMTFilePath);
        settings.setValue("positionID",globalConfigSettings.MSDMTclientSettings.positionID);
        settings.setValue("socketLatency",globalConfigSettings.MSDMTclientSettings.socketLatency);
        settings.setValue("sizeFlushBytes",globalConfigSettings.MSDMTclientSettings.sizeFlushBytes);
    }
    settings.endGroup();

    settings.beginGroup("Cipher");
    {
        settings.setValue("allowPT", globalConfigSettings.cipherSettings.allowPT);
        settings.setValue("useExtendedChecksum", globalConfigSettings.cipherSettings.useExtendedChecksum);
        //useExternalCipherDevice is used in CP Mil but ignored (permenantly set as false) in CP MARS
        if(globalConfigSettings.cipherSettings.useExternalCipherDevice){
            settings.setValue("useExternalCipherDevice", globalConfigSettings.cipherSettings.useExternalCipherDevice);
        }
    }//cipher
    settings.endGroup();
    settings.beginGroup("SQLStatements");
    {
        //        qDebug() << configSettings.sqlStatements.SQLStatementMap;
        settings.setValue("SQLStatementMap", globalConfigSettings.sqlStatements.SQLStatementMap);
    }
    settings.endGroup();

}//writeSettings

void MainWindow::readSettings()
{
    QString fileName = QCoreApplication::applicationDirPath()+"/"+qAppName()+".ini";
    QFile testFile(fileName);
    if(testFile.exists())
        isFirstTimeRun = false;
    QSettings settings(fileName,QSettings::IniFormat, this);
    settings.beginGroup("Application");
    //save MainWindow size and position
    {
        //        qDebug() << Q_FUNC_INFO << settings.value("size") << settings.value("pos");

        resize(settings.value("size", QSize(600, 220)).toSize());//resize window to saved settings or indicated values
        move(settings.value("pos", QPoint(100, 100)).toPoint());//reposition window to saved settings or indicated values

        //        QPoint appPosition = settings.value("pos", QPoint(100, 100)).toPoint();
        //        QScreen *primaryScreen = QGuiApplication::primaryScreen();
        //        //here's where the magic happens, if the app position is not within the current available geometry
        //        //then move it to a valid position
        //        if(!primaryScreen->availableVirtualGeometry().contains(appPosition)){
        //            //            qDebug() << "Application position is OFF-screen!";
        //            move(QPoint(10,10));
        //        }
        //        else {
        //            qDebug() << "Application position is on-screen... OK";
        //        }
    }
    settings.endGroup();
    settings.beginGroup("General");
    {
        globalConfigSettings.generalSettings.useProgressBar = settings.value("useProgressBar",true).toBool();
        globalConfigSettings.generalSettings.hideWindowFrame = settings.value("hideWindowFrame",false).toBool();
        globalConfigSettings.generalSettings.positionIdentifier = settings.value("positionID","CP").toString();
        globalConfigSettings.generalSettings.callSign = settings.value("callSign","NONE").toString();
        globalConfigSettings.generalSettings.showDebugInfo = settings.value("showDebugInfo",true).toBool();
        globalConfigSettings.generalSettings.messageSerialNumber = settings.value("extendedChecksumSerialNumber",0).toInt();
        globalConfigSettings.generalSettings.hideHandShakePackets = settings.value("hideHandShakePackets",false).toBool();
        globalConfigSettings.generalSettings.protectConfig = settings.value("protectConfig",false).toBool();
        globalConfigSettings.generalSettings.confirmApplicationExit = settings.value("confirmApplicationExit",false).toBool();
        globalConfigSettings.generalSettings.hashCheck = settings.value("hashCheck").toByteArray();
        globalConfigSettings.generalSettings.useRetryQueue = settings.value("useRetryQueue",false).toBool();
        globalConfigSettings.generalSettings.autoTXRetryQueue = settings.value("autoTXRetryQueue",false).toBool();
        globalConfigSettings.generalSettings.useSystemTray = settings.value("useSystemTray",true).toBool();
        globalConfigSettings.generalSettings.loadStyleSheet = settings.value("loadStyleSheet",true).toBool();
        globalConfigSettings.generalSettings.styleSheetFilename = settings.value("styleSheetFilename","../QStyleSheets/QSS_Dark_Blue.qss").toString();
        globalConfigSettings.generalSettings.modemSpeed = settings.value("modemSpeed","600").toString();
        globalConfigSettings.generalSettings.modemInterleave = settings.value("modemInterleave","L").toString();
        globalConfigSettings.generalSettings.machineAddressList = settings.value("machineAddressList").toStringList();
        globalConfigSettings.generalSettings.savedQRUParameters = settings.value("savedQRUParameters").toStringList();
    }
    settings.endGroup();

    settings.beginGroup("SettingsDialog");
    {
        globalConfigSettings.settings_Dialog.pos = settings.value("pos").toPoint();
        globalConfigSettings.settings_Dialog.size = settings.value("size").toSize();
    }
    settings.endGroup();

    settings.beginGroup("LogDialog");
    {
        globalConfigSettings.log_Dialog.pos = settings.value("pos").toPoint();
        globalConfigSettings.log_Dialog.size = settings.value("size").toSize();
    }
    settings.endGroup();

    settings.beginGroup("ModemDialog");
    {
        globalConfigSettings.modem_Dialog.pos = settings.value("pos").toPoint();
        globalConfigSettings.modem_Dialog.size = settings.value("size").toSize();
        globalConfigSettings.modem_Dialog.splitterState = settings.value("splitterState").toByteArray();
        globalConfigSettings.modem_Dialog.displayModemOnRecvError = settings.value("displayModemOnRecvError", false).toBool();
    }
    settings.endGroup();

    settings.beginGroup("KeyLoaderDialog");
    {
        globalConfigSettings.keyLoaderSettings.pos = settings.value("pos").toPoint();
        globalConfigSettings.keyLoaderSettings.size = settings.value("size").toSize();
        globalConfigSettings.keyLoaderSettings.splitterState = settings.value("splitterState").toByteArray();
    }
    settings.endGroup();

    settings.beginGroup("ChatDialog");
    {
        globalConfigSettings.chat_Dialog.pos = settings.value("pos").toPoint();
        globalConfigSettings.chat_Dialog.size = settings.value("size").toSize();
        globalConfigSettings.chat_Dialog.chatSplitterState = settings.value("chatSplitterState").toByteArray();
        globalConfigSettings.chat_Dialog.showChatDialog = settings.value("showChatDialog",true).toBool();
        globalConfigSettings.chat_Dialog.chatMessageListMap = settings.value("chatMessageListMap").toMap();
        globalConfigSettings.chat_Dialog.messageRetention = settings.value("messageRetention",1).toInt();//ONE DAY
        globalConfigSettings.chat_Dialog.delayBetweenRetries = settings.value("delayBetweenRetries",2).toInt();//TWO MINUTES
        globalConfigSettings.chat_Dialog.allowRetrieveMessagesLater = settings.value("allowRetrieveMessagesLater",true).toBool();
        globalConfigSettings.chat_Dialog.numRetries = settings.value("numRetries",1).toInt();//ONCE
        globalConfigSettings.chat_Dialog.keyName = settings.value("keyName").toString();
        globalConfigSettings.chat_Dialog.modemSpeed = settings.value("modemSpeed","600").toString();
        globalConfigSettings.chat_Dialog.shortInterleave = settings.value("shortInterleave",true).toBool();
        globalConfigSettings.chat_Dialog.useLocalModemSettings = settings.value("useLocalModemSettings",true).toBool();
    }
    settings.endGroup();

    settings.beginGroup("TCPDialog");
    {
        globalConfigSettings.tcp_Dialog.pos = settings.value("pos").toPoint();
        globalConfigSettings.tcp_Dialog.size = settings.value("size").toSize();
    }
    settings.endGroup();

    settings.beginGroup("UDPDialog");
    {
        globalConfigSettings.udp_Dialog.pos = settings.value("pos").toPoint();
        globalConfigSettings.udp_Dialog.size = settings.value("size").toSize();
    }
    settings.endGroup();

    settings.beginGroup("ALEDialog");
    {
        globalConfigSettings.ale_Dialog.pos = settings.value("pos").toPoint();
        globalConfigSettings.ale_Dialog.size = settings.value("size").toSize();
        globalConfigSettings.ale_Dialog.splitterState = settings.value("ALEsplitterState").toByteArray();
        globalConfigSettings.ale_Dialog.splitterState2 = settings.value("ALEsplitterState2").toByteArray();
    }
    settings.endGroup();

    settings.beginGroup("QueueDialog");
    {
        globalConfigSettings.queue_Dialog.pos = settings.value("pos").toPoint();
        globalConfigSettings.queue_Dialog.size = settings.value("size").toSize();
    }
    settings.endGroup();

    settings.beginGroup("ALE");
    {
        globalConfigSettings.aleSettings.ALEIPAddress = settings.value("ALEIPAddress","127.0.0.1").toString();
        globalConfigSettings.aleSettings.ALETCPPort = settings.value("ALETCPPort","23").toString();
        globalConfigSettings.aleSettings.aleLinkTimeOut = settings.value("aleLinkTimeOut","120").toString();
        globalConfigSettings.aleSettings.heardCallList = settings.value("heardCallList").toStringList();
        globalConfigSettings.aleSettings.knownALEAddressList = settings.value("knownALEAddressList").toStringList();
        globalConfigSettings.aleSettings.enableALE = settings.value("enableALE",false).toBool();
        globalConfigSettings.aleSettings.startScan = settings.value("startScan",false).toBool();
    }
    settings.endGroup();
    settings.beginGroup("IP");
    {
        //MSC  TCP Server
        globalConfigSettings.ipSettings.maxPorts = settings.value("maxPorts",10).toInt();
        globalConfigSettings.ipSettings.applicationDiscoveryPort = settings.value("applicationDiscoverPort",5000).toString();
        globalConfigSettings.ipSettings.listenIPAddress = settings.value("ipListenAddress","127.0.0.1").toString();
        globalConfigSettings.ipSettings.listenTCPPort = settings.value("ipListenPort","5001").toString();
        globalConfigSettings.ipSettings.onlyShowIP4 = settings.value("onlyShowIP4",true).toBool();
        //Raw TCP Server
        globalConfigSettings.ipSettings.rawListenIPAddress = settings.value("rawListenIPAddress","127.0.0.1").toString();
        globalConfigSettings.ipSettings.rawListenTCPPort = settings.value("rawListenTCPPort","5555").toString();
        globalConfigSettings.ipSettings.rawOnlyShowIP4 = settings.value("rawOnlyShowIP4",true).toBool();
        globalConfigSettings.ipSettings.rawUseRMICipher = settings.value("rawUseRMICipher",false).toBool();
        globalConfigSettings.ipSettings.rawDefaultCipherKey = settings.value("rawDefaultCipherKey").toString();
    }
    settings.endGroup();
    settings.beginGroup("TCPClient");
    {
        globalConfigSettings.clientSettings.IPAddress = settings.value("IPAddress","127.0.0.1").toString();
        globalConfigSettings.clientSettings.TCPPort = settings.value("TCPPort","7322").toString();
        globalConfigSettings.clientSettings.useUuencode = settings.value("useUuencode",false).toBool();
    }
    settings.endGroup();
    settings.beginGroup("dataPort");
    {
        globalConfigSettings.dataComPortSettings.portName = settings.value("name").toString();
        globalConfigSettings.dataComPortSettings.stringBaudRate = settings.value("baud","9600").toString();
        globalConfigSettings.dataComPortSettings.stringDataBits = settings.value("data","8").toString();
        globalConfigSettings.dataComPortSettings.stringParity = settings.value("parity","None").toString();
        globalConfigSettings.dataComPortSettings.stringStopBits = settings.value("stop","1").toString();
        globalConfigSettings.dataComPortSettings.stringFlowControl = settings.value("flow","None").toString();
        globalConfigSettings.dataComPortSettings.useRadioComPort = settings.value("useRadioComPort",false).toBool();
        globalConfigSettings.dataComPortSettings.useRTSforPTT = settings.value("useRTSforPTT",false).toBool();
        //        configSettings.dataComPortSettings.useRTSDelay = settings.value("useRTSDelay",false).toBool();
        globalConfigSettings.dataComPortSettings.msDelayBeforeDroppingRTS = settings.value("msRTSDelay",0).toInt();
        globalConfigSettings.dataComPortSettings.useDTR = settings.value("useDTR",false).toBool();
        globalConfigSettings.dataComPortSettings.waitForCTS = settings.value("waitForCTS",false).toBool();
        globalConfigSettings.dataComPortSettings.msDelayAfterCTS = settings.value("msDelayAfterCTS",10).toInt();
        globalConfigSettings.dataComPortSettings.waitForDCD = settings.value("waitForDCD",false).toBool();
        globalConfigSettings.dataComPortSettings.serialTimeout = settings.value("serialTimeout","750").toString();
        globalConfigSettings.dataComPortSettings.compressionIsDefault = settings.value("compressionIsDefault",false).toBool();
        globalConfigSettings.dataComPortSettings.currentDevice = settings.value("currentDevice","MS-DMT TCP").toString();
        globalConfigSettings.dataComPortSettings.autoStartSoftwareModem = settings.value("autoStartDMT",false).toBool();
        globalConfigSettings.dataComPortSettings.delayBeforeNextTX = settings.value("delayBeforeNextTX",6).toInt();
        globalConfigSettings.dataComPortSettings.configuredModemInterleave = settings.value("configuredModemInterleave","L").toString();
        globalConfigSettings.dataComPortSettings.configuredModemSpeed = settings.value("configuredModemSpeed","600").toString();
        globalConfigSettings.dataComPortSettings.resetSoftwareModem = settings.value("resetSoftwareModem",true).toBool();
        globalConfigSettings.dataComPortSettings.percentRunOn = settings.value("percentRunOn",5).toInt();
        globalConfigSettings.dataComPortSettings.EMCON = settings.value("EMCON",false).toBool();
        globalConfigSettings.dataComPortSettings.showModemDialog = settings.value("showModemDialog",false).toBool();
    }
    settings.endGroup();

    settings.beginGroup("MSDMTSettings");
    {
        globalConfigSettings.MSDMTclientSettings.DMTFilePath = settings.value("DMTFilePath").toString();
        globalConfigSettings.MSDMTclientSettings.useMSDMTFile = settings.value("useMSDMTFile",false).toBool();
        globalConfigSettings.MSDMTclientSettings.positionID = settings.value("positionID").toString();
        globalConfigSettings.MSDMTclientSettings.socketLatency = settings.value("socketLatency",0).toInt();
        globalConfigSettings.MSDMTclientSettings.sizeFlushBytes = settings.value("sizeFlushBytes",0).toInt();
    }
    settings.endGroup();

    settings.beginGroup("Cipher");
    {
        globalConfigSettings.cipherSettings.allowPT = settings.value("allowPT", false).toBool();
        globalConfigSettings.cipherSettings.useExtendedChecksum = settings.value("useExtendedChecksum", false).toBool();
        //useExternalCipherDevice is used in CP Mil but ignored (permenantly set as false) in CP MARS
        globalConfigSettings.cipherSettings.useExternalCipherDevice = settings.value("useExternalCipherDevice", false).toBool();
    }
    settings.endGroup();

    settings.beginGroup("SQLStatements");
    {
        globalConfigSettings.sqlStatements.SQLStatementMap = settings.value("SQLStatementMap").toMap();
        //        qDebug() << configSettings.sqlStatements.SQLStatementMap;
    }
    settings.endGroup();
}//readSettings

void MainWindow::updateSettings()
{
    //    qDebug() << "Function Name: " << Q_FUNC_INFO;
    //    QString funcName = Q_FUNC_INFO;
    //    emit signalUpdateDebugLog("Function Name: " + funcName);

    isReconfiguring = true;

    emit signalUpdateRetryQueueButton();
    
    if(globalConfigSettings.aleSettings.enableALE){
        connectALEClient();
    }
    else {
        disconnectALEClient();
    }

    ui->comboBoxSelectModemMode->clear();
    ui->comboBoxSelectModemBaud->clear();
    ui->comboBoxSelectModemInterleave->clear();
    
    if(globalConfigSettings.dataComPortSettings.currentDevice == "MS-DMT Serial" ||
        globalConfigSettings.dataComPortSettings.currentDevice == "MS-DMT TCP"){
        ui->groupBoxModem->show();
        ui->groupBoxDelay->hide();
        QStringList modemModesList = {"MS110A"};
        QStringList modemBaudList = {"75","150","300","600","1200","2400","4800"};
        QStringList modemInterleaveList = {"S","L"};
        ui->comboBoxSelectModemMode->addItems(modemModesList);
        ui->comboBoxSelectModemBaud->addItems(modemBaudList);
        //        ui->comboBoxSelectModemBaud->setCurrentText(configSettings.dataComPortSettings.configuredModemSpeed);
        ui->comboBoxSelectModemInterleave->addItems(modemInterleaveList);
        //        ui->comboBoxSelectModemInterleave->setCurrentText(configSettings.dataComPortSettings.configuredModemInterleave);
        ui->comboBoxConfiguredModemInterleave->setEnabled(false);
        ui->comboBoxConfiguredModemSpeed->setEnabled(false);
        
        ui->comboBoxSelectModemBaud->setCurrentText(globalConfigSettings.generalSettings.modemSpeed);
        ui->comboBoxSelectModemInterleave->setCurrentText(globalConfigSettings.generalSettings.modemInterleave);

        //AUTOSTART DMT
        if(DMTProcess->state() != QProcess::Running){
            if(globalConfigSettings.dataComPortSettings.autoStartSoftwareModem &&
                !globalConfigSettings.MSDMTclientSettings.DMTFilePath.isEmpty()){
                
                DMTProcess->setProgram(globalConfigSettings.MSDMTclientSettings.DMTFilePath);
                QFileInfo DMTFile(globalConfigSettings.MSDMTclientSettings.DMTFilePath);
                DMTProcess->setWorkingDirectory(DMTFile.absolutePath());
                emit signalUpdateApplicationLog("Starting DMT...");
                DMTProcess->start();
            }
        }
    }
    else {
        ui->groupBoxModem->hide();
        ui->groupBoxDelay->show();
        ui->comboBoxConfiguredModemInterleave->setEnabled(true);
        ui->comboBoxConfiguredModemSpeed->setEnabled(true);
        ui->comboBoxConfiguredModemInterleave->setCurrentText(globalConfigSettings.dataComPortSettings.configuredModemInterleave);
        ui->comboBoxConfiguredModemSpeed->setCurrentText(globalConfigSettings.dataComPortSettings.configuredModemSpeed);
    }

    QDateTime buildDateTime;
    buildDateTime.toTimeSpec(Qt::UTC);

    if(QDateTime::fromString(tr(__DATE__)+tr(__TIME__),"MMM  d yyyyHH:mm:ss").isValid()){
        buildDateTime = QDateTime::fromString(tr(__DATE__)+tr(__TIME__),"MMM  d yyyyHH:mm:ss");
    }//if this time format is valid
    else//else use this time format
        buildDateTime = QDateTime::fromString(tr(__DATE__)+tr(__TIME__),"MMM d yyyyHH:mm:ss");

    QString windowTitleString;
    windowTitleString.append(globalConfigSettings.generalSettings.positionIdentifier+" - ");
    windowTitleString.append(tr(APP_NAME_ABBV) + " - " +
                             globalConfigSettings.dataComPortSettings.currentDevice);
    ui->labelTitleBar->setText(windowTitleString);
    //    emit signalUpdateDebugLog("status_label - "+windowTitleString);
    QString version = VERSION;
    if(version.contains("alpha") || version.contains("beta")){
        windowTitleString.append(" - " +tr(VERSION));
        windowTitleString.append(" - " + tr(BUILDDATE));
    }
    else {
        windowTitleString.append(" - " + tr(RELEASEDATE));
    }
    this->setWindowTitle(windowTitleString);

    //    qDebug() << __DATE__ << __TIME__;
    
    if(globalConfigSettings.generalSettings.useProgressBar){
        ui->progressBarStatus->setValue(0);
        ui->progressBarStatus->show();
    }
    else {
        ui->progressBarStatus->setValue(0);
        ui->progressBarStatus->hide();
    }

    ui->pushButtonTCP->setEnabled(numberConnections > 0);
    
    if(globalConfigSettings.dataComPortSettings.currentDevice == "MS-DMT TCP"){
        modemSerialTimer->stop();
    }
    else {
        modemSerialTimer->setInterval(globalConfigSettings.dataComPortSettings.serialTimeout.toInt());
        modemSerialTimer->start();
        if(globalConfigSettings.dataComPortSettings.useDTR){
            modemSerialPort->setDataTerminalReady(true);
        }
        else {
            modemSerialPort->setDataTerminalReady(false);
        }
        logSerialSignals();
    }
    
    
    
    ui->checkBoxEMCON->setChecked(globalConfigSettings.dataComPortSettings.EMCON);

    Qt::WindowFlags flags = Qt::Window;
    if(globalConfigSettings.generalSettings.hideWindowFrame){
        flags |= Qt::FramelessWindowHint;
    }//hide window frame
    setWindowFlags(flags);
    qApp->processEvents();
    qApp->processEvents();
    //    this->resize(this->sizeHint());
    this->show();
    sendStatusChange();
    emit signalUpdateChatDialogConfig(xmlWrapper("status","",true));
    isReconfiguring = false;

}//updateSettings

QByteArray MainWindow::xmlWrapper(QString type, QByteArray message, bool isHandshake)
{
    //TODO move xmlWrapper to ClassXML
    QByteArray outData;
    QXmlStreamWriter xmlWriter(&outData);
    QString boolCheckString;

    switch (checksumBool) {
    case PASS:
        boolCheckString = "PASS";
        break;
    case FAIL:
        boolCheckString = "FAIL";
        break;
    case ERRO:
        boolCheckString = "ERRO";
        break;
    default:
        boolCheckString = "UNKN";
        break;
    }
    xmlWriter.setAutoFormatting(true);
    xmlWriter.writeStartDocument();
    xmlWriter.writeStartElement("V3PROTOCOL");
    //    xmlWriter.writeComment("This is a comment");
    xmlWriter.writeStartElement("HEADER");
    xmlWriter.writeTextElement("POSITIONID",globalConfigSettings.generalSettings.positionIdentifier);
    if(isHandshake){
        if(type == "ack"){
            xmlWriter.writeTextElement("COMMAND",type);
            if(ui->comboBoxSelectModemBaud->isEnabled())
                xmlWriter.writeTextElement("MODEMDATARATE", ui->comboBoxSelectModemBaud->currentText());
            if(ui->comboBoxSelectModemInterleave->isEnabled())
                xmlWriter.writeTextElement("MODEMINTERLEAVE", "S");
            if(ui->comboBoxSelectModemMode->isEnabled())
                xmlWriter.writeTextElement("MODEMWAVEFORM", ui->comboBoxSelectModemMode->currentText());
            xmlWriter.writeTextElement("COMPRESS", QString::number(static_cast<int>(useCompression)));
            //            qDebug() << QString::number(static_cast<int>(decryptionkey != "UNK" && decryptionkey != "PLAINTEXT" && !decryptionkey.isEmpty()));
            //            qDebug() << (decryptionkey != "UNK") << (decryptionkey != "PLAINTEXT") << (!decryptionkey.isEmpty());
            //            xmlWriter.writeTextElement("ENCRYPT", QString::number(static_cast<int>((decryptionkey != "UNK" && decryptionkey != "PLAINTEXT" && !decryptionkey.isEmpty()))));
            xmlWriter.writeTextElement("ENCRYPT", QString::number(static_cast<int>((decryptionkey != "PLAINTEXT" && !decryptionkey.isEmpty()))));
            xmlWriter.writeTextElement("ENCRYPTIONKEY", decryptionkey);
            xmlWriter.writeTextElement("SOURCESTATION",RXsourceStation);
            xmlWriter.writeTextElement("DESTINATIONSTATION",RXdestinationStation);
        }
        else if (type == "pong") {
            xmlWriter.writeTextElement("COMMAND",type);
        }
        else if(type=="config")
        {
            xmlWriter.writeTextElement("COMMAND",type);
            QString keyList;
            foreach (QString key, keyNameList) {
                keyList.append(key+",");
            }
            if(globalConfigSettings.cipherSettings.useExternalCipherDevice){
                xmlWriter.writeTextElement("KEYLIST", "EXTDEVICE");
            }
            else {
                xmlWriter.writeTextElement("KEYLIST", keyList);
            }

            QString heardCallList;
            foreach (QString tempString, globalConfigSettings.aleSettings.heardCallList) {
                heardCallList.append(tempString+',');
            }
            heardCallList.remove(heardCallList.lastIndexOf(','),1);
            xmlWriter.writeTextElement("CALLSIGNLIST", heardCallList);


            QString knownALEAddresses;
            foreach (QString tempString, globalConfigSettings.aleSettings.knownALEAddressList) {
                knownALEAddresses.append(tempString+',');
            }
            knownALEAddresses.remove(knownALEAddresses.lastIndexOf(','),1);
            xmlWriter.writeTextElement("ALEADDRESSLIST", knownALEAddresses);

            if(globalConfigSettings.dataComPortSettings.currentDevice == "MS-DMT Serial" ||
                globalConfigSettings.dataComPortSettings.currentDevice == "MS-DMT TCP"){
                QString modemRates = "75,150,300,600,1200,2400,4800";
                xmlWriter.writeTextElement("MODEMDATARATE", modemRates);
                QString modemInterleaves = "L,S";
                xmlWriter.writeTextElement("MODEMINTERLEAVE", modemInterleaves);
            }
            else {
                QString modemRates = "";
                xmlWriter.writeTextElement("MODEMDATARATE", modemRates);
                QString modemInterleaves = "";
                xmlWriter.writeTextElement("MODEMINTERLEAVE", modemInterleaves);
            }
            //            QString aleAddressList;
        }//1config
        else if (type=="status")
        {
            xmlWriter.writeTextElement("COMMAND",type);
            //            xmlWriter.writeTextElement("ANTENNABEAMHEADING", antennaBeamHeading);
            if(ui->comboBoxSelectModemBaud->isEnabled())
                xmlWriter.writeTextElement("MODEMDATARATE", ui->comboBoxSelectModemBaud->currentText());
            if(ui->comboBoxSelectModemInterleave->isEnabled())
                xmlWriter.writeTextElement("MODEMINTERLEAVE", ui->comboBoxSelectModemInterleave->currentText());
            if(ui->comboBoxSelectModemMode->isEnabled())
                xmlWriter.writeTextElement("MODEMWAVEFORM", ui->comboBoxSelectModemMode->currentText());
            if(b_isLinked){
                xmlWriter.writeTextElement("ALEADDRESS", linkedAddress);
            }
        }//2status
        else if (type=="helo")
        {
            xmlWriter.writeTextElement("COMMAND",type);
            xmlWriter.writeTextElement("LISTENIPADDRESS", globalConfigSettings.ipSettings.listenIPAddress);
            xmlWriter.writeTextElement("LISTENTCPPORT", globalConfigSettings.ipSettings.listenTCPPort);
        }//3helo
        else if (type=="update")
        {
            xmlWriter.writeTextElement("COMMAND",type);
            xmlWriter.writeTextElement("LISTENIPADDRESS", globalConfigSettings.ipSettings.listenIPAddress);
            xmlWriter.writeTextElement("LISTENTCPPORT", globalConfigSettings.ipSettings.listenTCPPort);
        }//4update
        else if (type=="bye")
        {
            xmlWriter.writeTextElement("COMMAND",type);
            xmlWriter.writeTextElement("LISTENIPADDRESS", globalConfigSettings.ipSettings.listenIPAddress);
            xmlWriter.writeTextElement("LISTENTCPPORT", globalConfigSettings.ipSettings.listenTCPPort);
        }//5bye
        else if (type=="error")
        {
            xmlWriter.writeTextElement("COMMAND",type);
        }//error
        xmlWriter.writeEndElement();//HEADER

        if(type =="error"){
            xmlWriter.writeStartElement("PAYLOAD");
            xmlWriter.writeTextElement("DATA",message);
            xmlWriter.writeEndElement();//PAYLOAD
        }//6error
        else if(type =="ack"){
            xmlWriter.writeStartElement("PAYLOAD");
            xmlWriter.writeTextElement("DATA",message);
            xmlWriter.writeEndElement();//PAYLOAD
        }//6error
    }//if this is a handshake
    else {//else data
        xmlWriter.writeTextElement("COMMAND","data");
        //        xmlWriter.writeTextElement("ANTENNABEAMHEADING", antennaBeamHeading);
        if(g_IsDigitalVoice){
            xmlWriter.writeTextElement("SOURCESTATION",RXsourceStation);
            xmlWriter.writeTextElement("DESTINATIONSTATION",RXdestinationStation);
            xmlWriter.writeTextElement("MODEMDATARATE", g_DVSpeed);
            xmlWriter.writeTextElement("MODEMINTERLEAVE", "V");
            //            xmlWriter.writeTextElement("MODEMWAVEFORM", ui->comboBoxSelectModemMode->currentText());
            xmlWriter.writeEndElement();//HEADER
            xmlWriter.writeStartElement("PAYLOAD");
            xmlWriter.writeTextElement("DATA",message);
            xmlWriter.writeEndElement();//PAYLOAD
            xmlWriter.writeEndElement();//V3PROTOCOL
            xmlWriter.writeEndDocument();
            return outData;
        }
        if(message.startsWith("ack|")){
            xmlWriter.writeTextElement("PRIORITY", "3");
            xmlWriter.writeTextElement("MODEMINTERLEAVE", "S");
        }
        else {
            xmlWriter.writeTextElement("PRIORITY", "4");
        }
        xmlWriter.writeTextElement("COMPRESS", QString::number(static_cast<int>(wasCompressed)));
        xmlWriter.writeTextElement("ENCRYPT", QString::number(static_cast<int>(wasEncrypted)));
        xmlWriter.writeTextElement("ENCRYPTIONKEY", decryptionkey);
        xmlWriter.writeTextElement("SOURCESTATION",RXsourceStation);
        xmlWriter.writeTextElement("DESTINATIONSTATION",RXdestinationStation);
        xmlWriter.writeTextElement("CHECKSUM", boolCheckString);
        xmlWriter.writeTextElement("LOCAL","0");
        //        xmlWriter.writeTextElement("WASAUTH", QString::number(static_cast<int>(wasAuthenticated)));
        //        xmlWriter.writeTextElement("AUTHPASSED", QString::number(static_cast<int>(authPassed)));
        //        xmlWriter.writeTextElement("AUTHAGENCY",authAgency);
        xmlWriter.writeEndElement();//HEADER
        xmlWriter.writeStartElement("PAYLOAD");
        xmlWriter.writeTextElement("DATA",message);
        xmlWriter.writeEndElement();//PAYLOAD
    }//else not a handshake
    xmlWriter.writeEndElement();//V3PROTOCOL
    xmlWriter.writeEndDocument();
    return outData;
}//xmlWrapper

void MainWindow::slotProcessRadioCommands(QByteArray dataByteArray)
{
    if(dataByteArray.size() < 1){
        return;
    }

    //    qDebug() << "Function Name: " << Q_FUNC_INFO;
    //    QString funcName = Q_FUNC_INFO;
    //    emit signalUpdateDebugLog("Function Name: " + funcName+" : "+dataByteArray);
}//slotProcessRadioCommands

void MainWindow::writeCommandModem(QByteArray dataByteArray)
{
    if(dataByteArray.isEmpty())
        return;
    //    qDebug() << "Function Name: " << Q_FUNC_INFO + data;
    //    QString funcName = Q_FUNC_INFO;
    //    emit signalUpdateDebugLog("Locking writeCommandModem");
    QMutexLocker mutexLocker(&writeCommandModemMutex);
    
    if(globalConfigSettings.dataComPortSettings.currentDevice == "MS-DMT TCP"){
        writeMSDMTCMDPort(dataByteArray);
    }//if tcp
    else {
        if(!modemSerialPort->isOpen()){
            return;
        }
        g_IsWritingData = true;
        if(globalConfigSettings.dataComPortSettings.useRadioComPort)
        {
            writeControlRadio(dataByteArray);
        }
        else
        {
            modemSerialPort->write(dataByteArray);
            modemSerialPort->flush();
            emit signalUpdateModemLog(dataByteArray,false,true);
        }
        g_IsWritingData = false;
    }//else serial
}//writeCommandModem

QByteArray MainWindow::encryptData(QByteArray dataByteArray)
{
    //    qDebug() << Q_FUNC_INFO;
    MSCCipherLibraryClass MSC_Cipher;
    //    qDebug() << useEncryption << useCompression << encryptionkey;

    dataByteArray = MSC_Cipher.QT_ProcessData(dataByteArray,
                                              encryptionkey,
                                              TXsourceStation,
                                              TXdestinationStation,
                                              useEncryption,
                                              useCompression,
                                              true);
    if(dataByteArray.startsWith("ERROR: ")){
        //        qDebug() << "Cipher error: " << dataByteArray;
        writeTcpSockets(xmlWrapper("error","Cipher error:  "+dataByteArray,true),"");
    }
    else {
#ifdef USE_FLUSH_PADDING
        for(int i = 0; i < globalConfigSettings.MSDMTclientSettings.sizeFlushBytes; i++)
        {
            qDebug() << i+1 << "appending flush byte...";
            dataByteArray.append(QByteArray::fromHex(FLUSH_BYTES));
        }
        //    qDebug() << payload.toHex().toUpper();
#endif
    }
    //    qDebug() << "Zeroizing cipher..." << MSC_Cipher.QT_Zeroize();
    return dataByteArray;
}//encryptData

QByteArray MainWindow::decryptData(QByteArray dataByteArray)
{
    //    if(dataByteArray.isEmpty()){
    //        qDebug() << "ERROR: Trying to decrypt empty plaintext";
    //        return "ERROR: Trying to decrypt empty plaintext";
    //    }
    //decrypt the data using MSC_Cipher dll
    wasCompressed = false;
    wasEncrypted = false;
    checksumBool = UNKN;
    RXsourceStation.clear();
    RXdestinationStation.clear();
    predictedByteCountSize = 0;
    decryptionkey.clear();
    wasAuthenticated=false;
    authPassed=false;
    authAgency.clear();

    MSCCipherLibraryClass MSC_Cipher;
    QStringList returnStringList = MSC_Cipher.QT_Decrypt(dataByteArray);
    //    QByteArrayList returnStringList = MSC_Cipher.QT_DecryptByte(dataByteArray);
    if(!returnStringList.at(0).startsWith("ERROR: ")){
        dataByteArray = printables(returnStringList.at(0).toLatin1());
        //        dataByteArray = returnStringList.at(0);
        if (returnStringList.at(1) == "PASS") {
            checksumBool = PASS;
        }
        else if (returnStringList.at(1) == "FAIL") {
            checksumBool = FAIL;
        }
        else if (returnStringList.at(1) == "ERRO") {
            checksumBool = ERRO;
        }
        else {
            checksumBool = UNKN;
        }

        RXsourceStation = returnStringList.at(2);
        if(!globalConfigSettings.aleSettings.heardCallList.contains(RXsourceStation))
            globalConfigSettings.aleSettings.heardCallList.append(RXsourceStation);
        globalConfigSettings.aleSettings.heardCallList.removeAll(globalConfigSettings.generalSettings.callSign);


        RXdestinationStation = returnStringList.at(3);
        predictedByteCountSize = returnStringList.at(4).toInt();
        if(returnStringList.at(5) == "COMPRESSED")
            wasCompressed = true;
        if(!returnStringList.at(6).isEmpty()){
            //            wasEncrypted = true;
            decryptionkey = returnStringList.at(6);
            wasEncrypted = (decryptionkey != "PLAINTEXT") ? true : false;
        }
        if(!returnStringList.at(7).isEmpty()){
            wasAuthenticated = static_cast<bool>(returnStringList.at(7).toInt());
        }
        if(!returnStringList.at(8).isEmpty()){
            authPassed = static_cast<bool>(returnStringList.at(8).toInt());
        }
        if(!returnStringList.at(9).isEmpty()){
            authAgency = returnStringList.at(9);
        }
    }//if no ERROR:
    else {
        dataByteArray = returnStringList.at(0).toLatin1();
        //        dataByteArray = returnStringList.at(0);
        if(dataByteArray.contains("Could not decrypt")){
            wasEncrypted = true;
            decryptionkey = "UNK";
        }
    }//else ERROR:
    //    qDebug() << "Zeroizing cipher..." << MSC_Cipher.QT_Zeroize();
    emit signalUpdateChatDialogConfig(xmlWrapper("config","",true));
    return dataByteArray;
}//decryptData

bool MainWindow::testDecrypt(QByteArray ciphertext)
{
    //    qDebug() << "Function Name: " << Q_FUNC_INFO;
    //    QString funcName = Q_FUNC_INFO;
    //    emit signalUpdateDebugLog("Function Name: " + funcName);
    //    bool wasEncrypted=false;
    QByteArray plaintextData = decryptData(ciphertext);
    if(!plaintextData.startsWith("ERROR: ")){
        return true;
    }
    else {
        return false;
    }
}//testDecrypt

int MainWindow::calculateDelayBetweenMessages(int dataSize)
{
    //THIS IS A HACK ATTEMPT AT APPROXIMATING TIME FOR 110A MODEM SPEED
    /************************************************************************************
                                      4.2.1
                                      Modulation and data signaling rates and tolerance
                                      .
                                      The  modulation  rates  expressed  in  baud  (Bd)  and  the  data  signaling  rates  expressed  in  bits  per
                                      second (bps) at the standard interfaces shown on Figure 1 shall be as listed below (as appropriate
                                      for each application listed in Table I).
                                      a. 50 Bd or bps (optional, for legacy use only)
                                      b. 75 X 2^m Bd or bps, up to and including 9600 Bd or bps, where m is a positive integer 0,1, 2, ... 7.

                                      2^0 = 1   * 75 = 75
                                      2^1 = 2   * 75 = 150
                                      2^2 = 4   * 75 = 300
                                      2^3 = 8   * 75 = 600
                                      2^4 = 16  * 75 = 1200
                                      2^5 = 32  * 75 = 2400
                                      2^6 = 64  * 75 = 4800
                                      2^7 = 128 * 75 = 9600

                                      The  data  signaling  rate  is  expressed  in  bps;  the  modulation  rate  is  expressed in  Bd.
                                      Data  signaling  rates  in  bps  and  modulation  rates  in  Bd  are  the  same  only  for binary signaling.
                                      Data signaling rates in bps relate to modulation rates in Bd through the following equation:

                                      Data signaling rates (bps) = k x modulation rates (Bd)
                                      where k = log2M is the number of binary digits per modulation symbol, and M is the number of modulation symbols.


                                      ********************************************************************************************************/
    double modemSpeed;
    if(ui->comboBoxConfiguredModemSpeed->isEnabled())
        modemSpeed = ui->comboBoxConfiguredModemSpeed->currentText().toDouble();
    else {
        modemSpeed = ui->comboBoxSelectModemBaud->currentText().toDouble();
    }
    double timeToTransmitData = 0.0;
    //    qDebug() << (double)dataSize << (double)dataSize*10.1 << modemSpeed;
    timeToTransmitData = dataSize * MODEM_SPEED_MULTIPLIER / modemSpeed;//bits per second
    //    qDebug() << timeToTransmitData;
    if(timeToTransmitData < 1.0)
        timeToTransmitData = 1.0;
    if(ui->comboBoxConfiguredModemInterleave->currentText() == "0"){
        timeToTransmitData += 0;
        //        qDebug() << timeToTransmitData;
    }
    else if(ui->comboBoxSelectModemBaud->currentText() == "4800"){
        timeToTransmitData += 1;
        //        qDebug() << timeToTransmitData;
    }
    else if(ui->comboBoxConfiguredModemInterleave->currentText() == "S"){
        timeToTransmitData += 1.2;
        //        qDebug() << timeToTransmitData;
    }
    else {
        timeToTransmitData += 6;
        //        qDebug() << timeToTransmitData;
    }
    if(timeToTransmitData < 1)
        timeToTransmitData = 1.0;
    //    qDebug() << dataSize << timeToTransmitData << qRound(timeToTransmitData);
    return qRound(timeToTransmitData);
}//loadChannels

void MainWindow::writeDataModem(QByteArray dataByteArray)
{
    //    qDebug() << Q_FUNC_INFO;
    if(dataByteArray.size() == 0)
        return;

    //    QString funcName = Q_FUNC_INFO;
    //    emit signalUpdateDebugLog("Function Name: " + funcName);
    //    qApp->processEvents();

    emit signalUpdateDebugLog("Locking writeModemMutex");
    QMutexLocker mutexLocker(&writeModemMutex);
    emit signalUpdateDebugLog("Locked writeModemMutex");
    //    qApp->processEvents();

    //    emit signalUpdateModemLog(dataByteArray, false, true);

    g_IsWritingData = true;

    /************************************************************************
    * USE TCP SOCKET
    * *********************************************************************/
    if(globalConfigSettings.dataComPortSettings.currentDevice == "TCPSocket"){
        if(writeDataTCPSocket(dataByteArray,
                               globalConfigSettings.clientSettings.IPAddress,
                               globalConfigSettings.clientSettings.TCPPort)){
            emit signalUpdateModemLog(dataByteArray, false, true);
        }
    }
    /************************************************************************
    * USE MS_DMT TCP Port
    * *********************************************************************/
    else if (globalConfigSettings.dataComPortSettings.currentDevice == "MS-DMT TCP") {
        //        qDebug() << "Use MSDMT TCP Port";
        if(writeMSDMTDataPort(dataByteArray)){
            emit signalUpdateDebugLog("Data sent to modem (TCP)...");

            if(writeMSDMTCMDPort("CMD:SENDBUFFER")){
                emit signalUpdateDebugLog("Command sent to modem (TCP) - CMD:SENDBUFFER");
            }//if write command modem
            else {
                emit signalUpdateModemLog("ERROR - Command failed! "+dataByteArray, false, true);
            }//else write command modem failed
        }//if write data to modem
        else {
            emit signalUpdateCriticalNotifications("ERROR: Problem writing data to MS-DMT Data Port!");
        }//else write data modem failed
        g_IsWritingData = false;
    }
    /************************************************************************
    * USE MS_DMT DMT File
    * *********************************************************************/
    else if(globalConfigSettings.dataComPortSettings.currentDevice == "MS-DMT Serial" &&
             globalConfigSettings.MSDMTclientSettings.useMSDMTFile){
        if(!modemSerialPort->isOpen())
            return;
        
        if(globalConfigSettings.dataComPortSettings.useDTR){
            emit signalUpdateDebugLog("Setting DTR low");
            modemSerialPort->setDataTerminalReady(false);
            while(isDTR()){
                //            qDebug() << "Waiting for DTR to drop...";
                emit signalUpdateDebugLog("Waiting for DTR to drop...");
                //                QApplication::processEvents();
                QThread::msleep(100);
                if(!globalConfigSettings.dataComPortSettings.useDTR)
                    break;
                //check for application exit - flush buffer and reset DTR and RTS
                if(g_ExitApplication){
                    modemSerialPort->flush();//flush the serial buffer
                    if(globalConfigSettings.dataComPortSettings.useRTSforPTT)
                        modemSerialPort->setRequestToSend(false);//set RTS low
                    if(globalConfigSettings.dataComPortSettings.useDTR)
                        modemSerialPort->setDataTerminalReady(true);//set DTR high
                    g_IsWritingData = false;
                    emit signalUpdateStatusDisplay("Idle...");
                    g_IsWritingData = false;
                    emit signalUpdateDebugLog("Un-Locking writeModemMutex");
                    return;
                }//if exit app
            }//while not DTR
        }//if use DTR

        //wait for DCD low
        if(globalConfigSettings.dataComPortSettings.waitForDCD){
            bool needDelay = false;
            logSerialSignals();
            emit signalUpdateDebugLog("Checking for DCD low...");
            while (isDCD()) {
                needDelay = true;
                emit signalUpdateDebugLog("Waiting for DCD low...");
                //                QApplication::processEvents();
                QThread::msleep(500);
                //if we're stuck waiting for DCD and we change settings to not use DCD break out of loop
                if(!globalConfigSettings.dataComPortSettings.waitForDCD)
                    break;
                //check for application exit - flush buffer and reset DTR and RTS
                if(g_ExitApplication){
                    modemSerialPort->flush();//flush the serial buffer
                    if(globalConfigSettings.dataComPortSettings.useRTSforPTT)
                        modemSerialPort->setRequestToSend(false);//set RTS low
                    if(globalConfigSettings.dataComPortSettings.useDTR)
                        modemSerialPort->setDataTerminalReady(true);//set DTR high
                    g_IsWritingData = false;
                    emit signalUpdateStatusDisplay("Idle...");
                    g_IsWritingData = false;
                    emit signalUpdateDebugLog("Un-Locking writeModemMutex");
                    return;
                }//if exit app
            }//while DCD is high
            emit signalUpdateStatusDisplay("Idle...");
            ui->comboBoxSelectModemMode->setCurrentText(modemWaveform);
            ui->comboBoxSelectModemBaud->setCurrentText(modemBaud);
            ui->comboBoxSelectModemInterleave->setCurrentText(modemInterleave);
            //            QApplication::processEvents();
            if(needDelay){
                emit signalUpdateDebugLog("Configured delay..");
                //                QApplication::processEvents();
                QThread::sleep(100);
                needDelay = false;
            }
        }//if wait for DCD
        //use <<CMD:SEND FILE>>
        //write data to file

        QTemporaryFile tempFile;

        if(tempFile.open())
        {
            tempFile.write(dataByteArray, dataByteArray.size());
            tempFile.close();
            while (tempFile.isOpen()) {
                QThread::currentThread()->msleep(100);
            }
            QString filename = QApplication::applicationDirPath()+"/DMTSendFile.dat";
            QFile dmtFile(filename);
            if(dmtFile.exists())
                dmtFile.remove();
            tempFile.copy(filename);
            //send command
            QByteArray command("<<CMD:SEND FILE>>"+filename.toLatin1()+"<>");
            emit signalUpdateApplicationLog(command);
            modemSerialPort->write(command);
            modemSerialPort->flush();
            emit signalUpdateDebugLog("Data sent to modem (File)...");
            emit signalUpdateModemLog(dataByteArray, false, true);
            emit signalUpdateStatusDisplay("Transmitting data...");
            //            QApplication::processEvents();
            emit signalUpdateDebugLog("**** "+QString::number(calculateDelayBetweenMessages(dataByteArray.size())*1000));
            QTimer::singleShot(calculateDelayBetweenMessages(dataByteArray.size())*1000, [=] {
                // must have CONFIG += c++11 in .pro file
                emit signalUpdateStatusDisplay("Idle...");
            });
        }
        //        else {
        //            qDebug() << "ERROR: " << tempFile.errorString();
        //        }
    }//if is MS-DMT and use MSDMT CMD:FILE

    /*****************************************************************
     * This is the 5710A code to buffer data to the modem checking for
     * serial signals as required
     * ******************************************************************/
    else {
        if(!modemSerialPort->isOpen())
            return;
        
        if(globalConfigSettings.dataComPortSettings.waitForDCD){
            int breakCounter = 10;//for breaking out of loop after 10 iterations
            while (isDCD() && breakCounter > 0) {
                emit signalUpdateDebugLog("Waiting for DCD low");
                QApplication::processEvents();
                QThread::msleep(100);
                breakCounter--;
            }//while DCD is high
        }//if wait for DCD
        
        if(globalConfigSettings.dataComPortSettings.useDTR){
            emit signalUpdateDebugLog("Setting DTR low");
            modemSerialPort->setDataTerminalReady(false);
            while (isDTR()) {
                emit signalUpdateDebugLog("Waiting for DTR low...");
                QApplication::processEvents();
                QThread::msleep(10);
                if(!globalConfigSettings.dataComPortSettings.useDTR)
                    break;
                //check for application exit - flush buffer and reset DTR and RTS
                if(g_ExitApplication){
                    modemSerialPort->flush();//flush the serial buffer
                    if(globalConfigSettings.dataComPortSettings.useRTSforPTT)
                        modemSerialPort->setRequestToSend(false);//set RTS low
                    if(globalConfigSettings.dataComPortSettings.useDTR)
                        modemSerialPort->setDataTerminalReady(true);//set DTR high
                    g_IsWritingData = false;
                    emit signalUpdateStatusDisplay("Idle...");
                    g_IsWritingData = false;
                    emit signalUpdateDebugLog("Un-Locking writeModemMutex");
                    return;
                }//if exit app
            }//while DTR is high
        }//if use DTR then set low
        
        if(globalConfigSettings.dataComPortSettings.useRTSforPTT)
        {
            //set RST high
            modemSerialPort->setRequestToSend(true);
            //wait for RTS high
            while (!isRTS()) {
                emit signalUpdateApplicationLog(QString(Q_FUNC_INFO)+" - Waiting for RTS high...");
                emit signalUpdateStatusDisplay("Waiting for RTS...");
                QThread::msleep(100);
                QApplication::processEvents();
                if(g_ExitApplication){
                    if(globalConfigSettings.dataComPortSettings.useRTSforPTT)
                        modemSerialPort->setRequestToSend(false);//set RTS low
                    if(globalConfigSettings.dataComPortSettings.useDTR)
                        modemSerialPort->setDataTerminalReady(true);//set DTR high
                    g_IsWritingData = false;
                    emit signalUpdateStatusDisplay("Idle...");
                    g_IsWritingData = false;
                    emit signalUpdateDebugLog("Un-Locking writeModemMutex");
                    return;
                }//if exit app
            }//while not RTS high
        }//if useRTS
        //wait for CTS
        if(globalConfigSettings.dataComPortSettings.waitForCTS){
            while(!isCTS()){
                //                qDebug() << "Waiting for CTS...";
                emit signalUpdateStatusDisplay("Waiting for CTS...");
                QThread::msleep(100);
                QApplication::processEvents();
                if(!globalConfigSettings.dataComPortSettings.waitForCTS){
                    break;
                }

                if(g_ExitApplication){
                    while(modemSerialPort->bytesToWrite()){
                        modemSerialPort->flush();//flush the serial buffer
                    }
                    if(globalConfigSettings.dataComPortSettings.useRTSforPTT)
                        modemSerialPort->setRequestToSend(false);//set RTS low
                    if(globalConfigSettings.dataComPortSettings.useDTR)
                        modemSerialPort->setDataTerminalReady(true);//set DTR high
                    g_IsWritingData = false;
                    emit signalUpdateStatusDisplay("Idle...");
                    g_IsWritingData = false;
                    emit signalUpdateDebugLog("Un-Locking writeModemMutex");
                    return;
                }//if exit app
            }//while !isCTS
        }//if wait for CTS
        
        QThread::msleep(globalConfigSettings.dataComPortSettings.msDelayAfterCTS);

        emit signalUpdateStatusDisplay("Buffering data...");

        for(int i=0; i< dataByteArray.size(); i++){
            //check for application exit - flush buffer and reset DTR and RTS
            if(g_ExitApplication){
                while(modemSerialPort->bytesToWrite()){
                    modemSerialPort->flush();//flush the serial buffer
                }
                if(globalConfigSettings.dataComPortSettings.useRTSforPTT)
                    modemSerialPort->setRequestToSend(false);//set RTS low
                if(globalConfigSettings.dataComPortSettings.useDTR)
                    modemSerialPort->setDataTerminalReady(true);//set DTR high
                g_IsWritingData = false;
                emit signalUpdateStatusDisplay("Idle...");
                g_IsWritingData = false;
                emit signalUpdateDebugLog("Un-Locking writeModemMutex");
                return;
            }//if exit app

            //send the byte out the serial port
            modemSerialPort->putChar(dataByteArray.at(i));
            while (modemSerialPort->bytesToWrite()) {
                modemSerialPort->flush();
                QApplication::processEvents();
            }//while bytes to write

            g_IsTransmitting = true;
            
            if(globalConfigSettings.dataComPortSettings.waitForCTS){
                if(!isCTS()){
                    emit signalUpdateDebugLog("Waiting for CTS...");
                    emit signalUpdateStatusDisplay("Waiting for CTS...");

                    while (Q_UNLIKELY(!isCTS())) {
                        QApplication::processEvents();
                        //check for application exit - flush buffer and reset DTR and RTS
                        if(g_ExitApplication){
                            while(modemSerialPort->bytesToWrite()){
                                modemSerialPort->flush();//flush the serial buffer
                            }
                            if(globalConfigSettings.dataComPortSettings.useRTSforPTT)
                                modemSerialPort->setRequestToSend(false);//set RTS low
                            if(globalConfigSettings.dataComPortSettings.useDTR)
                                modemSerialPort->setDataTerminalReady(true);//set DTR high
                            g_IsWritingData = false;
                            emit signalUpdateStatusDisplay("Idle...");
                            g_IsWritingData = false;
                            emit signalUpdateDebugLog("Un-Locking writeModemMutex");
                            return;
                        }//if exit app
                    }//while not CTS
                    emit signalUpdateStatusDisplay("Buffering data...");
                }//if !isCTS()
            }//if use RTS for PTT
        }//for each data byte
        while (modemSerialPort->bytesToWrite()) {
            modemSerialPort->flush();
            QApplication::processEvents();
        }//while bytes to write

        g_IsWritingData = false;

        //    emit signalUpdateDebugLog("Data buffering complete...");
        emit signalUpdateStatusDisplay("Transmitting data...");
        
        if(globalConfigSettings.dataComPortSettings.useRTSforPTT){
            QThread::msleep(globalConfigSettings.dataComPortSettings.msDelayBeforeDroppingRTS);
            modemSerialPort->setRequestToSend(false);
            while (isRTS()) {
                emit signalUpdateApplicationLog(QString(Q_FUNC_INFO)+" - Waiting for RTS low...");
                QThread::msleep(100);
                QApplication::processEvents();
                if(g_ExitApplication){
                    while(modemSerialPort->bytesToWrite()){
                        modemSerialPort->flush();//flush the serial buffer
                    }
                    if(globalConfigSettings.dataComPortSettings.useRTSforPTT)
                        modemSerialPort->setRequestToSend(false);//set RTS low
                    if(globalConfigSettings.dataComPortSettings.useDTR)
                        modemSerialPort->setDataTerminalReady(true);//set DTR high
                    g_IsWritingData = false;
                    emit signalUpdateStatusDisplay("Idle...");
                    g_IsWritingData = false;
                    emit signalUpdateDebugLog("Un-Locking writeModemMutex");
                    return;
                }//if exit app
            }//while not RTS high
        }//if use RTS
        
        if(globalConfigSettings.dataComPortSettings.useDTR){
            emit signalUpdateDebugLog("Setting DTR high");
            modemSerialPort->setDataTerminalReady(true);
            while (!isDTR()) {
                emit signalUpdateDebugLog("waiting for DTR high");
                QApplication::processEvents();
                QThread::msleep(10);
                if(!globalConfigSettings.dataComPortSettings.useDTR)
                    break;
                //check for application exit - flush buffer and reset DTR and RTS
                if(g_ExitApplication){
                    modemSerialPort->flush();//flush the serial buffer
                    if(globalConfigSettings.dataComPortSettings.useRTSforPTT)
                        modemSerialPort->setRequestToSend(false);//set RTS low
                    if(globalConfigSettings.dataComPortSettings.useDTR)
                        modemSerialPort->setDataTerminalReady(true);//set DTR high
                    g_IsWritingData = false;
                    emit signalUpdateStatusDisplay("Idle...");
                    g_IsWritingData = false;
                    emit signalUpdateDebugLog("Un-Locking writeModemMutex");
                    return;
                }//if exit app
            }//while DTR is low
        }//if use DTR then set high

    }//else buffer data to modem
    
    if(globalConfigSettings.dataComPortSettings.currentDevice != "MS-DMT TCP")
        QTimer::singleShot(calculateDelayBetweenMessages(dataByteArray.size())*1000, [=] {
            // must have CONFIG += c++11 in .pro file
            emit signalUpdateStatusDisplay("Idle...");
            g_IsTransmitting = false;
            g_IsRadioBusy = false;
        });

    emit signalUpdateDebugLog("9. Final check before leaving function...");
    emit signalUpdateDebugLog("Un-Locking writeModemMutex");
}//writeDataModem

bool MainWindow::writeDataTCPSocket(QByteArray dataByteArray, QString IPAddress, QString TCPPort)
{
    if(clientSocket->state() != QAbstractSocket::ConnectedState){
        clientSocket->connectToHost(QHostAddress(IPAddress),
                                    TCPPort.toUShort());
    }
    if(clientSocket->waitForConnected()){
        ui->pushButtonModem->setEnabled(clientSocket->isOpen());
        
        if(globalConfigSettings.clientSettings.useUuencode){
            emit signalUpdateModemLog(dataByteArray.toBase64(), false, true);
            emit signalUpdateStatusDisplay("Transmitting data...");
            clientSocket->write(dataByteArray.toBase64()+"==");
        }
        else {
            emit signalUpdateModemLog(dataByteArray, false, true);
            emit signalUpdateStatusDisplay("Transmitting data...");
            clientSocket->write(dataByteArray);
        }
        clientSocket->flush();
        return true;
    }
    else {
        //else error
        ui->labelModemConnectionStatus->setText("TCP Socket error!");
        emit signalUpdateCriticalNotifications(tr(Q_FUNC_INFO)+"TCP Socket error: " + clientSocket->errorString());
        emit signalUpdateStatusDisplay("Idle...");
        return false;
    }//else error
}//writeDataTCPSocket

bool MainWindow::writeMSDMTDataPort(QByteArray dataByteArray)
{
    //    QString funcName = Q_FUNC_INFO;
    //    emit signalUpdateDebugLog("Function Name: " + funcName);

    if(MSDMTDataSocket->isOpen()){
#ifdef PREPEND_FLUSH_BYTES
        for (int i=0; i<NUM_FLUSH_BYTES ; i++ ) {
            dataByteArray.prepend(QByteArray::fromHex(FLUSH_BYTES));
        }
#endif
#ifdef APPEND_FLUSH_BYTES
        for (int i=0; i<NUM_FLUSH_BYTES ; i++ ) {
            dataByteArray.append(QByteArray::fromHex(FLUSH_BYTES));
        }
#endif
        ui->pushButtonModem->setEnabled(MSDMTDataSocket->isOpen());
        //        emit signalUpdateStatusDisplay("Transmitting data...");
        MSDMTDataSocket->write(dataByteArray);
        MSDMTDataSocket->flush();
        return true;
    }//if connected
    else {
        //else error
        emit signalUpdateCriticalNotifications(tr(Q_FUNC_INFO)+"TCP Socket error: " +
                                               MSDMTDataSocket->errorString() +
                                               " - Is MS-DMT Alpha D running and configured for IP?");
        emit signalUpdateStatusDisplay("Idle...");
        return false;
    }//else error
}//writeMSDMTDataPort

bool MainWindow::writeMSDMTCMDPort(QByteArray dataByteArray)
{
    //    QString funcName = Q_FUNC_INFO;
    //    emit signalUpdateDebugLog("Function Name: " + funcName + " - " + dataByteArray);

    if(MSDMTCMDSocket->isOpen()){
        MSDMTCMDSocket->write(dataByteArray);
        MSDMTCMDSocket->flush();
        emit signalUpdateModemStatus(dataByteArray);
        return true;
    }//if connected
    else {
        //else error
        emit signalUpdateCriticalNotifications(tr(Q_FUNC_INFO)+"TCP Socket error: " + MSDMTCMDSocket->errorString());
        emit signalUpdateStatusDisplay("Idle...");
        return false;
    }//else error
}//writeMSDMTCMDPort

void MainWindow::logSerialSignals()
{
    if(!modemSerialPort->isOpen())
        return;
    emit signalUpdateDebugLog("RTS: " + QString::number(static_cast<int>((modemSerialPort->pinoutSignals() & QSerialPort::RequestToSendSignal))));
    emit signalUpdateDebugLog("CTS: " + QString::number(static_cast<int>((modemSerialPort->pinoutSignals() & QSerialPort::ClearToSendSignal))));
    emit signalUpdateDebugLog("DTR: " + QString::number(static_cast<int>((modemSerialPort->pinoutSignals() & QSerialPort::DataTerminalReadySignal))));
    emit signalUpdateDebugLog("DSR: " + QString::number(static_cast<int>((modemSerialPort->pinoutSignals() & QSerialPort::DataSetReadySignal))));
    emit signalUpdateDebugLog("DCD: " + QString::number(static_cast<int>((modemSerialPort->pinoutSignals() & QSerialPort::DataCarrierDetectSignal))));
    emit signalUpdateDebugLog("*******************************************");
}//logSerialSignals

bool MainWindow::isDSR(){
    return modemSerialPort->pinoutSignals() & QSerialPort::DataSetReadySignal;
}

bool MainWindow::isRTS(){
    return modemSerialPort->pinoutSignals() & QSerialPort::RequestToSendSignal;
}//isRTS

bool MainWindow::isCTS(){
    return modemSerialPort->pinoutSignals() & QSerialPort::ClearToSendSignal;
}//isCTS

bool MainWindow::isDTR(){
    return modemSerialPort->pinoutSignals() & QSerialPort::DataTerminalReadySignal;
}//isDTR

bool MainWindow::isDCD(){
    return modemSerialPort->pinoutSignals() & QSerialPort::DataCarrierDetectSignal;
}//isDCD

void MainWindow::writeControlRadio(QByteArray dataByteArray)
{
    if(isReconfiguring)
        return;
    if(dataByteArray.size() == 0)
        return;
    if(aleSocket->state() == QAbstractSocket::UnconnectedState){
        connectALEClient();
    }

    emit signalUpdateDebugLog("Locking writeRadioMutex "+dataByteArray);
    QMutexLocker mutexLocker(&writeRadioMutex);
    emit signalUpdateDebugLog("Locked writeRadioMutex "+dataByteArray);

    g_IsRadioBusy = true;

    aleSocket->write(dataByteArray+"\r\n");//needs "\r\n" for radio to accept command
    aleSocket->flush();
    emit signalUpdateALELog(">"+dataByteArray.trimmed());
    g_IsRadioBusy = false;
    emit signalUpdateDebugLog("Un-Locking writeRadioMutex");
}//writeControlRadio

void MainWindow::startTCPServer()
{
    QHostAddress v3TCPServerListenAddress, rawTCPServerListenAddress;

    //MSC  TCP Server
    if(MSCTCPServer->isListening()){
        globalConfigSettings.generalSettings.positionIdentifier = g_MyOldPositionID; //say bye with the old posID
        slotBroadcastID("bye");
        MSCTCPServer->close();

        //if we have a configured address set the listen address to that
        if(!globalConfigSettings.ipSettings.listenIPAddress.isEmpty())
            v3TCPServerListenAddress.setAddress(globalConfigSettings.ipSettings.listenIPAddress);
        if(v3TCPServerListenAddress.isNull()){
            //        qDebug() << listenAddress.toString();
            v3TCPServerListenAddress.setAddress(QHostAddress::Any);
        }
        
        ushort v3ListenPort = globalConfigSettings.ipSettings.listenTCPPort.toUShort();
        while (!MSCTCPServer->listen(v3TCPServerListenAddress,v3ListenPort)) {
            v3ListenPort++;
            if(v3ListenPort > (globalConfigSettings.ipSettings.listenTCPPort.toInt()+globalConfigSettings.ipSettings.maxPorts)){
                break;
            }//if we've tried more than 20 ports from default
        }//while port numbers are busy
        if(MSCTCPServer->isListening())
        {
            globalConfigSettings.ipSettings.listenTCPPort = QString::number(v3ListenPort);
            emit signalUpdateStatusBar();

            //introduce ourselves
            globalConfigSettings.generalSettings.positionIdentifier = g_MyNewPositionID;//update with the new posID
            QRandomGenerator random(static_cast<uint>(QTime::currentTime().msec()));
            //            qsrand(static_cast<uint>(QTime::currentTime().msec()));
            //random time between 10ms and 2000ms
            QTimer::singleShot(random.generate() % ((2000 + 1) - 10) + 10, [=] {
                // must have CONFIG += c++11 in .pro file
                //start the server
                slotBroadcastID("update");
            });
        }//if server is listening
        else {
            ui->labelServerStatus->setText("Server error");
            emit signalUpdateCriticalNotifications("Server error - "+MSCTCPServer->errorString() +
                                                   " - " +globalConfigSettings.ipSettings.listenIPAddress +
                                                   " : " +globalConfigSettings.ipSettings.listenTCPPort);
            //            emit signalUpdateDebugLog("status_label - Server error - "+MSCTCPServer->errorString() +
            //                                      " - " +configSettings.ipSettings.listenIPAddress +
            //                                      " : " +configSettings.ipSettings.listenTCPPort);
        }//else server error
    }//if we've been listening
    else {
        //if we have a configured address set the listen address to that
        if(!globalConfigSettings.ipSettings.listenIPAddress.isEmpty()){
            v3TCPServerListenAddress.setAddress(globalConfigSettings.ipSettings.listenIPAddress);
        }
        if(v3TCPServerListenAddress.isNull()){
            v3TCPServerListenAddress.setAddress(QHostAddress::Any);
        }
        
        ushort v3ListenPort = globalConfigSettings.ipSettings.listenTCPPort.toUShort();
        while (!MSCTCPServer->listen(v3TCPServerListenAddress,v3ListenPort)) {
            v3ListenPort++;
            if(v3ListenPort > (globalConfigSettings.ipSettings.listenTCPPort.toInt()+globalConfigSettings.ipSettings.maxPorts)){
                break;
            }//if we've tried more than 20 ports from default
        }//while port numbers are busy
        if(MSCTCPServer->isListening())
        {
            globalConfigSettings.ipSettings.listenTCPPort = QString::number(v3ListenPort);
            emit signalUpdateStatusBar();

            //introduce ourselves
            slotBroadcastID("helo");
        }//if server is listening
        else {
            ui->labelServerStatus->setText("Server error");
            emit signalUpdateCriticalNotifications("Server error - "+MSCTCPServer->errorString() +
                                                   " - " +globalConfigSettings.ipSettings.listenIPAddress +
                                                   " : " +globalConfigSettings.ipSettings.listenTCPPort);
            //            emit signalUpdateDebugLog("status_label - Server error - "+MSCTCPServer->errorString() +
            //                                      " - " +configSettings.ipSettings.listenIPAddress +
            //                                      " : " +configSettings.ipSettings.listenTCPPort);
        }//else server error
    }//else starting tcpserver for first time


    //Raw TCP Server
    if(rawTCPServer->isListening()){
        rawTCPServer->close();

        //if we have a configured address set the listen address to that
        if(!globalConfigSettings.ipSettings.rawListenIPAddress.isEmpty())
            rawTCPServerListenAddress.setAddress(globalConfigSettings.ipSettings.rawListenIPAddress);
        if(rawTCPServerListenAddress.isNull()){
            //        qDebug() << listenAddress.toString();
            rawTCPServerListenAddress.setAddress(QHostAddress::Any);
        }
        
        ushort rawListenPort = globalConfigSettings.ipSettings.rawListenTCPPort.toUShort();
        while (!rawTCPServer->listen(rawTCPServerListenAddress,rawListenPort)) {
            rawListenPort++;
            if(rawListenPort > (globalConfigSettings.ipSettings.rawListenTCPPort.toInt()+globalConfigSettings.ipSettings.maxPorts)){
                break;
            }//if we've tried more than 20 ports from default
        }//while port numbers are busy
        if(rawTCPServer->isListening())
        {
            globalConfigSettings.ipSettings.rawListenTCPPort = QString::number(rawListenPort);
        }//if server is listening
    }//if we've been listening
    else {
        //if we have a configured address set the listen address to that
        if(!globalConfigSettings.ipSettings.rawListenIPAddress.isEmpty()){
            rawTCPServerListenAddress.setAddress(globalConfigSettings.ipSettings.rawListenIPAddress);
        }
        if(rawTCPServerListenAddress.isNull()){
            rawTCPServerListenAddress.setAddress(QHostAddress::Any);
        }
        
        ushort rawListenPort = globalConfigSettings.ipSettings.rawListenTCPPort.toUShort();
        while (!rawTCPServer->listen(rawTCPServerListenAddress,rawListenPort)) {
            rawListenPort++;
            if(rawListenPort > (globalConfigSettings.ipSettings.rawListenTCPPort.toInt()+globalConfigSettings.ipSettings.maxPorts)){
                break;
            }//if we've tried more than 20 ports from default
        }//while port numbers are busy
        if(rawTCPServer->isListening())
        {
            globalConfigSettings.ipSettings.rawListenTCPPort = QString::number(rawListenPort);
        }//if server is listening
    }//else starting tcpserver for first time
}//startTCPServer

void MainWindow::connectTCPClient()
{
    if(modemSerialPort->isOpen()){
        modemSerialPort->close();
    }
    if(g_ApplicationStart){
        QTimer::singleShot(750, [=] {
            // must have CONFIG += c++11 in .pro file
            if(clientSocket->state() != QAbstractSocket::ConnectedState){
                clientSocket->connectToHost(QHostAddress(globalConfigSettings.clientSettings.IPAddress),
                                            globalConfigSettings.clientSettings.TCPPort.toUShort());
            }//connect to host
            if(clientSocket->waitForConnected()){
                ui->pushButtonModem->setEnabled(clientSocket->isOpen());
                ui->labelModemConnectionStatus->setText("TCP Socket: Connected - "+clientSocket->peerAddress().toString()+":"+QString::number(clientSocket->peerPort()));
                //                emit signalUpdateDebugLog("status_label - "+tr(Q_FUNC_INFO)+"TCP Socket Connected - " + clientSocket->errorString());
            }
            else {
                //else error
                ui->labelModemConnectionStatus->setText("TCP Socket error!");
                emit signalUpdateCriticalNotifications(tr(Q_FUNC_INFO)+"TCP Socket error: " + clientSocket->errorString());
                //                emit signalUpdateDebugLog("status_label - "+tr(Q_FUNC_INFO)+"TCP Socket error: " + clientSocket->errorString());
            }//else error
        });
    }//if application is starting
    else {
        if(clientSocket->isOpen()){
            clientSocket->close();
        }
        if(clientSocket->state() != QAbstractSocket::ConnectedState){
            clientSocket->connectToHost(QHostAddress(globalConfigSettings.clientSettings.IPAddress),
                                        globalConfigSettings.clientSettings.TCPPort.toUShort());
        }//connect to host
        if(clientSocket->waitForConnected()){
            ui->pushButtonModem->setEnabled(clientSocket->isOpen());
            ui->labelModemConnectionStatus->setText("TCP Socket: Connected - "+clientSocket->peerAddress().toString()+":"+QString::number(clientSocket->peerPort()));
            //            emit signalUpdateDebugLog("status_label - TCP Socket: Connected - "+clientSocket->peerAddress().toString()+":"+QString::number(clientSocket->peerPort()));
        }
        else {
            //else error
            emit signalUpdateCriticalNotifications(tr(Q_FUNC_INFO)+"TCP Socket error: " + clientSocket->errorString());
            //            emit signalUpdateDebugLog("status_label - "+tr(Q_FUNC_INFO)+"TCP Socket error: " + clientSocket->errorString());
        }//else error
    }//else application has been running
}//connectTCPClient

void MainWindow::connectMSDMTData(QString positionID, QString ipAddress, QString dataPort, bool autoConnect)
{
    if(autoConnect && globalConfigSettings.MSDMTclientSettings.positionID != positionID)
        return;

    if(modemSerialPort->isOpen()){
        modemSerialPort->close();
    }
    if(g_ApplicationStart){
        QTimer::singleShot(750, [=] {
            // must have CONFIG += c++11 in .pro file
            if(!MSDMTDataSocket->isOpen()){
                MSDMTDataSocket->connectToHost(QHostAddress(ipAddress),
                                               dataPort.toUShort());
            }//connect to host
            if(MSDMTDataSocket->waitForConnected()){
                ui->pushButtonModem->setEnabled(MSDMTDataSocket->isOpen());
                ui->labelModemConnectionStatus->setText("DMT Modem: "+globalConfigSettings.MSDMTclientSettings.positionID+" - "+
                                                        MSDMTDataSocket->peerAddress().toString()+":"+QString::number(MSDMTDataSocket->peerPort()));
                //                emit signalUpdateDebugLog("status_label - DMT Modem: "+positionID+" - "+
                //                                          ipAddress+":"+dataPort);
            }
            else {
                //else error
                emit signalUpdateCriticalNotifications(tr(Q_FUNC_INFO)+"TCP Socket error: " + MSDMTDataSocket->errorString());
                //                emit signalUpdateDebugLog(tr(Q_FUNC_INFO)+"TCP Socket error: " + MSDMTDataSocket->errorString());
            }//else error
        });
    }//if application is starting
    else {
        //first check that if we're already connected to the address:port then do nothing
        if(MSDMTDataSocket->isOpen()){
            //            qDebug() << MSDMTDataSocket->peerAddress().toString() << MSDMTDataSocket->peerPort();
            if(MSDMTDataSocket->peerAddress().toString() == QHostAddress(ipAddress).toString() &&
                MSDMTDataSocket->peerPort() == dataPort.toUShort()){
                ui->pushButtonModem->setEnabled(MSDMTDataSocket->isOpen());
                ui->labelModemConnectionStatus->setText("DMT Modem: "+globalConfigSettings.MSDMTclientSettings.positionID+" - "+
                                                        MSDMTDataSocket->peerAddress().toString()+":"+QString::number(MSDMTDataSocket->peerPort()));
                //                emit signalUpdateDebugLog("status_label - DMT Modem: "+positionID+" - "+
                //                                          ipAddress+":"+dataPort);
                return;
            }
        }
        if(MSDMTDataSocket->isOpen()){
            MSDMTDataSocket->close();
        }
        if(!MSDMTDataSocket->isOpen()){
            MSDMTDataSocket->connectToHost(QHostAddress(ipAddress),
                                           dataPort.toUShort());
        }//connect to host
        if(MSDMTDataSocket->waitForConnected()){
            ui->pushButtonModem->setEnabled(MSDMTDataSocket->isOpen());
            ui->labelModemConnectionStatus->setText("DMT Modem: "+globalConfigSettings.MSDMTclientSettings.positionID+" - "+
                                                    MSDMTDataSocket->peerAddress().toString()+":"+QString::number(MSDMTDataSocket->peerPort()));
            //            emit signalUpdateDebugLog("status_label - DMT Modem: "+positionID+" - "+
            //                                      ipAddress+":"+dataPort);
        }
        else {
            //else error
            emit signalUpdateCriticalNotifications(tr(Q_FUNC_INFO)+"TCP Socket error: " + MSDMTDataSocket->errorString());
            emit signalUpdateDebugLog(tr(Q_FUNC_INFO)+"TCP Socket error: " + MSDMTDataSocket->errorString());
        }//else error
    }//else application has been running
}//connectMSDMTData

void MainWindow::connectMSDMTCMD(QString positionID, QString ipAddress, QString cmdPort, bool autoConnect)
{
    if(autoConnect && globalConfigSettings.MSDMTclientSettings.positionID != positionID)
        return;

    if(modemSerialPort->isOpen()){
        modemSerialPort->close();
    }
    if(g_ApplicationStart){
        QTimer::singleShot(750, [=] {
            // must have CONFIG += c++11 in .pro file
            if(!MSDMTCMDSocket->isOpen()){
                MSDMTCMDSocket->connectToHost(QHostAddress(ipAddress),
                                              cmdPort.toUShort());
            }//connect to host
            if(MSDMTCMDSocket->waitForConnected()){
                ui->pushButtonModem->setEnabled(MSDMTCMDSocket->isOpen());
            }
            else {
                //else error
                emit signalUpdateCriticalNotifications(tr(Q_FUNC_INFO)+"TCP Socket error: " + MSDMTCMDSocket->errorString());
            }//else error
        });
    }//if application is starting
    else {
        if(MSDMTCMDSocket->isOpen()){
            //            qDebug() << MSDMTCMDSocket->peerAddress().toString() << MSDMTCMDSocket->peerPort();
            if(MSDMTCMDSocket->peerAddress().toString() == QHostAddress(ipAddress).toString() &&
                MSDMTCMDSocket->peerPort() == cmdPort.toUShort()){
                return;
            }
        }
        if(MSDMTCMDSocket->isOpen()){
            MSDMTCMDSocket->close();
        }
        if(!MSDMTCMDSocket->isOpen()){
            MSDMTCMDSocket->connectToHost(QHostAddress(ipAddress),
                                          cmdPort.toUShort());
        }//connect to host
        if(MSDMTCMDSocket->waitForConnected()){
            ui->pushButtonModem->setEnabled(MSDMTCMDSocket->isOpen());
            //            QTimer::singleShot(750, [=] {
            //                //                    qDebug() << Q_FUNC_INFO << "CMD:DATA RATE:?";
            //                //                    writeCommandModem("CMD:DATA RATE:?");
            //                //                    emit signalStartModemPingTimer();
            //                //                    emit signalStartModemResponseTimer();
            //            });
        }
        else {
            //else error
            emit signalUpdateCriticalNotifications(tr(Q_FUNC_INFO)+"TCP Socket error: " + MSDMTCMDSocket->errorString());
        }//else error
    }//else application has been running
}//connectMSDMTCMD

void MainWindow::connectALEClient()
{
    ui->groupBoxALE->hide();
    ui->pushButtonALE->setEnabled(false);
    if(g_ApplicationStart){
        QTimer::singleShot(750, [=] {
            // must have CONFIG += c++11 in .pro file
            if(!aleSocket->isOpen()){
                aleSocket->connectToHost(QHostAddress(globalConfigSettings.aleSettings.ALEIPAddress),
                                         globalConfigSettings.aleSettings.ALETCPPort.toUShort());
            }//connect to host
            if(aleSocket->waitForConnected()){
                ui->pushButtonALE->setEnabled(aleSocket->isOpen());
                ui->groupBoxALE->show();
                telnetSockerTimer->start();
                while (isALEBusy()) {
                    emit signalUpdateDebugLog("ALE is busy...");
                    QThread::msleep(100);
                    if(g_ExitApplication)
                        return;
                }
                writeControlRadio("CMD CHANNEL");
            }
            else {
                //else error
                emit signalUpdateCriticalNotifications(tr(Q_FUNC_INFO)+"TCP Socket error: " + aleSocket->errorString());
                ui->pushButtonALE->setEnabled(false);
            }//else error
        });
    }//if application is starting
    else {
        if(aleSocket->isOpen()){
            aleSocket->close();
        }
        if(globalConfigSettings.aleSettings.enableALE){
            if(!aleSocket->isOpen()){
                aleSocket->connectToHost(QHostAddress(globalConfigSettings.aleSettings.ALEIPAddress),
                                         globalConfigSettings.aleSettings.ALETCPPort.toUShort());
            }//connect to host
            if(aleSocket->waitForConnected()){
                ui->pushButtonALE->setEnabled(aleSocket->isOpen());
                ui->groupBoxALE->show();
                telnetSockerTimer->start();
                b_isCalling =
                    b_isClearing =
                    b_isLoadingChannels =
                    b_isWaitingLink =
                    b_isLoadingAddresses = 0;

                while (isALEBusy()) {
                    emit signalUpdateDebugLog("ALE is busy...");
                    QThread::msleep(100);
                    if(g_ExitApplication)
                        return;
                }
                writeControlRadio("CMD CHANNEL");
            }
            else {
                //else error
                emit signalUpdateCriticalNotifications(tr(Q_FUNC_INFO)+"TCP Socket error: " + aleSocket->errorString());
                ui->pushButtonALE->setEnabled(false);
            }//else error
        }
    }//else application has been running
}//connectALEClient

void MainWindow::disconnectALEClient()
{
    aleSocket->close();
    ui->groupBoxALE->hide();
    ui->pushButtonALE->setEnabled(false);


}//disconnectALEClient

void MainWindow::writeTcpSockets(QByteArray dataByteArray, QByteArray rawByteArray)
{
    //    qDebug() << "Function Name: " << Q_FUNC_INFO;
    //    QString funcName = Q_FUNC_INFO;
    //    emit signalUpdateDebugLog("Function Name: " + funcName);

    emit signalUpdateDebugLog("Locking writeTcpSocketsMutex");
    //    QApplication::processEvents();
    QMutexLocker mutexLocker(&writeTcpSocketsMutex);
    emit signalUpdateDebugLog("Locked writeTcpSocketsMutex");

    foreach (QTcpSocket *tcpSocket, MSCTCPSocketList) {
        if(!tcpSocket->isValid()){
            MSCTCPSocketList.removeAll(tcpSocket);//remove bad sockets
            tcpSocket->deleteLater();
        }
    }//for each socket

    foreach (QTcpSocket *tcpSocket, MSCTCPSocketList) {
        tcpSocket->write(dataByteArray);
        tcpSocket->flush();
    }//for each tcpSocket connection
    foreach (QTcpSocket *tcpSocket, rawTCPSocketList) {
        //        if(dynamic_cast<MyTcpSocketClass*>(tcpSocket->parent())->getIsRawSocket()){
        if(!globalConfigSettings.ipSettings.rawUseRMICipher){
            tcpSocket->write(rawByteArray);
            tcpSocket->flush();
        }
        else {
            ClassXML xmlClass(dataByteArray);
            foreach (QByteArray message, xmlClass.getMessageText()) {
                tcpSocket->write(message);
                tcpSocket->flush();
            }
        }
    }//for each rawtcpSocket connection

    emit signalUpdateTCPLog(QDateTime::currentDateTimeUtc().toString("ddMMMyy-hh:mm:ss.zzz - ").toLatin1()+"SENT TCP: \r\n"+dataByteArray);
    emit signalUpdateDebugLog("Un-Locking writeTcpSocketsMutex");
}//writeTcpSockets

void MainWindow::writeStreamingUDPSocket(QByteArray dataByteArray)
{
    //    QUdpSocket localUdpSocket;
    //    QByteArray datagram = xmlWrapper("data", dataByteArray, false);
    emit signalUpdateUDPLog(QDateTime::currentDateTimeUtc().toString("ddMMMyy-hh:mm:ss.zzz - ").toLatin1()+"SENT UDP: \r\n"+dataByteArray);
    QHostAddress udpAddress;
    udpAddress.setAddress(globalConfigSettings.ipSettings.listenIPAddress);
    //    streamingUDPSocket->writeDatagram(dataByteArray.data(), dataByteArray.size(),
    //                                 udpAddress, 5001);
    //    qDebug() << dataByteArray;
    streamingUDPSocket->writeDatagram(dataByteArray.data(), udpAddress, 5001);
}//writeStreamingUDPSocket

void MainWindow::updateStatusBar()
{
    ui->labelServerStatus->setText("Server listening - "+
                                   MSCTCPServer->serverAddress().toString()+
                                   " : "+QString::number(MSCTCPServer->serverPort()));
    ui->labelConnectionStatus->setText("Connections: "+QString::number(numberConnections));
    //    emit signalUpdateDebugLog("status_label - Connections: "+QString::number(numberConnections));
    ui->labelMessageQueue->setText("Message Queue: "+QString::number(mainMessageQueue->getMessageCount()));
    ui->labelRetryQueue->setText("Retry Queue: "+QString::number(retryMessageQueue->getMessageCount()));
    //    emit signalUpdateDebugLog("status_label - Retry Queue: "+QString::number(retryMessageQueue->getMessageCount()));
}//updateStatusBar

void MainWindow::writeReceivedFile(QByteArray byteArray)
{
    QString folder;
    QString filename = QDateTime::currentDateTimeUtc().toString("yyyyMMdd-HHmmsszzz");
    filename.append(".xml");
    QTemporaryFile writeFile;
    QTextStream out(&writeFile);

    folder.append(QApplication::applicationDirPath()+"/RECEIVED/");
    QDir rcvdFolder(folder);
    if(!rcvdFolder.exists()){
        if(!rcvdFolder.mkdir(folder)){
            emit signalUpdateDebugLog("Error creating RECEIVED folder");
            return;
        }
    }
    writeFile.open();
    out << byteArray;
    writeFile.close();
    if(!writeFile.copy(folder+filename)){
        emit signalUpdateDebugLog("Error creating received save file"+writeFile.errorString());
    }
}//writeReceivedFile

void MainWindow::writeSendFile(QByteArray byteArray)
{
    //    qDebug() << "Function Name: " << Q_FUNC_INFO;
    //    QString funcName = Q_FUNC_INFO;
    //    emit signalUpdateDebugLog("Function Name: " + funcName);
    QString folder;
    QString filename = QDateTime::currentDateTimeUtc().toString("yyyyMMdd-HHmmsszzz");
    filename.append(".xml");
    QTemporaryFile writeFile;
    QTextStream out(&writeFile);

    folder.append(QApplication::applicationDirPath()+"/SENT/");
    QDir sendFolder(folder);
    if(!sendFolder.exists()){
        if(!sendFolder.mkdir(folder)){
            emit signalUpdateDebugLog("Error creating SENT folder");
        }//if we can't make the folder
    }//if folder doesn't exist
    writeFile.open();
    out << byteArray;
    writeFile.close();
    if(!writeFile.copy(folder+filename)){
        emit signalUpdateDebugLog("Error creating received save file"+writeFile.errorString());
    }//if tempfile didnt copy to real file
}//writeSendFile

void MainWindow::writeRetryFile(QByteArray byteArray)
{
    //    qDebug() << "Function Name: " << Q_FUNC_INFO;
    //    QString funcName = Q_FUNC_INFO;
    //    emit signalUpdateDebugLog("Function Name: " + funcName);
    QString folder;
    QString filename = QDateTime::currentDateTimeUtc().toString("yyyyMMdd-HHmmsszzz");
    filename.append(".xml");
    QTemporaryFile writeFile;
    QTextStream out(&writeFile);

    folder.append(QApplication::applicationDirPath()+"/RETRY/");
    QDir retryFolder(folder);
    if(!retryFolder.exists()){
        if(!retryFolder.mkpath(folder)){
            emit signalUpdateDebugLog("Error creating RETRY folder");
        }//if we can't make the folder
    }//if folder doesn't exist
    writeFile.open();
    out << byteArray;
    writeFile.close();
    if(!writeFile.copy(folder+filename)){
        //        qDebug() << "Error creating received save file"+writeFile.errorString();
        emit signalUpdateDebugLog("Error creating received save file"+writeFile.errorString());
    }//if tempfile didnt copy to real file
}//writeRetryFile

void MainWindow::deleteRetryFile(QByteArray byteArray)
{
    //    qDebug() << "Function Name: " << Q_FUNC_INFO;
    //    QString funcName = Q_FUNC_INFO;
    //    emit signalUpdateDebugLog("Function Name: " + funcName);
    //The whole retry queue process needs work.  Recommend not using it until it is ironed out
    QFile readFile;

    QDir retryFolder(QApplication::applicationDirPath()+"/RETRY/");
    retryFolder.setFilter(QDir::Files);
    retryFolder.setNameFilters(QStringList() << "*.xml" << "*.XML");

    if(retryFolder.exists()){
        if(retryFolder.count() > 0){
            for(int i = 0; i < retryFolder.entryInfoList().size();i++)//for each file in the directory
            {
                readFile.setFileName(retryFolder.entryInfoList().at(i).absoluteFilePath());
                if(readFile.exists()){
                    readFile.open(QFile::ReadOnly);
                    QTextStream in(&readFile);
                    QByteArray readByteArray = in.readAll().toLatin1();
                    readFile.close();
                    if(readByteArray == byteArray){
                        //file match - delete file
                        readFile.remove();
                        break;
                    }
                }//if file exists
                else {
                    continue;
                }
            }//for each file in folder
        }//if there are files in folder
    }//if folder exists
}//deleteRetryFile

void MainWindow::readRetryFile()
{
    //    qDebug() << "Function Name: " << Q_FUNC_INFO;
    //    QString funcName = Q_FUNC_INFO;
    //    emit signalUpdateDebugLog("Function Name: " + funcName);
    //The whole retry queue process needs work.  Recommend not using it until it is ironed out
    retryMessageQueue->clearMessageQueue();
    emit signalUpdateRetryQueueButton();
    QFile readFile;

    QDir retryFolder(QApplication::applicationDirPath()+"/RETRY/");
    retryFolder.setFilter(QDir::Files);
    retryFolder.setNameFilters(QStringList() << "*.xml" << "*.XML");

    if(retryFolder.exists()){
        if(retryFolder.count() > 0){
            for(int i = 0; i < retryFolder.entryInfoList().size();i++)//for each file in the directory
            {
                readFile.setFileName(retryFolder.entryInfoList().at(i).absoluteFilePath());
                if(readFile.exists()){
                    readFile.open(QFile::ReadOnly);
                    QTextStream in(&readFile);
                    QByteArray readByteArray = in.readAll().toLatin1();
                    readFile.close();
                    ClassMessage *retryMessage = new ClassMessage(readByteArray, this);
                    if(!retryMessageQueue->insertMessage(retryMessage)){
                        emit signalUpdateApplicationLog("ERROR: Trying to add retry message to retry queue!");
                    }//if message wasnt inserted in queue
                    emit signalUpdateRetryQueueButton();
                }//if file exists
                else {
                    continue;
                }
            }//for each file in folder
        }//if there are files in folder
    }//if folder exists
}//readRetryFile

void MainWindow::closeSerialPorts()
{
    if(modemSerialPort->isOpen()){
        modemSerialPort->close();
    }
    ui->pushButtonModem->setEnabled(modemSerialPort->isOpen());
}//closeSerialPorts

void MainWindow::openSerialPorts(){
    if(globalConfigSettings.dataComPortSettings.currentDevice.contains("TCP")){
        if(modemSerialPort->isOpen()){
            modemSerialPort->close();
            ui->labelModemConnectionStatus->setText("Modem: Disconnected!");
        }
        //        emit signalUpdateDebugLog("status_label - Serial Port: Disconnected!");
        return;
    }
    //modem
    modemSerialPort->setPortName(globalConfigSettings.dataComPortSettings.portName);
    modemSerialPort->setBaudRate(globalConfigSettings.dataComPortSettings.baudRate);
    modemSerialPort->setDataBits(globalConfigSettings.dataComPortSettings.dataBits);
    modemSerialPort->setParity(globalConfigSettings.dataComPortSettings.parity);
    modemSerialPort->setStopBits(globalConfigSettings.dataComPortSettings.stopBits);
    modemSerialPort->setFlowControl(globalConfigSettings.dataComPortSettings.flowControl);
    if(modemSerialPort->open(QIODevice::ReadWrite)){
        //set DTR high
        if(globalConfigSettings.dataComPortSettings.useDTR){
            logSerialSignals();
            emit signalUpdateDebugLog("Setting DTR high...");
            modemSerialPort->setDataTerminalReady(true);
            logSerialSignals();
            while(!modemSerialPort->isDataTerminalReady()){
                emit signalUpdateDebugLog("Waiting for DTR high...");
                QApplication::processEvents();
                QThread::msleep(10);
            }//while not DTR
        }
    }//if serial port open
    else
    {
        emit signalUpdateCriticalNotifications(globalConfigSettings.dataComPortSettings.portName+" "+modemSerialPort->errorString());
        emit signalUpdateStatusDisplay(modemSerialPort->errorString());
        ui->labelModemConnectionStatus->setText("Serial Port: Disconnected!");
        //        emit signalUpdateDebugLog("status_label - Serial Port: Disconnected - "+modemSerialPort->errorString());
        //        DSRTestTimer->stop();
        return;
    }//else serial error
    emit signalUpdateStatusDisplay("Idle...");
    ui->pushButtonModem->setEnabled(modemSerialPort->isOpen());
    ui->labelModemConnectionStatus->setText("Serial Port: Connected - "+modemSerialPort->portName());
    //    emit signalUpdateDebugLog("status_label - Serial Port: Connected - "+modemSerialPort->portName());
    return;
}//openSerialPorts

void MainWindow::updateSerialPorts(){
    if(globalConfigSettings.dataComPortSettings.currentDevice.contains("TCP")){
        modemSerialPort->close();
        if(globalConfigSettings.dataComPortSettings.currentDevice == "MS-DMT TCP"){
            if(!MSDMTCMDSocket->isOpen() || !MSDMTDataSocket->isOpen()){
                ui->labelModemConnectionStatus->setText("DMT Modem: Disconnected!");
                //                emit signalUpdateDebugLog("status_label - DMT Modem: Disconnected!");
            }
            else {
                ui->labelModemConnectionStatus->setText("DMT Modem: "+globalConfigSettings.MSDMTclientSettings.positionID+" - "+
                                                        MSDMTDataSocket->peerAddress().toString()+":"+QString::number(MSDMTDataSocket->peerPort()));

                //                ui->labelModemConnectionStatus->setText("DMT Modem: Connected - "+MSDMTDataSocket->peerAddress().toString()+":"+QString::number(MSDMTDataSocket->peerPort()));
                //                emit signalUpdateDebugLog("status_label - DMT Modem: Connected - "+MSDMTDataSocket->peerAddress().toString()+":"+QString::number(MSDMTDataSocket->peerPort()));
            }
        }//if MS-DMT TCP
        else if (globalConfigSettings.dataComPortSettings.currentDevice == "TCPSocket") {
            if(clientSocket->state() != QAbstractSocket::ConnectedState){
                ui->labelModemConnectionStatus->setText("TCP Socket: Disconnected!");
                //                emit signalUpdateDebugLog("status_label - TCP Socket: Disconnected!");
            }
            else {
                ui->labelModemConnectionStatus->setText("TCP Socket: Connected - "+clientSocket->peerAddress().toString()+":"+QString::number(clientSocket->peerPort()));
                //                emit signalUpdateDebugLog("status_label - TCP Socket: Connected - "+clientSocket->peerAddress().toString()+":"+QString::number(clientSocket->peerPort()));
            }
        }//TCPSocket
        return;
    }
    if(g_ModemSerialPortChanged || isFirstTimeRun){
        //modem
        if(modemSerialPort->isOpen()){
            modemSerialPort->close();
        }
        modemSerialPort->setPortName(globalConfigSettings.dataComPortSettings.portName);
        modemSerialPort->setBaudRate(globalConfigSettings.dataComPortSettings.baudRate);
        modemSerialPort->setDataBits(globalConfigSettings.dataComPortSettings.dataBits);
        modemSerialPort->setParity(globalConfigSettings.dataComPortSettings.parity);
        modemSerialPort->setStopBits(globalConfigSettings.dataComPortSettings.stopBits);
        modemSerialPort->setFlowControl(globalConfigSettings.dataComPortSettings.flowControl);
        if(modemSerialPort->open(QIODevice::ReadWrite)){
            //set DTR high
            if(globalConfigSettings.dataComPortSettings.useDTR){
                logSerialSignals();
                emit signalUpdateDebugLog("Setting DTR high...");
                modemSerialPort->setDataTerminalReady(true);
                while(!modemSerialPort->isDataTerminalReady()){
                    emit signalUpdateDebugLog("Waiting for DTR high...");
                    //                    QApplication::processEvents();
                    QThread::msleep(10);
                }//while not DTR
            }
            g_ModemSerialPortChanged = false;
        }//if serial port open
        else
        {
            emit signalUpdateCriticalNotifications(globalConfigSettings.dataComPortSettings.portName+" "+modemSerialPort->errorString());
            emit signalUpdateStatusDisplay(modemSerialPort->errorString());
            ui->labelModemConnectionStatus->setText("Serial Port: Disconnected!");
            //            emit signalUpdateDebugLog("status_label - Serial Port: Disconnected - "+modemSerialPort->errorString());
            g_ModemSerialPortChanged = false;
            return;
        }//else serial error
    }
    emit signalUpdateStatusDisplay("Idle...");
    ui->pushButtonModem->setEnabled( modemSerialPort->isOpen() );
    ui->labelModemConnectionStatus->setText("Serial Port: Connected - "+modemSerialPort->portName());
    //    emit signalUpdateDebugLog("status_label - Serial Port: Connected - "+modemSerialPort->portName());
    isFirstTimeRun = false;
    return;
}//updateSerialPorts

void MainWindow::on_pushButtonSettings_clicked()
{
    static bool OK=false;

    settingsDialog->updateLocalSettings();

    if(!OK){
        if(globalConfigSettings.generalSettings.protectConfig){

            //get passphrase
            QByteArray passphrase = QInputDialog::getText(this, tr("Configuration Passphrase"),
                                                          tr("Enter passphrase:"), QLineEdit::Normal,
                                                          "",&OK).toLatin1();//get passphrase
            if(OK){
                QByteArray hashCheck = QCryptographicHash::hash(passphrase,QCryptographicHash::Sha256);

                //check passphrase
                if(hashCheck == globalConfigSettings.generalSettings.hashCheck){
                    settingsDialog->show();
                    settingsDialog->raise();
                }
            }//if OK
        }//if protect config
        else {
            settingsDialog->show();
            settingsDialog->raise();
        }
    }
    else {
        settingsDialog->show();
        settingsDialog->raise();
    }
}//on_pushButtonSettings_clicked

void MainWindow::on_pushButtonTCP_clicked()
{
    dialogTCP->show();
    dialogTCP->raise();
}//on_pushButtonTCP_clicked

void MainWindow::on_pushButtonUDP_clicked()
{
    dialogUDP->show();
    dialogUDP->raise();
}//on_pushButtonUDP_clicked

bool MainWindow::isRMIBusy()
{
    //    qDebug() << g_IsRadioBusy << g_IsTransmitting << g_IsWritingData;
    //    QApplication::processEvents();
    //    qDebug() << "g_IsRadioBusy" << g_IsRadioBusy
    //             << "g_IsTransmitting" << g_IsTransmitting
    //             << "g_IsReceiving" << g_IsReceiving
    //             << "g_IsWritingData" << g_IsWritingData;
    emit signalUpdateDebugLog("g_IsRadioBusy-" + QString::number(g_IsRadioBusy)
                              + "  g_IsTransmitting-" + QString::number(g_IsTransmitting)
                              + "  g_IsReceiving-" + QString::number(g_IsReceiving)
                              + "  g_IsWritingData-" + QString::number(g_IsWritingData));
    return g_IsRadioBusy || g_IsTransmitting || g_IsReceiving || g_IsWritingData;
    //    return false;
}//isRMIBusy

bool MainWindow::isALEBusy()
{
    emit signalUpdateDebugLog("isCalling-" + QString::number(b_isCalling)
                              + "isClearing-" + QString::number(b_isClearing)
                              + "isLoadingChannels-" + QString::number(b_isLoadingChannels)
                              + "isLoadingAddresses-" + QString::number(b_isLoadingAddresses)
                              + "isWaitingLink-" + QString::number(b_isWaitingLink));
    //    qDebug() << "isCalling" << isCalling
    //             << "isClearing" << isClearing
    //             << "isLoadingChannels" << isLoadingChannels
    //             << "isLoadingAddresses" << isLoadingAddresses
    //             << "isWaitingLink" << isWaitingLink;

    return b_isCalling || b_isClearing || b_isLoadingChannels || b_isWaitingLink ||
           b_isLoadingAddresses;
    //    return false;
}//isALEBusy

void MainWindow::slotUpdateProgressBar(int receivedDataSize, int totalDataSize)
{
    //    qDebug() << Q_FUNC_INFO;
    QMutexLocker mutexLocker(&progressBarMutex);
    ui->progressBarStatus->setMinimum(0);
    ui->progressBarStatus->setMaximum(totalDataSize);
    ui->progressBarStatus->setValue(receivedDataSize);
    //    QApplication::processEvents(QEventLoop::ExcludeSocketNotifiers);
}//slotUpdateProgressBar

void MainWindow::slotResetProgressBar()
{
    QMutexLocker mutexLocker(&progressBarMutex);
    ui->progressBarStatus->setValue(0);
}//slotResetProgressBar

void MainWindow::on_pushButtonLog_clicked()
{
    dialogLog->show();
    dialogLog->raise();
}//on_pushButtonLog_clicked

void MainWindow::on_pushButtonModem_clicked()
{
    dialogModem->show();
    dialogModem->raise();
}//on_pushButtonModem_clicked

void MainWindow::on_comboBoxSelectModemBaud_currentTextChanged(const QString &arg1)
{
    if(isReconfiguring)
        return;
    if(g_ApplicationStart)
        return;
    if(ignoreChange)
        return;
    
    globalConfigSettings.generalSettings.modemSpeed = arg1;
    
    if (globalConfigSettings.dataComPortSettings.currentDevice == "MS-DMT Serial") {
        if(arg1 == "4800"){
            writeCommandModem("<<CMD:DATA RATE>>4800U<>");
        }
        //        else if (arg1 == "75") {
        //            //            writeCommandModem("CMD:DATA RATE:75L");
        //            ui->comboBoxSelectModemInterleave->setCurrentText("L");
        //            on_comboBoxSelectModemInterleave_currentTextChanged("L");
        //        }
        else {
            writeCommandModem("<<CMD:DATA RATE>>"+arg1.toLatin1()+
                              ui->comboBoxSelectModemInterleave->currentText().toLatin1()+"<>");
        }
    }//if serial
    else if (globalConfigSettings.dataComPortSettings.currentDevice == "MS-DMT TCP") {
        if(arg1 == "4800"){
            isReconfiguring = true;
            ui->comboBoxSelectModemInterleave->clear();
            ui->comboBoxSelectModemInterleave->addItem("U");
            writeCommandModem("CMD:DATA RATE:4800U");
            isReconfiguring = false;
        }
        else if (arg1 == "75") {
            isReconfiguring = true;
            ui->comboBoxSelectModemInterleave->clear();
            ui->comboBoxSelectModemInterleave->addItem("L");
            writeCommandModem("CMD:DATA RATE:75L");
            isReconfiguring = false;
        }
        else {
            isReconfiguring = true;
            QString lastInterleave = ui->comboBoxSelectModemInterleave->currentText();
            ui->comboBoxSelectModemInterleave->clear();
            ui->comboBoxSelectModemInterleave->addItems({"L","S"});
            ui->comboBoxSelectModemInterleave->setCurrentText(lastInterleave);
            writeCommandModem("CMD:DATA RATE:"+arg1.toLatin1()+
                              ui->comboBoxSelectModemInterleave->currentText().toLatin1());
            isReconfiguring = false;
        }
        g_IsWaitingForModemResponse = true;
    }//else if tcp
    ui->comboBoxConfiguredModemSpeed->setCurrentText(arg1);
    sendStatusChange();
    emit signalUpdateChatDialogConfig(xmlWrapper("status","",true));
}//on_comboBoxSelectModemBaud_currentTextChanged

void MainWindow::on_comboBoxSelectModemInterleave_currentTextChanged(const QString &arg1)
{
    if(isReconfiguring)
        return;
    if(g_ApplicationStart)
        return;
    if(ignoreChange)
        return;
    
    globalConfigSettings.generalSettings.modemInterleave = arg1;
    
    if (globalConfigSettings.dataComPortSettings.currentDevice == "MS-DMT Serial") {
        if(ui->comboBoxSelectModemBaud->currentText() == "4800"){
            writeCommandModem("<<CMD:DATA RATE>>4800U");
        }
        else {
            writeCommandModem("<<CMD:DATA RATE>>"+ui->comboBoxSelectModemBaud->currentText().toLatin1()+
                              arg1.toLatin1()+"<>");
        }
    }//if serial
    else if (globalConfigSettings.dataComPortSettings.currentDevice == "MS-DMT TCP") {
        if(ui->comboBoxSelectModemBaud->currentText() == "4800"){
            isReconfiguring = true;
            ui->comboBoxSelectModemInterleave->clear();
            ui->comboBoxSelectModemInterleave->addItem("U");
            writeCommandModem("CMD:DATA RATE:4800U");
            isReconfiguring = false;
        }
        else if (ui->comboBoxSelectModemBaud->currentText() == "75") {
            isReconfiguring = true;
            ui->comboBoxSelectModemInterleave->clear();
            ui->comboBoxSelectModemInterleave->addItem("L");
            writeCommandModem("CMD:DATA RATE:75L");
            isReconfiguring = false;
        }
        else {
            isReconfiguring = true;
            QString lastInterleave = ui->comboBoxSelectModemInterleave->currentText();
            ui->comboBoxSelectModemInterleave->clear();
            ui->comboBoxSelectModemInterleave->addItems({"L","S"});
            ui->comboBoxSelectModemInterleave->setCurrentText(lastInterleave);
            writeCommandModem("CMD:DATA RATE:"+ui->comboBoxSelectModemBaud->currentText().toLatin1()+
                              arg1.toLatin1());
            isReconfiguring = false;
        }
        g_IsWaitingForModemResponse = true;
    }//else tcp
    ui->comboBoxConfiguredModemInterleave->setCurrentText(ui->comboBoxSelectModemInterleave->currentText());
    sendStatusChange();
    emit signalUpdateChatDialogConfig(xmlWrapper("status","",true));
}//on_comboBoxSelectModemInterleave_currentTextChanged

void MainWindow::on_comboBoxSelectModemMode_currentTextChanged(const QString &arg1)
{
    Q_UNUSED(arg1)
    if(isReconfiguring)
        return;
    if(g_ApplicationStart)
        return;
    if(ignoreChange)
        return;
    
    if (globalConfigSettings.dataComPortSettings.currentDevice == "MS-DMT Serial") {
        //MS-DMT only has one mode at the moment
        //add commands to change modes if that ever changes
    }
    sendStatusChange();
    emit signalUpdateChatDialogConfig(xmlWrapper("status","",true));
}//on_comboBoxSelectModemMode_currentTextChanged

void MainWindow::on_comboBoxConfiguredModemSpeed_currentTextChanged(const QString &arg1)
{
    globalConfigSettings.dataComPortSettings.configuredModemSpeed = arg1;
    writeSettings();
}//on_comboBoxConfiguredModemSpeed_currentTextChanged

void MainWindow::on_comboBoxConfiguredModemInterleave_currentTextChanged(const QString &arg1)
{
    globalConfigSettings.dataComPortSettings.configuredModemInterleave = arg1;
    writeSettings();
}//on_comboBoxConfiguredModemInterleave_currentTextChanged


void MainWindow::slotTXRetryQueue()
{
    if(isRMIBusy() || isALEBusy()){
        return;//if busy return
    }
    else
    {
        while (!retryMessageQueue->isEmpty()) {
            //take message from retry queue and add it to main queue
            emit signalUpdateDebugLog("Moving message from retry queue to main queue");
            mainMessageQueue->insertMessage(retryMessageQueue->getNextMessage());
            emit signalUpdateStatusBar();

        }//while we have retry messages... add them to main message queue
        //        timerCheckMainMessageQueue->start();
        emit signalUpdateRetryQueueButton();
    }//else move messages from retry queue to main queue
}//on_pushButtonTXRetryQueue_clicked

void MainWindow::on_pushButtonExit_clicked()
{
    if(globalConfigSettings.generalSettings.confirmApplicationExit){
        QMessageBox msgBox(this);
        msgBox.setText("Closing  Application!");
        msgBox.setInformativeText("Are you sure you want to close the application?");
        msgBox.setStandardButtons(QMessageBox::Yes | QMessageBox::No);
        msgBox.setDefaultButton(QMessageBox::No);
        int result = msgBox.exec();

        switch (result) {
        case QMessageBox::No:
            return;
            break;
        default:
            break;
        }
    }//if confirm exit
    this->close();
}//on_pushButtonExit_clicked

void MainWindow::on_checkBoxEMCON_clicked(bool checked)
{
    globalConfigSettings.dataComPortSettings.EMCON = checked;
}//on_checkBoxEMCON_clicked

void MainWindow::on_pushButtonRetryQueue_clicked()
{
    DialogRetryQueue *retryQueueDialog = new DialogRetryQueue(this);

    connect(retryQueueDialog, &DialogRetryQueue::signalUpdateLog,
            this, &MainWindow::signalUpdateApplicationLog);
    connect(retryQueueDialog, &DialogRetryQueue::signalUpdateStatusBar,
            this, &MainWindow::signalUpdateStatusBar);
    connect(retryQueueDialog, &DialogRetryQueue::signalReadRetryQueue,
            this, &MainWindow::readRetryFile);
    connect(retryQueueDialog, &DialogRetryQueue::signalTXRetryQueue,
            this, &MainWindow::slotTXRetryQueue);
    retryQueueDialog->exec();
}//on_pushButtonRetryQueue_clicked

#ifdef CHECKAGE
bool MainWindow::isTooOld()
{
    QFile thisApplication(QApplication::applicationFilePath());

    QFileInfo thisApplicationFileInfo(thisApplication);
    QString version(VERSION);

    if(version.contains("alpha")){
        if(thisApplicationFileInfo.lastModified().addDays(ALPHA_TIME) < QDateTime::currentDateTime()){
            displayIsTooOld(ALPHA_TIME, thisApplicationFileInfo.lastModified().toString("yyyyMMMdd-hh:mm:ss:zzz"));
            return true;
        }
        else {
            return false;
        }
    }//if alpha
    else if(version.contains("beta")){
        if(thisApplicationFileInfo.lastModified().addDays(BETA_TIME) < QDateTime::currentDateTime()){
            displayIsTooOld(BETA_TIME, thisApplicationFileInfo.lastModified().toString("yyyyMMMdd-hh:mm:ss:zzz"));
            return true;
        }
        else {
            return false;
        }
    }//if beta
    return false;
}//isTooOld

void MainWindow::displayIsTooOld(int days, QString fileDate)
{
    QMessageBox messageBox(this);
    messageBox.setWindowTitle("Application Expired!");
    messageBox.setText("This application is older than "+QString::number(days)+" days - "+fileDate+" and has expired!\n\n"
                                                                                                           "Application will now exit.");
    messageBox.exec();
}//displayIsTooOld
#endif

void MainWindow::on_pushButtonALE_clicked()
{
    dialogALE->show();
    dialogALE->raise();
}//on_pushButtonALE_clicked

void MainWindow::on_pushButtonALECAll_clicked()
{
    QByteArray address = ui->comboBoxSelectALEAddress->currentText().toLatin1();
    callThisAddress = address;
    QByteArray channel  = ui->comboBoxSelectChannel->currentText().toLatin1();
//    if(channel.startsWith('0'))
//        channel.remove(0,1);

    int callType = 0;
    if(ui->radioButtonIndividual->isChecked())
        callType = 1;
    else if (ui->radioButtonLQA->isChecked()) {
        callType = 2;
    }
    else if (ui->radioButtonNet->isChecked()) {
        callType = 3;
    }
    else if (ui->radioButtonAll->isChecked()) {
        callType = 4;
    }

    switch (callType) {
    case 1://IND CALL
        if(address.isEmpty()){
            return;
        }
        b_isCalling = true;
        writeControlRadio("CMD CALL "+address+" "+channel+" 0");
        break;
    case 2://LQA CALL
        b_isCalling = true;
        writeControlRadio("CMD CALL "+address+" "+channel+" 0");
        break;
    case 3://NET CALL
        b_isCalling = true;
        writeControlRadio("CMD CALL "+address+" "+channel+" 0");
        break;
    case 4://ALL CALL
        if(channel.isEmpty()){
            QMessageBox msgBox;
            msgBox.setIcon(QMessageBox::Warning);
            msgBox.setText("Please select a channel to perform ALL CALL");
            msgBox.exec();
            return;
        }
        b_isCalling = true;
        callThisAddress = "ALL";
        writeControlRadio("CMD GLOBAL ALLCALL "+channel+" 0");
        break;
    default:
        break;
    }
}//on_pushButtonALECAll_clicked

void MainWindow::on_pushButtonALEDisconnect_clicked()
{
    emit signalUpdateALEStatus("Dropping link...");
    b_isClearing = true;
    //    qApp->processEvents();
    if(b_isLinked){
        if(linkedAddress.isEmpty()){
            writeControlRadio("CMD CLEAR");
            b_isLinked = false;
        }
        else {
            writeControlRadio("CMD CLEAR "+linkedAddress.toLatin1());
            b_isLinked = false;
        }
    }//if isLinked
    else if (b_isCalling) {
        writeControlRadio("CMD KILL");
        b_linkKilled = true;
    }
    else {
        writeControlRadio("CMD CLEAR");
        b_linkKilled = true;
    }
}//on_pushButtonALEDisconnect_clicked

void MainWindow::on_pushButtonALEScan_clicked()
{
    if(b_isLinked){
        writeControlRadio("CMD CLEAR "+linkedAddress.toLatin1());
        b_isLinked = false;
        //        emit signalUpdateALELog("CMD CLEAR "+linkedAddress.toLatin1());
        //        qApp->processEvents(QEventLoop::ExcludeSocketNotifiers);
        //        qApp->processEvents();
        QThread::msleep(5000);
    }
    emit signalUpdateALEStatus("Syncing...");
    writeControlRadio("CMD START");
    //    emit signalUpdateALELog("CMD START");
}//on_pushButtonALEScan_clicked

void MainWindow::on_pushButtonALEStop_clicked()
{
    writeControlRadio("CMD STOP");
    //    emit signalUpdateALELog("CMD STOP");
    emit signalUpdateALEStatus("Idle...");
    ui->FrequencyDisplay->setText("--------");
    writeControlRadio("CMD STATUS");
    emit signalUpdateALEStatus("Syncing...");
    //    QApplication::processEvents(QEventLoop::ExcludeSocketNotifiers);
    //    writeControlRadio("CMD FFFF");
}//on_pushButtonALEStop_clicked

void MainWindow::on_lineEditSelectFrequency_returnPressed()
{
    writeControlRadio("CMD SET FREQ "+ui->lineEditSelectFrequency->text().simplified().toLatin1());
    //    emit signalUpdateALELog("CMD SET FREQ "+ui->lineEditSelectFrequency->text().simplified().toLatin1());
    emit signalUpdateALEStatus("Idle...");
}//on_lineEditSelectFrequency_returnPressed

void MainWindow::on_pushButtonSync_clicked()
{
    writeControlRadio("CMD CHANNEL");
    b_isLoadingChannels = true;
}

void MainWindow::slotUpdateRetryQueueButton()
{
    ui->pushButtonRetryQueue->setEnabled(!retryMessageQueue->isEmpty());
    ui->pushButtonDumpQueue->setEnabled(!mainMessageQueue->isEmpty());
}//on_pushButtonSync_clicked

void MainWindow::on_pushButtonDumpQueue_clicked()
{
    //    if(!mainMessageQueue->isEmpty()){
    dumpMessageQueue = true;
    //    on_pushButtonRetryQueue_clicked();
    //    }
}//on_pushButtonDumpQueue_clicked

void MainWindow::on_pushButtonResetRMI_clicked()
{
    b_isResetting = true;
    b_isCalling=false;
    b_isClearing=false;
    b_isWaitingLink=false;
    b_isLoadingAddresses=false;
    b_isLoadingChannels=false;
    g_IsRadioBusy=false;
    g_IsTransmitting=false;
    g_IsReceiving=false;
    g_IsWritingData=false;
    g_IsWaitingForModemResponse=false;
    g_HaveReceivedModemResponse=false;
    emit signalUpdateStatusDisplay("Idle...");
    emit signalResetProgressBar();

    writeCommandModem("CMD:RESET MDM");

    g_IsWaitingForModemResponse = true;
    g_HaveReceivedModemResponse = false;
    int counter = 0;
    while (g_IsWaitingForModemResponse && !g_HaveReceivedModemResponse) {
        emit signalUpdateDebugLog("Waiting for modem response...");
        QApplication::processEvents();
        QThread::msleep(100);
        if(counter++ > 10){
            emit signalUpdateDebugLog("Timeout - waiting for modem response...");
            break;
        }
    }//while
    g_IsWaitingForModemResponse = false;
    g_HaveReceivedModemResponse = false;
    slotHandleModemReceivedlData();
    b_isResetting = false;

}//on_pushButtonResetRMI_clicked

void MainWindow::slotUpdateCriticalNotifications(QString data)
{
    emit signalUpdateCriticalNotifications(data);
}//slotUpdateCriticalNotifications

void MainWindow::slotConnectToDMT(QString dmtConnection)
{
    QStringList dataConnectionsValues = DMTDataConnections.value(dmtConnection).split(':');
    QStringList cmdConnectionsValues = DMTCMDConnections.value(dmtConnection).split(':');
    if(cmdConnectionsValues.count() == 2 &&
        dataConnectionsValues.count() == 2){
        connectMSDMTCMD(dmtConnection,
                        cmdConnectionsValues.at(0),
                        cmdConnectionsValues.at(1), false);
        connectMSDMTData(dmtConnection,
                         dataConnectionsValues.at(0),
                         dataConnectionsValues.at(1), false);
    }
}//slotConnectToDMT

void MainWindow::slotDisconnectFromDMT()
{
    MSDMTCMDSocket->close();
    MSDMTDataSocket->close();
}//slotDisconnectFromDMT

void MainWindow::slotSendChatMessage(QByteArray dataByteArray)
{
    if(dataByteArray.isEmpty()){
        return;
    }
#ifdef ENHANCED_DEBUG
    emit signalUpdateDebugLog(Q_FUNC_INFO);
#endif
    handleTcpData(dataByteArray, nullptr);
}//slotSendChatMessage

//double MainWindow::calcAVGSNR(QMap<QDateTime, QList<double> > lastReceiveSignalReportMap)
//{
//    double AVGScore = 0.0, totalScore = 0.0;
//    QList <QList<double> > scoreList = lastReceiveSignalReportMap.values();
//    foreach (QList<double> scores, scoreList) {
//        totalScore += scores.at(2);
//    }
//    if(scoreList.count() > 0)
//        AVGScore = totalScore / scoreList.count();
//    //    qDebug() << "Avg SNR: " << AVGScore;

//    return AVGScore;
//}//calcAVGSNR

//double MainWindow::calcAVGBER(QMap<QDateTime, QList<double> > lastReceiveSignalReportMap)
//{
//    double AVGScore = 0.0, totalScore = 0.0;
//    QList <QList<double> > scoreList = lastReceiveSignalReportMap.values();
//    foreach (QList<double> scores, scoreList) {
//        totalScore += scores.at(0);
//    }
//    if(scoreList.count() > 0)
//        AVGScore = totalScore / scoreList.count();
//    //    qDebug() << "Avg BER: " << AVGScore;

//    return AVGScore;
//}//calcAVGBER

//double MainWindow::calcAVGFER(QMap<QDateTime, QList<double> > lastReceiveSignalReportMap)
//{
//    double AVGScore = 0.0, totalScore = 0.0;
//    QList <QList<double> > scoreList = lastReceiveSignalReportMap.values();
//    foreach (QList<double> scores, scoreList) {
//        totalScore += scores.at(1);
//    }
//    if(scoreList.count() > 0)
//        AVGScore = totalScore / scoreList.count();
//    //    qDebug() << "Avg FER: " << AVGScore;

//    return AVGScore;
//}//calcAVGFER

void MainWindow::on_pushButtonChat_clicked()
{
    dialogChat->show();
    dialogChat->raise();
}//on_pushButtonChat_clicked


void MainWindow::on_radioButtonAll_clicked(bool checked)
{
    ui->comboBoxSelectALEAddress->setEnabled(!checked);
    ui->comboBoxSelectChannel->setEnabled(checked);
}


void MainWindow::on_radioButtonIndividual_clicked(bool checked)
{
    ui->comboBoxSelectChannel->setCurrentText("");
    ui->comboBoxSelectALEAddress->setEnabled(checked);
    ui->comboBoxSelectChannel->setEnabled(!checked);
}

