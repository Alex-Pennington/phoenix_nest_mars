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
#include "dialogchat.h"
#include "ui_dialogchat.h"
#include "classipchatxml.h"

QDataStream &operator<<(QDataStream &stream, const MessageStructure *message) {
    stream << message->messageText <<
        message->destinationStation <<
        message->fromStation <<
        message->timeStamp <<
        message->messageID <<
        message->isOutgoing <<
        message->wasEncrypted <<
        message->checksumPass <<
        message->cipherKey <<
        message->msgStatus <<
        message->numTries;

    return stream;
}

QDataStream &operator>>(QDataStream &stream, MessageStructure *message) {
    stream >> message->messageText >>
        message->destinationStation >>
        message->fromStation >>
        message->timeStamp >>
        message->messageID >>
        message->isOutgoing >>
        message->wasEncrypted >>
        message->checksumPass >>
        message->cipherKey >>
        message->msgStatus >>
        message->numTries;

    return stream;
}

void DialogChat::moveEvent(QMoveEvent *event)
{
    globalConfigSettings.chat_Dialog.pos = this->pos();
    event->accept();
}

void DialogChat::resizeEvent(QResizeEvent *event)
{
    globalConfigSettings.chat_Dialog.size = this->size();
    event->accept();
}

void DialogChat::setupDialog(){
    ui->labelDigestCheck->hide();
    this->setWindowTitle("Chat - "+globalConfigSettings.generalSettings.positionIdentifier);
    this->move(globalConfigSettings.chat_Dialog.pos);
    this->resize(globalConfigSettings.chat_Dialog.size);
    
    ui->spinBoxMessageRetention->setValue(globalConfigSettings.chat_Dialog.messageRetention);
    ui->spinBoxNumRetries->setValue(globalConfigSettings.chat_Dialog.numRetries);
    ui->spinBoxDelayBetweenTries->setValue(globalConfigSettings.chat_Dialog.delayBetweenRetries);
    ui->checkBoxRetrieveMessagesLater->setChecked(globalConfigSettings.chat_Dialog.allowRetrieveMessagesLater);
    ui->splitterChat->restoreState(globalConfigSettings.chat_Dialog.chatSplitterState);
    ui->comboBoxSavedQRUParameters->addItems(globalConfigSettings.generalSettings.savedQRUParameters);
    ui->comboBoxSelectMachineAddress->addItems(globalConfigSettings.generalSettings.machineAddressList);
    ui->groupBoxModemSettings->setChecked(globalConfigSettings.chat_Dialog.useLocalModemSettings);
}

void DialogChat::loadDefaultSQLStatements()
{
    QString sqlStatementString = ui->plainTextEditSQLStatementMapping->toPlainText();
    if(sqlStatementString.isEmpty())
        return;

    QStringListIterator iterator(sqlStatementString.split("\n",Qt::SkipEmptyParts));
    while (iterator.hasNext()) {
        QString statement = iterator.next();
        int delimiterIndex = statement.indexOf(':') ;
        if (delimiterIndex != -1) {
            QString key = statement.left(delimiterIndex);
            QString value = statement.mid(delimiterIndex + 1).trimmed();
            globalConfigSettings.sqlStatements.SQLStatementMap.insert(key, value);
        }
    }
}

void DialogChat::loadMappedSQLStatementstoEditor()
{
    QMapIterator<QString, QVariant> mapIterator(globalConfigSettings.sqlStatements.SQLStatementMap);
    while (mapIterator.hasNext()) {
        mapIterator.next();
        ui->plainTextEditUserSQLStatementMapping->appendPlainText(mapIterator.key()+":"+
                                                                  mapIterator.value().toString());
    }
}

void DialogChat::loadSQLStatementMapKeys(QString SQLQueryText)
{
    ui->comboBoxSQLQuery->addItems(globalConfigSettings.sqlStatements.SQLStatementMap.keys());
    ui->comboBoxSQLQuery->setCurrentText(SQLQueryText);
}

void DialogChat::mapSQLStatements()
{
    ui->plainTextEditSQLStatementMapping->hide();
    QString SQLQueryText = ui->comboBoxSQLQuery->currentText();
    ui->comboBoxSQLQuery->clear();

    if(globalConfigSettings.sqlStatements.SQLStatementMap.isEmpty())
        loadDefaultSQLStatements();

    loadMappedSQLStatementstoEditor();
    loadSQLStatementMapKeys(SQLQueryText);
}

void DialogChat::updateMachineAddresses()
{
    QString machineAddressText = ui->comboBoxSelectMachineAddress->currentText();
    ui->comboBoxSelectMachineAddress->clear();
    ui->comboBoxSelectMachineAddress->addItems(globalConfigSettings.generalSettings.machineAddressList);
    ui->comboBoxSelectMachineAddress->setCurrentText(machineAddressText);
}

void DialogChat::updateQRUParameters()
{
    QString savedQRUParameters = ui->comboBoxSavedQRUParameters->currentText();
    ui->comboBoxSavedQRUParameters->clear();
    ui->comboBoxSavedQRUParameters->addItems(globalConfigSettings.generalSettings.savedQRUParameters);
    ui->comboBoxSavedQRUParameters->setCurrentText(savedQRUParameters);
}

void DialogChat::updateUIWidgets()
{
    mapSQLStatements();
    updateMachineAddresses();
    updateQRUParameters();
    if(globalConfigSettings.aleSettings.enableALE){
        ui->groupBoxALE->setEnabled(true);
        ui->groupBoxALE->show();
    }
    else {
        ui->groupBoxALE->setEnabled(false);
        ui->groupBoxALE->hide();
    }
}

DialogChat::DialogChat(QWidget *parent) :
    QDialog(parent),
    ui(new Ui::DialogChat)
{
    ui->setupUi(this);
    setupDialog();
    updateUIWidgets();

    readMessageListFromDisk();
    removeStaleMessages();
    refreshReceivedTextBrowser();

    //TODO remove when available
    ui->tabWidgetMain->removeTab(1); //Files tab
    ui->tabWidgetChat->removeTab(2); //2G ALE AMD Messaging tab
}

DialogChat::~DialogChat()
{
    saveMessageListToDisk();
    clearMessageList();
    delete ui;
}

void DialogChat::deleteMessageTimer(MessageStructure *message){
    if(message == nullptr || message->ackTimer == nullptr)
        return;

    message->ackTimer->stop();
    timerMessageMap.remove(message->ackTimer);
    delete message->ackTimer;
    message->ackTimer = nullptr;
}

void DialogChat::deleteMessageFromList(MessageStructure *message){
    if(message == nullptr)
        return;

    deleteMessageTimer(message);
    msgIDMap.remove(message->messageID);
    messageList.removeAll(message);
    delete message;
    message = nullptr;
}

void DialogChat::clearMessageList()
{
    foreach (MessageStructure *message, this->messageList) {
        deleteMessageFromList(message);
    }
    messageList.clear();
}

