#ifndef DLGTERMINAL_H
#define DLGTERMINAL_H

#include <QtCore/QtGlobal>
#include <QMainWindow>
#include <QChar>
#include <QFileSystemWatcher>
#include <QFile>
#include <QDir>
#include <QTcpSocket>
#include <QUdpSocket>
#include <QHostInfo>
#include <QMessageBox>
#include <QLabel>
#include <QDebug>
#include <QTimer>
#include <QDateTime>
#include <QFileDialog>
#include <QDesktopServices>
#include <QMap>
#include <QTimer>
#include <QtMath>
#include <QSettings>
#include <QTemporaryFile>
#include <QXmlStreamWriter>
#include <QNetworkInterface>
#include <QNetworkAddressEntry>
#include <QColorDialog>
#include <QCryptographicHash>
#include <QDirIterator>
#include <QTreeView>
#include <QTreeWidget>

//#include "classspelchecker.h"
#include "highlighter.h"

//#include "globals.h"
//#include "classsignmessage.h"

QT_BEGIN_NAMESPACE

class QLabel;

namespace Ui {
    class dlgterminal;
}

QT_END_NAMESPACE

class dlgterminal : public QMainWindow
{
    Q_OBJECT


public:
    //void sendDataToProcessingChain(QByteArray plaintextData);
    void sendDataToProcessingChain(QByteArray plaintextData, QString cipherKey, bool compress, QString recipient);
    explicit dlgterminal(QWidget *parent = 0);
    ~dlgterminal();

    struct GeneralSettings{
        QStringList callSignList;
        QString positionID;
        QString defaultRMI;
        bool ignoreMessagesNotForMe=false;
        bool ignoreACKMessages=false;
        bool showTXData=false;
        QStringList heardCallSignList;
        bool includeSalutation;
        QStringList machineAddressList;
        QString applicationDiscoveryPort;
        QString keyFile;
//        bool bypassInternetCheck;
        QString styleSheetFilename;
        bool loadStyleSheet;
        bool trackRMIChanges;

        bool hideALE;
        bool hideModem;
        bool hideProcessing;
        bool hideConnections;

        bool hideMachineTab;
        bool hideSettingsTab;
        bool hideXMLTab;
        bool performLineWrap;
        int lineLength;
        QString cmboChatLAN;
    };

    struct conferenceSettings{
        QMap<QString, QVariant> conferenceMap;
        int messageExpireTime;
        int expireCheckTime;
        QMap<QString, QVariant> defaultCipherKey;
    };

    struct conferenceChats{
        QMap<QString, QVariant> ChatText;
    };

    struct individualChats{
        QMap<QString, QVariant> ChatText;
    };

