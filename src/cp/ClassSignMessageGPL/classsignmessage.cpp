/****************************************************************************
**
** Copyright (C) 2016 - 2020 GRSS <greenradiosoftware@gmail.com>
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

#include "classsignmessage.h"

#include <QCryptographicHash>

ClassSignMessage::ClassSignMessage()
{

}//ClassSignMessage

bool ClassSignMessage::checkMessageDigest(QString messageText)
{
    QByteArray foundHash;
    bool isMessageBody = false, foundClassificationLine = false;
    QString messageBody;

//     original message format
//     ~~~~~~~~~~~~~~~~~~~~~~~
//     ...
//     BT\r
//     UNCLASS EFTO\r
//     \r
//     TEXT LINE 1\r
//     \r
//     \r
//     TEXT LINE 2\r
//     \r
//     BT\r
//     \r...

    //gets converted to (all extraneous whitespace is replace by a single space):
    // ... BT UNCLASS EFTO TEXT LINE 1 TEXT LINE 2 BT ...
    //before digest is calculated.  This way we don't have to worry about how many
    //line breaks or spaces between words there are incase the message formatting
    //gets changed going across the network.  All we care about are the actual words
    // !!!case sensitive!!! and not the whitespace.

/*
Example 1:
Actual message string: "This is line 1\r\rThis is line 2\r\r\r\r\rThis is line 3\rThis is line 4"
Simplified message string: "This is line 1 This is line 2 This is line 3 This is line 4"

Example 2:
Actual message string: "This        is         line        1\r\rThis    is  line              2\r\r\r\r\rThis is         line 3\rThis is line                    4"
Simplified message string: "This is line 1 This is line 2 This is line 3 This is line 4"
*/

    //I use '\r' terminated line inside Message Creator
    //If you use other line terminations then you'll have to adjust what to split on to get unterminated lines.
    //Later we terminate lines with '\n' so we match the original text
    messageText.replace("\r\n","\n");
    QStringList messageTextList = messageText.split('\n');//you may need to split with your line termination methodology
    foreach (QString line, messageTextList) {
        if(line.startsWith("BT ") || line == "BT"){
            if(!isMessageBody){
                isMessageBody = true;
            }
            else {
                isMessageBody = false;
            }
            continue;
        }//if BT
        if(isMessageBody){
            //find first line with UNCLASS on it
            if(!foundClassificationLine && line.startsWith("UNCLAS")){
                foundClassificationLine = true;
                continue;
            }
            messageBody.append(line+"\n");//original text lines were terminated with '\r' so we need to match but in reality
                                          //it doesn't matter because they are replaced by a single space character.
        }
    }//foreach line in  message body

    foreach (QString line, messageTextList) {
        if(line.simplified().startsWith("[DIGEST:")){
            QString hash = line.simplified();
            hash.remove("[DIGEST:");
            hash.chop(1);
            foundHash = hash.toLatin1();
            break;
        }
    }//get digest

    //compare given digest to digest of truncated message text
    //we use simplified to ensure we remove any extraneous spaces
    //to create the message digest createMessageDigest() was called with messageText.simplified()
    //see message format above
    return  foundHash == createMessageDigest(messageBody.simplified());
}//checkMessageDigest

QByteArray ClassSignMessage::createMessageDigest(QString messageText)
{
    //     Send only message content (between the BTs for ACP messages) to not include classification
    //     Reasoning is procedure should play no part in calculating the message digest and the digest should
    //     solely be calculated only on the message content.  This allows digest to be used regardless of what
    //     procedure is used in propagating the message.
    //
    //     This digest also ignores extraneous whitespace to allow for the various idiosyncrasies in line
    //     termination or line spacing.  The digest is calculated solely on the actual characters and a single
    //     whitespace between words.


    /*
    Example 1:
    Actual message string: "This is line 1\r\rThis is line 2\r\r\r\r\rThis is line 3\rThis is line 4"
    Simplified message string: "This is line 1 This is line 2 This is line 3 This is line 4"

    Example 2:
    Actual message string: "This        is         line        1\r\rThis    is  line              2\r\r\r\r\rThis is         line 3\rThis is line                    4"
    Simplified message string: "This is line 1 This is line 2 This is line 3 This is line 4"
    */
    return QCryptographicHash::hash(messageText.toLatin1(),QCryptographicHash::Md5).toHex().toUpper();
}//createMessageDigest
