/****************************************************************************
**
** Copyright (C) 2016 - 2019 Timothy Millea <timothy.j.millea@gmail.com>
** Released under GPL License
** See gpl-3.0.txt included with the source code.
**
****************************************************************************/

#include <QClipboard>
#include "dlgterminal.h"
#include "ui_dlgterminal.h"
#include "algorithm"
#include "stdlib.h"
#include "cstdlib"
#include <string.h>
#include <QTextStream>

#include "debug.h"
#include "classxml.h"
#include "globals.h"
#include "main.h"


const char APP_NAME[12] = "SM_Terminal";

int dlgterminal::truncateLines(QStringList &messageList, int lineLength)
{
    QStringList newMessageList;
    int cursorBump = 0;
    foreach (QString tempString, messageList) {
        if(tempString.count() > lineLength){
            QString fullString = tempString.left(lineLength);
            newMessageList.append(fullString);
            tempString.remove(0,lineLength);
            newMessageList.append(tempString);
            cursorBump++;
        }
        else {
            newMessageList.append(tempString);
        }
    }
    messageList = newMessageList;
    return cursorBump;
}//truncateLines

bool dlgterminal::hasLongLines(QStringList &messageList, int lineLength)
{
    foreach (QString tempString, messageList) {
        if(tempString.count() > lineLength){
            return true;
        }
    }
    return false;
}//hasLongLines

dlgterminal::dlgterminal(QWidget *parent) :
    QMainWindow(parent),
    ui(new Ui::dlgterminal)
{
    ui->setupUi(this);
    isApplicationStarting = true;
    myFont.setFamily("MS Shell Dlg 2");
    myFont.setPointSize(8);
    myFont.setStyleStrategy(QFont::PreferAntialias);

    readSettings();

    updateSettings();
    createTCPSocket();
    //this is used to auto configure TCP settings
    createUDPListener();
    sayHelo();
    startTCPTimer();
    initActionsConnections();

    ui->labelMessageAuthentication->hide();

    highlighter = new Highlighter(ui->lineEditChatText->document());

    ui->lineEditChatText->installEventFilter(this);

    ui->lineEditChatText->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(ui->lineEditChatText, SIGNAL( customContextMenuRequested(const QPoint &) ), this, SLOT(on_textEdit_ContextMenuRequest(const QPoint &)));

    isApplicationStarting = false;

}//dlgterminal

void dlgterminal::createTCPSocket()
{
    tcpSocket = new QTcpSocket;
    tcpSocket->setSocketOption(QAbstractSocket::KeepAliveOption,1);
    connect(tcpSocket, &QTcpSocket::disconnected,
            this, &dlgterminal::slotTCPSocketDisconnected);
    connect(tcpSocket, &QTcpSocket::readyRead,
            this, &dlgterminal::slotReadTcpData);
}//createTCPSocket

void dlgterminal::createUDPListener()
{
    udpListenSocket = new QUdpSocket(this);
    udpListenSocket->bind(configSettings.generalSettings.applicationDiscoveryPort.toUShort(), QUdpSocket::ReuseAddressHint);
    ui->textBrowserReceivedData->append("Discovery: "+
                                        udpListenSocket->localAddress().toString()+":"+QString::number(udpListenSocket->localPort()));
    connect(udpListenSocket,&QUdpSocket::readyRead,
            this, &dlgterminal::slotReadUdpData);
}//createUDPListener

void dlgterminal::sayHelo()
{
    //seed the random generator with current milliseconds time
    qsrand(uint(QTime::currentTime().msec()));
    //introduce ourselves at a random interval of 10 to 2000ms
    QTimer::singleShot(qrand() % ((2000 + 1) - 10) + 10, this, SLOT(slotSayHelo()));
}//sayHelo

void dlgterminal::startTCPTimer()
{
    //handle tcpData timer
    connect(&handleTCPDataTimer, &QTimer::timeout,
            this, &dlgterminal::slotHandleTCPData);
    handleTCPDataTimer.setInterval(tcpTimeout);
    handleTCPDataTimer.start();
}//startTCPTimer

dlgterminal::~dlgterminal()
{
    slotBroadcastID("bye");
    if(tcpSocket->isOpen())
        tcpSocket->close();
    delete tcpSocket;

    udpListenSocket->close();
    delete udpListenSocket;

    delete ui;
}//~dlgterminal

void dlgterminal::closeEvent(QCloseEvent *event)
{
    writeSettings();
    event->accept();
}//closeEvent

void dlgterminal::slotReadTcpData()
{
    if(!tcpSocket->isOpen())
        return;

    handleTCPDataTimer.setInterval(tcpTimeout);
    static bool busy = false;

    if(busy){
        //        qDebug() << "readData() busy...";
        return;
    }

    busy = true;

    QByteArray localByteArray;
    while (tcpSocket->bytesAvailable()) {
        localByteArray.append(tcpSocket->readAll());
    }

    if(localByteArray.count("<?xml ") > 1){
        //there is more than 1 XML document
        if(localByteArray.endsWith("</V3PROTOCOL>\n")){
            QString localString = localByteArray;
            QStringList localStringList = localString.split("</V3PROTOCOL>\n");
            foreach (QString string, localStringList) {
                if(!string.isEmpty())
                    handleTCPData(string.toLatin1()+"</V3PROTOCOL>\n");
            }
        }//if we have complete documents
        else{
            QString localString = localByteArray;
            QStringList localStringList = localString.split("</V3PROTOCOL>\n");
            for(int i=0;i<localStringList.count()-1;i++){
                handleTCPData(localStringList.at(i).toLatin1()+"</V3PROTOCOL>\n");
            }
            dataByteArray.append(localStringList.last());
        }//else we have incomplete documents
    }//if we have multiple xml documents
    else if(localByteArray.startsWith("<?xml ") && localByteArray.endsWith("</V3PROTOCOL>\n"))
    {
        handleTCPData(localByteArray);
    }//else is we have a single complete document
    else{
        dataByteArray.append(localByteArray);
    }//else we have a single incomplete document

    localByteArray.clear();
    busy = false;
}//slotReadTcpData

void dlgterminal::slotHandleTCPData()
{
    static bool isBusy = false;
    if(isBusy)
        return;
    isBusy = true;

    handleTCPDataTimer.setInterval(tcpTimeout);

    if(dataByteArray.size() > 0)
    {
        //        QColor fgColor = ui->textBrowserReceivedData->palette().color(ui->textBrowserReceivedData->foregroundRole());
        ui->textBrowserReceivedTCPData->append("Time out: "+dataByteArray);

        ClassXML xmlClass(dataByteArray);

        //v3Terminal does not use all the XML data so it is commented out
        bool wasEncrypted = xmlClass.getWasEncrypted();
        //        bool wasCompressed = xmlClass.getWasCompressed();
        QString positionID = xmlClass.getPositionID();
        QString command = xmlClass.getCommand();
        QStringList keyList = xmlClass.getKeyList();
        keyList.append("PLAINTEXT");
        QString encryptionKey = xmlClass.getEncryptionKey();
        keyList.removeAll("");
        QStringList callsignList = xmlClass.getCallsignList();
        QString ALEAddress = xmlClass.getALEAddres();
        QStringList aleAddressList = xmlClass.getALEAddressList();
        QString sourceStation = xmlClass.getSourceStation();
        QString destinationStation = xmlClass.getDestinationStation();
        QString checkSum = xmlClass.getChecksum();
        QString wasAuthenticated = xmlClass.getWasAuthenticated();
        QString authPassed = xmlClass.getAuthPassed();
        QString authAgency = xmlClass.getAuthAgency();
        QString receivedMessage = xmlClass.getReceivedMessage();

        if(xmlClass.getParsingError()){
            ui->textBrowserReceivedData->append("ERROR: Parsing TCP XML document!");
            ui->textBrowserMachine->append("ERROR: Parsing TCP XML document!");
        }
        if(configSettings.generalSettings.ignoreMessagesNotForMe)
        {
            if(!configSettings.generalSettings.callSignList.contains(destinationStation) &&
                    !destinationStation.isEmpty() &&
                    destinationStation != "ALL")
            {
                dataByteArray.clear();
                isBusy = false;
                return;
            }
        }

        if(command == "ack"){
            if(!configSettings.generalSettings.ignoreACKMessages){
                //                ui->textBrowserReceivedData->append("ACK: "+positionID+" - "+receivedMessage);
                ui->textBrowserReceivedData->append("ACK: "+positionID);
                ui->textBrowserMachine->append("ACK: "+positionID);
                //                ui->textBrowserMachine->append("ACK: "+positionID+" - "+receivedMessage);
            }
            dataByteArray.clear();
            isBusy = false;
            return;
        }
        else if (command=="config") {
            if(tcpSocket->isOpen()){
                //populate cipher key combo box
                ui->comboBoxKeyList->clear();
                ui->comboBoxKeyList->addItems(keyList);
                ui->comboBoxConfKeyList->clear();
                ui->comboBoxConfKeyList->addItems(keyList);
                ui->comboBoxKeyList->setCurrentText(configSettings.generalSettings.keyFile);
                //populate ale address combo box
                ui->comboBoxALEStations->clear();
                ui->comboBoxALEStations->addItems(aleAddressList);
                //populate callsign combo box
                foreach (QString tempString, callsignList) {
                    if(ui->comboBoxRecipient->findText(tempString) == -1){
                        ui->comboBoxRecipient->addItem(tempString);
                    }
                }
            }//if tcpSocket is open
            dataByteArray.clear();
            isBusy = false;
            return;
        }//if config
        else if (command == "status") {
            dataByteArray.clear();
            isBusy = false;
            return;
        }
        else if (command == "error") {
            ui->textBrowserReceivedData->append(QDateTime::currentDateTimeUtc().toString("hhmmZ: ")+positionID+" - "+receivedMessage);
            ui->textBrowserMachine->append(QDateTime::currentDateTimeUtc().toString("hhmmZ: ")+positionID+" - "+receivedMessage);
            dataByteArray.clear();
            isBusy = false;
            return;
        }
        else if (command == "data") {
            if(!receivedMessage.isEmpty()){
                if((bool)wasAuthenticated.toInt()){
                    ui->labelMessageAuthentication->show();
                    if((bool)authPassed.toInt()){
//                        ui->labelMessageAuthentication->setText("MAC: PASS - "+authAgency);
                        ui->labelMessageAuthentication->setText("MAC: PASS");
                        ui->labelMessageAuthentication->setStyleSheet("color: rgb(0,255,0)");
                    }
                    else {
                        ui->labelMessageAuthentication->setText("MAC: Authentication FAIL");
                        ui->labelMessageAuthentication->setStyleSheet("color : red");
                    }
                }
                else {
                    ui->labelMessageAuthentication->hide();
                    ui->labelMessageAuthentication->setText("MAC:");
                    ui->labelMessageAuthentication->setStyleSheet("color : black");
                }
                if(ui->comboBoxALEStations->findText(ALEAddress) == -1){
                    ui->comboBoxALEStations->addItem(ALEAddress);
                }
                if(ui->comboBoxRecipient->findText(sourceStation) == -1){
                    ui->comboBoxRecipient->addItem(sourceStation);
                }
                if(configSettings.generalSettings.ignoreMessagesNotForMe)
                {
                    if(configSettings.generalSettings.callSignList.contains(destinationStation) ||
                            destinationStation.isEmpty() ||
                            destinationStation == "ALL")
                    {
                        setChecksumLabel(checkSum);
                    }//if message is for me
                }//if I have ignore messages on
                else {
                    setChecksumLabel(checkSum);
                }//else show all messages
                //single line chat message - does not contain a newline character
                if(!receivedMessage.contains('\n')){
                    if(configSettings.generalSettings.ignoreMessagesNotForMe)
                    {
                        if(configSettings.generalSettings.callSignList.contains(destinationStation) ||
                                destinationStation.isEmpty() ||
                                destinationStation == "ALL")
                        {
                            QString usedEncryption;
                            if(wasEncrypted){
                                usedEncryption = "CT ";
                                ui->textBrowserReceivedData->append("\nRcvd9 "+usedEncryption+" "+encryptionKey+" "+checkSum.left(4)
                                                                    +" from "+sourceStation+" "
                                                                    +QDateTime::currentDateTimeUtc().toString("hhmmZ: ")+receivedMessage);
                                ui->textBrowserMachine->append("\nRcvd9 "+usedEncryption+" "+encryptionKey+" "+checkSum.left(4)
                                                               +" from "+sourceStation+" "
                                                               +QDateTime::currentDateTimeUtc().toString("hhmmZ: ")+receivedMessage);
                            }
                            else{
                                usedEncryption = "PT ";
                                ui->textBrowserReceivedData->append("\n\nRcvd10 "+
                                                                    usedEncryption+" "+encryptionKey+" "+
                                                                    checkSum.left(4)+
                                                                    " from "+sourceStation+" "+
                                                                    QDateTime::currentDateTimeUtc().toString("hhmmZ: ")+
                                                                    receivedMessage);
                                ui->textBrowserMachine->append("\n\nRcvd10 "+
                                                               usedEncryption+" "+encryptionKey+" "+
                                                               checkSum.left(4)+
                                                               " from "+sourceStation+" "+
                                                               QDateTime::currentDateTimeUtc().toString("hhmmZ: ")+
                                                               receivedMessage);
                            }
                        }//if message is for any of our callsigns
                    }//if ignore message not for me
                    else {
                        QString usedEncryption;
                        if(wasEncrypted){
                            usedEncryption = "CT ";
                            ui->textBrowserReceivedData->append("Rcvd11 "+usedEncryption+" "+encryptionKey+" "+checkSum.left(4)+" from "+sourceStation+" "+QDateTime::currentDateTimeUtc().toString("hhmmZ: ")+receivedMessage);
                            ui->textBrowserMachine->append("Rcvd11 "+usedEncryption+" "+encryptionKey+" "+checkSum.left(4)+" from "+sourceStation+" "+QDateTime::currentDateTimeUtc().toString("hhmmZ: ")+receivedMessage);
                        }
                        else{
                            usedEncryption = "PT ";
                            ui->textBrowserReceivedData->append("\n\nRcvd12 "+usedEncryption+" "+encryptionKey+" "+checkSum.left(4)+" from "+sourceStation+" "+QDateTime::currentDateTimeUtc().toString("hhmmZ: ")+receivedMessage);
                            ui->textBrowserMachine->append("\n\nRcvd12 "+usedEncryption+" "+encryptionKey+" "+checkSum.left(4)+" from "+sourceStation+" "+QDateTime::currentDateTimeUtc().toString("hhmmZ: ")+receivedMessage);
                        }
                    }
                }//if single line message
                else {
//                    if(receivedMessage.contains("[MAC|")){
//                        ui->labelMessageAuthentication->show();
//                        if(ClassSignMessage::certifyMessage(receivedMessage)){
//                            ui->labelMessageAuthentication->setText("MAC: Authenticated");
//                            ui->labelMessageAuthentication->setStyleSheet(QString("color: rgb(0,255,0)"));
//                        }
//                        else {
//                            ui->labelMessageAuthentication->setText("MAC: Not Authenticated");
//                            ui->labelMessageAuthentication->setStyleSheet(QString("QLabel {color : red}"));
//                        }
//                    }
//                    else {
//                        ui->labelMessageAuthentication->hide();
//                    }
                    if(configSettings.generalSettings.ignoreMessagesNotForMe)
                    {
                        if(configSettings.generalSettings.callSignList.contains(destinationStation) ||
                                destinationStation.isEmpty() ||
                                destinationStation == "ALL")
                        {
                            //                            //update v2/v3 radio buttons on Machine Tab
                            //                            foreach (QString messageLine, receivedMessage.split("\n")) {
                            //                                if(messageLine.startsWith("MM VERSION: v2")){
                            //                                    ui->radioButtonv2Machine->setChecked(true);
                            //                                    on_radioButtonv2Machine_clicked(ui->radioButtonv2Machine->isChecked());
                            //                                }
                            //                            }//for each line in message
                            QString usedEncryption;
                            if(wasEncrypted){
                                usedEncryption = "CT ";
                                ui->textBrowserReceivedData->append(
                                            "\n\nRcvd13 "+usedEncryption+" "+encryptionKey+" "+checkSum.left(4)+
                                            " from "+sourceStation+" "+
                                            QDateTime::currentDateTimeUtc().toString("hhmmZ: \n")+
                                            receivedMessage);
                                ui->textBrowserMachine->append(
                                            "\n\nRcvd13 "+usedEncryption+" "+encryptionKey+" "+checkSum.left(4)+
                                            " from "+sourceStation+" "+
                                            QDateTime::currentDateTimeUtc().toString("hhmmZ: \n")+
                                            receivedMessage);
                            }
                            else{
                                usedEncryption = "PT ";
                                ui->textBrowserReceivedData->append(
                                            "\n\nRcvd14 "+usedEncryption+" "+encryptionKey+" "+checkSum.left(4)+
                                            " from "+sourceStation+" "+
                                            QDateTime::currentDateTimeUtc().toString("hhmmZ: \n")+
                                            receivedMessage);
                                ui->textBrowserMachine->append(
                                            "\n\nRcvd14 "+usedEncryption+" "+encryptionKey+" "+checkSum.left(4)+
                                            " from "+sourceStation+" "+
                                            QDateTime::currentDateTimeUtc().toString("hhmmZ: \n")+
                                            receivedMessage);
                            }
                        }//if message is for any of our callsigns
                    }//if ignore messages is on
                    else {
                        //                        //update v2/v3 radio buttons on Machine Tab
                        //                        foreach (QString messageLine, receivedMessage.split("\n")) {
                        //                            if(messageLine.startsWith("MM VERSION: v2")){
                        //                                ui->radioButtonv2Machine->setChecked(true);
                        //                                on_radioButtonv2Machine_clicked(ui->radioButtonv2Machine->isChecked());
                        //                            }
                        //                        }//for each line in message
                        QString usedEncryption;
                        if(wasEncrypted){
                            usedEncryption = "CT ";
                            ui->textBrowserReceivedData->append(
                                        "\n\nRcvd15 "+usedEncryption+" "+encryptionKey+" "+checkSum.left(4)+
                                        " from "+sourceStation+" "+
                                        QDateTime::currentDateTimeUtc().toString("hhmmZ: \n")+
                                        receivedMessage);
                            ui->textBrowserMachine->append(
                                        "\n\nRcvd15 "+usedEncryption+" "+encryptionKey+" "+checkSum.left(4)+
                                        " from "+sourceStation+" "+
                                        QDateTime::currentDateTimeUtc().toString("hhmmZ: \n")+
                                        receivedMessage);
                            //SEND TO FILE
                        }
                        else{
                            usedEncryption = "PT ";
                            ui->textBrowserReceivedData->append(
                                        "\n\nRcvd16 "+usedEncryption+" "+encryptionKey+" "+checkSum.left(4)+
                                        " from "+sourceStation+" "+
                                        QDateTime::currentDateTimeUtc().toString("hhmmZ: \n")+
                                        receivedMessage);
                            ui->textBrowserMachine->append(
                                        "\n\nRcvd16 "+usedEncryption+" "+encryptionKey+" "+checkSum.left(4)+
                                        " from "+sourceStation+" "+
                                        QDateTime::currentDateTimeUtc().toString("hhmmZ: \n")+
                                        receivedMessage);
                        }
                    }
                }//else multiline message
            }//if we have a message

            dataByteArray.clear();
            isBusy = false;
            return;
        }
        else {
            dataByteArray.clear();
            isBusy = false;
            return;
        }
    }//if we have data to process
    else{
        dataByteArray.clear();
        isBusy = false;
        return;
    }
}//slotHandleTCPData