MessageStructure* DialogChat::buildMessageStructure(QByteArray data, QByteArray destinationStation)
{
    if(data.isEmpty() || destinationStation.isEmpty())
        return nullptr;

    MessageStructure *message = new MessageStructure;
    message->messageText = data;
    message->destinationStation = destinationStation;
    message->msgStatus = STATUS_PNDG;
    message->timeStamp = QDateTime::currentDateTimeUtc();
    message->messageID = globalConfigSettings.generalSettings.callSign.toLatin1()+
                         message->timeStamp.toString("ddhhmmsszzz").toLatin1();
    message->isOutgoing = true;
    if(destinationStation == "ALL"){
        message->ackTimer = nullptr;
        message->msgStatus = STATUS_ACKD;
    }
    else {
        createNewAckTimer(message);
    }
    return message;
}

MessageStructure* DialogChat::buildMessageStructure(QByteArray data, QByteArray destinationStation, QByteArray routeToStation)
{
    Q_UNUSED(routeToStation)
    MessageStructure *message = new MessageStructure;
    message->messageText = data;
    message->fromStation = globalConfigSettings.generalSettings.callSign.toLocal8Bit();
    message->destinationStation = destinationStation;
    message->msgStatus = STATUS_PNDG;
    message->timeStamp = QDateTime::currentDateTimeUtc();
    message->messageID = globalConfigSettings.generalSettings.callSign.toLatin1()+
                         message->timeStamp.toString(MSGID_TIME_FORMAT).toLatin1();
    message->isOutgoing = true;
    message->ackTimer = new QTimer(this);
    return message;
}

MessageStructure *DialogChat::buildMessageStructure(QByteArray receivedData)
{
    return ClassIPChatXML::unwrap(receivedData);
}

void DialogChat::setStatustoNACK(MessageStructure *message)
{
    if(message->msgStatus == STATUS_PNDG ||
        message->msgStatus == STATUS_RTNG)
    {
        message->msgStatus = STATUS_NACK;
    }
}

bool DialogChat::saveMessageListToDisk()
{
    QFile outFile(QApplication::applicationDirPath()+"/"+"201Chats"+".dat");
    QDataStream out(&outFile);
    if(!outFile.open(QIODevice::WriteOnly)){
        qDebug() << Q_FUNC_INFO << "File error - " << outFile.errorString();
        return false;
    }

    foreach (MessageStructure* message, messageList) {
        if(g_ExitApplication){
            setStatustoNACK(message);
        }
        out << message;
    }
    outFile.flush();
    outFile.close();
    return true;
}

void DialogChat::readMessageListFromDisk()
{
    clearMessageList();
    QFile inFile(QApplication::applicationDirPath()+"/"+"201Chats"+".dat");
    QDataStream in(&inFile);
    if(!inFile.open(QIODevice::ReadOnly))
        return;

    while (!inFile.atEnd()) {
        MessageStructure *message = new MessageStructure;
        in >> message;
        message->ackTimer = nullptr;
        messageList.append(message);
        setStatustoNACK(message);
        msgIDMap.insert(message->messageID, message);
    }
    inFile.close();
}

void DialogChat::appendNACKMessage(MessageStructure *message)
{
    if(!message->messageText.endsWith("<br><br>There was no response from the intended recipient. "
                                       "<br>Please make sure the address you are sending to exists and that it is running CP Chat."))
        message->messageText.append("<br><br>There was no response from the intended recipient. "
                                    "<br>Please make sure the address you are sending to exists and that it is running CP Chat.");
}

void DialogChat::formatOutNACKMessage(QString &msgText, MessageStructure *message)
{
    appendNACKMessage(message);
    QString wasEncrypted = (message->wasEncrypted) ? "CT " : "PT ";
    QString color = "<span style=\"color:red\">";
    msgText = color+"Failed "+
              wasEncrypted+
              message->timeStamp.toString("MMM dd hh:mm:ssZ:<br>");
    msgText.append(message->messageText);
    msgText.append("<br>~~<br></span>");
}

void DialogChat::removeNACKMessage(MessageStructure *message)
{
    if(message->messageText.endsWith("<br><br>There was no response from the intended recipient. "
                                      "<br>Please make sure the address you are sending to exists and that it is running CP Chat."))
        message->messageText.remove(message->messageText.indexOf("<br><br>There was no response from the intended recipient. "
                                                                 "<br>Please make sure the address you are sending to exists and that it is running CP Chat."),
                                    QByteArray("<br><br>There was no response from the intended recipient. "
                                               "<br>Please make sure the address you are sending to exists and that it is running CP Chat.").size());
}

void DialogChat::formatOutACKMessage(QString &msgText, MessageStructure *message)
{
    removeNACKMessage(message);
    QString wasEncrypted = (message->wasEncrypted) ? "CT " : "PT ";
    QString color = "<span style=\"color:lawngreen\">";
    msgText = color+"Sent "+
              wasEncrypted+
              " to "+message->destinationStation+
              message->timeStamp.toString(" MMM dd hh:mm:ssZ:<br>");
    msgText.append(QString(message->messageText).toHtmlEscaped());
    msgText.append("<br>~~<br></span>");
}

void DialogChat::formatOutPNDGMessage(QString &msgText, MessageStructure *message)
{
    removeNACKMessage(message);
    QString wasEncrypted = (message->wasEncrypted) ? "CT " : "PT ";
    QString  color = "<span style=\"color:goldenrod\">";
    msgText = color+"Pending "+
              wasEncrypted+
              " to "+message->destinationStation+
              message->timeStamp.toString(" MMM dd hh:mm:ssZ:<br>");
    msgText.append(QString(message->messageText).toHtmlEscaped());
    msgText.append("<br>~~<br></span>");
}

void DialogChat::formatOutRTNGMessage(QString &msgText, MessageStructure *message)
{
    removeNACKMessage(message) ;
    QString wasEncrypted = (message->wasEncrypted) ? "CT " : "PT ";
    QString color = "<span style=\"color:violet\">";
    msgText = color+"Routing "+
              wasEncrypted+
              " to "+message->destinationStation+
              message->timeStamp.toString(" MMM dd hh:mm:ssZ:<br>");
    msgText.append(QString(message->messageText).toHtmlEscaped());
    msgText.append("<br>~~<br></span>");
}

QString DialogChat::formatOutgoingMessageText(MessageStructure *message)
{
    QString displayText;
    if(message->msgStatus == STATUS_NACK){
        formatOutNACKMessage(displayText, message);
    }
    else if(message->msgStatus == STATUS_PNDG){
        formatOutPNDGMessage(displayText, message);
    }
    else if(message->msgStatus == STATUS_ACKD){
        formatOutACKMessage(displayText, message);
    }
    else if(message->msgStatus == STATUS_RTNG){
        formatOutRTNGMessage(displayText, message);
    }

    displayText.replace('\r', "");
    displayText.replace('\n', "<br>");

    return displayText;
}

