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
#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include "globals.h"

#include "dialogtcpip.h"
#include "dialogudp.h"
#include "dialoglog.h"
#include "dialogale.h"
#include "dialogmodem.h"
#include "dialogsettings.h"
#include "tcpsocket.h"
#include "classmessage.h"
#include "classmessagequeue.h"
#include "classchecksum.h"
#include "dialogprogressbar.h"
#include "dialogchat.h"

//#include <QFuture>
//#include <QTcpSocket>

#define MODEM_SPEED_MULTIPLIER 11.5

namespace Ui {
class MainWindow;
}

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

protected:

private slots:
    void slotSendChatMessage(QByteArray dataByteArray);

    void slotDSRTestTimeout();
    void slotDMTProcessStarted();

    void slotUpdateStylesheet(QString styleSheetString);
    void slotTXRetryQueue();
    void slotCheckMainMessageQueue();
    void slotCheckRetryMessageQueue();
    void slotProcessRadioCommands(QByteArray dataByteArray);
    //MODEM
    void slotReadModemSerialData();
    void slotHandleModemReceivedlData();
    void slotBroadcastID(QByteArray type);

    void slotReadUdpData();
    void slotReadStreamingUDPData();
    void slotUdpUpdate();

    void slotReadMSCTcpSocket();
    void slotReadRawTcpSocket();
    void slotHandleTcpData(QByteArray dataByteArray, QTcpSocket *tcpSocket);
    void slotHandleRawTcpData(QByteArray dataByteArray);
    void slotReadClientSocket();
    void slotReadMSDMTDataSocket();
    void slotReadMSDMTCMDSocket();
    void slotMSDMTCMDSocketDisconnect();
    void slotMSDMTDataSocketDisconnect();
    void slotClientSocketDisconnect();
    //ALE
    void slotReadALESocket();
    void slotHandleALEData();
    void slotALELinkTimeout();
    void slotSendAMDMessage(QByteArray dataByteArray);
    void slotALESocketDisconnected();
    void slotUpdateALEStatus(QString status);

    void slotMSCSocketDisconnected();
    void slotRawSocketDisconnected();
    void slotMakeNewMSCTCPConnection();
    void slotMakeNewRawTCPConnection();
    void slotUpdateStatusDisplay(QString status);
    void slotUpdateMainWindow();
    void slotUpdateProgressBar(int receivedDataSize, int totalDataSize);
    void slotResetProgressBar();
    void slotUpdateStatusBar();

    void on_comboBoxSelectModemBaud_currentTextChanged(const QString &arg1);
    void on_comboBoxSelectModemInterleave_currentTextChanged(const QString &arg1);
    void on_comboBoxSelectModemMode_currentTextChanged(const QString &arg1);
    void on_pushButtonSettings_clicked();
    void on_pushButtonLog_clicked();
    void on_pushButtonModem_clicked();
    void on_pushButtonTCP_clicked();
    void on_comboBoxConfiguredModemSpeed_currentTextChanged(const QString &arg1);
    void on_comboBoxConfiguredModemInterleave_currentTextChanged(const QString &arg1);
    void on_pushButtonUDP_clicked();
    void on_pushButtonExit_clicked();
    void on_checkBoxEMCON_clicked(bool checked);
    void on_pushButtonRetryQueue_clicked();

    void slot_SetModem75L();
    void slot_SetModem150L();
    void slot_SetModem300L();
    void slot_SetModem600L();
    void slot_SetModem1200L();

    void on_pushButtonALE_clicked();
    void on_pushButtonALECAll_clicked();
    void on_pushButtonALEDisconnect_clicked();
    void on_pushButtonALEScan_clicked();
    void on_pushButtonALEStop_clicked();
    void on_lineEditSelectFrequency_returnPressed();
    void on_pushButtonSync_clicked();
    void slotUpdateRetryQueueButton();
    void on_pushButtonDumpQueue_clicked();
    void on_pushButtonResetRMI_clicked();

    void on_pushButtonChat_clicked();

    void on_radioButtonAll_clicked(bool checked);

    void on_radioButtonIndividual_clicked(bool checked);