void dlgterminal::handleTCPData(QByteArray directDataByteArray)
{
    static bool isBusy = false;
    if(isBusy)
        return;
    isBusy = true;

    handleTCPDataTimer.setInterval(tcpTimeout);

    if(directDataByteArray.size() > 0)
    {
        ui->textBrowserReceivedTCPData->append("Direct: "+directDataByteArray);

        ClassXML xmlClass(directDataByteArray);

        //v3Terminal does not use all the XML data so it is commented out
        bool wasEncrypted = xmlClass.getWasEncrypted();
        //        bool wasCompressed = xmlClass.getWasCompressed();
        QString positionID = xmlClass.getPositionID();
        QString command = xmlClass.getCommand();
        //        QString listenIPAddress = xmlClass.getListenIPAddress();
        //        QString listenIPPort = xmlClass.getListenTCPPort();
        //        QString priority = xmlClass.getPriority() ;
        QStringList keyList = xmlClass.getKeyList();
        keyList.append("PLAINTEXT");
        keyList.removeAll("");
        QString encryptionKey = xmlClass.getEncryptionKey();
        QStringList callsignList = xmlClass.getCallsignList();
        QString ALEAddress = xmlClass.getALEAddres();
        QStringList aleAddressList = xmlClass.getALEAddressList();
        QString sourceStation = xmlClass.getSourceStation();
        QString destinationStation = xmlClass.getDestinationStation();
        //        QString antennaBeamHeading = xmlClass.getAntennaBeamHeading();
        //        QString radioChannel = xmlClass.getRadioChannel();
        //        QString radioFrequency = xmlClass.getRadioFrequency();
        //        QString radioOPMode = xmlClass.getRadioOPMode();
        //        QString radioTXMode = xmlClass.getRadioTXMode();
        QString modemDataRate = xmlClass.getModemDataRate();
        QString modemInterleave = xmlClass.getModemInterleave();
        QString currentModemSpeed = ui->comboBoxModemSpeed->currentText();
        QString currentModemInterleave = ui->comboBoxModemInterleave->currentText();
        QString wasAuthenticated = xmlClass.getWasAuthenticated();
        QString authPassed = xmlClass.getAuthPassed();
        QString authAgency = xmlClass.getAuthAgency();

        if(command == "config"){
            QStringList modemDataRateList = modemDataRate.split(',');
            ui->comboBoxModemSpeed->clear();
            ui->comboBoxModemSpeed->addItems(modemDataRateList);
            ui->comboBoxModemSpeed->setCurrentText(currentModemSpeed);
            ui->comboBoxModemInterleave->clear();
            ui->comboBoxModemInterleave->addItems(modemInterleave.split(','));
            ui->comboBoxModemInterleave->setCurrentText(currentModemInterleave);
            ui->comboBoxModemSpeed->setCurrentText(currentModemSpeed);
            ui->comboBoxModemInterleave->setCurrentText(currentModemInterleave);
        }
        else if (command == "status") {
            /*
                <V3PROTOCOL>
                    <HEADER>
                        <POSITIONID>CP_KNWD</POSITIONID>
                        <COMMAND>status</COMMAND>
                        <MODEMDATARATE>600</MODEMDATARATE>
                        <MODEMINTERLEAVE>L</MODEMINTERLEAVE>
                        <MODEMWAVEFORM>MS110A</MODEMWAVEFORM>
                    </HEADER>
                </V3PROTOCOL>
            */

            if(settingsTermVars.TrackRMI){
                ui->comboBoxModemSpeed->setCurrentText(modemDataRate);
                ui->comboBoxModemInterleave->setCurrentText(modemInterleave);
            }
            else {
                ui->comboBoxModemSpeed->setCurrentText(currentModemSpeed);
                ui->comboBoxModemInterleave->setCurrentText(currentModemInterleave);
            }
            if(!ALEAddress.isEmpty()){
                ui->comboBoxALEStations->setCurrentText(ALEAddress);
            }
            else {
                ui->comboBoxALEStations->setCurrentText("");
            }
        }
        //        QString modemWaveform = xmlClass.getModemWaveform();
        QString checkSum = xmlClass.getChecksum();
        QString receivedMessage = xmlClass.getReceivedMessage();

        if(xmlClass.getParsingError()){
            ui->textBrowserReceivedData->append("ERROR: Parsing TCP XML document!");
            ui->textBrowserMachine->append("ERROR: Parsing TCP XML document!");
        }
        if(configSettings.generalSettings.ignoreMessagesNotForMe)
        {
            if(!configSettings.generalSettings.callSignList.contains(destinationStation) &&
                    !destinationStation.isEmpty() &&
                    destinationStation != "ALL")
            {
                dataByteArray.clear();
                isBusy = false;
                return;
            }
        }

        if(command == "ack"){
            if(!configSettings.generalSettings.ignoreACKMessages){
                //                ui->textBrowserReceivedData->append("ACK: "+positionID+" - "+receivedMessage);
                ui->textBrowserReceivedData->append("ACK: "+positionID);
                ui->textBrowserMachine->append("ACK: "+positionID);
                //                ui->textBrowserMachine->append("ACK: "+positionID+" - "+receivedMessage);
            }
            dataByteArray.clear();
            isBusy = false;
            return;
        }
        else if (command=="config") {
            if(tcpSocket->isOpen()){
                //populate cipher key combo box
                ui->comboBoxKeyList->clear();
                ui->comboBoxKeyList->addItems(keyList);
                ui->comboBoxConfKeyList->clear();
                ui->comboBoxConfKeyList->addItems(keyList);
                ui->comboBoxKeyList->setCurrentText(configSettings.generalSettings.keyFile);
                //populate ale address combo box
                ui->comboBoxALEStations->clear();
                ui->comboBoxALEStations->addItems(aleAddressList);
                //populate callsign combo box
                foreach (QString tempString, callsignList) {
                    if(ui->comboBoxRecipient->findText(tempString) == -1){
                        ui->comboBoxRecipient->addItem(tempString);
                    }
                }
            }//if tcpSocket is open
            dataByteArray.clear();
            isBusy = false;
            return;
        }//if config
        else if (command == "status") {
            dataByteArray.clear();
            isBusy = false;
            return;
        }
        else if (command == "error") {
            ui->textBrowserReceivedData->append(QDateTime::currentDateTimeUtc().toString("hhmmZ: ")+positionID+" - "+receivedMessage);
            ui->textBrowserMachine->append(QDateTime::currentDateTimeUtc().toString("hhmmZ: ")+positionID+" - "+receivedMessage);
            dataByteArray.clear();
            isBusy = false;
            return;
        }
        else if (command == "data") {
            if(!receivedMessage.isEmpty()){
                if((bool)wasAuthenticated.toInt()){
                    ui->labelMessageAuthentication->show();
                    if((bool)authPassed.toInt()){
//                        ui->labelMessageAuthentication->setText("MAC: PASS - "+authAgency);
                        ui->labelMessageAuthentication->setText("MAC: PASS");
                        ui->labelMessageAuthentication->setStyleSheet("color: rgb(0,255,0)");
                    }
                    else {
                        ui->labelMessageAuthentication->setText("MAC: FAIL");
                        ui->labelMessageAuthentication->setStyleSheet("color : red");
                    }
                }//if was authenticated
                else {
                    ui->labelMessageAuthentication->hide();
                    ui->labelMessageAuthentication->setText("MAC:");
                    ui->labelMessageAuthentication->setStyleSheet("color : black");
                }//else was not authenticated

                if(ui->comboBoxALEStations->findText(ALEAddress) == -1){
                    ui->comboBoxALEStations->addItem(ALEAddress);
                }
                if(ui->comboBoxRecipient->findText(sourceStation) == -1){
                    ui->comboBoxRecipient->addItem(sourceStation);
                }
                if(configSettings.generalSettings.ignoreMessagesNotForMe)
                {
                    if(configSettings.generalSettings.callSignList.contains(destinationStation) ||
                            destinationStation.isEmpty() ||
                            destinationStation == "ALL")
                    {
                        setChecksumLabel(checkSum);
                    }//if message is for me
                }//if I have ignore messages on
                else {
                    setChecksumLabel(checkSum);
                }//else show all messages
                //single line chat message - does not contain a newline character
                if(!receivedMessage.contains('\n')){
                    if(configSettings.generalSettings.ignoreMessagesNotForMe)
                    {
                        if(configSettings.generalSettings.callSignList.contains(destinationStation) ||
                                destinationStation.isEmpty() ||
                                destinationStation == "ALL")
                        {
                            QString usedEncryption;
                            if(wasEncrypted){
                                usedEncryption = "CT ";
                                QString R = "";
                                if (settingsTermVars.dbgRcvdcount != true) {R = "\n\nRcvd ";} else {R = "\n\nRcvd1 ";}
                                ui->textBrowserReceivedData->append(R+usedEncryption+encryptionKey+" "+checkSum.left(4)+" from "+sourceStation+" "+QDateTime::currentDateTimeUtc().toString("hhmmZ: ")+receivedMessage);
                                ui->textBrowserMachine->append(R+usedEncryption+encryptionKey+" "+checkSum.left(4)+" from "+sourceStation+" "+QDateTime::currentDateTimeUtc().toString("hhmmZ: ")+receivedMessage);
                            }
                            else{
                                usedEncryption = "PT ";
                                QString R = "";
                                if (settingsTermVars.dbgRcvdcount != true) {R = "\n\nRcvd ";} else {R = "\n\nRcvd2 ";}
                                ui->textBrowserReceivedData->append(R+
                                                                    usedEncryption+encryptionKey+" "+checkSum.left(4)+
                                                                    " from "+sourceStation+" "+
                                                                    QDateTime::currentDateTimeUtc().toString("hhmmZ: ")+
                                                                    receivedMessage);
                                ui->textBrowserMachine->append(R+
                                                               usedEncryption+encryptionKey+" "+checkSum.left(4)+
                                                               " from "+sourceStation+" "+
                                                               QDateTime::currentDateTimeUtc().toString("hhmmZ: ")+
                                                               receivedMessage);
                            }
                        }//if message is for any of our callsigns
                    }//if ignore message not for me
                    else {
                        QString usedEncryption;
                        if(wasEncrypted){
                            usedEncryption = "CT ";
                            QString R = "";
                            if (settingsTermVars.dbgRcvdcount != true) {R = "\n\nRcvd ";} else {R = "\n\nRcvd3 ";}
                            ui->textBrowserReceivedData->append(R+usedEncryption+encryptionKey+" "+checkSum.left(4)+" from "+sourceStation+" "+QDateTime::currentDateTimeUtc().toString("hhmmZ: ")+receivedMessage);
                            ui->textBrowserMachine->append(R+usedEncryption+encryptionKey+" "+checkSum.left(4)+" from "+sourceStation+" "+QDateTime::currentDateTimeUtc().toString("hhmmZ: ")+receivedMessage);
                        }
                        else{
                            usedEncryption = "PT ";
                            QString R = "";
                            if (settingsTermVars.dbgRcvdcount != true) {R = "\n\nRcvd ";} else {R = "\n\nRcvd4 ";}
                            ui->textBrowserReceivedData->append(R+
                                                                usedEncryption+encryptionKey+" "+
                                                                checkSum.left(4)+" from "+
                                                                sourceStation+" "+
                                                                QDateTime::currentDateTimeUtc().toString("hhmmZ: ")+
                                                                receivedMessage);
                            ui->textBrowserMachine->append(R+
                                                           usedEncryption+encryptionKey+" "+checkSum.left(4)+" from "+
                                                           sourceStation+" "+
                                                           QDateTime::currentDateTimeUtc().toString("hhmmZ: ")+
                                                           receivedMessage+"</pre></font>");
                        }
                    }
                }//if single line message
                else {
//                    if(receivedMessage.contains("[MAC|")){
//                        ui->labelMessageAuthentication->show();
//                        if(ClassSignMessage::certifyMessage(receivedMessage)){
//                            ui->labelMessageAuthentication->setText("MAC: Authenticated");
//                            ui->labelMessageAuthentication->setStyleSheet(QString("color: rgb(0,255,0)"));
//                        }
//                        else {
//                            ui->labelMessageAuthentication->setText("MAC: Not Authenticated");
//                            ui->labelMessageAuthentication->setStyleSheet(QString("QLabel {color : red}"));
//                        }
//                    }
//                    else {
//                        ui->labelMessageAuthentication->hide();
//                    }
                    if(configSettings.generalSettings.ignoreMessagesNotForMe)
                    {
                        if(configSettings.generalSettings.callSignList.contains(destinationStation) ||
                                destinationStation.isEmpty() ||
                                destinationStation == "ALL")
                        {
                            //                            //update v2/v3 radio buttons on Machine Tab
                            //                            foreach (QString messageLine, receivedMessage.split("\n")) {
                            //                                if(messageLine.startsWith("MM VERSION: v2")){
                            //                                    ui->radioButtonv2Machine->setChecked(true);
                            //                                    on_radioButtonv2Machine_clicked(ui->radioButtonv2Machine->isChecked());
                            //                                }
                            //                            }//for each line in message
                            QString usedEncryption;
                            if(wasEncrypted){
                                usedEncryption = "CT ";
                                QString R = "";
                                if (settingsTermVars.dbgRcvdcount != true) {R = "\n\nRcvd ";} else {R = "\n\nRcvd5 ";}
                                ui->textBrowserReceivedData->append(
                                            R+
                                            usedEncryption+encryptionKey+" "+checkSum.left(4)+
                                            " from "+sourceStation+" "+
                                            QDateTime::currentDateTimeUtc().toString("hhmmZ: \n")+
                                            receivedMessage);
                                ui->textBrowserMachine->append(
                                            R+
                                            usedEncryption+encryptionKey+" "+checkSum.left(4)+
                                            " from "+sourceStation+" "+
                                            QDateTime::currentDateTimeUtc().toString("hhmmZ: \n")+
                                            receivedMessage);
                                writeRcvdToFile(R, usedEncryption, encryptionKey, checkSum.left(4), sourceStation, QDateTime::currentDateTimeUtc().toString("hhmmZ: "), receivedMessage);
                            }
                            else{
                                usedEncryption = "PT ";
                                QString R = "";
                                if (settingsTermVars.dbgRcvdcount != true) {R = "\n\nRcvd ";} else {R = "\n\nRcvd6 ";}
                                ui->textBrowserReceivedData->append(
                                            R+
                                            usedEncryption+encryptionKey+" "+checkSum.left(4)+
                                            " from "+sourceStation+" "+
                                            QDateTime::currentDateTimeUtc().toString("hhmmZ: \n")+
                                            receivedMessage);
                                ui->textBrowserMachine->append(
                                            R+
                                            usedEncryption+encryptionKey+" "+checkSum.left(4)+
                                            " from "+sourceStation+" "+
                                            QDateTime::currentDateTimeUtc().toString("hhmmZ: \n")+
                                            receivedMessage);
                            }
                        }//if message is for any of our callsigns
                    }//if ignore messages is on
                    else {
                        QString usedEncryption;
                        if(wasEncrypted){
                            usedEncryption = "CT ";
                            QString R = "";
                            if (settingsTermVars.dbgRcvdcount != true) {R = "\n\nRcvd ";} else {R = "\n\nRcvd7 ";}
                            ui->textBrowserReceivedData->append(
                                        R+
                                        usedEncryption+encryptionKey+" "+checkSum.left(4)+
                                        " from "+sourceStation+" "+
                                        QDateTime::currentDateTimeUtc().toString("hhmmZ: \n")+
                                        receivedMessage);
                            ui->textBrowserMachine->append(
                                        R+
                                        usedEncryption+encryptionKey+" "+checkSum.left(4)+
                                        " from "+sourceStation+" "+
                                        QDateTime::currentDateTimeUtc().toString("hhmmZ: \n")+
                                        receivedMessage);
                            writeRcvdToFile(R, usedEncryption, encryptionKey, checkSum.left(4), sourceStation, QDateTime::currentDateTimeUtc().toString("hhmmZ: "), receivedMessage);
                        }
                        else{
                            usedEncryption = "PT ";
                            QString R = "";
                            if (settingsTermVars.dbgRcvdcount != true) {R = "\n\nRcvd ";} else {R = "\n\nRcvd8 ";}
                            ui->textBrowserReceivedData->append(
                                        R+
                                        usedEncryption+encryptionKey+" "+checkSum.left(4)+
                                        " from "+sourceStation+" "+
                                        QDateTime::currentDateTimeUtc().toString("hhmmZ: \n")+
                                        receivedMessage);
                            ui->textBrowserMachine->append(
                                        R+
                                        usedEncryption+encryptionKey+" "+checkSum.left(4)+
                                        " from "+sourceStation+" "+
                                        QDateTime::currentDateTimeUtc().toString("hhmmZ: \n")+
                                        receivedMessage);
                            writeRcvdToFile(R, usedEncryption, encryptionKey, checkSum.left(4), sourceStation, QDateTime::currentDateTimeUtc().toString("hhmmZ: "), receivedMessage);
                        }
                    }
                }//else multiline message

                if(configSettings.generalSettings.callSignList.contains(destinationStation) ||
                        destinationStation.endsWith("-CN"))
                {
                    QString usedEncryption;
                    if(wasEncrypted){
                        usedEncryption = "CT ";
                    }
                    else {
                        usedEncryption = "PT ";
                    }

                    if(destinationStation.endsWith("-CN"))
                    {
                        //if the conference is not in our list
                        if(!configSettings.conferenceSettings.conferenceMap.keys().contains(destinationStation.toUpper()))
                        {
                            QStringList conferenceList;
                            conferenceList.append(sourceStation.toUpper());
                            configSettings.conferenceSettings.conferenceMap.insert(destinationStation,conferenceList);
                            writeSettings();
                        }
                        else {
                            QStringList callsignList = configSettings.conferenceSettings.conferenceMap.value(destinationStation).toStringList();
                            //if the callsign is not listed in the conference
                            if(!callsignList.contains(sourceStation)){
                                callsignList.append(sourceStation);
                                configSettings.conferenceSettings.conferenceMap.insert(destinationStation, callsignList);
                                writeSettings();
                            }
                        }

                        QString chatString = configSettings.conferenceChats.ChatText.value(destinationStation).toString();
                        chatString.append("\nRcvd "+usedEncryption+checkSum.left(4)+
                                          QDateTime::currentDateTimeUtc().toString(" yyyyMMMdd_hhmmZ: ")+
                                          receivedMessage);
                        configSettings.conferenceChats.ChatText.insert(destinationStation, chatString);

                        if(chattingWith == destinationStation){
                            updatetextBrowserSentData();
                        }//if chatting in conference
                        else{
                            //we have a new chat
                            isNewChat.insert(destinationStation,true);
                            //udate the conference sidebar
                        }
                    }//if the message is a conference message
                    else if(configSettings.generalSettings.callSignList.contains(destinationStation)){
                        QString chatString = configSettings.individualChats.ChatText.value(sourceStation).toString();
                        chatString.append("\nRcvd "+usedEncryption+checkSum.left(4)+
                                          QDateTime::currentDateTimeUtc().toString(" yyyyMMMdd_hhmmZ: ")+
                                          receivedMessage);
                        configSettings.individualChats.ChatText.insert(sourceStation, chatString);
                        if(chattingWith == sourceStation){
                            updatetextBrowserSentData();
                        }
                        else{
                            //we have a new chat
                            isNewChat.insert(sourceStation,true);
                            //udate the conference sidebar
                            updateConferenceSidebar();
                        }
                    }//if addressed to my callsign
                }//if message is to me or is a conference message
                else if (destinationStation == "ALL" && (1==2))
                {
                    QString chatString;
                    QString usedEncryption;
                    if(wasEncrypted){
                        usedEncryption = "CT ";
                    }
                    else {
                        usedEncryption = "PT ";
                    }
                    foreach (QString conference, configSettings.conferenceSettings.conferenceMap.keys()) {
                        chatString = configSettings.conferenceChats.ChatText.value(conference).toString();
                        chatString.append("\nRcvd_ALL "+usedEncryption+checkSum.left(4)+
                                          QDateTime::currentDateTimeUtc().toString("yyyyMMMdd_hhmmZ: ")+receivedMessage);
                        configSettings.conferenceChats.ChatText.insert(conference,chatString);
                    }//for each conference room add message to each conference chat
                }//if message is addressed to ALL
            }//if we have a message
            dataByteArray.clear();
            isBusy = false;
            return;
        }
        else {
            dataByteArray.clear();
            isBusy = false;
            return;
        }
    }//if we have data to process
    else{
        dataByteArray.clear();
        isBusy = false;
        return;
    }//else not data
}//handleTCPData

