/****************************************************************************
**
** Copyright (C) 2016 - 2019 Timothy Millea <timothy.j.millea@gmail.com>
** Released under GPL License
** See gpl-3.0.txt included with the source code.
**
****************************************************************************/

//#include "globals.h"

#include "classxml.h"
#include <QDebug>

QString ClassXML::getALEAddres() const
{
    return ALEAddress;
}//getALEAddres

QString ClassXML::getPriority() const
{
    return priority;
}//getPriority

QString ClassXML::getCompress() const
{
    return compress;
}//getCompress

QString ClassXML::getEncrypt() const
{
    return encrypt;
}//getEncrypt

QString ClassXML::getEncryptionKey() const
{
    return encryptionKey;
}//getEncryptionKey

QString ClassXML::getSourceStation() const
{
    return sourceStation;
}//getSourceStation

QString ClassXML::getDestinationStation() const
{
    return destinationStation;
}//getDestinationStation

QString ClassXML::getAntennaBeamHeading() const
{
    return antennaBeamHeading;
}//getAntennaBeamHeading

QString ClassXML::getListenIPAddress() const
{
    return listenIPAddress;
}//getListenIPAddress

QString ClassXML::getListenTCPPort() const
{
    return listenTCPPort;
}//getListenTCPPort

QString ClassXML::getRadioFrequency() const
{
    return radioFrequency;
}//getRadioFrequency

QString ClassXML::getRadioChannel() const
{
    return radioChannel;
}//getRadioChannel

QString ClassXML::getRadioOPMode() const
{
    return radioOPMode;
}//getRadioOPMode

QString ClassXML::getRadioTXMode() const
{
    return radioTXMode;
}//getRadioTXMode

QString ClassXML::getModemDataRate() const
{
    return modemDataRate;
}//getModemDataRate

QString ClassXML::getModemInterleave() const
{
    return modemInterleave;
}//getModemInterleave

QString ClassXML::getModemWaveform() const
{
    return modemWaveform;
}//getModemWaveform

QString ClassXML::getChecksum() const
{
    return checksum;
}//getChecksum

QString ClassXML::getReceivedMessage() const
{
    return receivedMessage;
}//getReceivedMessage

QStringList ClassXML::getReceivedMessageList() const
{
    return receivedMessageList;
}//getReceivedMessageList

int ClassXML::getReceivedMessageListCount() const
{
    return receivedMessageList.count();
}//getReceivedMessageList

QStringList ClassXML::getALEAddressList() const
{
    return ALEAddressList;
}//XMLParser()

QString ClassXML::getCommand() const
{
    return command;
}//getCommand

bool ClassXML::getWasCompressed() const
{
    return wasCompressed;
}//getWasCompressed

bool ClassXML::getWasEncrypted() const
{
    return wasEncrypted;
}//getWasEncrypted

QString ClassXML::getPositionID() const
{
    return positionID;
}//getPositionID

QStringList ClassXML::getKeyList() const
{
    return keyList;
}//parseXMLDocument

bool ClassXML::getParsingError() const
{
    return parsingError;
}//getParsingError