signals:
    void signalUpdateRetryQueueButton();
    void signalResetProgressBar();
    void signalUpdateProgressBar(int receivedDataSize, int totalDataSize);
    //LOG
    void signalUpdateApplicationLog(QString data);
    void signalUpdateCriticalNotifications(QString data);
    void signalUpdateDebugLog(QString data);
    //ALE
    void signalUpdateALELog(QByteArray data);
    void signalUpdateAMDMessage(QByteArray data);
    void signalUpdateALEAddressSelect(QStringList ALEHeardList);
    void signalUpdateALEHeardList(QStringList ALEHeardList);
    //MODEM
    void signalUpdateModemLog(QByteArray data, bool received, bool hexCode);
    void signalUpdateModemStatus(QByteArray data);
    void signalWriteModemData(QByteArray dataByte);
    //TCP
    void signalUpdateTCPLog(QByteArray data);
    //UDP
    void signalUpdateUDPLog(QByteArray data);
    //Radio
    void signalUpdateRadioCommand(QByteArray data);
    void signalUpdateRadioResponse(QByteArray data);
    void signalUpdateStatusDisplay(QString status);
    void signalUpdateALEStatus(QString status);
    void signalProcessRadioCommands(QByteArray data);
    void signalUpdateStatusBar();
    void signalUpdateDMTConnectionList(QStringList DMTConnections);

    void signalUpdateChatDialogMessage(QByteArray data);
    void signalUpdateChatDialogConfig(QByteArray configData);

public slots:
    void slotUpdateCriticalNotifications(QString data);
    void slotConnectToDMT(QString dmtConnection);
    void slotDisconnectFromDMT();

private:
    Ui::MainWindow *ui;

    QProcess *DMTProcess;

    bool isRMIBusy();
    bool isALEBusy();

    bool isResetModem = false;

    void sendStatusChange();

    //ALE
    QTimer *aleLinkTimeOutTimer;
    QString ALEErrorMessage;
    QMap<QString, QString> channelMap;
    QTimer *telnetSockerTimer;
    QTcpSocket *aleSocket;
    QByteArray ALEDataByteArray;
    void connectALEClient();
    void disconnectALEClient();
    bool b_isResetting = false, b_isScanning = false, b_isLinked = false,
        b_isCalling = false, b_isLoadingChannels = false,
        b_isClearing = false, b_messageInitiatedALECall = false,
        b_linkError = false, b_linkTimeOut = false, b_linkKilled = false,
        b_isWaitingLink = false, b_isLoadingAddresses = false, b_delaySet = false;

    QFuture<void> futureCheckMainMessageQueue;

    QString linkedAddress, callThisAddress;

    void handleTcpData(QByteArray dataByteArray, QTcpSocket *tcpSocket);
    void handleRawTcpData(QByteArray dataByteArray);

    //XML Stuff
    //TODO move xmlWrapper to ClassXML
    QByteArray xmlWrapper(QString type, QByteArray message, bool isHandshake);
    void handleUDPData(QByteArray data);
    void handleStreamingUDPData(QByteArray data);
    bool setupRadio(QByteArray xmlDocument);
    bool setupModem(QByteArray xmlDocument);
    bool setupCipher(QByteArray xmlDocument);
    void setupCompression(QByteArray xmlDocument);

    bool xmlParseRadioParameters(QByteArray xmlDocument);
    QStringList xmlParseModemParameters(QByteArray xmlDocument);
    bool xmlParseCipherParameters(QByteArray xmlDocument);

    bool xmlParseHeader(QByteArray xmlDocument);
    QList<QByteArray> xmlParseMessageList(QByteArray xmlDocument);

    QTimer *timerCheckMainMessageQueue, *timerCheckRetryMessageQueue;
    QMutex mainMessageQueueMutex, retryMessageQueueMutex, mainQueueLoopMutex;
    bool dumpMessageQueue = false;

    QTimer *udpTimer;
    QUdpSocket *udpListenSocket, *streamingUDPSocket;
    int numberConnections = 0;
    QByteArray modemReceivedDataByteArray;
    bool isRadioCommand = false;

    //cipher
    bool wasEncrypted = false;
    bool wasCompressed = false;
    bool foundKey = false;
    bool foundChecksum = false;

    bool wasAuthenticated = false;
    bool authPassed = false;
    QString authAgency;

    QByteArray decryptPlaintextCheck;
    bool isPlaintext(QByteArray byteArray);

    QStringList ALEHeardList;
    QStringList ALEAddressList;

    //MSC XML protocol stuff
    QString positionID, ALEAddress, ALEAddresses, TXsourceStation, TXdestinationStation,
        RXsourceStation, RXdestinationStation, encrypt,
        priority, compress, encryptionkey, decryptionkey, antennaBeamHeading, radioChannel, radioFrequency,
        radioOPMode, radioTXMode, modemBaud, modemInterleave, modemWaveform;

    bool useEncryption = true;
    bool useCompression = false;

    int predictedByteCountSize = 0;
    Bool checksumBool = UNKN;