void dlgterminal::slotTCPSocketDisconnected()
{
    ui->actionSend_File->setEnabled(false);
    showStatusMessage("   DISCONNECTED!");
    ui->comboBoxKeyList->clear();
    ui->comboBoxKeyList->addItem("PLAINTEXT");
    ui->comboBoxConfKeyList->clear();
    ui->comboBoxConfKeyList->addItem("PLAINTEXT");
//    setApplicationTitle("DISCONNECTED");
}//slotTCPSocketDisconnected

void dlgterminal::slotSayHelo()
{
    slotBroadcastID("helo");
}//slotSayHelo

void dlgterminal::slotBroadcastID(QByteArray type)
{
    QUdpSocket localUdpSocket;
    ClassXML xmlClass;
    QByteArray datagram = xmlClass.wrap(type,"",true, configSettings.generalSettings.positionID,
                                        ui->comboBoxALEStations->currentText(),
                                        QString::number((int)ui->checkBoxCompress->isChecked()),
                                        QString::number((int)!(ui->comboBoxKeyList->currentText() == "PLAINTEXT")),
                                        ui->comboBoxKeyList->currentText(),
                                        configSettings.generalSettings.callSignList.at(0).trimmed(),
                                        ui->comboBoxRecipient->currentText().trimmed(),
                                        "",
                                        "");
    QList<QNetworkInterface> interfaceList = QNetworkInterface::allInterfaces();
    foreach (QNetworkInterface interfaces, interfaceList) {
        foreach (QNetworkAddressEntry address, interfaces.addressEntries()) {
            localUdpSocket.writeDatagram(datagram.data(), datagram.size(),
                                         address.broadcast(), configSettings.generalSettings.applicationDiscoveryPort.toInt());            }
    }
}//slotBroadcastID

void dlgterminal::BroadcastChat(QByteArray message, QString Recipient)
{
    QUdpSocket localUdpSocket;
    ClassXML xmlClass;

    QByteArray datagram = xmlClass.wrap("data",message,false, configSettings.generalSettings.positionID,
                                        ui->comboBoxALEStations->currentText(),
                                        "0",
                                        QString::number((int)!(ui->comboBoxKeyList->currentText() == "PLAINTEXT")),
                                        ui->comboBoxKeyList->currentText(),
                                        configSettings.generalSettings.callSignList.at(0).trimmed(),
                                        Recipient.trimmed(),
                                        "",
                                        "");
    QList<QNetworkInterface> interfaceList = QNetworkInterface::allInterfaces();
    foreach (QNetworkInterface interfaces, interfaceList) {
        foreach (QNetworkAddressEntry address, interfaces.addressEntries()) {
            localUdpSocket.writeDatagram(datagram.data(), datagram.size(),
                                         address.broadcast(), configSettings.generalSettings.applicationDiscoveryPort.toInt());            }
    }
}//BroadcastChat

void dlgterminal::slotReadUdpData()
{
    //    qDebug() << "slotReadUdpData()";
    QByteArray udpData;
    while (udpListenSocket->hasPendingDatagrams()) {
        udpData.resize(udpListenSocket->pendingDatagramSize());
        udpListenSocket->readDatagram(udpData.data(), udpData.size());
        //                qDebug() << udpData;
        if(!udpData.isEmpty()){
            slotHandleUDPData(udpData);
        }//if datagram was not empty... process it
    }//while we have datagrams... read them
}//slotReadUdpData

void dlgterminal::slotHandleUDPData(QByteArray UDPData)
{
    ui->textBrowserReceivedUDPData->append(UDPData);

    ClassXML xmlClass(UDPData);
    //v3Terminal does not use all the XML data so it is commented out
    bool wasEncrypted = xmlClass.getWasEncrypted();
    //    bool wasCompressed = xmlClass.getWasCompressed();
    QString positionID = xmlClass.getPositionID();
    QString command = xmlClass.getCommand();
    QString listenIPAddress = xmlClass.getListenIPAddress();
    QString listenTCPPort = xmlClass.getListenTCPPort();
    //    QString priority = xmlClass.getPriority() ;
    //    QStringList keyList = xmlClass.getKeyList();
    //    QString ALEAddress = xmlClass.getALEAddres();
    //    QStringList aleAddressList = xmlClass.getALEAddressList();
    QString sourceStation = xmlClass.getSourceStation();
    QString destinationStation = xmlClass.getDestinationStation();
    //    QString antennaBeamHeading = xmlClass.getAntennaBeamHeading();
    //    QString radioChannel = xmlClass.getRadioChannel();
    //    QString radioFrequency = xmlClass.getRadioFrequency();
    //    QString radioOPMode = xmlClass.getRadioOPMode();
    //    QString radioTXMode = xmlClass.getRadioTXMode();
    //    QString modemDataRate = xmlClass.getModemDataRate();
    //    QString modemInterleave = xmlClass.getModemInterleave();
    //    QString modemWaveform = xmlClass.getModemWaveform();
    QString checkSum = xmlClass.getChecksum();
    QString receivedMessage = xmlClass.getReceivedMessage();
    if(xmlClass.getParsingError()){
        ui->textBrowserReceivedData->append("ERROR: Parsing UDP XML document!");
        ui->textBrowserMachine->append("ERROR: Parsing UDP XML document!");
    }//if parsing error

    if((command == "helo" || command == "update") //if helo or update
            && positionID != configSettings.generalSettings.positionID) //and positionID is not mine
    {
        if(!listenIPAddress.isEmpty() && !listenTCPPort.isEmpty())
        {
            tcpConnections.insert(positionID, listenIPAddress+":"+listenTCPPort);
            QString currentConnection = ui->comboBoxTCPConnections->currentText();
            ui->comboBoxTCPConnections->clear();
            ui->comboBoxTCPConnections->addItems(tcpConnections.keys());
            ui->comboBoxTCPConnections->setCurrentText(currentConnection);
            if(tcpSocket->peerAddress().toString().isEmpty()){
                if(positionID == configSettings.generalSettings.defaultRMI){
                    QHostAddress address;
                    address.setAddress(listenIPAddress);

                    tcpSocket->connectToHost(address,listenTCPPort.toInt());
                    if(tcpSocket->waitForConnected()){
                        showStatusMessage("Connected to "+positionID+
                                          " - "+tcpSocket->peerAddress().toString()+
                                          " : "+QString::number(tcpSocket->peerPort()));
//                        setApplicationTitle(positionID);
                        ui->actionSend_File->setEnabled(true);
                        ui->comboBoxTCPConnections->setCurrentText(positionID);
                    }//if socket opened
                    else{
                        ui->textBrowserReceivedData->append(tcpSocket->errorString());
                        ui->textBrowserMachine->append(tcpSocket->errorString());
                        showStatusMessage("Connection error - "+tcpSocket->errorString());
//                        setApplicationTitle("ERR");
                        ui->actionSend_File->setEnabled(false);
                    }//else connection error
                }//if positionID is our defaultRMI
            }
        }//if we're not already connected
    }//if helo or update and positionID is not us
    else if (command == "bye") {
        tcpConnections.remove(positionID);
        QString currentConnection = ui->comboBoxTCPConnections->currentText();
        ui->comboBoxTCPConnections->clear();
        ui->comboBoxTCPConnections->addItems(tcpConnections.keys());
        ui->comboBoxTCPConnections->setCurrentText(currentConnection);
        //        setApplicationTitle("NONE");
    }//else if bye
    else if((command == "data") && positionID != configSettings.generalSettings.positionID) //and positionID is not mine
    {
        if(configSettings.generalSettings.callSignList.contains(destinationStation) ||
                destinationStation.endsWith("-CN"))
        {
            QString usedEncryption;
            if(wasEncrypted){
                usedEncryption = "CT ";
            }
            else {
                usedEncryption = "PT ";
            }
            if(destinationStation.endsWith("-CN"))
            {
                //if the conference is not in our list
                if(!configSettings.conferenceSettings.conferenceMap.keys().contains(destinationStation.toUpper()))
                {
                    QStringList conferenceList;
                    conferenceList.append(sourceStation.toUpper());
                    configSettings.conferenceSettings.conferenceMap.insert(destinationStation,conferenceList);
                    writeSettings();
                }
                else {
                    QStringList callsignList = configSettings.conferenceSettings.conferenceMap.value(destinationStation).toStringList();
                    //if the callsign is not listed in the conference
                    if(!callsignList.contains(sourceStation)){
                        callsignList.append(sourceStation);
                        configSettings.conferenceSettings.conferenceMap.insert(destinationStation, callsignList);
                        writeSettings();
                    }
                }

                QString chatString = configSettings.conferenceChats.ChatText.value(destinationStation).toString();
                chatString.append("\nRcvd_UDP "+usedEncryption+checkSum.left(4)+
                                  QDateTime::currentDateTimeUtc().toString(" yyyyMMMdd_hhmmZ: ")+
                                  receivedMessage);
                configSettings.conferenceChats.ChatText.insert(destinationStation, chatString);

                if(chattingWith == destinationStation){
                    updatetextBrowserSentData();
                }//if chatting in conference
                else{
                    //we have a new chat
                    isNewChat.insert(destinationStation,true);
                    //udate the conference sidebar
                }
            }//if the message is a conference message
            else if( configSettings.generalSettings.callSignList.contains(destinationStation) )
            {
                QString chatString = configSettings.individualChats.ChatText.value(sourceStation).toString();
                chatString.append("\nRcvd_UDP "+usedEncryption+checkSum.left(4)+
                                  QDateTime::currentDateTimeUtc().toString(" yyyyMMMdd_hhmmZ: ")+
                                  receivedMessage);
                configSettings.individualChats.ChatText.insert(sourceStation, chatString);
                if(chattingWith == sourceStation){
                    updatetextBrowserSentData();
                }
                else{
                    //we have a new chat
                    isNewChat.insert(sourceStation,true);
                    //udate the conference sidebar
                }
            }//if addressed to my callsign
            updateConferenceSidebar();
        }//if message is to me or is a conference message
    }
}//slotHandleUDPData

void dlgterminal::sendDataTCPSocket()
{
    /***************************************************
     * TCP IP
     * ************************************************/
    while (messageQueue.size() > 0) {
        if(tcpSocket->isOpen()){
            //                qDebug() << messageQueue.first();
            tcpSocket->write(messageQueue.first());
            while (tcpSocket->bytesToWrite() > 0) {
                tcpSocket->flush();
            }//while bytes to write

            //if display Tx Data
            if(configSettings.generalSettings.showTXData)
            {
                QString useEncryption;
                if(!(ui->comboBoxKeyList->currentText() == "PLAINTEXT")){
                    useEncryption = "CT";
                    ui->textBrowserReceivedData->append("\nSent "+
                                                        useEncryption+" to "+ui->comboBoxRecipient->currentText().trimmed()+": "+
                                                        printables(lastSentMessage));
                }
                else{
                    useEncryption = "PT";
                    ui->textBrowserReceivedData->append("\nSent "+
                                                        useEncryption+" to "+ui->comboBoxRecipient->currentText().trimmed()+": "+
                                                        printables(lastSentMessage));
                }
            }//if display Tx Data

            messageQueue.pop_front();
            //            ui->pushButtonResend->setEnabled(true);
            //            ui->lineEditChatText->clear();
            //            ui->lineEditInstructions->clear();
        }//if tcp socket is open
        else
            return;
    }//while we have messages to send
}//writeData

void dlgterminal::initActionsConnections()
{
    connect(ui->actionQuit, &QAction::triggered, this, &dlgterminal::close);
    ui->actionQuit->setEnabled(true);
    ui->pushButtonSendTx->setEnabled(false);
    ui->actionSend_File->setEnabled(false);
//    ui->menuHelp->addAction(QObject::tr("About Qt"), qApp, SLOT(aboutQt()));
}//initActionsConnections

void dlgterminal::showStatusMessage(const QString &message)
{
    ui->labelConnectionStatus->setText(message);
}//showStatusMessage

void dlgterminal::writeSettings()
{
    if (DebugFunctions > 1) {qDebug() << Q_FUNC_INFO;}
    QString fileName = "/home/user/MSC/StationMapper/Terminal.ini";
    QSettings settings(fileName,QSettings::IniFormat, this);
    settings.clear();
    settings.beginGroup("Application");
    {
        //save dlgterminal size and position
        settings.setValue("size", size());
        settings.setValue("pos", pos());
        settings.setValue("splitterTerminalTab",ui->splitter->saveState());
        settings.setValue("splitterChatHorz",ui->splitterChatHorz->saveState());
        settings.setValue("splitterChatVert",ui->splitterChatVert->saveState());
    }
    settings.endGroup();
    settings.beginGroup("General");
    {
        settings.setValue("callsignList",configSettings.generalSettings.callSignList);
        settings.setValue("positionID",configSettings.generalSettings.positionID);
        settings.setValue("defaultRMI", configSettings.generalSettings.defaultRMI);
        settings.setValue("ignoreMessagesNotForMe",configSettings.generalSettings.ignoreMessagesNotForMe);
        settings.setValue("ignoreACKMessages",configSettings.generalSettings.ignoreACKMessages);
        settings.setValue("showTXData",configSettings.generalSettings.showTXData);
        settings.setValue("heardCallSignList", configSettings.generalSettings.heardCallSignList);
        settings.setValue("includeSalutation", configSettings.generalSettings.includeSalutation);
        settings.setValue("applicationDiscoveryPort", configSettings.generalSettings.applicationDiscoveryPort);
        settings.setValue("keyFile", configSettings.generalSettings.keyFile);
        settings.setValue("machineAddressList",configSettings.generalSettings.machineAddressList);
        settings.setValue("styleSheetFilename",configSettings.generalSettings.styleSheetFilename);
        settings.setValue("loadStyleSheet",configSettings.generalSettings.loadStyleSheet);
        //settings.setValue("trackRMIChanges",configSettings.generalSettings.trackRMIChanges);
        settings.setValue("hideALE",configSettings.generalSettings.hideALE);
        settings.setValue("hideConnections",configSettings.generalSettings.hideConnections);
        settings.setValue("hideModem",configSettings.generalSettings.hideModem);
        settings.setValue("hideProcessing",configSettings.generalSettings.hideProcessing);
        settings.setValue("performLineWrap",configSettings.generalSettings.performLineWrap);
        settings.setValue("lineLength",configSettings.generalSettings.lineLength);
        configSettings.generalSettings.cmboChatLAN = ui->cmboChatLAN->currentText();
        settings.setValue("cmboChatLAN",configSettings.generalSettings.cmboChatLAN);
        //cmboChatLAN

        //        if(configSettings.generalSettings.bypassInternetCheck){
        //            settings.setValue("bypassInternetCheck",configSettings.generalSettings.bypassInternetCheck);
        //        }
    }
    settings.endGroup();
    settings.beginGroup("ConferenceSettings");
    {
        settings.setValue("conferenceMap",configSettings.conferenceSettings.conferenceMap);
        settings.setValue("messageExpireTime",configSettings.conferenceSettings.messageExpireTime);
        settings.setValue("expireCheckTime",configSettings.conferenceSettings.expireCheckTime);
        settings.setValue("defaultCipherKey",configSettings.conferenceSettings.defaultCipherKey);
    }
    settings.endGroup();

    fileName = "/home/user/MSC/StationMapper/conferencechats.ini";
    QSettings settingsconfchat(fileName,QSettings::IniFormat, this);
    settingsconfchat.clear();

    settingsconfchat.beginGroup("ConferenceChats");
    {
        settingsconfchat.setValue("ConferenceChatsText",configSettings.conferenceChats.ChatText);
    }
    settingsconfchat.endGroup();


    fileName = "/home/user/MSC/StationMapper/individualchats.ini";
    QSettings settingsindvchat(fileName,QSettings::IniFormat, this);
    settingsindvchat.clear();

    settingsindvchat.beginGroup("IndividualChats");
    {
        settingsindvchat.setValue("IndividualChatsText",configSettings.individualChats.ChatText);
    }
    settingsindvchat.endGroup();
}//writeSettings