QString DialogChat::formatReceivedMessageText(MessageStructure *message)
{
    //    QString color = "<span style=\"color:powderblue\">";
    QString color = "<span>";
    QString wasEncrypted = (message->wasEncrypted) ? "CT " : "PT ";
    QString displayText = color+"Rcvd "+
                          wasEncrypted+
                          " "+
                          message->cipherKey+
                          " "+
                          message->checksumPass+
                          " "+
                          "from "+message->fromStation+
                          " "+
                          message->timeStamp.toString(" MMM dd hh:mm:ssZ:<br>");
    QString msgText = QString(message->messageText).toHtmlEscaped();

    while(msgText.endsWith("\r\n"))
        msgText.chop(2);
    while(msgText.endsWith('\n'))
        msgText.chop(1);
    displayText.append(msgText);
    displayText.replace('\r', "");
    displayText.replace('\n', "<br>");
    displayText.append("<br>~~<br></span>");

    return displayText;
}

QString DialogChat::formatEmptyMessageList(){
    //    QString color = "<span style=\"color:white\">";
    QString color = "<span>";
    QString displayText = color+"Message list is empty!";
    displayText.append("<br>~~<br></span>");
    return displayText;
}

void DialogChat::appendMessagesToTextBrowser()
{
    foreach (MessageStructure *message, this->messageList) {

        if(message->isOutgoing)
            ui->textBrowserReceivedChatMessage->append(formatOutgoingMessageText(message));

        else
            ui->textBrowserReceivedChatMessage->append(formatReceivedMessageText(message));
    }
}

void DialogChat::refreshReceivedTextBrowser()
{
    ui->textBrowserReceivedChatMessage->clear();

    if(messageList.isEmpty())
        ui->textBrowserReceivedChatMessage->append(formatEmptyMessageList());

    else
        appendMessagesToTextBrowser();

    ui->pushButtonEraseMessages->setEnabled(!messageList.isEmpty());
}

void DialogChat::removeStaleMessages()
{
    if(globalConfigSettings.chat_Dialog.messageRetention <= 0)
        return;

    foreach (MessageStructure *message, messageList) {

        if(message->timeStamp.addDays(globalConfigSettings.chat_Dialog.messageRetention) < QDateTime::currentDateTimeUtc())
            deleteMessageFromList(message);

    }

    saveMessageListToDisk();
}

QByteArray DialogChat::wrapChatMessage(MessageStructure* message)
{
    ClassXML xmlWrapper;
    QByteArray plaintextData = ClassIPChatXML::wrap("chat", message,0,0,"");
    QByteArray sentMessageText;
    QString encryptKey = message->cipherKey;
    QString encryptMessage = (encryptKey != "PLAINTEXT") ? "1" : "0";
    QString ALEAddress;
    ALEAddress.clear();
    if(globalConfigSettings.aleSettings.enableALE){
        ALEAddress = ui->comboBoxALEAddress->currentText();
    }

    QString modemSpeed, modemInterleave;

    if(ui->groupBoxModemSettings->isChecked()){
        modemSpeed = ui->comboBoxModem->currentText();
        modemInterleave = ui->checkBoxInterleave->isChecked() ? "S" : "L";

    }
    else {
        modemSpeed = globalConfigSettings.generalSettings.modemSpeed;
        modemInterleave = globalConfigSettings.generalSettings.modemInterleave;
    }

    sentMessageText = xmlWrapper.wrap("data",
                                      plaintextData,
                                      false,
                                      globalConfigSettings.generalSettings.positionIdentifier,
                                      ALEAddress,
                                      "1",//compress
                                      encryptMessage,
                                      encryptKey,
                                      globalConfigSettings.generalSettings.callSign,
                                      message->destinationStation,
                                      modemSpeed,
                                      modemInterleave,
                                      A_ROUTINE);

    return sentMessageText;
}

void DialogChat::slotResendDatagram()
{
    QTimer *senderTimer = static_cast<QTimer*>(sender());

    MessageStructure * message;
    if((message = timerMessageMap.value(senderTimer)) == nullptr)
        return;

    if(message->msgStatus == STATUS_ACKD)
        deleteMessageTimer(message);

    else if(message->numTries < globalConfigSettings.chat_Dialog.numRetries){
        message->numTries++;
        emitSendMessageSignal(wrapChatMessage(message));
        message->ackTimer->start(globalConfigSettings.chat_Dialog.delayBetweenRetries*1000*60);
    }

    else {
        deleteMessageTimer(message);
        message->msgStatus = STATUS_NACK;
        appendNACKMessage(message);
        refreshReceivedTextBrowser();
    }
}

void DialogChat::on_pushButtonResendMessage_clicked()
{
    if(!ifIsOKToSend())
        return;

    if(lastSentMessage.isEmpty())
        return;

    QByteArray formattedMsgText = formatQByteArraylACP201();
    MessageStructure *newMessage = buildMessageStructure(formattedMsgText,
                                                         ui->comboBoxRecipient->currentText().toLocal8Bit());
    if(newMessage == nullptr)
        return;

    newMessage->wasEncrypted = (ui->comboBoxCurrentKey->currentText() != "PLAINTEXT") ? true : false;
    newMessage->cipherKey = ui->comboBoxCurrentKey->currentText();
    newMessage->numTries = 1;
    messageList.append(newMessage);
    timerMessageMap.insert(newMessage->ackTimer, newMessage);
    msgIDMap.insert(newMessage->messageID, newMessage);

    removeStaleMessages();

    emitSendMessageSignal(wrapChatMessage(newMessage));
    refreshReceivedTextBrowser();
}

void DialogChat::on_pushButtonSendMessage_clicked()
{
    if(ui->plainTextEditSendMessage->toPlainText().isEmpty())
        return;

    if(!ifIsOKToSend())
        return;

    if(isTXTFile(ui->plainTextEditSendMessage->toPlainText().trimmed()))
        lastSentMessage = "\r\n\r\n"+getFileContents(ui->plainTextEditSendMessage->toPlainText().trimmed().remove("file:///"))+"\r\n\r\n";

    else
        lastSentMessage = "\r\n\r\n"+ui->plainTextEditSendMessage->toPlainText()+"\r\n\r\n";


    on_pushButtonResendMessage_clicked();
    ui->plainTextEditSendMessage->clear();
}

QByteArray DialogChat::formatQByteArraylACP201()
{
    QByteArray data;
    data.append(QString(ui->comboBoxRecipient->currentText()+
                        " DE "+
                        globalConfigSettings.generalSettings.callSign+" // ").toLatin1());
    data.append(QString(lastSentMessage+" // ").toLocal8Bit());
    data.append(ui->lineEditTerminalInstructions->text().toLocal8Bit());
    data.append(ui->checkBoxTerminalAR->isChecked() ? " AR" : " K");
    data.append(QDateTime::currentDateTimeUtc().toString(" dd-hhmmZ").toLatin1());
    return data;
}

bool DialogChat::isTXTFile(QString fileName){
    return (fileName.startsWith("file:///") && fileName.endsWith(".txt")) ? true : false;
}//isTXTFile