//    QByteArray printables(QByteArray byteArray);

    void writeSettings();
    void readSettings();
    void updateSettings();

    ClassMessage *currentMessage;
    void checkMainMessageQueue();
    int calculateDelayBetweenMessages(int dataSize);
    int sendMessages(QList<QByteArray> messageList, int messageSerialNumber);
    void writeDataModem(QByteArray dataByteArray);
    bool writeDataTCPSocket(QByteArray dataByteArray, QString IPAddress, QString TCPPort);
    bool writeMSDMTDataPort(QByteArray dataByteArray);
    bool writeMSDMTCMDPort(QByteArray dataByteArray);

    void logSerialSignals();
    QByteArray encryptData(QByteArray dataByteArray);
    QByteArray decryptData(QByteArray dataByteArray);
    bool testDecrypt(QByteArray ciphertext);
    void writeCommandModem(QByteArray modemCommand);
    void writeControlRadio(QByteArray radioCommand);
    void startTCPServer();
    void connectTCPClient();
    void connectMSDMTData(QString positionID, QString ipAddress, QString dataPort, bool autoConnect);
    void connectMSDMTCMD(QString positionID, QString ipAddress, QString cmdPort, bool autoConnect);
    void writeTcpSockets(QByteArray dataByteArray, QByteArray rawByteArray);
    void writeStreamingUDPSocket(QByteArray dataByteArray);
    void updateStatusBar();
    void writeReceivedFile(QByteArray dataByteArray);

    //retry queue stuff
    ClassMessageQueue *mainMessageQueue, *retryMessageQueue;
    void readRetryFile();
    void writeRetryFile(QByteArray byteArray);
    void writeSendFile(QByteArray byteArray);
    void deleteRetryFile(QByteArray byteArray);

    QTimer *modemSerialTimer, *radioSerialTimer;//,*modemResponseTimer;// *modemPingTimer,
    void closeSerialPorts();
    void openSerialPorts();
    void updateSerialPorts();

    QTcpServer *MSCTCPServer, *rawTCPServer;
    QList<QTcpSocket *> MSCTCPSocketList, rawTCPSocketList;
    QMap<QString, QTcpSocket> MSCTCPSocketMap;

    QMap<QString, QString> DMTDataConnections;
    QMap<QString, QString> DMTCMDConnections;
    QTcpSocket *clientSocket, *MSDMTDataSocket, *MSDMTCMDSocket;

    DialogChat *dialogChat;
    DialogTCPIP *dialogTCP;
    DialogUDP *dialogUDP;
    DialogLog *dialogLog;
    DialogAle *dialogALE;
    DialogModem *dialogModem;
    DialogSettings *settingsDialog;

    DialogProgressBar* dialogProgressBar;

    QString activeModem = "1";

    bool isRTS();
    bool isCTS();
    bool isDTR();
    bool isDCD();
    bool isDSR();


    bool isReconfiguring = false;
    bool ignoreChange = false;

    QMutex progressBarMutex;
    QMutex updateStatusDisplayMutex;
    QMutex slotReadRadioSerialDataMutex;
    QMutex writeModemMutex;
    QMutex writeRadioMutex;
    QMutex handleALEDataMutex;
    QMutex slotProcessRadioCommandsMutex;
    QMutex writeCommandModemMutex;
    QMutex writeTcpSocketsMutex;
    QMutex slotReadTCPSocketMutex;
    QMutex slotReadUDPSocketMutex;
    QMutex slotHandleTCPDataMutex;
    QMutex slotHandleUDPDataMutex;
#if defined INTERNETCHECK || defined INTERNETWARNING
    bool checkInternet();
#endif
#ifdef CHECKAGE
    bool isTooOld();
    void displayIsTooOld(int days, QString fileDate);
#endif
    //system tray stuff
    void systemTray();
    QSystemTrayIcon *trayIcon;
    QMenu *trayIconMenu;
    QAction *restoreAction;
    QAction *quitAction;
    void quitApplication();

    QAction * openLogDialog;
    QAction * openALEDialog;
    QAction * openModemDialog;
    QAction * openTCPDialog;
    QAction * openUDPDialog;
    // modem speeds
    QAction* modem75L;
    QAction* modem150L;
    QAction* modem300L;
    QAction* modem600L;
    QAction* modem1200L;

    void purgeOldSentFiles();
    void purgeOldReceivedFiles();
};
#endif // MAINWINDOW_H