void dlgterminal::readSettings()
{
    if (DebugFunctions > 1) {qDebug() << Q_FUNC_INFO;}
    QString fileName = "/home/user/MSC/StationMapper/Terminal.ini";

    QSettings settings(fileName,QSettings::IniFormat, this);

    /*******************************************************************************************************************************/
    settings.beginGroup("Application");
    {
        ui->splitterChatHorz->restoreState(settings.value("splitterChatHorz").toByteArray());
        ui->splitterChatVert->restoreState(settings.value("splitterChatVert").toByteArray());
        ui->splitter->restoreState(settings.value("splitterTerminalTab").toByteArray());
    }
    settings.endGroup();
    settings.beginGroup("General");{
        configSettings.generalSettings.callSignList = settingsTermVars.GuardedCallSigns.split(" ");
        configSettings.generalSettings.positionID = settingsTermVars.PositionID;
        configSettings.generalSettings.defaultRMI = settings.value("defaultRMI","").toString();
        configSettings.generalSettings.ignoreMessagesNotForMe = settingsTermVars.IgnoreNotforMe;
        configSettings.generalSettings.ignoreACKMessages = settingsTermVars.IgnoreNotforMe;

        configSettings.generalSettings.showTXData = settings.value("showTXData",true).toBool();
        configSettings.generalSettings.heardCallSignList = settings.value("heardCallSignList","ALL").toStringList();
        configSettings.generalSettings.includeSalutation = settings.value("includeSalutation",false).toBool();
        configSettings.generalSettings.applicationDiscoveryPort = settings.value("applicationDiscoveryPort", "5000").toString();
        configSettings.generalSettings.keyFile = settings.value("keyFile").toString();
        configSettings.generalSettings.machineAddressList = settings.value("machineAddressList","").toStringList();
        configSettings.generalSettings.styleSheetFilename = settings.value("styleSheetFilename","").toString();
        configSettings.generalSettings.loadStyleSheet = settings.value("loadStyleSheet",false).toBool();
        //configSettings.generalSettings.trackRMIChanges = settings.value("trackRMIChanges",false).toBool();
        configSettings.generalSettings.hideALE = settings.value("hideALE",true).toBool();
        configSettings.generalSettings.hideConnections = settings.value("hideConnections",false).toBool();
        configSettings.generalSettings.hideModem = settings.value("hideModem",false).toBool();
        configSettings.generalSettings.hideProcessing = settings.value("hideProcessing",false).toBool();
        configSettings.generalSettings.performLineWrap = settings.value("performLineWrap",true).toBool();
        configSettings.generalSettings.lineLength = settings.value("lineLength",69).toInt();
        configSettings.generalSettings.cmboChatLAN = settings.value("cmboChatLAN","TCP & UDP").toString();
    }
    settings.endGroup();
    settings.beginGroup("ConferenceSettings");
    {
        configSettings.conferenceSettings.conferenceMap = settings.value("conferenceMap").toMap();
//        qDebug() << configSettings.conferenceSettings.conferenceMap.size();
        configSettings.conferenceSettings.messageExpireTime = settings.value("messageExpireTime",1).toInt();
        configSettings.conferenceSettings.expireCheckTime = settings.value("expireCheckTime",15).toInt();
        //        chatPasswords = settings.value("passwordList").toMap();
        configSettings.conferenceSettings.defaultCipherKey = settings.value("defaultCipherKey").toMap();
    }
    settings.endGroup();

    fileName = "/home/user/MSC/StationMapper/conferencechats.ini";
    QSettings settingsconfchat(fileName,QSettings::IniFormat, this);

    settingsconfchat.beginGroup("ConferenceChats");
    {
        configSettings.conferenceChats.ChatText = settingsconfchat.value("ConferenceChatsText").toMap();
    }
    settingsconfchat.endGroup();

    fileName = "/home/user/MSC/StationMapper/individualchats.ini";
    QSettings settingsindvchat(fileName,QSettings::IniFormat, this);
    settingsindvchat.beginGroup("IndividualChats");
    {
        configSettings.individualChats.ChatText = settingsindvchat.value("IndividualChatsText").toMap();
    }
    settingsindvchat.endGroup();
}//readSettings

void dlgterminal::updateSettings()
{
    //    qDebug() << "UPDATE SETTINGS";
    updateConferenceSidebar();
    ui->checkBoxDisplayTXData->setChecked(configSettings.generalSettings.showTXData);
    ui->comboBoxRecipient->clear();
    ui->comboBoxRecipient->addItems(configSettings.generalSettings.heardCallSignList);
    ui->checkBoxIncludeSalutation->setChecked(configSettings.generalSettings.includeSalutation);
    QString callSignConcanate;
    configSettings.generalSettings.callSignList.removeAll("");
    if(configSettings.generalSettings.callSignList.isEmpty())
        configSettings.generalSettings.callSignList.append("NONE");
    foreach (QString callsign, configSettings.generalSettings.callSignList) {
        callSignConcanate.append(callsign+',');
    }
    callSignConcanate.remove(callSignConcanate.lastIndexOf(','),1);
    tcpTimeout = 100;

    ui->labelClassification->setStyleSheet("color: rgb(255,0,0)");
    ui->labelClassification->setFont(myFont);

    ui->comboBoxSelectMachineAddress->clear();
    ui->comboBoxSelectMachineAddress->addItems(configSettings.generalSettings.machineAddressList);

    ui->actionALE_Config->setChecked(configSettings.generalSettings.hideALE);
    ui->actionConnections_Config->setChecked(configSettings.generalSettings.hideConnections);
    ui->actionModem_Config->setChecked(configSettings.generalSettings.hideModem);
    ui->actionProcessing_Config->setChecked(configSettings.generalSettings.hideProcessing);

    on_actionALE_Config_triggered(configSettings.generalSettings.hideALE);
    on_actionConnections_Config_triggered(configSettings.generalSettings.hideConnections);
    on_actionModem_Config_triggered(configSettings.generalSettings.hideModem);
    on_actionProcessing_Config_triggered(configSettings.generalSettings.hideProcessing);

    ui->checkBoxInsertLineBreaks->setChecked(configSettings.generalSettings.performLineWrap);
    ui->spinBoxLineLength->setValue(configSettings.generalSettings.lineLength);
    ui->cmboChatLAN->setCurrentText(configSettings.generalSettings.cmboChatLAN);
}//updateSettings

void dlgterminal::on_pushButtonSendTx_clicked()
{
    QByteArray plaintext;
    if(ui->plainTextEditSendData->toPlainText().isEmpty())
        return;
    if(configSettings.generalSettings.callSignList.isEmpty() ||
            settingsTermVars.GuardedCallSigns.startsWith("NONE")){
        QMessageBox noCallMsgBox;
        noCallMsgBox.setWindowTitle(tr(APP_NAME)+" - ERROR");
        noCallMsgBox.setText("ERROR - No call sign configured!");
        noCallMsgBox.exec();
        return;
    }//if not call sign configured
    if(!tcpSocket->isOpen()){
        QMessageBox noTCPMsgBox;
        noTCPMsgBox.setWindowTitle(tr(APP_NAME)+" - ERROR");
        noTCPMsgBox.setText("ERROR - No TCP Connection!");
        noTCPMsgBox.exec();
        return;
    }//no TCP connection
    //check for file:///
    if(ui->plainTextEditSendData->toPlainText().startsWith("file:///") &&
            (!ui->plainTextEditSendData->toPlainText().endsWith(".txt") && !ui->plainTextEditSendData->toPlainText().endsWith(".TXT")))
    {
        return;
    }//if not text file extension
    if(ui->plainTextEditSendData->toPlainText().startsWith("file:///") &&
            (ui->plainTextEditSendData->toPlainText().endsWith(".txt") || ui->plainTextEditSendData->toPlainText().endsWith(".TXT")))
    {
        //        qDebug() << "Drag and drop... OK";
        on_actionSend_File_triggered();
        ui->plainTextEditSendData->clear();
        return;
    }//if file
    if(configSettings.generalSettings.includeSalutation){
        plaintext.append("\r\n"+ui->comboBoxRecipient->currentText().trimmed()+" DE "+
                         configSettings.generalSettings.callSignList.at(0).trimmed()+"\r\n     \r\n");
    }
    else {
        plaintext.append("     \r\n");
    }
    QStringList tempStringList = ui->plainTextEditSendData->toPlainText().split("\n");
    foreach (QString tempString, tempStringList) {
        tempString.remove("\r");
        tempString = tempString.trimmed();
        tempString.append("\r\n");
        plaintext.append(tempString);
    }
    if(!plaintext.isEmpty()){
        sendDataToProcessingChain(plaintext, ui->comboBoxKeyList->currentText(), ui->checkBoxCompress->isChecked(), ui->comboBoxRecipient->currentText().trimmed());
        if(ui->checkBoxDisplayTXData->isChecked())
            ui->textBrowserReceivedTCPData->append(plaintext);
        ui->plainTextEditSendData->clear();
    }//if data
}//on_pushButtonSendTx_clicked

void dlgterminal::on_actionClear_triggered()
{
    ui->textBrowserReceivedData->clear();
    ui->plainTextEditSendData->clear();
}//on_actionClear_triggered

void dlgterminal::on_actionSend_File_triggered()
{
    if(configSettings.generalSettings.callSignList.isEmpty() ||
            settingsTermVars.GuardedCallSigns.startsWith("NONE")){
        QMessageBox noCallMsgBox;
        noCallMsgBox.setWindowTitle(tr(APP_NAME)+" - ERROR");
        noCallMsgBox.setText("ERROR - No call sign configured!");
        noCallMsgBox.exec();
        return;
    }//if not call sign configured
    if(!tcpSocket->isOpen()){
        QMessageBox noTCPMsgBox;
        noTCPMsgBox.setWindowTitle(tr(APP_NAME)+" - ERROR");
        noTCPMsgBox.setText("ERROR - No TCP Connection!");
        noTCPMsgBox.exec();
        return;
    }//no TCP connection

    QFile inFile;
    if(ui->plainTextEditSendData->toPlainText().startsWith("file:///") &&
            (!ui->plainTextEditSendData->toPlainText().endsWith(".txt") && !ui->plainTextEditSendData->toPlainText().endsWith(".TXT")))
    {
        return;
    }//if not text file extension
    if(ui->plainTextEditSendData->toPlainText().startsWith("file:///") &&
            (ui->plainTextEditSendData->toPlainText().endsWith(".txt") || ui->plainTextEditSendData->toPlainText().endsWith(".TXT")))
    {
        QString tempString = ui->plainTextEditSendData->toPlainText();
        tempString.remove("file:///");
        inFile.setFileName(tempString);
    }//else send file from Tx Data window
    //else if file was draged or pasted into order wire text box
    else if(ui->plainTextEditSendData->toPlainText().startsWith("file:///") &&
            (ui->plainTextEditSendData->toPlainText().endsWith(".txt") || ui->plainTextEditSendData->toPlainText().endsWith(".TXT")))
    {
        //        qDebug() << "Drag and drop... OK" << ui->lineEditOrderWireText->text();
        QString tempString = ui->lineEditChatText->document()->toPlainText();
        tempString.remove("file:///");
        inFile.setFileName(tempString);
    }//else send file from orderwire text box
    //if we didn't get a file from the above code then open a file dialog to get a file
    if(!inFile.exists())
        inFile.setFileName(QFileDialog::getOpenFileName(this,tr("Open File"), "./", tr("Text Files (*.txt)")));

    if(!inFile.exists())//if we didn't get a file
        return;//then return
    //OK, we got a file now read it in
    QTextStream in(&inFile);
    inFile.open(QFile::ReadOnly);
    QByteArray dataArray;
    dataArray.append(in.readAll());
    inFile.close();
    if(!dataArray.isEmpty()){
        sendDataToProcessingChain(dataArray, ui->comboBoxKeyList->currentText(), ui->checkBoxCompress->isChecked(), ui->comboBoxRecipient->currentText().trimmed());//if the file wasn't empty send the data to cipher
        if(ui->checkBoxDisplayTXData->isChecked())
            ui->textBrowserReceivedTCPData->append(dataArray);
    }//if we have data to send
}//on_actionSend_File_triggered

void dlgterminal::on_actionSend_ConfFile_triggered(QString cipherKey, bool compress)
{
    if(configSettings.generalSettings.callSignList.isEmpty() ){
        QMessageBox noCallMsgBox;
        noCallMsgBox.setWindowTitle(tr(APP_NAME)+" - ERROR");
        noCallMsgBox.setText("ERROR - No call sign configured!");
        noCallMsgBox.exec();
        return;
    }//if not call sign configured
    if(!tcpSocket->isOpen()){
        QMessageBox noTCPMsgBox;
        noTCPMsgBox.setWindowTitle(tr(APP_NAME)+" - ERROR");
        noTCPMsgBox.setText("ERROR - No TCP Connection!");
        noTCPMsgBox.exec();
        return;
    }//no TCP connection

    QFile inFile;
    if(ui->plainTextEditConfSendData->toPlainText().startsWith("file:///") &&
            (!ui->plainTextEditConfSendData->toPlainText().endsWith(".txt") &&
             !ui->plainTextEditConfSendData->toPlainText().endsWith(".TXT")))
    {
        return;
    }//if not text file extension
    if(ui->plainTextEditConfSendData->toPlainText().startsWith("file:///") &&
            (ui->plainTextEditConfSendData->toPlainText().endsWith(".txt") ||
             ui->plainTextEditConfSendData->toPlainText().endsWith(".TXT")))
    {
        QString tempString = ui->plainTextEditConfSendData->toPlainText();
        tempString.remove("file:///");
        inFile.setFileName(tempString);
    }//else send file from Tx Data window
    //else if file was draged or pasted into order wire text box
    else if(ui->plainTextEditConfSendData->toPlainText().startsWith("file:///") &&
            (ui->plainTextEditConfSendData->toPlainText().endsWith(".txt") ||
             ui->plainTextEditConfSendData->toPlainText().endsWith(".TXT")))
    {
        //        qDebug() << "Drag and drop... OK" << ui->lineEditOrderWireText->text();
//        QString tempString = ui->lineEditChatText->text();
        QString tempString = ui->lineEditChatText->document()->toPlainText();
        tempString.remove("file:///");
        inFile.setFileName(tempString);
    }//else send file from orderwire text box
    //if we didn't get a file from the above code then open a file dialog to get a file
    if(!inFile.exists())
        inFile.setFileName(QFileDialog::getOpenFileName(this,tr("Open File"), "./", tr("Text Files (*.txt)")));

    if(!inFile.exists())//if we didn't get a file
        return;//then return
    //OK, we got a file now read it in
    QTextStream in(&inFile);
    inFile.open(QFile::ReadOnly);
    QByteArray dataArray;
    dataArray.append(in.readAll());
    inFile.close();
    if(!dataArray.isEmpty()){
        sendDataToProcessingChain(dataArray, cipherKey, compress, chattingWith);//if the file wasn't empty send the data to cipher
        if(ui->checkBoxDisplayTXData->isChecked())
            ui->textBrowserReceivedTCPData->append(dataArray);
    }//if we have data to send
}//on_actionSend_File_triggered

void dlgterminal::sendDataToProcessingChain(QByteArray plaintextData, QString cipherKey, bool compress, QString recipient)
{
    ClassXML xmlClass;
    if(cipherKey == "PLAINTEXT")
    {
        QMessageBox plaintextWarningMsgBox;
        plaintextWarningMsgBox.setWindowTitle(tr(APP_NAME)+" - WARNING");
        plaintextWarningMsgBox.setText("WARNING - Sending in plaintext mode!");
        plaintextWarningMsgBox.setInformativeText("Do you want to continue?");
        plaintextWarningMsgBox.setStandardButtons(QMessageBox::Yes | QMessageBox::No);
        plaintextWarningMsgBox.setDefaultButton(QMessageBox::No);
        int ret = plaintextWarningMsgBox.exec();
        switch (ret) {
        case QMessageBox::Yes:
            lastSentMessage=plaintextData;
            plaintextData = xmlClass.wrap("data",plaintextData,false, configSettings.generalSettings.positionID,
                                          ui->comboBoxALEStations->currentText(),
                                          QString::number((int)ui->checkBoxCompress->isChecked()),
                                          QString::number((int)!(ui->comboBoxKeyList->currentText() == "PLAINTEXT")),
                                          ui->comboBoxKeyList->currentText(),
                                          configSettings.generalSettings.callSignList.at(0).trimmed(),
                                          recipient.trimmed(),
                                          ui->comboBoxModemSpeed->currentText(),
                                          ui->comboBoxModemInterleave->currentText());
            messageQueue.push_back(plaintextData);
            ui->textBrowserReceivedTCPData->append(plaintextData);
            sendDataTCPSocket();
            return;
            break;
        default:
            return;
            break;
        }//switch
    }//if not encrypt checked
    else {
        lastSentMessage=plaintextData;
        plaintextData = xmlClass.wrap("data",plaintextData,false, configSettings.generalSettings.positionID,
                                      ui->comboBoxALEStations->currentText(),
                                      QString::number((int)compress),
                                      QString::number((int)!(cipherKey == "PLAINTEXT")),
                                      cipherKey,
                                      configSettings.generalSettings.callSignList.at(0).trimmed(),
                                      recipient.trimmed(),
                                      ui->comboBoxModemSpeed->currentText(),
                                      ui->comboBoxModemInterleave->currentText());
        messageQueue.push_back(plaintextData);
        ui->textBrowserReceivedTCPData->append(plaintextData);
        sendDataTCPSocket();
    }//else we're sending encrypted
}//sendDataToProcessingChain

void dlgterminal::on_tabWidgetMain_currentChanged(int index)
{
    writeSettings();
}//on_tabWidgetMain_currentChanged