QString DialogChat::getFileContents(QString fileName){
    QFile inFile(fileName);
    QTextStream in(&inFile);
    if(inFile.open(QFile::ReadOnly))
        return in.readAll();
    else
        return "";
}//getFileContents

void DialogChat::on_lineEditChat_returnPressed()
{
    if(ui->lineEditChat->text().isEmpty())
        return;

    if(!ifIsOKToSend())
        return;

    lastSentMessage = ui->lineEditChat->text();

    if(isTXTFile(lastSentMessage))
        lastSentMessage = "\r\n\r\n"+getFileContents(ui->lineEditChat->text().trimmed().remove("file:///"))+"\r\n\r\n";

    if(lastSentMessage.isEmpty())
        return;

    on_pushButtonResendMessage_clicked();
    ui->lineEditChat->clear();
}

bool DialogChat::ifIsOKToSend(){
    if(ui->comboBoxRecipient->currentText().isEmpty()){
        QMessageBox msgBox(this);
        msgBox.setWindowTitle("Blank Recipient");
        msgBox.setText("Message recipient can not be blank!");
        msgBox.exec();
        return false;
    }

    if(ui->comboBoxCurrentKey->currentText().isEmpty()){
        QMessageBox msgBox(this);
        msgBox.setWindowTitle("Blank Key");
        msgBox.setText("Cipher Key can not be blank!");
        msgBox.exec();
        return false;
    }

    if(ui->comboBoxModem->currentText().isEmpty()){
        QMessageBox msgBox(this);
        msgBox.setWindowTitle("Blank Modem Speed");
        msgBox.setText("Modem speed can not be blank!");
        msgBox.exec();
        return false;
    }
    return true;
}

void DialogChat::updateCallsignListComboBox(ClassXML &xmlClass)
{
    QString currentCallSign = ui->comboBoxRecipient->currentText();
    ui->comboBoxRecipient->clear();

    QStringList callSignList = xmlClass.getCallSignList();
    callSignList.removeAll(globalConfigSettings.generalSettings.callSign);
    callSignList.removeAll("ALL");
    callSignList.sort();
    callSignList.insert(0, "ALL");

    ui->comboBoxRecipient->addItems(callSignList);

    currentCallSign.isEmpty() ? ui->comboBoxRecipient->setCurrentText("ALL") : ui->comboBoxRecipient->setCurrentText(currentCallSign);
}

void DialogChat::updateKeyListComboBox(ClassXML &xmlClass)
{
    ui->comboBoxCurrentKey->clear();

    QStringList keyList;
    if((keyList = xmlClass.getKeyList()).isEmpty()) return;

    ui->comboBoxCurrentKey->addItems(keyList);
    ui->comboBoxCurrentKey->setCurrentText(globalConfigSettings.chat_Dialog.keyName);
}

void DialogChat::updateModemListComboBox(ClassXML &xmlClass)
{
    ui->comboBoxModem->clear();

    QStringList modemRates;
    if((modemRates = xmlClass.getModemDataRate().split(',', Qt::SkipEmptyParts)).isEmpty())
        modemRates.append("N/A");

    ui->comboBoxModem->addItems(modemRates);
    ui->comboBoxModem->setCurrentText(globalConfigSettings.chat_Dialog.modemSpeed);
    ui->checkBoxInterleave->setChecked(globalConfigSettings.chat_Dialog.shortInterleave);
}

void DialogChat::slotUpdateConfig(QByteArray config)
{
    isReconfiguring = true;
    ClassXML xmlClass(config);

    if(xmlClass.getCommand() == "config"){
        updateKeyListComboBox(xmlClass);
        updateCallsignListComboBox(xmlClass);
        updateModemListComboBox(xmlClass);
        ui->comboBoxALEAddress->clear();
        ui->comboBoxALEAddress->addItems(xmlClass.getALEAddressList());
    }
    else if (xmlClass.getCommand() == "status") {
        qDebug() << "";
    }

    isReconfiguring = false;
}

void DialogChat::checkIfMessagesForStation()
{
    if(!globalConfigSettings.chat_Dialog.allowRetrieveMessagesLater) return;

    ClassXML xmlWrapper;
    foreach (MessageStructure *message, messageList) {

        bool sendMessageToCaller = message->destinationStation == g_senderCallsign
                                   &&
                                   message->msgStatus == STATUS_NACK
                                   &&
                                   message->isOutgoing;

        if(sendMessageToCaller) {
            message->timeStamp = QDateTime::currentDateTimeUtc();
            message->msgStatus = STATUS_PNDG;
            removeNACKMessage(message);

            createNewAckTimer(message);

            emitSendMessageSignal(wrapChatMessage(message));
            break;//!!!only send one message at a time - the ACK or NACK response will trigger this method again
        }
    }
}

void DialogChat::createNewAckTimer(MessageStructure *message)
{
    deleteMessageTimer(message);
    message->ackTimer = new QTimer(this);
    connect(message->ackTimer, &QTimer::timeout,
            this, &DialogChat::slotResendDatagram);
    message->ackTimer->start(globalConfigSettings.chat_Dialog.delayBetweenRetries*1000*60);
    timerMessageMap.insert(message->ackTimer, message);

}

QByteArray DialogChat::createACKMessage(MessageStructure *newMessage)
{
    QByteArray ackMessageArray = ClassIPChatXML::wrap("ack", newMessage, 0, 0, "");
    ClassXML xmlWrapper;
    QString encryptMessage = (ui->comboBoxCurrentKey->currentText() != "PLAINTEXT") ? "1" : "0";
    QString encryptKey = ui->comboBoxCurrentKey->currentText();
    ackMessageArray = xmlWrapper.wrap("data",
                                      ackMessageArray,
                                      false,
                                      globalConfigSettings.generalSettings.positionIdentifier,
                                      "",
                                      "1",
                                      encryptMessage,
                                      encryptKey,
                                      globalConfigSettings.generalSettings.callSign,
                                      newMessage->destinationStation,
                                      ui->comboBoxModem->currentText(),
                                      globalConfigSettings.chat_Dialog.shortInterleave ? "S" : "L",
                                      A_PRIORITY);
    return ackMessageArray;
}

QByteArray DialogChat::createNACKMessage(MessageStructure *newMessage)
{
    ClassXML xmlWrapper;
    QString encryptMessage = (ui->comboBoxCurrentKey->currentText() != "PLAINTEXT") ? "1" : "0";
    QString encryptKey = ui->comboBoxCurrentKey->currentText();
    QByteArray nackMessage = ClassIPChatXML::wrap("nack", newMessage, 0, 0, "");
    nackMessage = xmlWrapper.wrap("data",
                                  nackMessage,
                                  false,
                                  globalConfigSettings.generalSettings.positionIdentifier,
                                  "",
                                  "1",
                                  encryptMessage,
                                  encryptKey,
                                  globalConfigSettings.generalSettings.callSign,
                                  newMessage->destinationStation,
                                  ui->comboBoxModem->currentText(),
                                  globalConfigSettings.chat_Dialog.shortInterleave ? "S" : "L",
                                  A_PRIORITY);
    return nackMessage;
}