bool ClassXML::parseXMLDocument(QByteArray xmlDocument)
{
//    qDebug() << xmlDocument;
    bool returnError = true;

    if(xmlDocument.size() > 0)
    {
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
            if(xmlReader.name() == "V3PROTOCOL"){
                bool isV3PROTOCOL = true;
                while (isV3PROTOCOL) {
                    xmlReader.readNextStartElement();
//                    qDebug() << xmlReader.tokenString() << xmlReader.name();
                    if(xmlReader.isEndElement() && xmlReader.name() == "V3PROTOCOL"){
                        isV3PROTOCOL = false;
                    }//end RIMPACKET
                    else if (xmlReader.name() == "HEADER") {
                        returnError = false;
                        bool isHEADER = true;
                        while (isHEADER) {
                            xmlReader.readNextStartElement();
//                            qDebug() << xmlReader.tokenString() << xmlReader.name();
                            if(xmlReader.isEndElement() && xmlReader.name() == "HEADER"){
                                isHEADER = false;
                            }//end HEADER
                            //else grab all the header info
                            else if (xmlReader.name() == "POSITIONID") {
                                positionID = xmlReader.readElementText();
                            }
                            else if (xmlReader.name() == "COMMAND") {
                                command = xmlReader.readElementText();
                            }
                            else if (xmlReader.name() == "KEYLIST") {
                                QString keyListString = xmlReader.readElementText();
                                if(keyListString.contains(",")){
                                    keyList=keyListString.split(',',QString::SkipEmptyParts);
                                    keyList.removeAll("");
                                    keyList.removeAll(" ");
                                }//if we have a comma separated list
                                else {
                                    keyList.append(keyListString);
                                }
                            }//KEYLIST
                            else if (xmlReader.name() == "ALEADDRESSLIST") {
                                QString aleAddressString = xmlReader.readElementText();
                                if(aleAddressString.contains(",")){
                                    ALEAddressList=aleAddressString.split(',',QString::SkipEmptyParts);
                                    ALEAddressList.removeAll("");
                                    ALEAddressList.removeAll(" ");
                                    ALEAddressList.prepend("");
                                }//if we have a comma separated list
                            }//ALEADDRESSLIST
                            else if (xmlReader.name() == "LISTENIPADDRESS") {
                                listenIPAddress = xmlReader.readElementText();
                            }
                            else if (xmlReader.name() == "LISTENTCPPORT") {
                                listenTCPPort = xmlReader.readElementText();
                            }
                            else if (xmlReader.name() == "PRIORITY") {
                                priority = xmlReader.readElementText();
                            }
                            else if (xmlReader.name() == "COMPRESS") {
                                wasCompressed = xmlReader.readElementText().toInt();
                            }//
                            else if (xmlReader.name() == "CALLSIGNLIST") {
                                QString callsigns = xmlReader.readElementText();
                                callsignList = callsigns.split(',');
                                callsignList.removeAll("");
                                callsignList.removeAll(" ");

                            }//CALLSIGNLIST
                            else if (xmlReader.name() == "ENCRYPT") {
                                wasEncrypted = xmlReader.readElementText().toInt();
                            }
                            else if (xmlReader.name() == "ENCRYPTIONKEY") {
                                encryptionKey = xmlReader.readElementText();
                            }
                            else if (xmlReader.name() == "SOURCESTATION") {
                                sourceStation = xmlReader.readElementText();
                            }
                            else if (xmlReader.name() == "DESTINATIONSTATION") {
                                destinationStation = xmlReader.readElementText();
                            }
                            else if (xmlReader.name() == "ALEADDRESS") {
                                ALEAddress = xmlReader.readElementText();
                            }
                            else if (xmlReader.name() == "ANTENNABEAMHEADING") {
                                antennaBeamHeading = xmlReader.readElementText();
                            }
                            else if (xmlReader.name() == "RADIOCHANNEL") {
                                radioChannel = xmlReader.readElementText();
                            }
                            else if (xmlReader.name() == "RADIOFREQUENCY") {
                                radioFrequency = xmlReader.readElementText();
                            }
                            else if (xmlReader.name() == "RADIOOPMODE") {
                                radioOPMode = xmlReader.readElementText();
                            }
                            else if (xmlReader.name() == "RADIOTXMODE") {
                                radioTXMode = xmlReader.readElementText();
                            }
                            else if (xmlReader.name() == "MODEMDATARATE") {
                                modemDataRate = xmlReader.readElementText();
                            }
                            else if (xmlReader.name() == "MODEMINTERLEAVE") {
                                modemInterleave = xmlReader.readElementText();
                            }
                            else if (xmlReader.name() == "MODEMWAVEFORM") {
                                modemWaveform = xmlReader.readElementText();
                            }
                            else if (xmlReader.name() == "CHECKSUM") {
                                checksum = xmlReader.readElementText();
                            }//checksum
                            else if (xmlReader.name() == "WASAUTH") {
                                wasAuthenticated = xmlReader.readElementText();
                            }//checksum
                            else if (xmlReader.name() == "AUTHPASSED") {
                                authPassed = xmlReader.readElementText();
                            }//checksum
                            else if (xmlReader.name() == "AUTHAGENCY") {
                                authAgency = xmlReader.readElementText();
                            }//checksum
                            else {
                                qDebug() << "ERROR parsing xml header: " << xmlReader.tokenString() << xmlReader.name() << xmlReader.lineNumber();
                                returnError = true;
                            }
                        }//while is HEADER
                    }//HEADER
                    else if (xmlReader.name() == "PAYLOAD") {
                        bool isPAYLOAD = true;
                        while (isPAYLOAD) {
                            xmlReader.readNextStartElement();
//                            qDebug() << xmlReader.tokenString() << xmlReader.name();
                            if(xmlReader.isEndElement() && xmlReader.name() == "PAYLOAD"){
                                isPAYLOAD = false;
                            }//end PAYLOAD
                            else if (xmlReader.name() == "DATA") {
                                receivedMessage.append(printables(xmlReader.readElementText().toLatin1()));
//                                qDebug() << Q_FUNC_INFO << receivedMessage;
                                receivedMessageList.append(receivedMessage);
                            }//if DATA
                            else {
                                qDebug() << "ERROR parsing xml payload: " << xmlReader.tokenString() << xmlReader.name() << xmlReader.lineNumber();
                                returnError = true;
                            }
                        }//while isMESSAGES
                    }//else if PAYLOAD
                }//while iSV3PROTOCOL
            }//if RMI PACKET
        }//while not end of document
    }//if we have data
    return returnError;
}//parseXMLDocument