void dlgterminal::on_lineEditChatText_returnPressed()
{
    //chat test empty
    if(ui->lineEditChatText->document()->isEmpty())
        return;

    if(configSettings.generalSettings.callSignList.isEmpty() ||
            settingsTermVars.GuardedCallSigns.startsWith("NONE")){
        QMessageBox noCallMsgBox;
        noCallMsgBox.setWindowTitle(tr(APP_NAME)+" - ERROR");
        noCallMsgBox.setText("ERROR - No call sign configured!");
        noCallMsgBox.exec();
        return;
    }//if not call sign configured
    if(!tcpSocket->isOpen()){
        QMessageBox noTCPMsgBox;
        noTCPMsgBox.setWindowTitle(tr(APP_NAME)+" - ERROR");
        noTCPMsgBox.setText("ERROR - No TCP Connection!");
        noTCPMsgBox.exec();
        return;
    }//no TCP connection
    //check for file:///
    if(ui->plainTextEditSendData->toPlainText().startsWith("file:///") &&
            (!ui->plainTextEditSendData->toPlainText().endsWith(".txt") && !ui->plainTextEditSendData->toPlainText().endsWith(".TXT")))
    {
        return;
    }//if not text file extension
    if(ui->plainTextEditSendData->toPlainText().startsWith("file:///") &&
            (ui->plainTextEditSendData->toPlainText().endsWith(".txt") || ui->plainTextEditSendData->toPlainText().endsWith(".TXT")))
    {
        //        qDebug() << "Drag and drop... OK";
        on_actionSend_File_triggered();
        ui->lineEditChatText->clear();
        return;
    }
    QByteArray plaintextData;
    //    plaintextData.append("\n");
    if(!ui->comboBoxRecipient->currentText().isEmpty()){
        plaintextData.append(ui->comboBoxRecipient->currentText().trimmed());
    }
    else {
        plaintextData.append("ALL");
    }
    plaintextData.append(" DE "+configSettings.generalSettings.callSignList.at(0).trimmed());
    plaintextData.append(" // ");
    plaintextData.append(ui->lineEditChatText->document()->toPlainText());
    plaintextData.append(" // ");
    if(!ui->lineEditInstructions->text().isEmpty())
        plaintextData.append(ui->lineEditInstructions->text().toUpper()+" ");
    plaintextData.append(QDateTime::currentDateTimeUtc().toString("hhmmZ"));
    if(ui->checkBoxAR->isChecked())
    {
        plaintextData.append(" AR");
        ui->checkBoxAR->setChecked(false);
    }
    else
        plaintextData.append(" K");
    if(!plaintextData.isEmpty()){
        sendDataToProcessingChain(plaintextData, ui->comboBoxConfKeyList->currentText(), ui->checkBoxConfCompress->isChecked(), ui->comboBoxRecipient->currentText().trimmed());
        ui->pushButtonResend->setEnabled(true);
        ui->lineEditChatText->clear();
        ui->lineEditInstructions->clear();
    }//if data
}//on_lineEditOrderWireText_returnPressed

QByteArray dlgterminal::printables(QByteArray byteArray)
{
    for(int i=0;i<byteArray.size();i++){
        if(byteArray.at(i) == '\r' ||
                byteArray.at(i) == '\n' )
            continue;
        else if(byteArray.at(i) < 32 || byteArray.at(i) > 126)
            byteArray[i] = '.';
    }
    return byteArray;
}//printables

void dlgterminal::on_pushButtonResend_clicked()
{
    if(configSettings.generalSettings.callSignList.isEmpty() ||
            settingsTermVars.GuardedCallSigns.startsWith("NONE")){
        QMessageBox noCallMsgBox;
        noCallMsgBox.setWindowTitle(tr(APP_NAME)+" - ERROR");
        noCallMsgBox.setText("ERROR - No call sign configured!");
        noCallMsgBox.exec();
        return;
    }//if not call sign configured
    if(!tcpSocket->isOpen()){
        QMessageBox noTCPMsgBox;
        noTCPMsgBox.setWindowTitle(tr(APP_NAME)+" - ERROR");
        noTCPMsgBox.setText("ERROR - No TCP Connection!");
        noTCPMsgBox.exec();
        return;
    }//no TCP connection
    sendDataToProcessingChain(lastSentMessage, ui->comboBoxKeyList->currentText(), ui->checkBoxCompress->isChecked(), ui->comboBoxRecipient->currentText().trimmed());
//    if(ui->checkBoxDisplayTXData->isChecked())
//        ui->textBrowserReceivedTCPData->append(lastSentMessage);
}//on_pushButtonResend_clicked

void dlgterminal::on_pushButtonClearRawData_clicked()
{
    ui->textBrowserReceivedUDPData->clear();
    ui->textBrowserReceivedTCPData->clear();
}//on_pushButtonClearRawData_clicked

void dlgterminal::on_actionInstruction_Manual_triggered()
{
    QString fileName = "/v3TerminalUserManual.pdf";
    QFile lgplv3(":"+fileName);
    QFile oldlicense(qApp->applicationDirPath().append(fileName));

    if(oldlicense.exists()){
        qDebug() << "Removed Read Only Flag - " << oldlicense.setPermissions(QFile::ReadOther | QFile::WriteOther);
        if(oldlicense.remove())
            qDebug() << "Removed old license file...OK";
        else
            qDebug() << "ERROR - Can not remove license file - " << oldlicense.errorString();
    }
    else
    {
        qDebug() << "Old license file not found...";
    }

    if(!lgplv3.copy(qApp->applicationDirPath().append(fileName))){
        qDebug() << qApp->applicationDirPath().append(fileName);
        qDebug() << "Error copying License Manual! " << lgplv3.errorString();
    }
    QDesktopServices::openUrl(QUrl::fromLocalFile(qApp->applicationDirPath().append(fileName)));
}//on_actionInstruction_Manual_triggered

void dlgterminal::on_pushButtonClearRx_clicked()
{
    setChecksumLabel("");
    ui->labelMessageAuthentication->setText("MAC:");
    ui->labelMessageAuthentication->setStyleSheet("color : black");
    ui->labelMessageAuthentication->hide();

}//on_pushButtonClearRx_clicked

void dlgterminal::checkBoxIgnoreMessages()
{
//    configSettings.generalSettings.ignoreMessagesNotForMe = ui->checkBoxIgnoreMessages->isChecked();
}//on_checkBoxIgnoreMessages_clicked

void dlgterminal::lineEditPositionID(const QString &arg1)
{
    configSettings.generalSettings.positionID = arg1;
    writeSettings();
}//on_lineEditPositionID_textChanged

void dlgterminal::on_pushButtonConnectTCP_clicked()
{
    if(ui->comboBoxTCPConnections->currentText().isEmpty())
        return;

    QString addressString = tcpConnections.value(ui->comboBoxTCPConnections->currentText());
    QHostAddress address(addressString.left(addressString.lastIndexOf(':')));
    int port = addressString.mid(addressString.lastIndexOf(':')+1,addressString.size()-addressString.lastIndexOf(':')).toInt();

    if(tcpSocket->isOpen()){
        //are we already connected to the address we want? if so return
        if(tcpSocket->peerAddress() == address && tcpSocket->peerPort() == port){
            showStatusMessage("Connected to "+ui->comboBoxTCPConnections->currentText()+
                              " - "+tcpSocket->peerAddress().toString()+
                              " : "+QString::number(tcpSocket->peerPort()));
            ui->actionSend_File->setEnabled(true);
//            setApplicationTitle(ui->comboBoxTCPConnections->currentText());
            return;
        }//if we're already connected to same address/port
        else{
            //disconnect so we can connect to a new ip:port
            tcpSocket->close();
            if(tcpSocket->waitForDisconnected()){
                tcpSocket->connectToHost(address,port);
                if(tcpSocket->waitForConnected()){
                    showStatusMessage("Connected to "+ui->comboBoxTCPConnections->currentText()+
                                      " - "+tcpSocket->peerAddress().toString()+
                                      " : "+QString::number(tcpSocket->peerPort()));
                    ui->actionSend_File->setEnabled(true);
//                    setApplicationTitle(ui->comboBoxTCPConnections->currentText());
                }//wait for connect
                else{
                    showStatusMessage("Connection error - "+tcpSocket->errorString());
//                    setApplicationTitle("ERR");
                    ui->actionSend_File->setEnabled(false);
                }//else connection error
            }//wait for disconnect
            else{
                tcpSocket->connectToHost(address,port);
                if(tcpSocket->waitForConnected()){
                    showStatusMessage("Connected to "+ui->comboBoxTCPConnections->currentText()+
                                      " - "+tcpSocket->peerAddress().toString()+
                                      " : "+QString::number(tcpSocket->peerPort()));
//                    setApplicationTitle(ui->comboBoxTCPConnections->currentText());
                    ui->actionSend_File->setEnabled(true);
                }//if socket opened
                else{
                    ui->textBrowserReceivedData->append(tcpSocket->errorString()+": " + address.toString()+": " + QString::number(port));
                    showStatusMessage("Connection error - "+tcpSocket->errorString());
//                    setApplicationTitle("ERR");
                    ui->actionSend_File->setEnabled(false);
                }//else connection error
            }//try connection again
        }//else connect to different ip:port
    }//if socket is open
    else {
        tcpSocket->connectToHost(address,port);
        if(tcpSocket->waitForConnected()){
            showStatusMessage("Connected to "+ui->comboBoxTCPConnections->currentText()+
                              " - "+tcpSocket->peerAddress().toString()+
                              " : "+QString::number(tcpSocket->peerPort()));
//            setApplicationTitle(ui->comboBoxTCPConnections->currentText());
            ui->actionSend_File->setEnabled(true);
        }//if socket opened
        else{
            ui->textBrowserReceivedData->append(tcpSocket->errorString() + address.toString() + QString::number(port));
            showStatusMessage("Connection error - "+tcpSocket->errorString());
//            setApplicationTitle("ERR");
            ui->actionSend_File->setEnabled(false);
        }//else connection error
    }//else socket was not already opened
}//on_pushButtonConnectTCP_clicked

void dlgterminal::on_pushButtonConnectALE_clicked()
{
    if(ui->comboBoxALEStations->currentText().isEmpty())
        return;
    //send radio command to connect to ALE station
    ClassXML xmlClass;
    lastSentMessage = "Calling: "+ui->comboBoxALEStations->currentText().toLatin1();
    messageQueue.push_back(xmlClass.wrap("ALE CALL","",true, configSettings.generalSettings.positionID,
                                         ui->comboBoxALEStations->currentText(),
                                         QString::number((int)ui->checkBoxCompress->isChecked()),
                                         QString::number((int)!(ui->comboBoxKeyList->currentText() == "PLAINTEXT")),
                                         ui->comboBoxKeyList->currentText(),
                                         configSettings.generalSettings.callSignList.at(0).trimmed(),
                                         ui->comboBoxRecipient->currentText().trimmed(),
                                         "",
                                         ""));
    sendDataTCPSocket();
}//on_pushButtonConnectALE_clicked

void dlgterminal::on_plainTextEditSendData_textChanged()
{
    static bool isBusy = false;

    if(isBusy)
        return;

    isBusy = true;
    ui->pushButtonSendTx->setEnabled(!ui->plainTextEditSendData->toPlainText().isEmpty() &&
                                     tcpSocket->isOpen());
    ui->pushButtonClearTx->setEnabled(!ui->plainTextEditSendData->toPlainText().isEmpty());

    if(ui->plainTextEditSendData->toPlainText().toUpper().startsWith("FILE:///") &&
            (ui->plainTextEditSendData->toPlainText().toUpper().endsWith(".TXT") ||
             ui->plainTextEditSendData->toPlainText().toUpper().endsWith(".XML"))){
        //do nothing
    }
    else if(ui->checkBoxInsertLineBreaks->isChecked()){
        QTextCursor cursor(ui->plainTextEditSendData->textCursor());
        if(cursor.columnNumber() >= ui->spinBoxLineLength->value()){
            cursor.insertText("\n");
        }
    }
    isBusy = false;
}//on_plainTextEditSendData_textChanged

void dlgterminal::on_textBrowserReceivedData_textChanged()
{
    ui->pushButtonSelectRx->setEnabled(!ui->textBrowserReceivedData->toPlainText().isEmpty());
    ui->pushButtonClearRx->setEnabled(!ui->textBrowserReceivedData->toPlainText().isEmpty());
}//on_textBrowserReceivedData_textChanged

void dlgterminal::on_textBrowserReceivedData_selectionChanged()
{
    ui->pushButtonCopyRx->setEnabled(!ui->textBrowserReceivedData->textCursor().selectedText().isEmpty());
}//on_textBrowserReceivedData_selectionChanged

void dlgterminal::on_comboBoxALEStations_currentIndexChanged(const QString &arg1)
{
    ui->pushButtonConnectALE->setEnabled(!arg1.isEmpty());
}//on_comboBoxALEStations_currentIndexChanged

void dlgterminal::on_pushButtonSetDefaultRMI_clicked()
{
    configSettings.generalSettings.defaultRMI=ui->comboBoxTCPConnections->currentText();
    writeSettings();

    on_pushButtonConnectTCP_clicked();

}//on_pushButtonSetDefaultRMI_clicked

void dlgterminal::on_lineEditInstructions_returnPressed()
{
    on_lineEditChatText_returnPressed();
}//on_lineEditInstructions_returnPressed

//void dlgterminal::on_lineEditCallSign_2_textEdited(const QString &arg1)
//{
//    QString tempString = arg1.toUpper();
//    QStringList tempStringList = tempString.split(',',QString::SkipEmptyParts);
//    configSettings.generalSettings.callSignList = tempStringList;
//    writeSettings();
//}//on_lineEditCallSign_2_textEdited

void dlgterminal::on_comboBoxKeyList_currentTextChanged(const QString &arg1)
{
    //QImage badIconImage(":/images/CryptoCommBadIcon.png");
    //QImage goodIconImage(":/images/CryptoCommIcon.png");
    //goodIconImage.scaledToWidth(ui->labelIcon->width());
    //badIconImage.scaledToWidth(ui->labelIcon->width());

    if(!(ui->comboBoxKeyList->currentText() == "PLAINTEXT")){
        //ui->labelIcon->setPixmap(QPixmap::fromImage(goodIconImage));
        ui->labelClassification->setStyleSheet("color: rgb(0,0,0)");
        ui->labelClassification->setText("AUTHORIZED TO PROCESS UNCLASSIFIED//FOUO ONLY");
    }//if CT is selected
    else{
        //ui->labelIcon->setPixmap(QPixmap::fromImage(badIconImage));
        ui->labelClassification->setStyleSheet("color: rgb(255,0,0)");
        ui->labelClassification->setText("AUTHORIZED TO PROCESS UNCLASSIFIED ONLY");
    }//else CT is not selected
}//on_comboBoxKeyList_currentTextChanged

void dlgterminal::on_pushButtonSetDefaultKey_clicked()
{
    configSettings.generalSettings.keyFile = ui->comboBoxKeyList->currentText();
}//on_pushButtonSetDefaultKey_clicked

void dlgterminal::pushButtonBroadcastID()
{
    slotSayHelo();
}//on_pushButtonBroadcastID_clicked

void dlgterminal::on_lineEditAddMachineAddress_returnPressed()
{
    on_pushButtonAddMachineAddress_clicked();
}//on_lineEditAddMachineAddress_returnPressed

void dlgterminal::on_pushButtonAddMachineAddress_clicked()
{
    QString machineAddressNoSpacesString = removeSpaces(ui->lineEditAddMachineAddress->text().toUpper());
    if(machineAddressNoSpacesString.isEmpty()){
        ui->lineEditAddMachineAddress->clear();
        return;
    }
    if(!machineAddressNoSpacesString.endsWith("-CM",Qt::CaseInsensitive))
    {
        machineAddressNoSpacesString = machineAddressNoSpacesString.toUpper()+"-CM";
    }
    else {
        machineAddressNoSpacesString = machineAddressNoSpacesString.toUpper();
    }
    if(!configSettings.generalSettings.machineAddressList.contains(machineAddressNoSpacesString))
    {
        configSettings.generalSettings.machineAddressList.append(machineAddressNoSpacesString);
    }
    ui->comboBoxSelectMachineAddress->clear();
    ui->comboBoxSelectMachineAddress->addItems(configSettings.generalSettings.machineAddressList);
    ui->lineEditAddMachineAddress->clear();

    writeSettings();
}//on_pushButtonAddMachineAddress_clicked

void dlgterminal::on_pushButtonRemoveMachineAddress_clicked()
{
    configSettings.generalSettings.machineAddressList.removeAll(ui->comboBoxSelectMachineAddress->currentText());
    ui->comboBoxSelectMachineAddress->clear();
    ui->comboBoxSelectMachineAddress->addItems(configSettings.generalSettings.machineAddressList);
}

void dlgterminal::on_pushButtonQRU_clicked()
{
    if(configSettings.generalSettings.callSignList.isEmpty() ||
            settingsTermVars.GuardedCallSigns.startsWith("NONE")){
        QMessageBox noCallMsgBox;
        noCallMsgBox.setWindowTitle(tr(APP_NAME)+" - ERROR");
        noCallMsgBox.setText("ERROR - No call sign configured!");
        noCallMsgBox.exec();
        return;
    }//if not call sign configured
    if(ui->comboBoxSelectMachineAddress->currentText().isEmpty()){
        QMessageBox noCallMsgBox;
        noCallMsgBox.setWindowTitle(tr(APP_NAME)+" - ERROR");
        noCallMsgBox.setText("ERROR - No machine address selected!");
        noCallMsgBox.exec();
        return;
    }//if no machine address is selected
    if(!tcpSocket->isOpen()){
        QMessageBox noTCPMsgBox;
        noTCPMsgBox.setWindowTitle(tr(APP_NAME)+" - ERROR");
        noTCPMsgBox.setText("ERROR - No TCP Connection!");
        noTCPMsgBox.exec();
        return;
    }//no TCP connection
    QByteArray machineCommand;
    //    if(ui->radioButtonv2Machine->isChecked()){
    //        machineCommand.append("     \r\n");
    //        machineCommand.append(ui->comboBoxSelectMachineAddress->currentText()+ " DE "+configSettings.generalSettings.callSignList.at(0)+"\r\n");
    //        machineCommand.append("QRU "+ui->lineEditMessageParamaters->text().toUpper()+"\r\n");
    //        machineCommand.append("K\r\n");
    //    }//send v2 command syntax
    //    else {
    machineCommand.append(ui->comboBoxSelectMachineAddress->currentText());
    machineCommand.append(" DE "+configSettings.generalSettings.callSignList.at(0));
    machineCommand.append(" // ");
    machineCommand.append("QRU "+ui->lineEditMessageParamaters->text().toUpper());
    if(ui->checkBoxCompressResults->isChecked()){
        machineCommand.append(" [COMP]");
    }//is compress results
    machineCommand.append(" // ");
    machineCommand.append(QDateTime::currentDateTimeUtc().toString("hhmmZ"));
    machineCommand.append(" K");
    //    }//sent v3 command syntax
    ui->textBrowserMachine->append(machineCommand);
    sendDataToProcessingChain(machineCommand, ui->comboBoxKeyList->currentText(), ui->checkBoxCompress->isChecked(), ui->comboBoxSelectMachineAddress->currentText().trimmed());
}//on_pushButtonQRU_clicked

