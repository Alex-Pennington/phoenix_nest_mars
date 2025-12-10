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
#ifndef DIALOGCHAT_H
#define DIALOGCHAT_H

#include "globals.h"
#include "classxml.h"
#include "classsignmessage.h"

// QTextToSpeech removed - not used in GPL build


#define TIME_FORMAT "dd-hhmmZ"
#define VOICE_TIME_FORMAT "dd-hhmm.zzzZ"
#define MSGID_TIME_FORMAT "ddhhmmsszzz"

enum MSG_STATUS {
    STATUS_NONE,
    STATUS_ACKD,
    STATUS_NACK,
    STATUS_PNDG,
    STATUS_RTNG,
};

/**
 * @brief The MessageStructure class
 */
struct MessageStructure{
    /**
     * @brief dataByteArray - message text
     */
    QByteArray messageText;
    /**
     * @brief destinationStation - who the message is for
     */
    QByteArray destinationStation;
    /**
     * @brief fromStation - who the message is from
     */
    QByteArray fromStation;
    /**
     * @brief timeStamp - when the message was created
     */
    QDateTime timeStamp;
    /**
     * @brief messageID - unique message ID
     */
    QByteArray messageID;
    /**
     * @brief ackTimer - timer that calls resend message
     */
    QTimer *ackTimer = nullptr;
    /**
     * @brief isOutgoing - out going message
     */
    bool isOutgoing = true;
    /**
     * @brief wasEncrypted - is the message to be (outgoing) or was it encrypted (incomming)
     */
    bool wasEncrypted = false;
    /**
     * @brief checksumPass - did message pass checksum
     */
    QString checksumPass = "UNKN";
    /**
     * @brief cipherKey - key to be usd to encrypt message
     */
    QString cipherKey;
    /**
     * @brief msgStatus - holds PNDG, ACKN, NACK, RTNG
     */
    MSG_STATUS msgStatus;
    /**
     * @brief numTries - the number of times the message has been sent
     */
    int numTries = 0;
};

namespace Ui {
class DialogChat;
}

class DialogChat : public QDialog
{
    Q_OBJECT

protected:
    /**
     * @brief moveEvent - saves current dialog position when dialog is moved
     * @param event
     */
    virtual void moveEvent(QMoveEvent *event);
    /**
     * @brief resizeEvent - saves current dialog size when dialog is resized
     * @param event
     */
    virtual void resizeEvent(QResizeEvent *event);
public:
    /**
     * @brief DialogChat - dialog constructor
     * @param parent
     */
    explicit DialogChat(QWidget *parent = nullptr);
    ~DialogChat();
    /**
     * @brief updateQRUParameters - updates machine tab QRU sta
     */
    void updateQRUParameters();

private:
    /**
     * @brief isReconfiguring - used when reconfiguring UI so changes to widgets don't call the slots
     */
    bool isReconfiguring = false;
    /**
     * @brief cannedMessageList - stores list of 3G SMS canned messages
     */
    QStringList cannedMessageList;
    /**
     * @brief ui - this dialog UI
     */
    Ui::DialogChat *ui;
    /**
     * @brief ALEHeardList - list of station CP has heard
     */
    QStringList ALEHeardList;
    /**
     * @brief lastSentMessage - contain the basic message text of teh last sent message before any formatting or timestamps
     */
    QString lastSentMessage;
    /**
     * @brief formatQByteArrayACP201 - format message text to ACP201
     * @param dataArray - message text
     * @param recipient - message recipient
     * @param callsign - out callsign
     * @return returns QSting of message text formatted in ACP201
     */
    /**
     * @brief chatPartner - who we are chatting with in this chat dialog
     */
    QString chatPartner;
    /**
     * @brief message - contains all the parts of the message
     */
    MessageStructure message;
    /**
     * @brief messageList - list of messages currently being tracked
     */
    QList<MessageStructure*> messageList;
    /**
     * @brief timerMessageMap - list of all timers mapped to corresponding message - for faster lookups
     */
    QMap<QTimer*,MessageStructure*> timerMessageMap;//used to find message from timer by lookup rather than looping through list
    /**
     * @brief msgIDMap - list of all message IDs mapped to corresponding message - used to find message by ID rather than by looping through list for faster lookups
     */
    QMap<QString, MessageStructure*> msgIDMap;
    /**
     * @brief callSignMessageMap - list of callsigns mapped to message
     */
    QMap<QString,QMap<QString, MessageStructure*> > callSignMessageMap;

