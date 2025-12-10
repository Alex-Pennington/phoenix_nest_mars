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
#include "classxml.h"

QString ClassXML::getALEAddres() const
{
    return ALEAddress;
}

QString ClassXML::getPriority() const
{
    return priority;
}

QString ClassXML::getCompress() const
{
    return compress;
}

QString ClassXML::getEncrypt() const
{
    return encrypt;
}

QString ClassXML::getEncryptionKey() const
{
    return encryptionKey;
}

QString ClassXML::getSourceStation() const
{
    return sourceStation;
}

QString ClassXML::getDestinationStation() const
{
    return destinationStation;
}

QString ClassXML::getAntennaBeamHeading() const
{
    return antennaBeamHeading;
}

QString ClassXML::getListenIPAddress() const
{
    return listenIPAddress;
}

QString ClassXML::getListenTCPPort() const
{
    return listenTCPPort;
}

QString ClassXML::getRadioFrequency() const
{
    return radioFrequency;
}

QString ClassXML::getRadioChannel() const
{
    return radioChannel;
}

QString ClassXML::getRadioOPMode() const
{
    return radioOPMode;
}

QString ClassXML::getRadioTXMode() const
{
    return radioTXMode;
}

QString ClassXML::getModemDataRate() const
{
    return modemDataRate;
}

QString ClassXML::getModemInterleave() const
{
    return modemInterleave;
}

QString ClassXML::getModemWaveform() const
{
    return modemWaveform;
}

QString ClassXML::getChecksum() const
{
    return checksum;
}

QString ClassXML::getReceivedMessage() const
{
    return receivedMessage;
}

QStringList ClassXML::getReceivedMessageList() const
{
    return receivedMessageList;
}

int ClassXML::getReceivedMessageListCount() const
{
    return receivedMessageList.count();
}

QStringList ClassXML::getALEAddressList() const
{
    return ALEAddressList;
}

QStringList ClassXML::getCallSignList() const
{
    return callsignlist;
}

QString ClassXML::getCommand() const
{
    return command;
}

bool ClassXML::getWasCompressed() const
{
    return wasCompressed;
}

bool ClassXML::getWasEncrypted() const
{
    return this->wasEncrypted;
}

QString ClassXML::getPositionID() const
{
    return positionID;
}

QStringList ClassXML::getKeyList() const
{
    return keyList;
}

bool ClassXML::getParsingError() const
{
    return parsingError;
}