void dlgterminal::on_pushButtonZEE_clicked()
{
    if(configSettings.generalSettings.callSignList.isEmpty() ||
            settingsTermVars.GuardedCallSigns.startsWith("NONE")){
        QMessageBox noCallMsgBox;
        noCallMsgBox.setWindowTitle(tr(APP_NAME)+" - ERROR");
        noCallMsgBox.setText("ERROR - No call sign configured!");
        noCallMsgBox.exec();
        return;
    }//if not call sign configured
    if(ui->comboBoxSelectMachineAddress->currentText().isEmpty()){
        QMessageBox noCallMsgBox;
        noCallMsgBox.setWindowTitle(tr(APP_NAME)+" - ERROR");
        noCallMsgBox.setText("ERROR - No machine address selected!");
        noCallMsgBox.exec();
        return;
    }//if no machine address is selected
    if(!tcpSocket->isOpen()){
        QMessageBox noTCPMsgBox;
        noTCPMsgBox.setWindowTitle(tr(APP_NAME)+" - ERROR");
        noTCPMsgBox.setText("ERROR - No TCP Connection!");
        noTCPMsgBox.exec();
        return;
    }//no TCP connection
    QByteArray machineCommand;
    //    if(ui->radioButtonv2Machine->isChecked()){
    //        machineCommand.append("     \r\n");
    //        machineCommand.append(ui->comboBoxSelectMachineAddress->currentText()+ " DE "+configSettings.generalSettings.callSignList.at(0)+"\r\n");
    //        machineCommand.append("ZEE "+ui->lineEditMessageParamaters->text().toUpper()+"\r\n");
    //        machineCommand.append("K\r\n");
    //    }//send v2 command syntax
    //    else {
    machineCommand.append(ui->comboBoxSelectMachineAddress->currentText());
    machineCommand.append(" DE "+configSettings.generalSettings.callSignList.at(0));
    machineCommand.append(" // ");
    machineCommand.append("ZEE "+ui->lineEditMessageParamaters->text().toUpper());
    if(ui->checkBoxCompressResults->isChecked()){
        machineCommand.append(" [COMP]");
    }//is compress results
    machineCommand.append(" // ");
    machineCommand.append(QDateTime::currentDateTimeUtc().toString("hhmmZ"));
    machineCommand.append(" K");
    //    }//sent v3 command syntax
    ui->textBrowserMachine->append(machineCommand);
    sendDataToProcessingChain(machineCommand, ui->comboBoxKeyList->currentText(), ui->checkBoxCompress->isChecked(), ui->comboBoxSelectMachineAddress->currentText().trimmed());
}//on_pushButtonZEE_clicked

void dlgterminal::on_pushButtonQSL_clicked()
{
    if(configSettings.generalSettings.callSignList.isEmpty() ||
            settingsTermVars.GuardedCallSigns.startsWith("NONE")){
        QMessageBox noCallMsgBox;
        noCallMsgBox.setWindowTitle(tr(APP_NAME)+" - ERROR");
        noCallMsgBox.setText("ERROR - No call sign configured!");
        noCallMsgBox.exec();
        return;
    }//if not call sign configured
    if(ui->comboBoxSelectMachineAddress->currentText().isEmpty()){
        QMessageBox noCallMsgBox;
        noCallMsgBox.setWindowTitle(tr(APP_NAME)+" - ERROR");
        noCallMsgBox.setText("ERROR - No machine address selected!");
        noCallMsgBox.exec();
        return;
    }//if no machine address is selected
    if(!tcpSocket->isOpen()){
        QMessageBox noTCPMsgBox;
        noTCPMsgBox.setWindowTitle(tr(APP_NAME)+" - ERROR");
        noTCPMsgBox.setText("ERROR - No TCP Connection!");
        noTCPMsgBox.exec();
        return;
    }//no TCP connection
    QByteArray machineCommand;
    //    if(ui->radioButtonv2Machine->isChecked()){
    //        machineCommand.append("     \r\n");
    //        machineCommand.append(ui->comboBoxSelectMachineAddress->currentText()+ " DE "+configSettings.generalSettings.callSignList.at(0)+"\r\n");
    //        machineCommand.append("QSL "+ui->lineEditMessageParamaters->text().toUpper()+"\r\n");
    //        machineCommand.append("K\r\n");
    //    }//send v2 command syntax
    //    else {
    machineCommand.append(ui->comboBoxSelectMachineAddress->currentText());
    machineCommand.append(" DE "+configSettings.generalSettings.callSignList.at(0));
    machineCommand.append(" // ");
    machineCommand.append("QSL "+ui->lineEditMessageParamaters->text().toUpper());
    machineCommand.append(" // ");
    machineCommand.append(" ");
    machineCommand.append(QDateTime::currentDateTimeUtc().toString("hhmmZ"));
    machineCommand.append(" K");
    //    }//sent v3 command syntax
    ui->textBrowserMachine->append(machineCommand);
    sendDataToProcessingChain(machineCommand, ui->comboBoxKeyList->currentText(), ui->checkBoxCompress->isChecked(), ui->comboBoxSelectMachineAddress->currentText().trimmed());
}//on_pushButtonQSL_clicked

QString dlgterminal::removeSpaces(QString stringData)
{
    QByteArrayList byteArrayList = stringData.toLatin1().split(' ');
    byteArrayList.removeAll("");
    return (QString)byteArrayList.join("");
}//removeSpaces

QByteArray dlgterminal::removeSpaces(QByteArray byteArrayData)
{
    QByteArrayList byteArrayList = byteArrayData.split(' ');
    byteArrayList.removeAll("");
    return byteArrayList.join("");
}//removeSpaces


void dlgterminal::on_lineEditSQLQuery_returnPressed()
{
    on_pushButtonSQLQuery_clicked();
}//on_lineEditSQLQuery_returnPressed

void dlgterminal::on_pushButtonSQLQuery_clicked()
{
    if(configSettings.generalSettings.callSignList.isEmpty() ||
            settingsTermVars.GuardedCallSigns.startsWith("NONE")){
        QMessageBox noCallMsgBox;
        noCallMsgBox.setWindowTitle(tr(APP_NAME)+" - ERROR");
        noCallMsgBox.setText("ERROR - No call sign configured!");
        noCallMsgBox.exec();
        return;
    }//if not call sign configured
    if(ui->comboBoxSelectMachineAddress->currentText().isEmpty()){
        QMessageBox noCallMsgBox;
        noCallMsgBox.setWindowTitle(tr(APP_NAME)+" - ERROR");
        noCallMsgBox.setText("ERROR - No machine address selected!");
        noCallMsgBox.exec();
        return;
    }//if no machine address is selected
    if(!tcpSocket->isOpen()){
        QMessageBox noTCPMsgBox;
        noTCPMsgBox.setWindowTitle(tr(APP_NAME)+" - ERROR");
        noTCPMsgBox.setText("ERROR - No TCP Connection!");
        noTCPMsgBox.exec();
        return;
    }//no TCP connection
    if(!ui->lineEditSQLQuery->text().isEmpty()){
        QByteArray machineCommand;
        //        if(ui->radioButtonv2Machine->isChecked()){
        //            machineCommand.append("     \r\n");
        //            machineCommand.append(ui->comboBoxSelectMachineAddress->currentText()+ " DE "+configSettings.generalSettings.callSignList.at(0)+"\r\n");
        //            machineCommand.append("SQL "+ui->lineEditSQLQuery->text().toUpper()+"\r\n");
        //            machineCommand.append("K\r\n");
        //        }//send v2 command syntax
        //        else {
        machineCommand.append(ui->comboBoxSelectMachineAddress->currentText());
        machineCommand.append(" DE "+configSettings.generalSettings.callSignList.at(0));
        machineCommand.append(" // ");
        machineCommand.append("SQL "+ui->lineEditSQLQuery->text().toUpper());
        machineCommand.append(" // ");
        machineCommand.append(QDateTime::currentDateTimeUtc().toString("hhmmZ"));
        machineCommand.append(" K");
        //        }//sent v3 command syntax
        ui->textBrowserMachine->append(machineCommand);
        sendDataToProcessingChain(machineCommand, ui->comboBoxKeyList->currentText(), ui->checkBoxCompress->isChecked(), ui->comboBoxSelectMachineAddress->currentText().trimmed());
    }
    else {
        QByteArray machineCommand;
        //        if(ui->radioButtonv2Machine->isChecked()){
        //            machineCommand.append("     \r\n");
        //            machineCommand.append(ui->comboBoxSelectMachineAddress->currentText()+ " DE "+configSettings.generalSettings.callSignList.at(0)+"\r\n");
        //            machineCommand.append("SQL "+ui->comboBoxSQLQuery->currentText().toUpper()+"\r\n");
        //            machineCommand.append("K\r\n");
        //        }//send v2 command syntax
        //        else {
        machineCommand.append(ui->comboBoxSelectMachineAddress->currentText());
        machineCommand.append(" DE "+configSettings.generalSettings.callSignList.at(0));
        machineCommand.append(" // ");
        machineCommand.append("SQL "+ui->comboBoxSQLQuery->currentText().toUpper());
        machineCommand.append(" // ");
        machineCommand.append(QDateTime::currentDateTimeUtc().toString("hhmmZ"));
        machineCommand.append(" K");
        //        }//sent v3 command syntax
        ui->textBrowserMachine->append(machineCommand);
        sendDataToProcessingChain(machineCommand, ui->comboBoxKeyList->currentText(), ui->checkBoxCompress->isChecked(), ui->comboBoxSelectMachineAddress->currentText().trimmed());
    }
}//on_pushButtonSQLQuery_clicked

void dlgterminal::on_lineEditMessageParamaters_returnPressed()
{
    if(configSettings.generalSettings.callSignList.isEmpty() ||
            settingsTermVars.GuardedCallSigns.startsWith("NONE")){
        QMessageBox noCallMsgBox;
        noCallMsgBox.setWindowTitle(tr(APP_NAME)+" - ERROR");
        noCallMsgBox.setText("ERROR - No call sign configured!");
        noCallMsgBox.exec();
        return;
    }//if not call sign configured
    if(ui->comboBoxSelectMachineAddress->currentText().isEmpty()){
        QMessageBox noCallMsgBox;
        noCallMsgBox.setWindowTitle(tr(APP_NAME)+" - ERROR");
        noCallMsgBox.setText("ERROR - No machine address selected!");
        noCallMsgBox.exec();
        return;
    }//if no machine address is selected
    if(!tcpSocket->isOpen()){
        QMessageBox noTCPMsgBox;
        noTCPMsgBox.setWindowTitle(tr(APP_NAME)+" - ERROR");
        noTCPMsgBox.setText("ERROR - No TCP Connection!");
        noTCPMsgBox.exec();
        return;
    }//no TCP connection
    QByteArray machineCommand;
    //    if(ui->radioButtonv2Machine->isChecked()){
    //        machineCommand.append("     \r\n");
    //        machineCommand.append(ui->comboBoxSelectMachineAddress->currentText()+ " DE "+configSettings.generalSettings.callSignList.at(0)+"\r\n");
    //        machineCommand.append(ui->lineEditMessageParamaters->text().toUpper()+"\r\n");
    //        machineCommand.append("K\r\n");
    //    }//send v2 command syntax
    //    else {
    machineCommand.append(ui->comboBoxSelectMachineAddress->currentText());
    machineCommand.append(" DE "+configSettings.generalSettings.callSignList.at(0));
    machineCommand.append(" // ");
    machineCommand.append(ui->lineEditMessageParamaters->text().toUpper());
    machineCommand.append(" // ");
    machineCommand.append(QDateTime::currentDateTimeUtc().toString("hhmmZ"));
    machineCommand.append(" K");
    //    }//sent v3 command syntax
    ui->textBrowserMachine->append(machineCommand);
    sendDataToProcessingChain(machineCommand, ui->comboBoxKeyList->currentText(), ui->checkBoxCompress->isChecked(), ui->comboBoxRecipient->currentText().trimmed());
}//on_lineEditMessageParamaters_returnPressed

void dlgterminal::checkBoxIgnoreACKMessages(bool checked)
{
    configSettings.generalSettings.ignoreACKMessages = checked;
    writeSettings();
}//on_checkBoxIgnoreACKMessages_clicked


void dlgterminal::on_comboBoxRecipient_editTextChanged(const QString &arg1)
{
    if(isApplicationStarting)
        return;
    for(int i=0; i < ui->comboBoxRecipient->count(); i++){
        configSettings.generalSettings.heardCallSignList.append(ui->comboBoxRecipient->itemText(i));
    }
    configSettings.generalSettings.heardCallSignList.removeDuplicates();
    writeSettings();
}//on_comboBoxSelCall_editTextChanged

void dlgterminal::setChecksumLabel(QString checksumString)
{
    if(checksumString == "PASS"){
        ui->labelCheckSum->setText("Checksum: PASS");
        ui->labelCheckSum->setStyleSheet("color: rgb(0,255,0)");
        ui->labelCheckSum->setFont(myFont);
        ui->labelCheckSum_2->setText("Checksum: PASS");
        ui->labelCheckSum_2->setStyleSheet("color: rgb(0,255,0)");
        ui->labelCheckSum_2->setFont(myFont);
    }
    else if (checksumString == "FAIL") {
        ui->labelCheckSum->setText("Checksum: FAIL");
        ui->labelCheckSum->setStyleSheet("color: red");
        ui->labelCheckSum->setFont(myFont);
        ui->labelCheckSum_2->setText("Checksum: FAIL");
        ui->labelCheckSum_2->setStyleSheet("color: red");
        ui->labelCheckSum_2->setFont(myFont);
    }
    else if (checksumString == "ERRO") {
        ui->labelCheckSum->setText("Checksum: ERRO");
        ui->labelCheckSum->setStyleSheet("color: yellow");
        ui->labelCheckSum->setFont(myFont);
        ui->labelCheckSum_2->setText("Checksum: ERRO");
        ui->labelCheckSum_2->setStyleSheet("color: yellow");
        ui->labelCheckSum_2->setFont(myFont);
    }
    else if (checksumString == "") {
        ui->labelCheckSum->setText("Checksum:");
        ui->labelCheckSum->setStyleSheet("color: black");
        ui->labelCheckSum->setFont(myFont);
        ui->labelCheckSum_2->setText("Checksum:");
        ui->labelCheckSum_2->setStyleSheet("color: gray");
        ui->labelCheckSum_2->setFont(myFont);
    }
    else {
        ui->labelCheckSum->setText("Checksum: UNKN");
        ui->labelCheckSum->setStyleSheet("color: black");
        ui->labelCheckSum->setFont(myFont);
        ui->labelCheckSum_2->setText("Checksum: UNKN");
        ui->labelCheckSum_2->setStyleSheet("color: gray");
        ui->labelCheckSum_2->setFont(myFont);
    }
}//setChecksumLabel

void dlgterminal::on_radioButtonv2Machine_clicked(bool checked)
{
    ui->tabSQL->setEnabled(!checked);
    ui->checkBoxCompressResults->setChecked(false);
    ui->checkBoxCompressResults->setEnabled(false);
}//on_radioButtonv2Machine_clicked

//void dlgterminal::on_checkBoxTrackRMI_clicked(bool checked)
//{
//    qDebug() << "on_checkBoxTrackRMI_clicked " << checked;
//    configSettings.generalSettings.trackRMIChanges = checked;
//    writeSettings();
//}

void dlgterminal::on_actionALE_Config_triggered(bool checked)
{
    if(checked)
        ui->groupBoxALE->hide();
    else
        ui->groupBoxALE->show();
    configSettings.generalSettings.hideALE = checked;
}//on_actionALE_Config_triggered

void dlgterminal::on_actionConnections_Config_triggered(bool checked)
{
    if(checked)
        ui->groupBoxConnections->hide();
    else
        ui->groupBoxConnections->show();
    configSettings.generalSettings.hideConnections = checked;
}//on_actionConnections_Config_triggered

void dlgterminal::on_actionModem_Config_triggered(bool checked)
{
    if(checked)
        ui->groupBoxModem->hide();
    else
        ui->groupBoxModem->show();
    configSettings.generalSettings.hideModem = checked;
}//on_actionModem_Config_triggered

void dlgterminal::on_actionProcessing_Config_triggered(bool checked)
{
    if(checked)
        ui->groupBoxProcessing->hide();
    else
        ui->groupBoxProcessing->show();
    configSettings.generalSettings.hideProcessing = checked;
}//on_actionProcessing_Config_triggered


void dlgterminal::on_pushButtonRemoveRecipient_clicked()
{
    configSettings.generalSettings.heardCallSignList.removeAll(ui->comboBoxRecipient->currentText());
    writeSettings();
    ui->comboBoxRecipient->clear();
    configSettings.generalSettings.heardCallSignList.removeDuplicates();
    ui->comboBoxRecipient->addItems(configSettings.generalSettings.heardCallSignList);
}//on_pushButtonRemoveRecipient_clicked

void dlgterminal::on_checkBoxInsertLineBreaks_clicked(bool checked)
{
    configSettings.generalSettings.performLineWrap = checked;
    writeSettings();
}//on_checkBoxInsertLineBreaks_clicked

void dlgterminal::on_spinBoxLineLength_valueChanged(int arg1)
{
    configSettings.generalSettings.lineLength = arg1;
    writeSettings();
}//on_spinBoxLineLength_valueChanged


void dlgterminal::on_checkBoxDisplayTXData_clicked(bool checked)
{
    configSettings.generalSettings.showTXData = checked;
}//on_checkBoxDisplayTXData_clicked

void dlgterminal::on_pushButtonZDK_clicked()
{
    if(configSettings.generalSettings.callSignList.isEmpty() ||
            settingsTermVars.GuardedCallSigns.startsWith("NONE")){
        QMessageBox noCallMsgBox;
        noCallMsgBox.setWindowTitle(tr(APP_NAME)+" - ERROR");
        noCallMsgBox.setText("ERROR - No call sign configured!");
        noCallMsgBox.exec();
        return;
    }//if not call sign configured
    if(ui->comboBoxSelectMachineAddress->currentText().isEmpty()){
        QMessageBox noCallMsgBox;
        noCallMsgBox.setWindowTitle(tr(APP_NAME)+" - ERROR");
        noCallMsgBox.setText("ERROR - No machine address selected!");
        noCallMsgBox.exec();
        return;
    }//if no machine address is selected
    if(!tcpSocket->isOpen()){
        QMessageBox noTCPMsgBox;
        noTCPMsgBox.setWindowTitle(tr(APP_NAME)+" - ERROR");
        noTCPMsgBox.setText("ERROR - No TCP Connection!");
        noTCPMsgBox.exec();
        return;
    }//no TCP connection
    QByteArray machineCommand;
    //    if(ui->radioButtonv2Machine->isChecked()){
    //        machineCommand.append("     \r\n");
    //        machineCommand.append(ui->comboBoxSelectMachineAddress->currentText()+ " DE "+configSettings.generalSettings.callSignList.at(0)+"\r\n");
    //        machineCommand.append("ZDK "+ui->lineEditMessageParamaters->text().toUpper()+"\r\n");
    //        machineCommand.append("K\r\n");
    //    }//send v2 command syntax
    //    else {
    machineCommand.append(ui->comboBoxSelectMachineAddress->currentText());
    machineCommand.append(" DE "+configSettings.generalSettings.callSignList.at(0));
    machineCommand.append(" // ");
    machineCommand.append("ZDK "+ui->lineEditMessageParamaters->text().toUpper());
    if(ui->checkBoxCompressResults->isChecked()){
        machineCommand.append(" [COMP]");
    }//is compress results
    machineCommand.append(" // ");
    machineCommand.append(QDateTime::currentDateTimeUtc().toString("hhmmZ"));
    machineCommand.append(" K");
    //    }//sent v3 command syntax
    ui->textBrowserMachine->append(machineCommand);
    sendDataToProcessingChain(machineCommand, ui->comboBoxKeyList->currentText(), ui->checkBoxCompress->isChecked(), ui->comboBoxSelectMachineAddress->currentText().trimmed());
}//on_pushButtonZDK_clicked