void DialogChat::processExistingMessage(MessageStructure *newMessage,  const ClassXML &xmlClass)
{
    MessageStructure *oldMessage = msgIDMap.value(newMessage->messageID);
    ackMessage(oldMessage, xmlClass);

    delete newMessage;
}

void DialogChat::addMessageToLists(MessageStructure *newMessage, const ClassXML &xmlClass)
{
    newMessage->fromStation = xmlClass.getSourceStation().toLocal8Bit();
    messageList.append(newMessage);
    msgIDMap.insert(newMessage->messageID, newMessage);
}

void DialogChat::ackMessage(MessageStructure *message, const ClassXML &xmlClass)
{
    bool isMessageSpecificallyAddressedToMe = message->destinationStation != "ALL" &&
                                              message->destinationStation == globalConfigSettings.generalSettings.callSign;
    if(isMessageSpecificallyAddressedToMe){
        QByteArray ackMessage = xmlClass.getChecksum() == "PASS" ? createACKMessage(message) : createNACKMessage(message);
        emitSendMessageSignal(ackMessage);
    }
}

void DialogChat::processNewMessage(MessageStructure *newMessage, const ClassXML &xmlClass)
{
    addMessageToLists(newMessage, xmlClass);
    ackMessage(newMessage, xmlClass);
}

void DialogChat::handleUIAfterChatMessage(QString receivedMessage)
{
    updateDigestCheckLabel(receivedMessage);
    ui->tabWidgetChat->setCurrentWidget(ui->tabACP201Chat);
    this->raise();
    ui->lineEditChat->setFocus();
}

void DialogChat::processChatMessage(const QString &receivedMessageText, const ClassXML &xmlClass)
{
    MessageStructure *newMessage = buildMessageStructure(receivedMessageText.toLocal8Bit());
    if (newMessage == nullptr)
        return;

    g_senderCallsign = xmlClass.getSourceStation();

    newMessage->wasEncrypted = xmlClass.getWasEncrypted();
    newMessage->checksumPass = xmlClass.getChecksum();
    newMessage->cipherKey = xmlClass.getEncryptionKey();
    newMessage->ackTimer = nullptr;

    if(msgIDMap.contains(newMessage->messageID)){
        processExistingMessage(newMessage, xmlClass);
    }
    else{
        processNewMessage(newMessage, xmlClass);
        handleUIAfterChatMessage(newMessage->messageText);
    }
    checkIfMessagesForStation();
}

void DialogChat::processACKMessage(QString receivedMessageText)
{
    QStringList tempList = receivedMessageText.trimmed().split('|',Qt::SkipEmptyParts);
    if(tempList.size() < 2) return;

    QString msgID = tempList.at(1).trimmed();
    MessageStructure * oldMessage = msgIDMap.value(msgID);
    if(oldMessage == nullptr) return;

    oldMessage->msgStatus = STATUS_ACKD;
    deleteMessageTimer(oldMessage);

    removeStaleMessages();
    refreshReceivedTextBrowser();

    checkIfMessagesForStation();
}

void DialogChat::processNACKMessage(QString receivedMessageText)
{
    QStringList tempList = receivedMessageText.trimmed().split('|',Qt::SkipEmptyParts);
    if(tempList.size() < 2) return;

    QString msgID = tempList.at(1).trimmed();
    MessageStructure * oldMessage = msgIDMap.value(msgID);
    if(oldMessage == nullptr) return;

    oldMessage->msgStatus = STATUS_NACK;
    deleteMessageTimer(oldMessage);

    removeStaleMessages();
    refreshReceivedTextBrowser();

    checkIfMessagesForStation();
}

void DialogChat::processMessagesReceivedFromMessagemachine(const QString &receivedMessageText, const ClassXML &xmlClass)
{
    QString tempReceivedMessageText = receivedMessageText;
    while (tempReceivedMessageText.endsWith('\r') || tempReceivedMessageText.endsWith('\n')) {
        tempReceivedMessageText.chop(1);
    }

    QStringList receivedMessageList = tempReceivedMessageText.split("NNNN", Qt::SkipEmptyParts);

    foreach (QString message, receivedMessageList) {
        message.append("NNNN");
        MessageStructure *newMessage = new MessageStructure();
        newMessage->timeStamp = QDateTime::currentDateTimeUtc();
        newMessage->messageID = xmlClass.getSourceStation().toLocal8Bit()+
                                newMessage->timeStamp.toString("ddhhmmsszzz").toLocal8Bit();
        newMessage->ackTimer = nullptr;
        newMessage->checksumPass = xmlClass.getChecksum();
        newMessage->cipherKey = xmlClass.getEncryptionKey();
        newMessage->messageText = message.toLocal8Bit();
        newMessage->destinationStation = xmlClass.getDestinationStation().toLocal8Bit();
        newMessage->isOutgoing = false;
        newMessage->msgStatus = STATUS_ACKD;
        newMessage->fromStation = xmlClass.getSourceStation().toLocal8Bit();
        newMessage->numTries = 0;
        newMessage->wasEncrypted = xmlClass.getWasEncrypted();

        messageList.append(newMessage);
        msgIDMap.insert(newMessage->messageID, newMessage);
        updateDigestCheckLabel(message);

    }

    ui->textBrowserMachine->append(receivedMessageText);
}

void DialogChat::slotMessageReceived(QByteArray message)
{
    if(message.isEmpty()) return;

    ClassXML xmlClass(message);
    QString receivedMessageText = xmlClass.getReceivedMessage();

    if(receivedMessageText.startsWith("chat|"))
        processChatMessage(receivedMessageText, xmlClass);

    else if (receivedMessageText.startsWith("file|"))
        qDebug() << "TODO handle file transfers (after ARQ is built)";//TODO

    else if (receivedMessageText.startsWith("image|"))
        qDebug() << "TODO handle image transfers (after ARQ is built)";//TODO

    else if (receivedMessageText.startsWith("voice|"))
        qDebug() << "TODO handle voice transfers (after ARQ is built)";//TODO

    else if(receivedMessageText.startsWith("ack|"))
        processACKMessage(receivedMessageText);

    else if(receivedMessageText.startsWith("nack|"))
        processNACKMessage(receivedMessageText);

    else if (receivedMessageText.contains("-CM DE ") ||
             receivedMessageText.contains("-CM//"))
        handleMachineResponse(receivedMessageText);

    else if(!receivedMessageText.contains("-CM DE ") && !receivedMessageText.contains("-CM//"))
        processMessagesReceivedFromMessagemachine(receivedMessageText, xmlClass);

    else
        ui->textBrowserMachine->append(receivedMessageText);

    removeStaleMessages();
    refreshReceivedTextBrowser();
}

void DialogChat::on_spinBoxMessageRetention_valueChanged(int arg1)
{
    if(g_ApplicationStart || isReconfiguring)
        return;

    globalConfigSettings.chat_Dialog.messageRetention = arg1;
}

