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
#ifndef GLOBALS_H
#define GLOBALS_H

#include "version.h"

#define APP_NAME "Communications Processor"
#define APP_NAME_KEY_LOADER "Key Loader"
#define APP_NAME_ABBV "CP"
#define RELEASEDATE __DATE__
#define BUILDDATE RELEASEDATE " - " __TIME__
//#define AUTHORS "Timothy Millea"
#define qt_LICENSE "LGPLv3"
#define COPYRIGHT "Copyright © 2016-2024 GRSS"
#define APP_LICENSE "LICENSE: \nGreen Radio Software Solutions grants US Army NETCOM, its DoD and Federal affiliates, \
along with US Army MARS Members in good standing, license to use this software for \
official duties related to MARS activities."
#define RIGHTS "All rights reserved"
#define WARRANTY "The program is provided AS IS with NO WARRANTY OF ANY KIND, INCLUDING THE WARRANTY OF DESIGN, MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE."
#define qt_COPYRIGHT "Copyright © The Qt Company Ltd. and other contributors"
#define SYSTEMINFO QSysInfo::prettyProductName()
#define MSGBLOCKSIZE 8
#define BUGREPORTS "Questions and Bug Reports are to be sent through your State/Region S6"

#define USE_FLUSH_PADDING
#define FLUSH_BYTES "00"
//#define NUM_FLUSH_BYTES 3

//#define INTERNETCHECK
//#define INTERNETWARNING
#define CHECKAGE

#define SNR_MIN -3
#define SNR_MAX 12

#define BER_MIN 0
#define BER_MAX 500

#define FER_MIN -10
#define FER_MAX 10

//32/64bit test
#if _WIN32 || _WIN64
#if _WIN64
#define ENV64
#else
#define ENV32
#endif

#elif __GNUC__
#if __x86_64 || __ppc64__
#define ENV64
#else
#define ENV32
#endif
#endif

//#define CIPHER_DEBUG_ON

#include <QDir>
#include <QString>
#include <QStringList>
#include <QDataStream>
#include <QLibrary>
#include <QDebug>
#include <QQueue>
#include <QCloseEvent>
#include <QMainWindow>
#include <QNetworkInterface>
#include <QSerialPort>
#include <QTcpServer>
#include <QUdpSocket>
#include <QTimer>
#include <QDateTime>
#include <QSettings>
#include <QMessageBox>
#include <QThread>
#include <QProcess>
#include <QByteArray>
#include <QTcpServer>
#include <QTcpSocket>
#include <QFile>
#include <QDir>
#include <QUuid>
#include <QStack>
#include <QFileDialog>
#include <QXmlStreamReader>
#include <QXmlStreamWriter>
#include <QCryptographicHash>
#include <QDesktopServices>
#include <QMutex>
#include <QMutexLocker>
#include <QTemporaryFile>
#include <QDataStream>
#include <QHostInfo>
#include <QInputDialog>
#include <QSystemTrayIcon>
#include <QScreen>
#include <QEvent>
#include <QFuture>
#include <QtConcurrent>
#include <QTableView>
#include <QAbstractTableModel>


//#include "../../ClassEvent/classevent.h"

//checksum stuff
enum Bool {
            PASS,
            FAIL,
            ERRO,
            UNKN
};

////event stuff
//enum EventStatus {
//    RUNNING_OK,
//    RUNNING_QUEUED,
//    RUNNING_STALLED,
//    RUNNING_UNK,
//    COMPLETE_OK,
//    COMPLETE_ACK,
//    COMPLETE_ERR,
//    COMPLETE_TIMEOUT,
//};

//enum EventDirection{
//    EVENT_IN,
//    EVENT_OUT
//};


inline QByteArray printables(QByteArray byteArray)
{
    qPrintable("");

    for (int i = 0; i < byteArray.size(); i++) {
        char currentByte = byteArray.at(i);

        if (currentByte == '\r' ||
            currentByte == '\n' ||
            currentByte == '\t')
            continue;

        else if (currentByte < 32 || currentByte > 126)
            byteArray[i] = '.';

    }//foreach char in byteArray
    return byteArray;
}//printables

struct Dialog_ChatSettings
{
    QPoint pos;
    QSize size;
    QByteArray chatSplitterState;
    bool showChatDialog;
    QMap<QString,QVariant> chatMessageListMap;
    int messageRetention;
    int delayBetweenRetries;
    int numRetries;
    bool allowRetrieveMessagesLater = false;
    QString keyName;
    QString modemSpeed;
    bool shortInterleave;
    bool useLocalModemSettings = true;
};

struct Dialog_LogSettings
{
    QPoint pos;
    QSize size;
};

struct Dialog_ModemSettings
{
    QPoint pos;
    QSize size;
    QByteArray splitterState;
    bool displayModemOnRecvError;
};

struct Dialog_TCPSettings
{
    QPoint pos;
    QSize size;
};

struct Dialog_UDPSettings
{
    QPoint pos;
    QSize size;
};

struct Dialog_ALESettings
{
    QPoint pos;
    QSize size;
    QByteArray splitterState;
    QByteArray splitterState2;
};

struct Dialog_QueueSettings
{
    QPoint pos;
    QSize size;
};

struct Dialog_Settings
{
    QPoint pos;
    QSize size;
};