void dlgterminal::on_checkBoxIncludeSalutation_clicked(bool checked)
{
    configSettings.generalSettings.includeSalutation=checked;
    writeSettings();
}

bool dlgterminal::eventFilter(QObject *obj, QEvent *event) {
    if (event->type () == QEvent::KeyPress) {
        QKeyEvent *keyEvent = static_cast<QKeyEvent *>(event);
        if (obj == ui->lineEditChatText) {
            if ((keyEvent->key() == Qt::Key::Key_Enter) || (keyEvent->key() == Qt::Key::Key_Return)) { //Enter
                if (1==1) {qDebug() << "Ate Key Press" << keyEvent->key();}
                on_lineEditChatText_returnPressed();
                return true;
            }
            return false;
        } else {
            return false;
        }
    }
    return false;
}

void dlgterminal::on_textEdit_ContextMenuRequest(const QPoint &pos) {
    if (DebugTerminal > 1) {qDebug() << Q_FUNC_INFO;}

    SuggestionMap.clear();

    QMenu contextMenu(tr("Context menu"), this);
    contextMenu.setToolTipsVisible(true);
    prev = ui->lineEditChatText->textCursor();
    csr = ui->lineEditChatText->cursorForPosition(pos);
    csr.select(QTextCursor::WordUnderCursor);
    csr.selectedText();
    //Notes_textEdit->setTextCursor(csr);

    if (csr.hasSelection()) {
        QString text = csr.selectedText();
     /* if (DebugTerminal > 5) {qDebug() << text << Notes_SpellChecker.isSpellingCorrect(text);}
        if (!Notes_SpellChecker.isSpellingCorrect(text)) {
            QAction * AddWord = new QAction("Add Word", this);
            AddWord->setToolTip("Add word to dictionary");
            contextMenu.addAction(AddWord);
            connect(AddWord, SIGNAL(triggered()), this, SLOT(on_AddWord()));
            contextMenu.addSeparator();

            QStringList suggestions = Notes_SpellChecker.suggestCorrections(text);
            if (DebugTerminal > 5) {qDebug() << suggestions;}
            for (int idx = 0; idx < suggestions.size() ; idx++) {
                    QAction * SelectSuggestion = new QAction(suggestions.at(idx), this);
                    SuggestionMap[SelectSuggestion] = suggestions.at(idx); //memory leak
                    connect(SelectSuggestion, SIGNAL(triggered()), this, SLOT(on_SelectSuggestion()));
                    contextMenu.addAction(SelectSuggestion);
            }
        }*/
    }

    contextMenu.addSeparator();
    QAction Cut("Cut", this);
    contextMenu.addAction(&Cut);
    connect(&Cut, SIGNAL(triggered()), this, SLOT(on_cut()));
    QAction Copy("Copy", this);
    contextMenu.addAction(&Copy);
    connect(&Copy, SIGNAL(triggered()), this, SLOT(on_copy()));
    QAction Paste("Paste", this);
    contextMenu.addAction(&Paste);
    connect(&Paste, SIGNAL(triggered()), this, SLOT(on_paste()));


    contextMenu.exec(ui->lineEditChatText->mapToGlobal(pos));
}

void dlgterminal::on_SelectSuggestion() {
    QString word = SuggestionMap[(QAction*)sender()];

    csr.beginEditBlock();

    csr.removeSelectedText();
    csr.insertText(word);

    csr.endEditBlock();
    //Notes_textEdit->setTextCursor(prev);
}

void dlgterminal::on_AddWord() {
    QString text = csr.selectedText();
//    Notes_SpellChecker.addWord(text);
//    highlighter->addWord(text);
//    highlighter->rehighlight();
}

void dlgterminal::on_cut() {
    QString selected = ui->lineEditChatText->textCursor().selectedText();
    QApplication::clipboard()->setText(selected);
    ui->lineEditChatText->textCursor().removeSelectedText();
}

void dlgterminal::on_copy() {
    QString selected = ui->lineEditChatText->textCursor().selectedText();
    QApplication::clipboard()->setText(selected);
}

void dlgterminal::on_paste() {
    QTextCursor selected = ui->lineEditChatText->textCursor();
    selected.beginEditBlock();

    selected.removeSelectedText();
    selected.insertText(QApplication::clipboard()->text());

    selected.endEditBlock();
}

void dlgterminal::writeRcvdToFile(QString R, QString usedEncryption, QString encryptionKey, QString checkSum, QString sourceStation, QString DT, QString receivedMessage) {
    if ((DebugFunctions > 1) || (DebugTerminal > 0)) {qDebug() << Q_FUNC_INFO;}
    bool VZCZCMMM = false;
    WorkingVars.lastRX = receivedMessage;
    if (settingsTermVars.sendToFile == true) {
        QDir dir;
        QString tmpDir = settingsTermVars.ReceivedFolder;
        if (!dir.exists(tmpDir)) {dir.mkdir(tmpDir);}
        QString tmpTimestamp = QString::number(QDateTime::currentSecsSinceEpoch(), 10);
        QString tempDoc;
        tempDoc = receivedMessage;
        QTextStream file( &tempDoc, QIODevice::ReadOnly );
        QString fileLine ="";
        QString PRECEDENCE = "";
        QString DE  = "UNKN";
        QString RI  = "UNKN";
        QString NR  = "UNKN";
        QString DTG  = "UNKN";
        bool DoWhileState = true;
        while (!file.atEnd() && DoWhileState) {
            if (receivedMessage.contains("VZCZCMMM")) {
                VZCZCMMM = true;
                while (!fileLine.contains("VZCZCMMM")) {
                    fileLine = file.readLine();
                    //qDebug() << fileLine;

                }
                //qDebug() << "Found : VZCZCMMM";
                fileLine = file.readLine();
                //qDebug() << fileLine;
                if (!fileLine.contains("RR") && !fileLine.contains("PP") && !fileLine.contains("II") && !fileLine.contains("OO")) {
                    PRECEDENCE = "U";
                } else {
                    PRECEDENCE = fileLine.at(0);
                    fileLine = file.readLine();
                    //qDebug() << fileLine;
                }
                if (fileLine.contains("DE")) {
                    QStringList fileToken = fileLine.split(" ");
                    if (fileToken.size() == 4) {
                        DE = fileToken.at(0);
                        RI = fileToken.at(1);
                        NR = fileToken.at(2);
                        DTG = fileToken.at(3);
                    } else if (fileToken.size() == 5) {
                        DE = fileToken.at(1);
                        RI = fileToken.at(2);
                        NR = fileToken.at(3);
                        DTG = fileToken.at(4);
                    }
                }
                fileLine = file.readLine(); //ZNY EEEE
                fileLine = file.readLine();
                if (PRECEDENCE == "U" && (fileLine.contains("R ") || !fileLine.contains("P ") || !fileLine.contains("I ") || !fileLine.contains("O "))) {
                    PRECEDENCE = fileLine.at(0);
                }
                DoWhileState = false;
            }
            DoWhileState = false;
        }
        if (settingsTermVars.sendVZCZMMM && (VZCZCMMM != true)) {}
        else {
            QString tmpFilename = "";
            QString tmpStr  = "_" + PRECEDENCE + "_" + RI + "_" + NR + "_" + DTG +"_";
            tmpFilename = tmpDir + tmpStr + tmpTimestamp + ".txt";
            dir.setPath(tmpDir);
            QFile out(tmpFilename);
            out.open(QFile::WriteOnly);
            receivedMessage = receivedMessage.trimmed();
            receivedMessage.append("\r\n");
            receivedMessage.append("\r\n");
            receivedMessage.append("\r\n");
            receivedMessage.append("\r\nusedEncryption:");
            receivedMessage.append(usedEncryption);
            receivedMessage.append("\r\nencryptionKey:");
            receivedMessage.append(encryptionKey);
            receivedMessage.append("\r\ncheckSum:");
            receivedMessage.append(checkSum);
            receivedMessage.append("\r\nsourceStation:");
            receivedMessage.append(sourceStation);
            receivedMessage.append("\r\nDT:");
            receivedMessage.append(DT);
            out.write(receivedMessage.toUtf8());
            out.close();
            qDebug() << "Received file:" << tmpFilename;
        }
    }


    if ((SettingsVars.NCSTab == true) && (settingsTermVars.autoImportRoster == true) && (checkSum == "PASS") && (receivedMessage.contains("ZKS"))) {
        bool ZKSfound = false;
        QMessageBox msgBox;
        msgBox.setText("Attention");
        msgBox.setInformativeText("Confirm roster import from " + sourceStation);
        msgBox.setStandardButtons(QMessageBox::Ok | QMessageBox::Cancel);
        msgBox.setDefaultButton(QMessageBox::Cancel);
        int ret = msgBox.exec();
        if (ret != QMessageBox::Ok) { return;}
        auto ptrNCS = ptrWindow->wMainWindowTab->ptrdlgNCS;
        QTextStream in(&receivedMessage, QIODevice::ReadOnly);
        while (!in.atEnd()) {
            QString fileLine = in.readLine();
            if (fileLine == "ZKS") {
                ZKSfound = true;
                fileLine = in.readLine();
                const QSignalBlocker blocker(ptrNCS->RosterTable);
                for(int i=ptrNCS->RosterTable->rowCount(); i >= 0 ;i--) {
                    ptrNCS->RosterTable->removeRow(i);
                }
                RosterVar.reset();
                ptrWindow->updateStatusBar();
                ptrNCS->leCommand->clear();
            }
            if (ZKSfound == true) {
                if (fileLine == "BT") { break;}
                QStringList line = fileLine.split(" ");
                QString status = "";
                QString tasking = "";
                int leftParen = fileLine.indexOf("(");
                int rightParen = fileLine.indexOf(")");
                if ((leftParen != -1) && (rightParen != -1)) {
                    status = "[" + fileLine.mid((leftParen+1),(rightParen-leftParen-1)) + "]";
                }
                QString tmpCS = line.at(0);
                if (fileLine.contains("NCS")) {
                    tasking = " (NCS)";
                } else if (fileLine.contains("ANCS")) {
                    tasking = " (ANCS)";
                }
                ptrNCS->leCommand->append(tmpCS+" "+status +tasking);
                ptrNCS->commandExec();
            }
        }
    } else if ((SettingsVars.NCSTab == true) && (settingsTermVars.autoImportRoster == true) && (checkSum == "PASS") && receivedMessage.contains("NET:") && receivedMessage.contains("NCS:")) {
        bool NCSfound = false;
        QMessageBox msgBox;
        msgBox.setText("Attention");
        msgBox.setInformativeText("Confirm roster import from " + sourceStation);
        msgBox.setStandardButtons(QMessageBox::Ok | QMessageBox::Cancel);
        msgBox.setDefaultButton(QMessageBox::Cancel);
        int ret = msgBox.exec();
        if (ret != QMessageBox::Ok) { return;}
        auto ptrNCS = ptrWindow->wMainWindowTab->ptrdlgNCS;
        QTextStream in(&receivedMessage, QIODevice::ReadOnly);
        while (!in.atEnd()) {
            QString fileLine = in.readLine();
            if (fileLine.contains("NCS:")) {
                QString tasking = "";
                tasking = " (NCS)";
                NCSfound = true;
                const QSignalBlocker blocker(ptrNCS->RosterTable);
                for(int i=ptrNCS->RosterTable->rowCount(); i >= 0 ;i--) {
                    ptrNCS->RosterTable->removeRow(i);
                }
                RosterVar.reset();
                ptrWindow->updateStatusBar();
                ptrNCS->leCommand->clear();
                QStringList line = fileLine.split(" ");
                if (line.size() > 0 ){
                    ptrNCS->leCommand->append(line.at(1) + tasking);
                }
                fileLine = in.readLine();
            }
            if (NCSfound == true) {
                if (fileLine.contains("//")) { break;}
                QStringList line = fileLine.split(",");
                QString tmpCS = line.at(0);
                QString status = "";
                if (line.size() > 0) {
                    if (line.at(1) != "") { status = " [CLOSED " + line.at(1) + "]";}
                }
                QString tasking = "";
                if (fileLine.contains("ANCS")) { tasking = " (ANCS)";}
                ptrNCS->leCommand->append(tmpCS + tasking + status);
                ptrNCS->commandExec();
            }
        }

    }
}

void dlgterminal::on_treeWidgetConferences_itemClicked(QTreeWidgetItem *item, int column)
{
    //    qDebug() <<     ui->treeWidgetConferences->currentItem()->text(0);
    chattingWith = item->text(0);
    ui->actionSend_File->setEnabled(tcpSocket->isOpen());
    if(chattingWith.endsWith("-CN")){
        ui->comboBoxConfKeyList->setCurrentText(
                    configSettings.conferenceSettings.defaultCipherKey.value(chattingWith).toString());
        ui->groupBoxReceivedChats->setTitle("Conference chats "+chattingWith);
        ui->groupBoxMultiLineChat->setTitle("Multi Line chat in "+chattingWith);
        ui->groupBoxConfSingleLineChat->setTitle("Chatting in "+chattingWith);
    }
    else {
        ui->groupBoxReceivedChats->setTitle("Individual chats "+chattingWith);
        ui->groupBoxMultiLineChat->setTitle("Multi Line chat with "+chattingWith);
        ui->groupBoxConfSingleLineChat->setTitle("Chatting with "+chattingWith);
    }
    updatetextBrowserSentData();
}//on_treeWidgetConferences_itemClicked

void dlgterminal::updatetextBrowserSentData()
{
    ui->textBrowserChatConversation->clear();
    /*
     * Rcvd CT PASS 2018Aug04_0020Z: PW//J0G-CN DE AAM//Test single line chat// 0020Z K
     * Sent CT 2018Aug04_0020Z: J0G-CN DE AAM2//Test single line chat// 0020Z K
     */
    if(chattingWith.endsWith("-CN")){
        QString chatString = configSettings.conferenceChats.ChatText.value(chattingWith).toString();
        QStringList chatStringList = chatString.split('\n');
        QStringList tempStringList;
        foreach (QString tempString, chatStringList) {
            if(tempString.startsWith("Sent ") || tempString.startsWith("Rcvd"))
                tempStringList = tempString.split(": ");

            if(tempString.startsWith("Sent PT ") || tempString.startsWith("Rcvd PT ")){
                //                ui->textBrowserSentData->append("");
                if(ui->checkBoxShowTimestamps->isChecked()){
                    ui->textBrowserChatConversation->append(tempString);
                }//if show timestamps
                else {
                    ui->textBrowserChatConversation->append(tempStringList.last());
                }//else dont show timestamps
            }//if plaintext data header
            else if(tempString.startsWith("Sent CT ") || tempString.startsWith("Rcvd CT ")){
                //                ui->textBrowserSentData->append("");
                if(ui->checkBoxShowTimestamps->isChecked()){
                    ui->textBrowserChatConversation->append(tempString);
                }//if show timestamps
                else {
                    ui->textBrowserChatConversation->append(tempStringList.last());
                }//else dont show timestamps
            }//else ciphertext data header
            else {
                ui->textBrowserChatConversation->append(tempString);
            }//else multiline text so just display it
        }//for each line if conference chat text
    }//if chatting in conference room
    else{
        QString chatString = configSettings.individualChats.ChatText.value(chattingWith).toString();
        QStringList chatStringList = chatString.split('\n');
        QStringList tempStringList;
        foreach (QString tempString, chatStringList) {
            if(tempString.startsWith("Sent ") || tempString.startsWith("Rcvd"))
                tempStringList = tempString.split(": ");

            if(tempString.startsWith("Sent PT ") || tempString.startsWith("Rcvd PT ")){
                //                ui->textBrowserSentData->append("");
                if(ui->checkBoxShowTimestamps->isChecked()){
                    ui->textBrowserChatConversation->append(tempString);
                }//if show timestamps
                else {
                    ui->textBrowserChatConversation->append(tempStringList.last());
                }//else dont show timestamps
            }//if plaintext data header
            else if(tempString.startsWith("Sent CT ") || tempString.startsWith("Rcvd CT ")){
                //                ui->textBrowserSentData->append("");
                if(ui->checkBoxShowTimestamps->isChecked()){
                    ui->textBrowserChatConversation->append(tempString);
                }//if show timestamps
                else {
                    ui->textBrowserChatConversation->append(tempStringList.last());
                }//else dont show timestamps
            }//else ciphertext data header
            else {
                ui->textBrowserChatConversation->append(tempString);
            }//else multiline text so just display it
        }//for each line if conference chat text
    }//else chating with individual

    isNewChat.insert(chattingWith,false);
    updateConferenceSidebar();
    writeSettings();
}//updatetextBrowserSentData

void dlgterminal::updateConferenceSidebar()
{
    //udate the conference sidebar
    ui->treeWidgetConferences->clear();
    ui->treeWidgetConferences->setColumnCount(2);
    foreach (QString conferenceName, configSettings.conferenceSettings.conferenceMap.keys()) {
        QTreeWidgetItem * conferenceItem = new QTreeWidgetItem(ui->treeWidgetConferences);
        conferenceItem->setText(0,conferenceName);
        if(isNewChat.value(conferenceName))
            conferenceItem->setText(1,"*");

        foreach (QString callsign, configSettings.conferenceSettings.conferenceMap.value(conferenceName).toStringList()) {
            QTreeWidgetItem * callsignItem = new QTreeWidgetItem(conferenceItem);
            callsignItem->setText(0,callsign);
            if(isNewChat.value(callsign))
                callsignItem->setText(1,"*");

            conferenceItem->addChild(callsignItem);
        }
        ui->treeWidgetConferences->addTopLevelItem(conferenceItem);
    }
    ui->treeWidgetConferences->expandAll();

    //updata conference combo box
    ui->comboBoxConferenceList->clear();
    ui->comboBoxConferenceList->addItems(configSettings.conferenceSettings.conferenceMap.keys());
}//updateConferenceSidebar