QByteArray ClassXML::printables(QByteArray byteArray)
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

QString ClassXML::calculatePriority(QByteArray dataByteArray)
{
    //    qDebug() << dataByteArray;
    QString messageString = dataByteArray;

    QStringList messageStringList = messageString.split("\n", QString::SkipEmptyParts);
    foreach (QString tempString, messageStringList) {
        if(tempString.startsWith("RR ") ||
                tempString.startsWith("R "))
        {
            return "4";
        }
        else if (tempString.startsWith("PP ") ||
                 tempString.startsWith("P ")) {
            return "3";
        }
        else if (tempString.startsWith("OO ") ||
                 tempString.startsWith("O ")) {
            return "2";
        }
        else if (tempString.startsWith("ZZ ") ||
                 tempString.startsWith("Z ")) {
            return "1";
        }
    }//foreach
    return "4";//return routine by default
}

QString ClassXML::getAuthAgency() const
{
    return authAgency;
}

QString ClassXML::getAuthPassed() const
{
    return authPassed;
}

QString ClassXML::getWasAuthenticated() const
{
    return wasAuthenticated;
}

QStringList ClassXML::getCallsignList() const
{
    return callsignList;
}//calculatePriority

ClassXML::ClassXML(QByteArray xmlDocument, QObject *parent) : QObject(parent)
{
    if(xmlDocument.size() > 0){
        this->parsingError = parseXMLDocument(xmlDocument);
    }
}//ClassXML

ClassXML::ClassXML(QObject *parent) : QObject(parent)
{

}//ClassXML

//QByteArray ClassXML::wrap(QByteArray message, bool isHandshake, QString positionID, QString ALEStation,
//                          QString compress, QString encrypt, QString encryptionKey, QString sourceStation,
//                          QString destinationStation)
//{
//    QByteArray outData;
//    QXmlStreamWriter xmlWriter(&outData);

//    xmlWriter.setAutoFormatting(true);
//    xmlWriter.writeStartDocument();
//    xmlWriter.writeStartElement("V3PROTOCOL");
//    //    xmlWriter.writeComment("This is a comment");
//    xmlWriter.writeStartElement("HEADER");

//    xmlWriter.writeTextElement("POSITIONID",positionID);
//    if(isHandshake){
//        xmlWriter.writeTextElement("COMMAND",message);
// //       xmlWriter.writeTextElement("ALEADDRESS",ALEStation);
//        xmlWriter.writeEndElement();//HEADER
//    }//if HandShake
//    else {
//        xmlWriter.writeTextElement("COMMAND", "data");
//        xmlWriter.writeTextElement("PRIORITY", calculatePriority(message));
//        xmlWriter.writeTextElement("COMPRESS",compress);
//        xmlWriter.writeTextElement("ENCRYPT",encrypt);
//        xmlWriter.writeTextElement("ENCRYPTIONKEY",encryptionKey);
//        xmlWriter.writeTextElement("SOURCESTATION",sourceStation);
//        xmlWriter.writeTextElement("DESTINATIONSTATION",destinationStation);
//        xmlWriter.writeTextElement("ALEADDRESS",ALEStation);
//        //        xmlWriter.writeTextElement("ANTENNABEAMHEADING","");
//        //        xmlWriter.writeTextElement("KEYLIST","");
//        //        xmlWriter.writeTextElement("ALEADDRESSLIST","");
//        //        xmlWriter.writeTextElement("LISTENIPADDRESS","");
//        //        xmlWriter.writeTextElement("LISTENTCPPORT","");
//        //        xmlWriter.writeTextElement("RADIOCHANNEL","");
//        //        xmlWriter.writeTextElement("RADIOFREQUENCY","");
//        //        xmlWriter.writeTextElement("RADIOOPMODE","");
//        //        xmlWriter.writeTextElement("RADIOTXMODE","");
//        //        xmlWriter.writeTextElement("MODEMDATARATE","");
//        //        xmlWriter.writeTextElement("MODEMINTERLEAVE","");
//        //        xmlWriter.writeTextElement("MODEMWAVEFORM","");
//        //        xmlWriter.writeTextElement("CHECKSUM","");
//        xmlWriter.writeEndElement();//HEADER
//        xmlWriter.writeStartElement("PAYLOAD");
//        //write a message
//        xmlWriter.writeTextElement("DATA",message);
//        //write more messages if sending bulk messages to RMI
//        xmlWriter.writeEndElement();//PAYLOAD
//    }//else not handshake