    /**
     * @brief clearMessageList - deletes all messages and timers then clears list
     */
    void clearMessageList();
    /**
     * @brief buildMessageStructure - builds a new message
     * @param data - formatted message text
     * @param destinationStation - who the message is for
     * @return
     */
    MessageStructure* buildMessageStructure(QByteArray data,
                                            QByteArray destinationStation);
    /**
     * @brief buildMessageStructure - build message from received data to add to message list
     * @param receivedData - data as received
     * @return
     */
    MessageStructure* buildMessageStructure(QByteArray receivedData);

    /**
     * @brief buildMessageStructure - build message for routed message
     * @param data - formatted message text
     * @param destinationStation - who the message is for
     * @param routeToStation - station to route message to
     * @return
     */
    MessageStructure *buildMessageStructure(QByteArray data, QByteArray destinationStation, QByteArray routeToStation);

    /**
     * @brief saveMessageListToDisk - write the message list to disk (binary format)
     * @return
     */
    bool saveMessageListToDisk();
    /**
     * @brief readMessageListFromDisk - read messages from disk and store in list
     */
    void readMessageListFromDisk();
    /**
     * @brief operator << - writes message structure to stream
     * @param stream
     * @param message
     * @return
     */
    friend QDataStream &operator<<(QDataStream &stream, const MessageStructure *message);
    /**
     * @brief operator >> - reads message structure from stream
     * @param stream
     * @param message
     * @return
     */
    friend QDataStream &operator>>(QDataStream &stream, MessageStructure *message);
    /**
     * @brief refreshReceivedTextBrowser - redisplay message list in browser
     */
    void refreshReceivedTextBrowser();
    /**
     * @brief removeStaleMessages - remove messages older than set date from list
     */
    void removeStaleMessages();
    /**
     * @brief deleteMessageFromList - remove specific message from list
     * @param message
     */
    void deleteMessageFromList(MessageStructure *message);
    /**
     * @brief deleteMessageTimer - delete soecific timer
     * @param message
     */
    void deleteMessageTimer(MessageStructure *message);
    /**
     * @brief formatOutgoingMessageText - formats and colorizes outgoing message based on ACK status
     * @param message
     * @return
     */
    QString formatOutgoingMessageText(MessageStructure *message);
    /**
     * @brief formatReceivedMessageText - formats and colorizes incoming messages
     * @param message
     * @return
     */
    QString formatReceivedMessageText(MessageStructure *message);
    /**
     * @brief formatEmptyMessageList - format for when message list is empty
     * @return
     */
    QString formatEmptyMessageList();
    /**
     * @brief setupDialog - set up dialog when first constructed
     */
    void setupDialog();
    /**
     * @brief checkIfMessagesForStation - check to see if there are any outgoing messages for station we just heard from
     */
    void checkIfMessagesForStation();

    //image stuff
    QString lastOpenImageLocation, lastOpenFileLocation, lastSaveImageLocation;

    //FILE STUFF
    QDir receivedFileDirectory;
    QString receivedSelectedFileName;
    QFile receivedSelectedFile;
    bool b_IsFirstPacket = false;

    /**
     * @brief clearFileList - delete file packets from list and clear list
     */
    void clearFileList();
    /**
     * @brief initializeFileFolders - create folders if not found
     */
    void initializeFileFolders();