void dlgterminal::on_pushButtonConfSendTx_clicked()
{
    QByteArray plaintextDataArray;
    if(ui->plainTextEditConfSendData->toPlainText().isEmpty())
        return;
    if(configSettings.generalSettings.callSignList.isEmpty()){
        QMessageBox noCallMsgBox;
        noCallMsgBox.setWindowTitle(tr(APP_NAME)+" - ERROR");
        noCallMsgBox.setText("ERROR - No call sign configured!");
        noCallMsgBox.exec();
        return;
    }//if not call sign configured
    if(chattingWith.isEmpty()){
        QMessageBox noConfMsgBox;
        noConfMsgBox.setWindowTitle(tr(APP_NAME)+" - ERROR");
        noConfMsgBox.setText("ERROR - No conference selected!");
        noConfMsgBox.exec();
        return;
    }//if no conference selected
    if(!tcpSocket->isOpen()){
        QMessageBox noTCPMsgBox;
        noTCPMsgBox.setWindowTitle(tr(APP_NAME)+" - ERROR");
        noTCPMsgBox.setText("ERROR - No TCP Connection!");
        noTCPMsgBox.exec();
        return;
    }//if no TCP connection

    //check for file:///
    if(ui->plainTextEditConfSendData->toPlainText().startsWith("file:///") &&
            ui->plainTextEditConfSendData->toPlainText().endsWith(".txt"))
    {
        //        qDebug() << "Drag and drop... OK";
        on_actionSend_ConfFile_triggered(ui->comboBoxConfKeyList->currentText(), ui->checkBoxConfCompress->isChecked());
        ui->plainTextEditConfSendData->clear();
        return;
    }//if file

    //    plaintextDataArray.append(chattingWith+" DE "+configSettings.generalSettings.callSign+"\n");
    QStringList tempStringList = ui->plainTextEditConfSendData->toPlainText().split("\n");
    ui->plainTextEditConfSendData->clear();

    foreach (QString tempString, tempStringList) {
        tempString.remove("\r");
        tempString = tempString.trimmed();
        tempString.append("\r\n");
        plaintextDataArray.append(" "+tempString);
    }
    if(!plaintextDataArray.isEmpty()){
        sendDataToProcessingChain(QString(chattingWith+" DE "+
                                          configSettings.generalSettings.callSignList.at(0)+
                                          "\n").toLatin1()+plaintextDataArray,
                                  ui->comboBoxConfKeyList->currentText(),
                                  ui->checkBoxConfCompress->isChecked(), chattingWith);//if the file wasn't empty send the data to cipher

        QByteArray chatTextDisplay, chatTextStore;
        QByteArray timeStamp;

        timeStamp.append("Sent ");
        if(ui->comboBoxConfKeyList->currentText() != "PLAINTEXT"){
            timeStamp.append("CT ");
        }
        else {
            timeStamp.append("PT ");
        }
        timeStamp.append(QDateTime::currentDateTimeUtc().toString("yyyyMMMdd_hhmmZ: "));

        if(ui->checkBoxShowTimestamps->isChecked()){
            chatTextDisplay.append(timeStamp);
        }
        chatTextStore.append(timeStamp);
        chatTextDisplay.append(chattingWith+" DE "+configSettings.generalSettings.callSignList.at(0)+"\n");
        chatTextDisplay.append(plaintextDataArray);
        chatTextStore.append(chattingWith+" DE "+configSettings.generalSettings.callSignList.at(0)+"\n");
        chatTextStore.append(plaintextDataArray);

        if(timeStamp.contains("Sent PT ")){
            ui->textBrowserChatConversation->append(chatTextDisplay);
        }
        else {
            ui->textBrowserChatConversation->append(chatTextDisplay);
        }

        if(chattingWith.endsWith("-CN")){
            QString chatString = configSettings.conferenceChats.ChatText.value(chattingWith).toString();
            chatString.append("\n"+chatTextStore);
            configSettings.conferenceChats.ChatText.insert(chattingWith,chatString);
        }
        else {
            QString chatString = configSettings.individualChats.ChatText.value(chattingWith).toString();
            chatString.append("\n"+chatTextStore);
            configSettings.individualChats.ChatText.insert(chattingWith,chatString);
        }
        writeSettings();
        updateConferenceSidebar();
    }//if we have data to send
    plaintextDataArray.clear();
}//on_pushButtonConfSendTx_clicked

void dlgterminal::on_pushButtonAddConference_clicked()
{
    if(ui->lineEditAddConference->text().isEmpty())
        return;
    QString newConference = ui->lineEditAddConference->text().toUpper();
    ui->lineEditAddConference->clear();
    if(!newConference.endsWith("-CN"))
        newConference.append("-CN");

    //if the conference is not in our list
    if(!configSettings.conferenceSettings.conferenceMap.keys().contains(newConference))
    {
        QStringList tempList;
        configSettings.conferenceSettings.conferenceMap.insert(newConference,tempList);
        qDebug() << configSettings.conferenceSettings.conferenceMap;
        writeSettings();
        //udate the conference sidebar
        updateConferenceSidebar();
    }
}//on_pushButtonAddConference_clicked

void dlgterminal::on_plainTextEditConfSendData_textChanged()
{
    ui->pushButtonConfSendTx->setEnabled(!ui->plainTextEditConfSendData->toPlainText().isEmpty() &&
                                         tcpSocket->isOpen());
    ui->pushButtonConfClearTx->setEnabled(!ui->plainTextEditConfSendData->toPlainText().isEmpty());
}//on_plainTextEditConfSendData_textChanged

void dlgterminal::on_textBrowserConfSentData_textChanged()
{
    ui->pushButtonConfSelectRx->setEnabled(!ui->textBrowserChatConversation->toPlainText().isEmpty());
    ui->pushButtonConfCopyRx->setEnabled(!ui->textBrowserChatConversation->toPlainText().isEmpty());
}//on_textBrowserConfSentData_textChanged

void dlgterminal::on_lineEditOrderWireText_returnPressed()
{
    QByteArray plaintextDataArray;
    if(ui->lineEditOrderWireText->text().isEmpty())
        return;
    if(configSettings.generalSettings.callSignList.isEmpty()){
        QMessageBox noCallMsgBox;
        noCallMsgBox.setWindowTitle(tr(APP_NAME)+" - ERROR");
        noCallMsgBox.setText("ERROR - No call sign configured!");
        noCallMsgBox.exec();
        return;
    }//if not call sign configured
    if(chattingWith.isEmpty()){
        QMessageBox noConfMsgBox;
        noConfMsgBox.setWindowTitle(tr(APP_NAME)+" - ERROR");
        noConfMsgBox.setText("ERROR - No conference selected!");
        noConfMsgBox.exec();
        return;
    }//if no conference selected
    if(!tcpSocket->isOpen()){
        QMessageBox noTCPMsgBox;
        noTCPMsgBox.setWindowTitle(tr(APP_NAME)+" - ERROR");
        noTCPMsgBox.setText("ERROR - No TCP Connection!");
        noTCPMsgBox.exec();
        return;
    }//if no TCP connection


    //check for file:///
    plaintextDataArray.clear();
    if(ui->lineEditOrderWireText->text().startsWith("file:///") &&
            ui->lineEditOrderWireText->text().endsWith(".txt")){
        //        qDebug() << "Drag and drop... OK";
        on_actionSend_File_triggered();
        ui->lineEditOrderWireText->clear();
        return;
    }

    plaintextDataArray.append(ui->lineEditOrderWireText->text().toLatin1());

    if(!plaintextDataArray.isEmpty()){
        QByteArray data;
        data.append(QString(chattingWith+" DE "+configSettings.generalSettings.callSignList.at(0)+" // ").toLatin1());
        data.append(QString(plaintextDataArray+" // "));
        if(!ui->lineEditConfInstructions->text().isEmpty())
            data.append(ui->lineEditConfInstructions->text().toLatin1()+" ");

        data.append(QDateTime::currentDateTimeUtc().time().toString("hhmmZ "));
        if(ui->checkBoxConfAR->isChecked())
            data.append("AR");
        else
            data.append("K");

        if (ui->cmboChatLAN->currentText() == "TCP & UDP") {
            sendDataToProcessingChain(data, ui->comboBoxConfKeyList->currentText(), ui->checkBoxConfCompress->isChecked(), chattingWith);//if the file wasn't empty send the data to cipher
            BroadcastChat(data,chattingWith);
        } else if (ui->cmboChatLAN->currentText() == "TCP Only (CP)") {
            sendDataToProcessingChain(data, ui->comboBoxConfKeyList->currentText(), ui->checkBoxConfCompress->isChecked(), chattingWith);//if the file wasn't empty send the data to cipher
        } else if (ui->cmboChatLAN->currentText() == "UDP Only (VPN)") {
            BroadcastChat(data,chattingWith);
        }

        QByteArray chatTextDisplay, chatTextStore;
        QByteArray timeStamp;

        timeStamp.append("Sent ");
        if(ui->comboBoxConfKeyList->currentText() != "PLAINTEXT"){
            timeStamp.append("CT ");
        }
        else {
            timeStamp.append("PT ");
        }
        timeStamp.append(QDateTime::currentDateTimeUtc().toString("yyyyMMMdd_hhmmZ: "));

        if(ui->checkBoxShowTimestamps->isChecked()){
            chatTextDisplay.append(timeStamp);
        }
        chatTextStore.append(timeStamp);
        //        if(!chatPasswords.value(chattingWith).toString().isEmpty())
        //            chatTextDisplay.append("PW//");
        chatTextDisplay.append(chattingWith+" DE "+configSettings.generalSettings.callSignList.at(0)+" // ");
        chatTextDisplay.append(plaintextDataArray+" // ");
        if(!ui->lineEditConfInstructions->text().isEmpty())
            chatTextDisplay.append(ui->lineEditConfInstructions->text()+" ");
        chatTextDisplay.append(QDateTime::currentDateTimeUtc().time().toString("hhmmZ "));
        if(ui->checkBoxConfAR->isChecked())
            chatTextDisplay.append("AR\n");
        else
            chatTextDisplay.append("K\n");

        //        if(!chatPasswords.value(chattingWith).toString().isEmpty())
        //            chatTextStore.append("PW//");
        chatTextStore.append(chattingWith+" DE "+configSettings.generalSettings.callSignList.at(0)+" // ");
        chatTextStore.append(plaintextDataArray+" // ");
        if(!ui->lineEditConfInstructions->text().isEmpty())
            chatTextStore.append(ui->lineEditConfInstructions->text()+" ");
        chatTextStore.append(QDateTime::currentDateTimeUtc().time().toString("hhmmZ "));
        if(ui->checkBoxConfAR->isChecked())
            chatTextStore.append("AR\n");
        else
            chatTextStore.append("K\n");

        if(timeStamp.contains("Sent PT ")){
            ui->textBrowserChatConversation->append(chatTextDisplay);
        }
        else {
            ui->textBrowserChatConversation->append(chatTextDisplay);
        }

        if(chattingWith.endsWith("-CN")){
            QString chatString = configSettings.conferenceChats.ChatText.value(chattingWith).toString();
            chatString.append("\n"+chatTextStore);
            configSettings.conferenceChats.ChatText.insert(chattingWith,chatString);
        }
        else {
            QString chatString = configSettings.individualChats.ChatText.value(chattingWith).toString();
            chatString.append("\n"+chatTextStore);
            configSettings.individualChats.ChatText.insert(chattingWith,chatString);
        }
        ui->pushButtonConfResend->setEnabled(true);
        ui->lineEditOrderWireText->clear();
        ui->lineEditConfInstructions->clear();

        writeSettings();
        updateConferenceSidebar();
        plaintextDataArray.clear();
    }//if we have data to send
}//on_lineEditOrderWireText_returnPressed

void dlgterminal::on_lineEditConfInstructions_returnPressed()
{
    on_lineEditOrderWireText_returnPressed();
}//on_lineEditConfInstructions_returnPressed


void dlgterminal::on_pushButtonSetDefaultCipherKey_clicked()
{
    if(chattingWith.endsWith("-CN")){
        configSettings.conferenceSettings.defaultCipherKey.insert(chattingWith,ui->comboBoxConfKeyList->currentText());
    }
    writeSettings();
}//on_pushButtonSetDefaultCipherKey_clicked

void dlgterminal::on_lineEditAddConference_returnPressed()
{
    on_pushButtonAddConference_clicked();
}//on_lineEditAddConference_returnPressed

void dlgterminal::on_pushButtonDeleteConference_clicked()
{
    QString conferenceName = ui->comboBoxConferenceList->currentText();
    //TODO - Need popup warning about deleting chat history
    //    qDebug() << "Deleting conference: " << conferenceName;
    configSettings.conferenceSettings.conferenceMap.remove(ui->comboBoxConferenceList->currentText());

    configSettings.conferenceChats.ChatText.insert(conferenceName,"");
    isNewChat.insert(conferenceName,false);
    ui->textBrowserChatConversation->clear();
    if(chattingWith == conferenceName)
    {
        ui->actionSend_File->setEnabled(false);
        chattingWith.clear();
        ui->groupBoxReceivedChats->setTitle("Conversation");
        ui->groupBoxMultiLineChat->setTitle("Multiline Chat");
        ui->groupBoxConfSingleLineChat->setTitle("Chat");
    }
    updateConferenceSidebar();
}//on_pushButtonDeleteConference_clicked

void dlgterminal::on_pushButtonClearChatHistory_clicked()
{
    QString conferenceName = ui->comboBoxConferenceList->currentText();
    //TODO - Need popup warning about deleting chat history
    //    qDebug() << "Deleting chat history: " << conferenceName;

    configSettings.conferenceChats.ChatText.insert(conferenceName,"");
    isNewChat.insert(conferenceName,false);
    ui->textBrowserChatConversation->clear();
    updateConferenceSidebar();
}//on_pushButtonClearChatHistory_clicked

void dlgterminal::on_pushButtonConfResend_clicked()
{
    if(configSettings.generalSettings.callSignList.isEmpty()){
        QMessageBox noCallMsgBox;
        noCallMsgBox.setWindowTitle(tr(APP_NAME)+" - ERROR");
        noCallMsgBox.setText("ERROR - No call sign configured!");
        noCallMsgBox.exec();
        return;
    }//if not call sign configured
    if(chattingWith.isEmpty()){
        QMessageBox noConfMsgBox;
        noConfMsgBox.setWindowTitle(tr(APP_NAME)+" - ERROR");
        noConfMsgBox.setText("ERROR - No conference selected!");
        noConfMsgBox.exec();
        return;
    }//if no conference selected
    if(!tcpSocket->isOpen()){
        QMessageBox noTCPMsgBox;
        noTCPMsgBox.setWindowTitle(tr(APP_NAME)+" - ERROR");
        noTCPMsgBox.setText("ERROR - No TCP Connection!");
        noTCPMsgBox.exec();
        return;
    }//if no TCP connection
    sendDataToProcessingChain(lastSentMessage, ui->comboBoxConfKeyList->currentText(), ui->checkBoxConfCompress->isChecked(), chattingWith);
}//on_pushButtonConfResend_clicked

void dlgterminal::on_comboBoxConfKeyList_currentTextChanged(const QString &arg1)
{
    QImage badIconImage(":/images/CryptoCommBadIcon.png");
    QImage goodIconImage(":/images/CryptoCommIcon.png");
    goodIconImage.scaledToWidth(ui->labelIcon->width());
    badIconImage.scaledToWidth(ui->labelIcon->width());

    if(!(ui->comboBoxConfKeyList->currentText() == "PLAINTEXT")){
        ui->labelChatIcon->setPixmap(QPixmap::fromImage(goodIconImage));
        ui->labelChatClassification->setText("AUTHORIZED TO PROCESS UNCLASSIFIED//FOUO ONLY");
    }//if CT is selected
    else{
        ui->labelChatIcon->setPixmap(QPixmap::fromImage(badIconImage));
        ui->labelChatClassification->setText("AUTHORIZED TO PROCESS UNCLASSIFIED ONLY");
    }//else CT is not selected
}

void dlgterminal::on_checkBoxShowTimestamps_clicked()
{
    updatetextBrowserSentData();
}

void dlgterminal::on_pushButtonPing_clicked()
{
    if(configSettings.generalSettings.callSignList.isEmpty()){
        QMessageBox noCallMsgBox;
        noCallMsgBox.setWindowTitle(tr(APP_NAME)+" - ERROR");
        noCallMsgBox.setText("ERROR - No call sign configured!");
        noCallMsgBox.exec();
        return;
    }//if not call sign configured
    if(ui->comboBoxSelectMachineAddress->currentText().isEmpty()){
        QMessageBox noCallMsgBox;
        noCallMsgBox.setWindowTitle(tr(APP_NAME)+" - ERROR");
        noCallMsgBox.setText("ERROR - No machine address selected!");
        noCallMsgBox.exec();
        return;
    }//if no machine address is selected
    if(!tcpSocket->isOpen()){
        QMessageBox noTCPMsgBox;
        noTCPMsgBox.setWindowTitle(tr(APP_NAME)+" - ERROR");
        noTCPMsgBox.setText("ERROR - No TCP Connection!");
        noTCPMsgBox.exec();
        return;
    }//no TCP connection
    QByteArray machineCommand;
    //    if(ui->radioButtonv2Machine->isChecked()){
    //        machineCommand.append("     \r\n");
    //        machineCommand.append(ui->comboBoxSelectMachineAddress->currentText()+ " DE "+configSettings.generalSettings.callSignList.at(0)+"\r\n");
    //        machineCommand.append("QRU "+ui->lineEditMessageParamaters->text().toUpper()+"\r\n");
    //        machineCommand.append("K\r\n");
    //    }//send v2 command syntax
    //    else {
    machineCommand.append(ui->comboBoxSelectMachineAddress->currentText());
    machineCommand.append(" DE "+configSettings.generalSettings.callSignList.at(0));
    machineCommand.append(" // ");
    machineCommand.append("VER");
//    if(ui->checkBoxCompressResults->isChecked()){
//        machineCommand.append(" [COMP]");
//    }//is compress results
    machineCommand.append(" // ");
    machineCommand.append(QDateTime::currentDateTimeUtc().toString("hhmmZ"));
    machineCommand.append(" K");
    //    }//sent v3 command syntax
    ui->textBrowserMachine->append(machineCommand);
    sendDataToProcessingChain(machineCommand, ui->comboBoxKeyList->currentText(), ui->checkBoxCompress->isChecked(), ui->comboBoxSelectMachineAddress->currentText().trimmed());
}