void DialogChat::on_splitterChat_splitterMoved(int pos, int index)
{
    Q_UNUSED(pos)
    Q_UNUSED(index)
    if(g_ApplicationStart)
        return;

    globalConfigSettings.chat_Dialog.chatSplitterState = ui->splitterChat->saveState();
}

void DialogChat::on_spinBoxDelayBetweenTries_valueChanged(int arg1)
{
    if(g_ApplicationStart || isReconfiguring)
        return;
    globalConfigSettings.chat_Dialog.delayBetweenRetries = arg1;
}

void DialogChat::on_pushButtonEraseMessages_clicked()
{
    if(messageList.isEmpty()) return;

    QMessageBox msgBox(this);
    msgBox.setText("Deleting all saved messages!");
    msgBox.setInformativeText("This action will permanently delete all saved messages!\nDo you wish to continue?");
    msgBox.setStandardButtons(QMessageBox::Yes | QMessageBox::No);
    msgBox.setDefaultButton(QMessageBox::No);
    int result = msgBox.exec();

    if(result == QMessageBox::Yes){
        foreach (MessageStructure *message, messageList)
            deleteMessageFromList(message);

        saveMessageListToDisk();
        refreshReceivedTextBrowser();
    }
}

void DialogChat::on_checkBoxRetrieveMessagesLater_clicked(bool checked)
{
    if(g_ApplicationStart || isReconfiguring)
        return;

    globalConfigSettings.chat_Dialog.allowRetrieveMessagesLater = checked;
}

void DialogChat::on_spinBoxNumRetries_valueChanged(int arg1)
{
    if(g_ApplicationStart || isReconfiguring)
        return;

    globalConfigSettings.chat_Dialog.numRetries = arg1;
}

void DialogChat::on_comboBoxCurrentKey_currentTextChanged(const QString &arg1)
{
    if(g_ApplicationStart || isReconfiguring)
        return;

    globalConfigSettings.chat_Dialog.keyName = arg1;
}

void DialogChat::on_comboBoxModem_currentTextChanged(const QString &arg1)
{
    if(g_ApplicationStart || isReconfiguring)
        return;

    globalConfigSettings.chat_Dialog.modemSpeed = arg1;
}

void DialogChat::on_pushButtonDeleteSelectedRecipient_clicked()
{
    if(ui->comboBoxRecipient->currentText() == "ALL")
        return;
    
    globalConfigSettings.aleSettings.heardCallList.removeAll(ui->comboBoxRecipient->currentText());
    globalConfigSettings.aleSettings.heardCallList.sort();

    ui->comboBoxRecipient->clear();
    ui->comboBoxRecipient->addItems(globalConfigSettings.aleSettings.heardCallList);
    ui->comboBoxRecipient->insertItem(0,"ALL");
}

void DialogChat::on_checkBoxInterleave_clicked(bool checked)
{
    if(g_ApplicationStart || isReconfiguring)
        return;

    globalConfigSettings.chat_Dialog.shortInterleave = checked;
}

void DialogChat::on_pushButtonRXFiles_clicked()
{
    const QString explorer = "explorer";
    QStringList param;
    if (!QFileInfo(receivedFileDirectory.path()).isDir())
        param << QLatin1String("/select,");
    param << QDir::toNativeSeparators(receivedFileDirectory.path());
    QProcess::startDetached(explorer, param);
}

void DialogChat::on_pushButtonSelectFile_clicked()
{
    QString initialPath = lastOpenFileLocation.isEmpty() ? QDir::homePath() + "/Desktop" : lastOpenFileLocation;

    QFileDialog dialog(this);
    dialog.setWindowTitle(tr("Select file"));
    dialog.setDirectory(initialPath);
    dialog.setNameFilter(tr("Image Files (*.jpg)"));

    if (!dialog.exec())
        return;

    QString selectedFilePath = dialog.selectedFiles().value(0);
    receivedSelectedFile.setFileName(selectedFilePath);

    if (!receivedSelectedFile.exists())
        return;

    QFileInfo fileInfo(receivedSelectedFile);
    lastOpenFileLocation = fileInfo.absoluteFilePath();
    ui->lineEditFileName->setText(lastOpenFileLocation);

    int fileSize = fileInfo.size();
    QString fileSizeString;

    if (fileSize < 1000)
        fileSizeString = QString::number(fileSize) + tr(" bytes");

    else if (fileSize < 1000000)
        fileSizeString = QString::number(static_cast<double>(fileSize) / 1000.0) + tr(" Kbytes");

    else
        fileSizeString = QString::number(static_cast<double>(fileSize) / 1000000.0) + tr(" Mbytes");

    ui->labelFileInfo->setText(tr("File size - ") + fileSizeString);

    ui->pushButtonSendFile->setEnabled(true);
}

void DialogChat::on_pushButtonSendFile_clicked()
{
    if (!receivedSelectedFile.exists()){
        qDebug() << Q_FUNC_INFO << "ERROR: Received file does not exist!";
        return;
    }

    if (!receivedSelectedFile.open(QIODevice::ReadOnly)){
        qDebug() << Q_FUNC_INFO << receivedSelectedFile.errorString();
        return;
    }

    QByteArray readBuffer;
    readBuffer = receivedSelectedFile.readAll();
    receivedSelectedFile.close();

    readBuffer = readBuffer.toBase64();
    readBuffer.append("==");

    ClassXML xmlWrapper;
    QString encryptMessage = (ui->comboBoxCurrentKey->currentText() != "PLAINTEXT") ? "1" : "0";
    QString encryptKey = ui->comboBoxCurrentKey->currentText();

    QByteArray sentMessageText = xmlWrapper.wrap("data",
                                                 readBuffer,
                                                 false,
                                                 globalConfigSettings.generalSettings.positionIdentifier,
                                                 "",
                                                 "1",
                                                 encryptMessage,
                                                 encryptKey,
                                                 globalConfigSettings.generalSettings.callSign,
                                                 this->chatPartner.toLocal8Bit(),
                                                 ui->comboBoxModem->currentText(),
                                                 globalConfigSettings.chat_Dialog.shortInterleave ? "S" : "L",
                                                 A_ROUTINE);

    emitSendMessageSignal(sentMessageText);
}

QByteArray DialogChat::wrapMachineCommand(QByteArray machineCommandArray)
{
    QString encryptMessage = (ui->comboBoxCurrentKey->currentText() != "PLAINTEXT") ? "1" : "0";
    QString encryptKey = ui->comboBoxCurrentKey->currentText();
    ClassXML xmlWrapper;

    QString destinationCall = ui->comboBoxSelectMachineAddress->currentText();
    destinationCall.remove("-CM");

    QString modemSpeed, modemInterleave;

    if(ui->groupBoxModemSettings->isChecked()){
        modemSpeed = ui->comboBoxModem->currentText();
        modemInterleave = ui->checkBoxInterleave->isChecked() ? "S" : "L";
    }
    else {
        modemSpeed = globalConfigSettings.generalSettings.modemSpeed;
        modemInterleave = globalConfigSettings.generalSettings.modemInterleave;
    }

    QByteArray sentMessageText = xmlWrapper.wrap("data",
                                                 machineCommandArray,
                                                 false,
                                                 globalConfigSettings.generalSettings.positionIdentifier,
                                                 "",
                                                 "1",
                                                 encryptMessage,
                                                 encryptKey,
                                                 globalConfigSettings.generalSettings.callSign,
                                                 destinationCall,
                                                 modemSpeed,
                                                 modemInterleave,
                                                 A_ROUTINE);

    return sentMessageText;
}