    /**
     * @brief displayErrorMessage - displays QMessageBox with parameters
     * @param title - QMessageBox title string
     * @param error = QMessageBox error message string
     */
    void displayErrorMessage(QString title, QString error);
    /**
     * @brief buildMachineCommand - builds MSC Style machine command
     * @param command - command string
     * @return
     */
    QByteArray buildMachineCommand(QByteArray command);
    /**
     * @brief removeSpaces - removes all space characters from QString
     * @param stringData - string to remove spaces from
     * @return
     */
    QString removeSpaces(QString stringData);
    /**
     * @brief removeSpaces - removes all space characters from QByteArray
     * @param bytearrayData - QByteArray to remove spaces from
     * @return
     */
    QByteArray removeSpaces(QByteArray bytearrayData);
    /**
     * @brief saveQRUToSettings - saves all QRU strings to settings
     */
    void saveQRUToSettings();
    /**
     * @brief updateUIWidgets - updates UI widgets with new data
     */
    void updateUIWidgets();
    /**
     * @brief ifIsOKToSend - checks some parameters to see if it's OK to send message
     * @return
     */
    bool ifIsOKToSend();
    /**
     * @brief updateMachineAddresses - updates comboBox with machine addresses from settings
     */
    void updateMachineAddresses();
    /**
     * @brief mapSQLStatements - maps SQL statements to their respective key
     */
    void mapSQLStatements();
    /**
     * @brief loadDefaultSQLStatements - loads defaut SQL statements
     */
    void loadDefaultSQLStatements();
    /**
     * @brief loadMappedSQLStatementstoEditor - loads the QSL statment text editor with current list of SQL statements
     */
    void loadMappedSQLStatementstoEditor();
    /**
     * @brief loadSQLStatementMapKeys - populates comboBox with SQL Map keys
     * @param SQLQueryText
     */
    void loadSQLStatementMapKeys(QString SQLQueryText);
    /**
     * @brief setStatustoNACK - change message PNDG/RTNG status to NACK status
     * @param message
     */
    void setStatustoNACK(MessageStructure *message);
    /**
     * @brief formatOutNACKMessage - formats msgText depending on message status
     * @param msgText
     * @param message
     */
    void formatOutNACKMessage(QString &msgText, MessageStructure *message);
    /**
     * @brief formatOutACKMessage - formats msgText depending on message status
     * @param msgText
     * @param message
     */
    void formatOutACKMessage(QString &msgText, MessageStructure *message);
    /**
     * @brief formatOutPNDGMessage - formats msgText depending on message status
     * @param msgText
     * @param message
     */
    void formatOutPNDGMessage(QString &msgText, MessageStructure *message);
    /**
     * @brief formatOutRTNGMessage - formats msgText depending on message status
     * @param msgText
     * @param message
     */
    void formatOutRTNGMessage(QString &msgText, MessageStructure *message);
    /**
     * @brief appendNACKMessage - appends NACK warning message to message text
     * @param message
     */
    void appendNACKMessage(MessageStructure *message);
    /**
     * @brief removeNACKMessage - removes NACK warning message from message text
     * @param message
     */
    void removeNACKMessage(MessageStructure *message);
    /**
     * @brief wrapMSCXML - wraps plaintextDate in MSC XML
     * @param plaintextData
     * @param xmlWrapper
     * @param message
     * @param sentMessageText
     */
    void wrapMSCXML(QByteArray plaintextData, ClassXML xmlWrapper, MessageStructure* message, QByteArray &sentMessageText);
    /**
     * @brief updateKeyListComboBox - clears and updates key comboBox with current list of keys (may be blank)
     * @param xmlClass
     */
    void updateKeyListComboBox(ClassXML &xmlClass);
    /**
     * @brief updateCallsignListComboBox - clears and updates callsign comboBox with current list of call signs
     * @param xmlClass
     */
    void updateCallsignListComboBox(ClassXML &xmlClass);
    /**
     * @brief updateModemListComboBox - clears and updates modem speed comboBox with current list of modem speeds
     * @param xmlClass
     */
    void updateModemListComboBox(ClassXML &xmlClass);
    /**
     * @brief wrapChatMessage - wraps message text in "chat" wrapper
     * @param message
     * @return
     */
    QByteArray wrapChatMessage(MessageStructure* message);
    /**
     * @brief wrapACKMessage - wraps message text in "ack" wrapper
     * @param newMessage
     * @return
     */
    QByteArray createACKMessage(MessageStructure *newMessage);
    /**
     * @brief wrapNACKMessage - wraps message text in "nack" wrapper
     * @param newMessage
     * @return
     */
    QByteArray createNACKMessage(MessageStructure *newMessage);
    /**
     * @brief wrapMachineCommand - wraps machine command in MSC XML wrapper
     * @param machineCommandArray
     * @return
     */
    QByteArray wrapMachineCommand(QByteArray machineCommandArray);
    /**
     * @brief removeExistingWidgetsFromSQLWidgetFrame - when changing SQL statments from combobox, removes existing widgets from SQLFrame area
     */
    void removeExistingWidgetsFromSQLWidgetFrame();
    /**
     * @brief removeLeadingCharsandBracket - utility to aid in parsing fieldNames from SQL statement
     * @param SQLStatement
     */
    void removeLeadingCharsandBracket(QString &SQLStatement);
    /**
     * @brief appendFieldNamestoList - after parsing fieldNames append them to list
     * @param SQLStatement
     * @return
     */
    QStringList appendFieldNamestoList(QString SQLStatement);
    /**
     * @brief addWidgetsToSQLWidgetFrame - create QLabel and QLineEdit widges from fieldNames in fieldNameList
     * @param fieldNameList
     */
    void addWidgetsToSQLWidgetFrame(QStringList fieldNameList);
    /**
     * @brief appendMessagesToTextBrowser - appends each message in messageList to text browser
     */
    void appendMessagesToTextBrowser();
    /**
     * @brief createNewAckTimer - creates a new ackTimer for message
     * @param message
     */
    void createNewAckTimer(MessageStructure *message);
    /**
     * @brief sendMachineCommand - sends the machine command
     * @param command
     */
    void sendMachineCommand(QByteArray command);
    /**
     * @brief machineConfigError - checks for machine command config error
     * @return
     */
    bool machineConfigError();
    void processExistingMessage(MessageStructure *newMessage, const ClassXML &xmlClass);