    struct Settings {
        struct GeneralSettings generalSettings;
        struct conferenceSettings conferenceSettings;
        struct conferenceChats conferenceChats;
        struct individualChats individualChats;
    };
    Settings configSettings;
protected:
    void closeEvent(QCloseEvent *event);

private slots:
    //TCP
    void slotReadTcpData();
    void slotHandleTCPData();
    void slotTCPSocketDisconnected();
    //UDP
    void slotSayHelo();
    void slotBroadcastID(QByteArray type);
    void slotReadUdpData();
    void slotHandleUDPData(QByteArray UDPData);
    //UI
    //actions
    void on_actionClear_triggered();
    void on_actionSend_File_triggered();
    void on_actionInstruction_Manual_triggered();
    //void on_actionAbout_triggered();
    void on_actionALE_Config_triggered(bool checked);
    void on_actionConnections_Config_triggered(bool checked);
    void on_actionModem_Config_triggered(bool checked);
    void on_actionProcessing_Config_triggered(bool checked);
    //ui events
    void on_pushButtonSendTx_clicked();
    void on_pushButtonResend_clicked();
    void on_pushButtonClearRawData_clicked();
    void on_pushButtonClearRx_clicked();
    void on_pushButtonConnectTCP_clicked();
    void on_pushButtonSetDefaultRMI_clicked();
    void on_pushButtonConnectALE_clicked();
    void on_pushButtonSetDefaultKey_clicked();
    void pushButtonBroadcastID();
    void on_pushButtonAddMachineAddress_clicked();
    void on_pushButtonRemoveMachineAddress_clicked();
    //void on_pushButtonVER_clicked();
    void on_pushButtonQRU_clicked();
    void on_pushButtonZEE_clicked();
    void on_pushButtonZDK_clicked();
    void on_pushButtonQSL_clicked();
    void on_pushButtonSQLQuery_clicked();
    void on_pushButtonRemoveRecipient_clicked();
    void on_lineEditChatText_returnPressed();
    void on_lineEditAddMachineAddress_returnPressed();
    void on_lineEditSQLQuery_returnPressed();
    void on_lineEditMessageParamaters_returnPressed();
    void lineEditPositionID(const QString &arg1);
    void on_lineEditInstructions_returnPressed();
    void on_tabWidgetMain_currentChanged(int index);
    void on_plainTextEditSendData_textChanged();
    void on_textBrowserReceivedData_textChanged();
    void on_textBrowserReceivedData_selectionChanged();
    void on_comboBoxALEStations_currentIndexChanged(const QString &arg1);
    void on_comboBoxKeyList_currentTextChanged(const QString &arg1);
    void on_comboBoxRecipient_editTextChanged(const QString &arg1);
    void checkBoxIgnoreACKMessages(bool checked);
    void checkBoxIgnoreMessages();
    void on_checkBoxInsertLineBreaks_clicked(bool checked);
    void on_checkBoxDisplayTXData_clicked(bool checked);
    void on_checkBoxIncludeSalutation_clicked(bool checked);
    //void on_radioButtonv3Machine_clicked(bool checked);
    void on_radioButtonv2Machine_clicked(bool checked);
    void on_spinBoxLineLength_valueChanged(int arg1);
    void on_textEdit_ContextMenuRequest(const QPoint &pos);
    void on_SelectSuggestion();
    void on_copy();
    void on_paste();
    void on_cut();
    void on_AddWord();
    void on_treeWidgetConferences_itemClicked(QTreeWidgetItem *item, int column);
    void on_pushButtonConfSendTx_clicked();
    void on_pushButtonAddConference_clicked();
    void on_plainTextEditConfSendData_textChanged();
    void on_textBrowserConfSentData_textChanged();
    void on_lineEditOrderWireText_returnPressed();
    void on_lineEditConfInstructions_returnPressed();
    void on_pushButtonSetDefaultCipherKey_clicked();
    void on_lineEditAddConference_returnPressed();
    void on_pushButtonDeleteConference_clicked();
    void on_pushButtonClearChatHistory_clicked();
    void on_pushButtonConfResend_clicked();
    void on_comboBoxConfKeyList_currentTextChanged(const QString &arg1);
    void on_checkBoxShowTimestamps_clicked();
    void on_pushButtonPing_clicked();
    void on_actionSend_ConfFile_triggered(QString cipherKey, bool compress);
    void BroadcastChat(QByteArray tempData, QString Recipient);

signals:

public slots:
//    void on_checkBoxTrackRMI_clicked(bool checked);

private:
    //conference chat
    QString chattingWith;
    void updatetextBrowserSentData();
    QMap<QString, bool> isNewChat;
    void updateConferenceSidebar();

    //Terminal chat
    bool isApplicationStarting = false;
    QFont myFont;
    QByteArray dataByteArray; //global bytearray of received data
    QTimer handleTCPDataTimer;
    QString appNameString = "StationMapperTerminal";

    //functions
    int truncateLines(QStringList &messageList, int lineLength);
    bool hasLongLines(QStringList &messageList, int lineLength);
    void displayIsTooOld(int days, QString fileDate);
    QString removeSpaces(QString stringData);
    QByteArray removeSpaces(QByteArray byteArrayData);
    QByteArray printables(QByteArray byteArray);
    void showStatusMessage(const QString &message);
    inline void initActionsConnections();
    //UDP
    inline void createUDPListener();
    inline void sayHelo();
    //TCP
    inline void startTCPTimer();
    inline void createTCPSocket();
    //incoming data
    void handleTCPData(QByteArray directDataByteArray);
    //outgoing data
    void sendDataTCPSocket();
    //settings ini
    void writeSettings();
    void readSettings();
    void updateSettings();

    //variables
    //TCP Stuff
    QMap<QString, QString> tcpConnections;
    QTcpSocket *tcpSocket;

    //UDP Stuff
    QUdpSocket *udpListenSocket;

    //settings stuff
    //gui stuff
    Ui::dlgterminal *ui;

    QByteArray lastSentMessage;
    int tcpTimeout;

    QByteArrayList messageQueue;

    void setChecksumLabel(QString checksumString);
    Highlighter *highlighter;
//    classspelchecker Notes_SpellChecker;
    QMap<QAction*, QString> SuggestionMap;
    QTextCursor prev;
    QTextCursor csr;

    void writeRcvdToFile(QString R, QString usedEncryption, QString encryptionKey, QString checkSum, QString sourceStation, QString DT, QString receivedMessage);
protected:
    bool eventFilter(QObject *obj, QEvent *event) override;
};

#endif // DLGTERMINAL_H