void DialogChat::sendMachineCommand(QByteArray command){
    if(machineConfigError())
        return;

    QByteArray machineCommandArray = buildMachineCommand(command);
    QByteArray sentMessageText = wrapMachineCommand(machineCommandArray);

    emitSendMessageSignal(sentMessageText);
}

QByteArray DialogChat::buildMachineCommand(QByteArray command)
{
    QByteArray machineCommand;
    machineCommand.append(ui->comboBoxSelectMachineAddress->currentText().toLatin1());
    machineCommand.append(QString(" DE "+globalConfigSettings.generalSettings.callSign).toLatin1());
    machineCommand.append(" // ");
    machineCommand.append(command.toUpper());

    if(ui->checkBoxCompressResults->isChecked())
        machineCommand.append(" [COMP]");

    machineCommand.append(" // ");
    machineCommand.append(QDateTime::currentDateTimeUtc().toString("hhmmZ").toLatin1());
    machineCommand.append(" K");
    ui->textBrowserMachine->append(machineCommand+"\n");
    return machineCommand;
}

bool DialogChat::machineConfigError()
{
    if(globalConfigSettings.generalSettings.callSign.isEmpty()){
        displayErrorMessage(tr(APP_NAME)+" - ERROR","ERROR - No call sign configured!");
        return true;
    }

    if(ui->comboBoxSelectMachineAddress->currentText().isEmpty()){
        displayErrorMessage(tr(APP_NAME)+" - ERROR","ERROR - No machine address selected!");
        return true;
    }

    return false;
}

void DialogChat::displayErrorMessage(QString title, QString error){
    QMessageBox msgBox(this);
    msgBox.setWindowTitle(title);
    msgBox.setText(error);
    msgBox.exec();
}

void DialogChat::on_pushButtonPing_clicked()
{
    sendMachineCommand("VER");
}

void DialogChat::on_pushButtonAddMachineAddress_clicked()
{
    QString machineAddressNoSpacesString = removeSpaces(ui->lineEditAddMachineAddress->text().toUpper());
    if(machineAddressNoSpacesString.isEmpty()){
        ui->lineEditAddMachineAddress->clear();
        return;
    }

    if(!machineAddressNoSpacesString.endsWith("-CM",Qt::CaseInsensitive))
        machineAddressNoSpacesString = machineAddressNoSpacesString.toUpper()+"-CM";

    else
        machineAddressNoSpacesString = machineAddressNoSpacesString.toUpper();

    if(!globalConfigSettings.generalSettings.machineAddressList.contains(machineAddressNoSpacesString))
        globalConfigSettings.generalSettings.machineAddressList.append(machineAddressNoSpacesString);

    ui->comboBoxSelectMachineAddress->clear();
    ui->comboBoxSelectMachineAddress->addItems(globalConfigSettings.generalSettings.machineAddressList);
    ui->comboBoxSelectMachineAddress->setCurrentText(machineAddressNoSpacesString);
    ui->lineEditAddMachineAddress->clear();
}

QString DialogChat::removeSpaces(QString stringData)
{
    QByteArrayList byteArrayList = stringData.toLatin1().split(' ');
    byteArrayList.removeAll("");
    return QString(byteArrayList.join(""));
}

QByteArray DialogChat::removeSpaces(QByteArray bytearrayData)
{
    QByteArrayList byteArrayList = bytearrayData.split(' ');
    byteArrayList.removeAll("");
    return byteArrayList.join("");
}

void DialogChat::on_pushButtonRemoveMachineAddress_clicked()
{
    globalConfigSettings.generalSettings.machineAddressList.removeAll(ui->comboBoxSelectMachineAddress->currentText());
    ui->comboBoxSelectMachineAddress->clear();
    ui->comboBoxSelectMachineAddress->addItems(globalConfigSettings.generalSettings.machineAddressList);
}

void DialogChat::on_pushButtonQRU_clicked()
{
    QByteArray enteredParameters = ui->lineEditMessageParamaters->text().toUpper().toLocal8Bit();
    QByteArray selectedSavedParameters = ui->comboBoxSavedQRUParameters->currentText().toLocal8Bit();

    if (enteredParameters.isEmpty() && selectedSavedParameters.isEmpty())
        return;


    QByteArray machineCommand = "QRU ";

    if (enteredParameters.isEmpty() && !selectedSavedParameters.isEmpty())
        machineCommand += selectedSavedParameters;

    else if (!enteredParameters.isEmpty()) {
        machineCommand += enteredParameters;
        saveQRUToSettings();
    }

    if (!machineCommand.isEmpty())
        sendMachineCommand(machineCommand);

}

void DialogChat::saveQRUToSettings()
{
    globalConfigSettings.generalSettings.savedQRUParameters.append(ui->lineEditMessageParamaters->text().toUpper());
    globalConfigSettings.generalSettings.savedQRUParameters.removeDuplicates();
    globalConfigSettings.generalSettings.savedQRUParameters.sort();

    ui->comboBoxSavedQRUParameters->clear();
    ui->comboBoxSavedQRUParameters->addItems(globalConfigSettings.generalSettings.savedQRUParameters);
}

void DialogChat::on_pushButtonZEE_clicked()
{
    sendMachineCommand("ZEE "+ui->lineEditMessageParamaters->text().toUpper().toLatin1());
}

void DialogChat::on_pushButtonQSL_clicked()
{
    sendMachineCommand("QSL "+ui->lineEditMessageParamaters->text().toUpper().toLatin1());
}

void DialogChat::on_pushButtonSQLQuery_clicked()
{
    QString SQLCommand = globalConfigSettings.sqlStatements.SQLStatementMap.value(ui->comboBoxSQLQuery->currentText()).toString();

    foreach (QWidget *widget, ui->frameSQLWidgets->findChildren<QWidget *>()) {
        QLineEdit* lineEdit = qobject_cast<QLineEdit*>(widget);
        if (lineEdit)
            SQLCommand.replace("[" + lineEdit->objectName() + "]", lineEdit->text());
    }

    sendMachineCommand("SQL " + SQLCommand.toLatin1());
}

void DialogChat::on_pushButtonResetSQLStatements_clicked()
{
    globalConfigSettings.sqlStatements.SQLStatementMap.clear();
    ui->plainTextEditUserSQLStatementMapping->clear();
    updateUIWidgets();
}