    void processNewMessage(MessageStructure *newMessage, const ClassXML &xmlClass);

    void handleUIAfterChatMessage(QString receivedMessage);

    void addMessageToLists(MessageStructure *newMessage, const ClassXML &xmlClass);

    void ackMessage(MessageStructure *message, const ClassXML &xmlClass);

    void processChatMessage(const QString &receivedMessageText, const ClassXML &xmlClass);
    void processACKMessage(QString receivedMessageText);

    void processNACKMessage(QString receivedMessageText);

    void processMessagesReceivedFromMessagemachine(const QString &receivedMessageText, const ClassXML &xmlClass);

    void parseSQLStatementsFromTextBrowser();

    void updateSQLUIWidgets();

    void handleMachineResponse(QString receivedMessageText);
    void updateDigestCheckLabel(QString receivedMessageText);

    void emitSendMessageSignal(QByteArray messageText);


    bool isTXTFile(QString fileName);

    QString getFileContents(QString fileName);

    QByteArray formatQByteArraylACP201();

private slots:
    void slotResendDatagram();
    void on_pushButtonResendMessage_clicked();
    void on_pushButtonSendMessage_clicked();
    void on_lineEditChat_returnPressed();
    /**
     * @brief on_pushButtonACP201EraseMessages_clicked - clears messages in list after warning
     */
    void on_pushButtonEraseMessages_clicked();
    /**
     * @brief on_comboBoxCurrentKey_currentTextChanged - sets the encryption key to be used
     * @param arg1
     */
    void on_comboBoxCurrentKey_currentTextChanged(const QString &arg1);
    /**
     * @brief on_comboBoxModem_currentTextChanged - sets the modem baud
     * @param arg1
     */
    void on_comboBoxModem_currentTextChanged(const QString &arg1);
    /**
     * @brief on_checkBoxInterleave_clicked - toggles whether short interleave is to be used
     * @param checked
     */
    void on_checkBoxInterleave_clicked(bool checked);
    /**
     * @brief on_spinBoxMessageRetention_valueChanged - sets how many days to retain messages in database
     * @param arg1
     */
    void on_spinBoxMessageRetention_valueChanged(int arg1);
    /**
     * @brief on_spinBoxDelayBetweenTries_valueChanged - sets the delay time between message resends
     * @param arg1
     */
    void on_spinBoxDelayBetweenTries_valueChanged(int arg1);
    /**
     * @brief on_spinBoxNumRetries_valueChanged - number of times to send message if it fails
     * @param arg1
     */
    void on_spinBoxNumRetries_valueChanged(int arg1);
    /**
     * @brief on_splitterChat_splitterMoved - store the current splitter position
     * @param pos
     * @param index
     */
    void on_splitterChat_splitterMoved(int pos, int index);
    /**
     * @brief on_checkBoxRetrieveMessagesLater_clicked - toggle alowance of messages to be retrieved at a later time if the fail initially
     * @param checked
     */
    void on_checkBoxRetrieveMessagesLater_clicked(bool checked);
    /**
     * @brief on_pushButtonDeleteSelectedRecipient_clicked - deletes teh selected station name from the comboBox
     */
    void on_pushButtonDeleteSelectedRecipient_clicked();
    /**
     * @brief on_pushButtonRXFiles_clicked - open received files folder
     */
    void on_pushButtonRXFiles_clicked();
    /**
     * @brief on_pushButtonSelectFile_clicked - select file to send
     */
    void on_pushButtonSelectFile_clicked();
    /**
     * @brief on_pushButtonSendFile_clicked - send the selected file
     */
    void on_pushButtonSendFile_clicked();
    /**
     * @brief on_pushButtonPing_clicked - send the ping machine command
     */
    void on_pushButtonPing_clicked();
    /**
     * @brief on_pushButtonAddMachineAddress_clicked - add machine address to comboBox
     */
    void on_pushButtonAddMachineAddress_clicked();
    /**
     * @brief on_pushButtonRemoveMachineAddress_clicked - remove machine address from comboBox
     */
    void on_pushButtonRemoveMachineAddress_clicked();
    /**
     * @brief on_pushButtonQRU_clicked - send the QRU machine command
     */
    void on_pushButtonQRU_clicked();
    /**
     * @brief on_pushButtonZEE_clicked - send the ZEE machine command
     */
    void on_pushButtonZEE_clicked();
    /**
     * @brief on_pushButtonQSL_clicked - send the QSL machine command
     */
    void on_pushButtonQSL_clicked();
    /**
     * @brief on_pushButtonSQLQuery_clicked - send the selected SQL query statment as a machine command
     */
    void on_pushButtonSQLQuery_clicked();
    /**
     * @brief on_pushButtonResetSQLStatements_clicked - reset the edited sql statments to default
     */
    void on_pushButtonResetSQLStatements_clicked();
    /**
     * @brief on_comboBoxSQLQuery_currentTextChanged - bulds the SQL input widgets for use input
     * @param arg1
     */
    void on_comboBoxSQLQuery_currentTextChanged(const QString &arg1);
    /**
     * @brief on_pushButtonEditSQLStatements_clicked - enables the text browser for editing
     */
    void on_pushButtonEditSQLStatements_clicked();
    /**
     * @brief on_pushButtonSaveSQLStatements_clicked - saves the edited sql statement text and disables the text broswer
     */
    void on_pushButtonSaveSQLStatements_clicked();
    /**
     * @brief on_pushButtonZEE_2_clicked - sends the ZEE command from the SQL tab
     */
    void on_pushButtonZEE_2_clicked();
    /**
     * @brief on_pushButtonQSL_2_clicked - sends the QSL command from teh SQL tab
     */
    void on_pushButtonQSL_2_clicked();
    void on_pushButtonALEConnect_clicked();

    void on_lineEditAddMachineAddress_returnPressed();

    void on_groupBoxModemSettings_clicked(bool checked);

public slots:
    /**
     * @brief slotUpdateConfig - slot called when CP has updated config info
     * @param config
     */
    void slotUpdateConfig(QByteArray config);
    /**
     * @brief slotMessageReceived - slot called when message is received
     * @param message
     */
    void slotMessageReceived(QByteArray message);
signals:
    /**
     * @brief signalSendMessage - emit this signal to send a message
     * @param messageText - formatted message to send
     */
    void signalSendMessage(QByteArray messageText);
    /**
     * @brief signalSendSMSMessage - send 3G SMS message
     * @param message - message text
     * @param sourceAddress - who the message is from
     * @param destinationAddress - who the message is to
     */
    void signalSendSMSMessage(const QByteArray message, const QByteArray sourceAddress, const QByteArray destinationAddress);
    /**
     * @brief signalUpdateRadioCannedMessages - reprogram the radio canned messages
     */
    void signalUpdateRadioCannedMessages();
};

#endif // DIALOGCHAT_H