bool ClassXML::parseXMLDocument(QByteArray xmlDocument)
{
    bool returnError = true;

    if(xmlDocument.size() > 0)
    {
        QXmlStreamReader xmlReader(xmlDocument);
        int retryCount=0;
        while (!xmlReader.isStartDocument()) {
            xmlReader.readNext();
            retryCount++;
            if(retryCount > 10){
                return returnError;
            }
        }
        while (!xmlReader.atEnd()) {
            xmlReader.readNextStartElement();
            if(xmlReader.atEnd())
            {
                break;
            }
            if(xmlReader.name() == tr("V3PROTOCOL")){
                bool isV3PROTOCOL = true;
                protocol = "V3PROTOCOL";
                while (isV3PROTOCOL) {
                    xmlReader.readNextStartElement();
                    if(xmlReader.isEndElement() && xmlReader.name() == tr("V3PROTOCOL")){
                        isV3PROTOCOL = false;
                    }//end RIMPACKET
                    else if (xmlReader.name() == tr("HEADER")) {
                        returnError = false;
                        bool isHEADER = true;
                        while (isHEADER) {
                            xmlReader.readNextStartElement();
                            if(xmlReader.isEndElement() && xmlReader.name() == tr("HEADER")){
                                isHEADER = false;
                            }//end HEADER
                            else if (xmlReader.name() == tr("POSITIONID")) {
                                positionID = xmlReader.readElementText();
                            }
                            else if (xmlReader.name() == tr("COMMAND")) {
                                command = xmlReader.readElementText();
                            }
                            else if (xmlReader.name() == tr("KEYLIST")) {
                                QString keyListString = xmlReader.readElementText();
                                keyList=keyListString.split(',',Qt::SkipEmptyParts);
                                keyList.removeAll("");
                                keyList.removeAll(" ");
                            }
                            else if (xmlReader.name() == tr("CALLSIGNLIST")) {
                                QString callSignList = xmlReader.readElementText();
                                if(callSignList.contains(",")){
                                    callsignlist=callSignList.split(',',Qt::SkipEmptyParts);
                                    callsignlist.removeAll("");
                                    callsignlist.removeAll(" ");
                                }
                                else {
                                    callsignlist.append(callSignList);
                                }
                            }
                            else if (xmlReader.name() == tr("ALEADDRESSLIST")) {
                                QString aleAddressString = xmlReader.readElementText();
//                                if(aleAddressString.contains(",")){
                                    ALEAddressList = aleAddressString.split(',',Qt::SkipEmptyParts);
                                    ALEAddressList.removeAll("");
                                    ALEAddressList.removeAll(" ");
                                    ALEAddressList.prepend("");
//                                }
                            }
                            else if (xmlReader.name() == tr("LISTENIPADDRESS")) {
                                listenIPAddress = xmlReader.readElementText();
                            }
                            else if (xmlReader.name() == tr("LISTENTCPPORT")) {
                                listenTCPPort = xmlReader.readElementText();
                            }
                            else if (xmlReader.name() == tr("PRIORITY")) {
                                priority = xmlReader.readElementText();
                            }
                            else if (xmlReader.name() == tr("COMPRESS")) {
                                compress = xmlReader.readElementText();
                                wasCompressed = compress == "1" ? true : false;
                            }
                            else if (xmlReader.name() == tr("ENCRYPT")) {
                                encrypt = xmlReader.readElementText();
                                wasEncrypted = encrypt == "1" ? true : false;
                            }
                            else if (xmlReader.name() == tr("ENCRYPTIONKEY")) {
                                encryptionKey = xmlReader.readElementText();
                            }
                            else if (xmlReader.name() == tr("SOURCESTATION")) {
                                sourceStation = xmlReader.readElementText();
                                if(sourceStation.isEmpty()){
                                    sourceStation = globalConfigSettings.generalSettings.callSign;
                                }
                            }
                            else if (xmlReader.name() == tr("DESTINATIONSTATION")) {
                                destinationStation = xmlReader.readElementText();
                                if(destinationStation.isEmpty()){
                                    destinationStation = "ALL";
                                }
                            }
                            else if (xmlReader.name() == tr("ALEADDRESS")) {
                                ALEAddress = xmlReader.readElementText();
                            }
                            else if (xmlReader.name() == tr("ANTENNABEAMHEADING")) {
                                antennaBeamHeading = xmlReader.readElementText();
                            }
                            else if (xmlReader.name() == tr("RADIOCHANNEL")) {
                                radioChannel = xmlReader.readElementText();
                            }
                            else if (xmlReader.name() == tr("RADIOFREQUENCY")) {
                                radioFrequency = xmlReader.readElementText();
                            }
                            else if (xmlReader.name() == tr("RADIOOPMODE")) {
                                radioOPMode = xmlReader.readElementText();
                            }
                            else if (xmlReader.name() == tr("RADIOTXMODE")) {
                                radioTXMode = xmlReader.readElementText();
                            }
                            else if (xmlReader.name() == tr("MODEMDATARATE")) {
                                modemDataRate = xmlReader.readElementText();
                            }
                            else if (xmlReader.name() == tr("MODEMINTERLEAVE")) {
                                modemInterleave = xmlReader.readElementText();
                            }
                            else if (xmlReader.name() == tr("MODEMWAVEFORM")) {
                                modemWaveform = xmlReader.readElementText();
                            }
                            else if (xmlReader.name() == tr("CHECKSUM")) {
                                checksum = xmlReader.readElementText();
                            }
                            else {
                                qDebug() << "ERROR parsing xml header: " << xmlReader.tokenString() << xmlReader.name() << xmlReader.errorString() << xmlReader.lineNumber();
                                returnError = true;
                            }
                        }//while is HEADER
                    }//HEADER
                    else if (xmlReader.name() == tr("PAYLOAD")) {
                        bool isPAYLOAD = true;
                        while (isPAYLOAD) {
                            xmlReader.readNextStartElement();
                            if(xmlReader.isEndElement() && xmlReader.name() == tr("PAYLOAD")){
                                isPAYLOAD = false;
                            }//end PAYLOAD
                            else if (xmlReader.name() == tr("DATA")) {
                                QString tempString = xmlReader.readElementText();
                                receivedMessage.append(printables(tempString.toLatin1()));
                                QStringList messageStringList = receivedMessage.split('\n');
                                if(messageStringList.last().isEmpty())
                                    messageStringList.removeLast();
                                QString message;
                                foreach (QString tempString, messageStringList) {
                                    message.append(tempString+"\r\n");
                                }
                                receivedMessage = message;
                                receivedMessageList.append(receivedMessage);
                                messageText.append(receivedMessage.toLatin1());
                            }
                            else {
                                qDebug() << "ERROR parsing xml payload: " << xmlReader.tokenString() << xmlReader.name() << xmlReader.lineNumber();
                                returnError = true;
                            }
                        }//while isMESSAGES
                    }//else if PAYLOAD
                }//while iSV3PROTOCOL
            }//if V3PROTOCOL
            else if (xmlReader.name() == tr("MSDMT")) {
                protocol = "MSDMT";
                bool isDMTPROTOCOL = true;
                while (isDMTPROTOCOL) {
                    xmlReader.readNextStartElement();
                    if(xmlReader.isEndElement() && xmlReader.name() == tr("MSDMT")){
                        isDMTPROTOCOL = false;
                    }//end RIMPACKET
                    else if (xmlReader.name() == tr("HEADER")) {
                        returnError = false;
                        bool isHEADER = true;
                        while (isHEADER) {
                            xmlReader.readNextStartElement();
                            if(xmlReader.isEndElement() && xmlReader.name() == tr("HEADER")){
                                isHEADER = false;
                            }//end HEADER
                            else if (xmlReader.name() == tr("POSITIONID")) {
                                positionID = xmlReader.readElementText();
                            }
                            else if (xmlReader.name() == tr("COMMAND")) {
                                command = xmlReader.readElementText();
                            }
                            else if (xmlReader.name() == tr("LISTENIPADDRESS")) {
                                listenDMTAddress = xmlReader.readElementText();
                            }
                            else if (xmlReader.name() == tr("LISTENDATAPORT")) {
                                listenDMTDataPort = xmlReader.readElementText();
                            }
                            else if (xmlReader.name() == tr("LISTENCMDPORT")) {
                                listenDMTCmdPort = xmlReader.readElementText();
                            }
                            else {
                                qDebug() << "ERROR parsing xml payload: " << xmlReader.tokenString() << xmlReader.name() << xmlReader.lineNumber();
                                returnError = true;
                            }
                        }//while isHEADER
                    }//if HEADER
                }//while isDMTPROTOCOL
            }//else MSDMT
            else {
                qDebug() << "ERROR: Protocol not recognized!: " << xmlReader.tokenString() << xmlReader.name() << xmlReader.errorString() << xmlReader.lineNumber();
                returnError = true;
                break;
            }
        }//while not end of document
    }//if we have data
    return returnError;
}//parseXMLDocument

