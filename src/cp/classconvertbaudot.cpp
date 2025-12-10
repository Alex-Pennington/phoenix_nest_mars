#include "classconvertbaudot.h"
#include "baudot.h"

ClassConvertBaudot::ClassConvertBaudot(QObject *parent) : QObject(parent)
{

}

QByteArray ClassConvertBaudot::convertBaudottoASCII(QByteArray baudotMessage)
{
    QByteArray returnMsg;
    // try to find a match of the current character in baudot
    foreach (QChar charByte, baudotMessage) {
        qDebug() << charByte << lettersMap.value(charByte) << figuresMap.value(charByte);
        if(charByte == 0x1b){//FIGS
            isFigures = true;
            continue;
        }
        else if (charByte == 0x1f) {//LTRS
            isFigures = false;
            continue;
        }
        else if (charByte == 0x00) {
            continue;
        }

        if(isFigures){
            if(figuresMap.keys().contains(charByte)){
                returnMsg.append(figuresMap.value(charByte));
            }
        }
        else {
            if(lettersMap.keys().contains(charByte)){
                returnMsg.append(lettersMap.value(charByte));
            }
        }
    }//for each char in msg
    return returnMsg;
}//convertBaudottoASCII

QByteArray ClassConvertBaudot::convertASCIItoBaudot(QByteArray ASCIIMessage)
{
    QByteArray returnMsg;
    foreach (QChar charByte, ASCIIMessage) {
        //check to see if charByte is FIGS or LTRS
        if(lettersMap.values().contains(charByte)){
            if(isFigures){
                isFigures = false;
                figureChange = true;
            }//if we're in FIGS set FIGS to false and set change to true
            else {
                figureChange=false;
            }//else we havent changed
        }//if charByte is a figure
        else if(figuresMap.values().contains(charByte)) {
            if(isFigures){
                figureChange = false;
            }//if we're already in FIGS set change to false
            else {
                isFigures = true;
                figureChange = true;
            }//else we just changed to FIGS
        }//else we're a letter
        else {
            continue;
        }//else we've sent a character not in the Baudot tables
        if(isFigures && figureChange){
            qDebug() << charByte << figuresMap.key(charByte);
            returnMsg.append(FIGS+figuresMap.key(charByte));
        }//if we've just changed to figures send FIGS symbol with charByte
        else if (!isFigures && figureChange) {
            qDebug() << charByte << lettersMap.key(charByte);
            returnMsg.append(LTRS+lettersMap.key(charByte));
        }//if we've just changed to letters send LTRS symbol with charByte
        else if(isFigures){
            returnMsg.append(figuresMap.key(charByte));
        }//else append figures map symbol
        else {
            returnMsg.append(lettersMap.key(charByte));
        }//else append letters map symbol

    }//for each ASCII character is message
    return returnMsg;
}//convertASCIItoBaudot