struct DataComPort {
    QString portName;
    qint32 baudRate;
    QString stringBaudRate;
    QSerialPort::DataBits dataBits;
    QString stringDataBits;
    QSerialPort::Parity parity;
    QString stringParity;
    QSerialPort::StopBits stopBits;
    QString stringStopBits;
    QSerialPort::FlowControl flowControl;
    QString stringFlowControl;
    bool useRTSforPTT;
    //    bool useRTSDelay;
    int msDelayBeforeDroppingRTS;
    int msDelayAfterCTS;
    bool useDTR;
    bool waitForCTS;
    bool waitForDCD;
    bool useRadioComPort;
    QString serialTimeout;
    bool compressionIsDefault;
    QString currentDevice;
    int delayBeforeNextTX;
    QString configuredModemSpeed;
    QString configuredModemInterleave;
    bool autoStartSoftwareModem;
    bool resetSoftwareModem;
    int percentRunOn;
    bool EMCON;
    bool showModemDialog;
};

struct ALESettings {
    QString ALEIPAddress;
    QString ALETCPPort;
    QStringList heardCallList;
    QStringList knownALEAddressList;
    bool enableALE;
    bool startScan;
    QString aleLinkTimeOut;
    int recommendedTimeout;
    QString opMode;
};

struct IPSettings {
    //MSC IP Server
    QString listenIPAddress;
    QString listenTCPPort;
    QString applicationDiscoveryPort;
    int maxPorts;
    bool onlyShowIP4;

    QString rawListenIPAddress;
    QString rawListenTCPPort;
    bool rawOnlyShowIP4;
    bool rawUseRMICipher;
    QString rawDefaultCipherKey;
};

struct TCPClientSettings {
    QString IPAddress;
    QString TCPPort;
    bool useUuencode;
};

struct MSDMTClientSettings {
    QString positionID;
    int socketLatency;
    QString DMTFilePath;
    bool useMSDMTFile;
    int sizeFlushBytes;
};

struct General {
    bool useProgressBar;
    bool hideWindowFrame;
    QString positionIdentifier;
    QString callSign;
//    QStringList callSignList;
    bool showDebugInfo;
    int messageSerialNumber;
    bool hideHandShakePackets;
    bool protectConfig;
    QByteArray hashCheck;
    bool useRetryQueue;
    bool autoTXRetryQueue;
    bool useSystemTray;
    bool confirmApplicationExit;
    QString styleSheetFilename;
    bool loadStyleSheet;
    QString modemSpeed;
    QString modemInterleave;
    QStringList machineAddressList;
    QStringList savedQRUParameters;
};

struct CipherSettings {
    bool allowPT;
    bool useExtendedChecksum;
    //useExternalCipherDevice is used in CP Mil but ignored (permenantly set as false) in CP MARS
    bool useExternalCipherDevice; //inline hardware or CT device/radio
};

struct FileManagement {
    bool expireImmediateMessages;
    bool expirePriorityMessages;
    bool expireRoutineMessages;

    int expireImmediateMessagesHours;
    int expirePriorityMessagesHours;
    int expireRoutineMessagesHours;
};

struct MELPSettings{
    QString listenIPAddress;
    int listenTCPPort;
};

struct SQLStatements{
    QMap<QString, QVariant> SQLStatementMap;
};

struct KeyLoader_Settings{
    QPoint pos;
    QSize size;
    QByteArray splitterState;
};

struct Settings {
    struct KeyLoader_Settings keyLoaderSettings;
    struct Dialog_ChatSettings chat_Dialog;
    struct MELPSettings melpSettings;
    struct DataComPort dataComPortSettings;
    struct IPSettings ipSettings;
    struct TCPClientSettings clientSettings;
    struct MSDMTClientSettings MSDMTclientSettings;
    struct General generalSettings;
    struct CipherSettings cipherSettings;
    struct FileManagement fileManagement;
    struct ALESettings aleSettings;
    struct Dialog_Settings settings_Dialog;
    struct Dialog_ALESettings ale_Dialog;
    struct Dialog_LogSettings log_Dialog;
    struct Dialog_TCPSettings tcp_Dialog;
    struct Dialog_UDPSettings udp_Dialog;
    struct Dialog_ModemSettings modem_Dialog;
    struct Dialog_QueueSettings queue_Dialog;
    struct SQLStatements sqlStatements;
};


extern Settings globalConfigSettings;
extern Settings localConfigSettings;

extern bool g_TCPServerChanged;
extern bool g_ExitApplication;
extern bool g_ApplicationStart;
extern bool isFirstTimeRun;

//status between clients and CP
extern bool g_IsTransmitting;
extern bool g_IsReceiving;
extern bool g_IsRadioBusy;
extern bool g_IsWritingData;
extern bool g_IsDigitalVoice;
extern QString g_DVSpeed;
extern bool g_IsWaitingForModemResponse;
extern bool g_HaveReceivedModemResponse;
//extern bool g_IsLinked;

extern bool g_IsCipherInit;

extern QString BUILD_DATE;
extern QString APP_DIR;
extern QString MSC_DIR;
extern QString CIPHER_DIR;
extern QStringList keyNameList;

extern QString g_MyNewPositionID, g_MyOldPositionID;

//extern QSerialPort *radioSerialPort;
extern QSerialPort *modemSerialPort;
extern bool g_RadioSerialPortChanged, g_ModemSerialPortChanged, g_TCPSocketChanged, g_ALEChanged, g_MSDMTTCPChanged;

//extern ClassEvent *currentInEvent, *currentOutEvent;
extern QString g_senderCallsign;

#endif // GLOBALS_H