void DialogChat::removeExistingWidgetsFromSQLWidgetFrame()
{
    foreach (QWidget *widget, ui->frameSQLWidgets->findChildren<QWidget *>()) {
        //at this point in time there can only be label and QLineEdit widgets - 17 Oct 2023, Millea
        bool isLabelWidget = dynamic_cast<const QLabel*>(widget) != nullptr;
        bool isQLineEditWidget = dynamic_cast<const QLineEdit*>(widget) != nullptr;

        bool removeWidget = isLabelWidget || isQLineEditWidget;

        if(removeWidget)
            widget->deleteLater();

    }
}

void DialogChat::removeLeadingCharsandBracket(QString &SQLStatement)
{
    while (!SQLStatement.startsWith('['))
        SQLStatement.remove(0,1);

    SQLStatement.remove(0,1); // remove [
}

QStringList DialogChat::appendFieldNamestoList(QString SQLStatement)
{
    QStringList fieldNameList;

    while (SQLStatement.count('[') > 0) {

        removeLeadingCharsandBracket(SQLStatement);

        QString fieldName = SQLStatement.left(SQLStatement.indexOf(']'));
        fieldNameList.append(fieldName);

        SQLStatement.remove(fieldName+']');
    }

    return fieldNameList;
}

void DialogChat::addWidgetsToSQLWidgetFrame(QStringList fieldNameList)
{
    foreach (QString fieldName, fieldNameList) {

        QLabel *fieldLabel = new QLabel;
        fieldLabel->setObjectName(fieldName);
        fieldLabel->setText(fieldName);
        ui->frameSQLWidgets->layout()->addWidget(fieldLabel);

        QLineEdit * fieldLineEdit = new QLineEdit;
        fieldLineEdit->setObjectName(fieldName);
        fieldLineEdit->setClearButtonEnabled(true);
        ui->frameSQLWidgets->layout()->addWidget(fieldLineEdit);
    }
}

void DialogChat::on_comboBoxSQLQuery_currentTextChanged(const QString &arg1)
{
    if(arg1.isEmpty())
        return;
    
    QString SQLStatement = globalConfigSettings.sqlStatements.SQLStatementMap.value(arg1).toString();

    removeExistingWidgetsFromSQLWidgetFrame();

    if(!SQLStatement.startsWith(QString("SELECT * "), Qt::CaseInsensitive)){
        qDebug() << "ERROR - Not a select statement";
        return;
    }

    if(SQLStatement.contains('[') &&
        SQLStatement.count('[') != SQLStatement.count(']')){
        qDebug() << "ERROR - no braces found or unmatched braces [ ]";
        return;
    }

    QStringList fieldNameList = appendFieldNamestoList(SQLStatement);

    addWidgetsToSQLWidgetFrame(fieldNameList);
}

void DialogChat::on_pushButtonEditSQLStatements_clicked()
{
    ui->plainTextEditUserSQLStatementMapping->setEnabled(true);
    ui->pushButtonSaveSQLStatements->setEnabled(true);
    ui->pushButtonEditSQLStatements->setEnabled(false);
}

void DialogChat::parseSQLStatementsFromTextBrowser()
{
    QStringList sqlList =
        ui->plainTextEditUserSQLStatementMapping->toPlainText().split("\n",Qt::SkipEmptyParts);
    
    globalConfigSettings.sqlStatements.SQLStatementMap.clear();

    foreach (const QString& statement, sqlList) {
        int delimiterIndex = statement.indexOf(':');
        if (delimiterIndex != -1) {
            QString key = statement.left(delimiterIndex);
            QString value = statement.mid(delimiterIndex + 1).trimmed();
            globalConfigSettings.sqlStatements.SQLStatementMap.insert(key, value);
        }
    }
}

void DialogChat::updateSQLUIWidgets()
{
    ui->comboBoxSQLQuery->clear();
    ui->comboBoxSQLQuery->addItems(globalConfigSettings.sqlStatements.SQLStatementMap.keys());

    ui->plainTextEditUserSQLStatementMapping->setEnabled(false);
    ui->pushButtonSaveSQLStatements->setEnabled(false);
    ui->pushButtonEditSQLStatements->setEnabled(true);
}

void DialogChat::handleMachineResponse(QString receivedMessageText)
{
    ui->textBrowserMachine->append(receivedMessageText);
}

void DialogChat::updateDigestCheckLabel(QString receivedMessageText)
{
    if(receivedMessageText.contains("[DIGEST:")){
        ui->labelDigestCheck->show();
        if(ClassSignMessage::checkMessageDigest(receivedMessageText)){
            ui->labelDigestCheck->setText("Digest: Passed");
            ui->labelDigestCheck->setStyleSheet(QString("QLabel {color : #78ff6e}"));
        }
        else {
            ui->labelDigestCheck->setText("Digest: Failed");
            ui->labelDigestCheck->setStyleSheet(QString("QLabel {color : #ff5959}"));
        }
    }
    else {
        ui->labelDigestCheck->hide();
    }
}

void DialogChat::emitSendMessageSignal(QByteArray messageText)
{
    emit signalSendMessage(messageText);
}

void DialogChat::on_pushButtonSaveSQLStatements_clicked()
{
    if(ui->plainTextEditUserSQLStatementMapping->toPlainText().isEmpty()){
        return;
    }
    parseSQLStatementsFromTextBrowser();
    updateSQLUIWidgets();
}

void DialogChat::on_pushButtonZEE_2_clicked()
{
    sendMachineCommand("ZEE "+ui->lineEditMessageParamaters_2->text().toUpper().toLatin1());
}

void DialogChat::on_pushButtonQSL_2_clicked()
{
    sendMachineCommand("QSL "+ui->lineEditMessageParamaters_2->text().toUpper().toLatin1());
}

void DialogChat::on_pushButtonALEConnect_clicked()
{
    QString ALEAddress = ui->comboBoxALEAddress->currentText();
    if(ALEAddress.isEmpty())
        return;

    ClassXML xmlWrapper;
    QByteArray command = xmlWrapper.wrap("ALE CALL",
                                         "",
                                         true,
                                         globalConfigSettings.generalSettings.positionIdentifier,
                                         ALEAddress,
                                         "1",//compress
                                         "1",//encrypt
                                         ui->comboBoxCurrentKey->currentText(),
                                         globalConfigSettings.generalSettings.callSign,
                                         "",
                                         ui->comboBoxModem->currentText(),
                                         globalConfigSettings.chat_Dialog.shortInterleave ? "S" : "L",
                                         A_ROUTINE);

    qDebug() << command;
    emitSendMessageSignal(command);
}

void DialogChat::on_lineEditAddMachineAddress_returnPressed()
{
    on_pushButtonAddMachineAddress_clicked();
}

void DialogChat::on_groupBoxModemSettings_clicked(bool checked)
{
    checked ? ui->groupBoxModemSettings->setTitle("Modem") : ui->groupBoxModemSettings->setTitle("Use Global Settings");
    globalConfigSettings.chat_Dialog.useLocalModemSettings = checked;
}