//    xmlWriter.writeEndElement();//V3PROTOCOL
//    xmlWriter.writeEndDocument();
//    return outData;
//}//wrap

QByteArray ClassXML::wrap(QString type, QByteArray message, bool isHandshake, QString positionID, QString ALEStation,
                          QString compress, QString encrypt, QString encryptionKey, QString sourceStation,
                          QString destinationStation,QString modemSpeed, QString modemInterleave)
{
    QByteArray outData;
    QXmlStreamWriter xmlWriter(&outData);

    xmlWriter.setAutoFormatting(true);
    xmlWriter.writeStartDocument();
    xmlWriter.writeStartElement("V3PROTOCOL");
    //    xmlWriter.writeComment("This is a comment");
    xmlWriter.writeStartElement("HEADER");

    xmlWriter.writeTextElement("POSITIONID",positionID);
    xmlWriter.writeTextElement("COMMAND",type);
    if(isHandshake){
        if (type == "helo") {
            xmlWriter.writeTextElement("LISTENIPADDRESS","127.0.0.1");
            xmlWriter.writeTextElement("LISTENTCPPORT","5009");
        }
        else if (type == "ALE CALL") {
            xmlWriter.writeTextElement("ALEADDRESS",ALEStation);
        }

        xmlWriter.writeEndElement();//HEADER
    }//if HandShake
    else {
        if(type == "data"){
            xmlWriter.writeTextElement("PRIORITY", calculatePriority(message).trimmed());
            xmlWriter.writeTextElement("COMPRESS",compress.trimmed());
            xmlWriter.writeTextElement("ENCRYPT",encrypt.trimmed());
            xmlWriter.writeTextElement("ENCRYPTIONKEY",encryptionKey.trimmed());
            xmlWriter.writeTextElement("SOURCESTATION",sourceStation.trimmed());
            xmlWriter.writeTextElement("DESTINATIONSTATION",destinationStation.trimmed());
            xmlWriter.writeTextElement("ALEADDRESS",ALEStation.trimmed());
            //        xmlWriter.writeTextElement("ANTENNABEAMHEADING","");
            //        xmlWriter.writeTextElement("KEYLIST","");
            //        xmlWriter.writeTextElement("ALEADDRESSLIST","");
            xmlWriter.writeTextElement("LISTENIPADDRESS","127.0.0.1");
            xmlWriter.writeTextElement("LISTENTCPPORT","5009");
            //        xmlWriter.writeTextElement("RADIOCHANNEL","");
            //        xmlWriter.writeTextElement("RADIOFREQUENCY","");
            //        xmlWriter.writeTextElement("RADIOOPMODE","");
            //        xmlWriter.writeTextElement("RADIOTXMODE","");
            xmlWriter.writeTextElement("MODEMDATARATE",modemSpeed.trimmed());
            xmlWriter.writeTextElement("MODEMINTERLEAVE",modemInterleave.trimmed());
            //        xmlWriter.writeTextElement("MODEMWAVEFORM","");
            //        xmlWriter.writeTextElement("CHECKSUM","");
            xmlWriter.writeEndElement();//HEADER
            xmlWriter.writeStartElement("PAYLOAD");
            //write a message
            xmlWriter.writeTextElement("DATA",message);
            //write more messages if sending bulk messages to RMI
            xmlWriter.writeEndElement();//PAYLOAD
        }//if data
    }//else not handshake
    xmlWriter.writeEndElement();//V3PROTOCOL
    xmlWriter.writeEndDocument();
    return outData;
}//wrap single message