QString ClassXML::getListenDMTAddress() const
{
    return listenDMTAddress;
}

QString ClassXML::getListenDMTCmdPort() const
{
    return listenDMTCmdPort;
}

QString ClassXML::getListenDMTDataPort() const
{
    return listenDMTDataPort;
}

QString ClassXML::getProtocol() const
{
    return protocol;
}

QByteArrayList ClassXML::getMessageText() const
{
    return messageText;
}

ClassXML::ClassXML(QByteArray xmlDocument, QObject *parent) : QObject(parent)
{
    sourceStation.clear();
    destinationStation.clear();
    if(xmlDocument.size() > 0){
        this->parsingError = parseXMLDocument(xmlDocument);
    }
}

ClassXML::ClassXML(QObject *parent) : QObject(parent)
{

}

QByteArray ClassXML::wrap(QString type, QByteArray message, bool isHandshake, QString positionID, QString ALEStation,
                          QString compress, QString encrypt, QString encryptionKey, QString sourceStation,
                          QString destinationStation,QString modemSpeed, QString modemInterleave, XML_PRIORITY AType)
{
    QByteArray outData;
    QXmlStreamWriter xmlWriter(&outData);

    xmlWriter.setAutoFormatting(true);
    xmlWriter.writeStartDocument();
    xmlWriter.writeStartElement("V3PROTOCOL");
    xmlWriter.writeStartElement("HEADER");

    xmlWriter.writeTextElement("POSITIONID",positionID);
    xmlWriter.writeTextElement("COMMAND",type);
    if(isHandshake){
        if (type == "helo") {
        }
        else if (type == "ALE CALL") {
            xmlWriter.writeTextElement("ALEADDRESS",ALEStation);
        }

        xmlWriter.writeEndElement();//HEADER
    }//if HandShake
    else if(type == "data"){
            xmlWriter.writeTextElement("PRIORITY", calculatePriority(message,AType).trimmed());
            xmlWriter.writeTextElement("COMPRESS",compress.trimmed());
            xmlWriter.writeTextElement("ENCRYPT",encrypt.trimmed());
            xmlWriter.writeTextElement("ENCRYPTIONKEY",encryptionKey.trimmed());
            xmlWriter.writeTextElement("SOURCESTATION",sourceStation.trimmed());
            xmlWriter.writeTextElement("DESTINATIONSTATION",destinationStation.trimmed());
            xmlWriter.writeTextElement("ALEADDRESS",ALEStation.trimmed());
            xmlWriter.writeTextElement("MODEMDATARATE",modemSpeed.trimmed());
            xmlWriter.writeTextElement("MODEMINTERLEAVE",modemInterleave.trimmed());
            xmlWriter.writeTextElement("CHECKSUM","PASS");
            xmlWriter.writeEndElement();//HEADER
            xmlWriter.writeStartElement("PAYLOAD");
            //write a message
            xmlWriter.writeTextElement("DATA",message);
            //write more messages if sending bulk messages to CP
            xmlWriter.writeEndElement();//PAYLOAD
        }//if data
    xmlWriter.writeEndElement();//V3PROTOCOL
    xmlWriter.writeEndDocument();
    return outData;
}//wrap single message

QString ClassXML::calculatePriority(QByteArray dataByteArray, XML_PRIORITY type)
{
    QString messageString = dataByteArray;

    QStringList messageStringList = messageString.split("\n", Qt::SkipEmptyParts);
    foreach (QString tempString, messageStringList) {
        if(type == A_ROUTINE)
        {
            return "4";
        }
        else if (type == A_PRIORITY) {
            return "3";
        }
        else if (type == A_IMMEDIATE) {
            return "2";
        }
        else if (type == A_FLASH) {
            return "1";
        }
    }
    return "4";//return routine by default
}
