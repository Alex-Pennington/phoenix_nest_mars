#include "classchan.h"
#include "debug.h"

#include <QDebug>

classchan::classchan() {
    if ((DebugFunctions > 1)){qDebug() << "classchan::classchan()";}
    pa = new chan[10];
    for (int i = 0; i < 10; i++) {
        pa[i].ChIdx = "";
        pa[i].Freq = 0;

    }
    length = 10;
    nextIndex = 0;
}

classchan::~classchan() {
    if ((DebugFunctions > 1) || (Debug_clsDB > 0)){qDebug() << "clsDB::~clsDB()";}
    delete [] pa;
}

chan& classchan::operator[](int index) {
    if (Debug_clsDB > 8) {qDebug() << Q_FUNC_INFO;}
    if ((index == 9999) or (index > length)) {
        index = 0;
    }
    chan *pnewa; // pointer for new array
    if (index >= length) { // is element in the array?
        pnewa = new chan[index + 10]; // allocate a bigger array
        for (int i = 0; i < nextIndex; i++) // copy old values
            pnewa[i] = pa[i];
        for (int j = nextIndex; j < index + 10; j++) { // initialize remainder
            pa[j].ChIdx = "";
            pa[j].Freq = 0;

        }
        length = index + 10; // set length to bigger size
        delete [] pa; // delete the old array
        pa = pnewa; // reassign the new array
    }
    if (index > nextIndex) // set nextIndex past index
        nextIndex = index + 1;
    return *(pa + index); // a reference to the element
}

void classchan::add(QString vChIdx, double vFreq) {
    if ((DebugFunctions > 9) || (Debug_clsDB > 8)) {qDebug() << "classchan::add " << vChIdx << vFreq;}
    chan *pnewa;
    if (nextIndex == (length - 1) ) {
        length = length + 10;
        pnewa = new chan[length];
        for (int i = 0; i < nextIndex; i++) {
            pnewa[i] = pa[i];
        }
        for (int j = nextIndex; j < length; j++) {
            pnewa[j].ChIdx = "";
            pnewa[j].Freq = 0;
        }
        delete [] pa;
        pa = pnewa;
    }
    pa[nextIndex].ChIdx= vChIdx;
    pa[nextIndex].Freq = vFreq;

    nextIndex++;
}

bool classchan::searchIdx ( QString searchTerm ) {
    if (DebugFunctions > 4) {qDebug() << Q_FUNC_INFO;}
    for ( int i = 0 ; i < nextIndex ; i++ ) {
        if (pa[i].ChIdx == searchTerm) {
            searchResult = i;
            return true;
        }
    }
    return false;
}

int classchan::size() {
    if (DebugFunctions > 1) {qDebug() << Q_FUNC_INFO;}
    return (nextIndex - 1);
}

void classchan::print() {
    if (DebugFunctions > 2) {qDebug() << Q_FUNC_INFO;}
    for ( int i = 0 ; i < nextIndex ; i++ ) {
        qDebug() << pa[i].ChIdx << pa[i].Freq;
    }
    return;
}

int classchan::parseWChanCSV (QString vWorkingChanCSV, relFreq prtVarRelFreq[]) {
    QList<QString> ret;
    bool ChanNotFoundFlag = false;
    QString ChannelsNotFound = "";
    int maxCount = 0;
    QStringList lineToken = vWorkingChanCSV.split(",", QString::SplitBehavior::SkipEmptyParts);
    if (lineToken.size() >= 24) {
        maxCount = 24;
        //display error
    } else {
        maxCount = lineToken.size();
    }
    for (int i=0 ; i < maxCount ; i++) {

        prtVarRelFreq[i].ChanId = lineToken.at(i);
        if (searchIdx(lineToken.at(i))) {
            double varFreq = pa[searchResult].Freq / 1000;
            prtVarRelFreq[i].Mhz = varFreq;
            prtVarRelFreq[i].padFreq = pad(varFreq,2,2);
        } else {
            ChanNotFoundFlag = true;
            ChannelsNotFound = ChannelsNotFound + " " + lineToken.at(i);
        }
    }
    if (ChanNotFoundFlag) {
        //Display Message Box
    }
    return maxCount;
}

QString classchan::pad(double input, int beforePoint, int afterPoint) {
        QString ret;
        const QString string = QString::number(input,'f');
        //qDebug() << "string" << string;
        const QStringList split = string.split('.');
        //qDebug() << split;
        if (split.size() == 2) {
            QString IntegerPart = split.at(0).rightJustified(beforePoint, ' ');
            ret = IntegerPart + "." + split.at(1).leftJustified(afterPoint, '0',true);
        }
        //qDebug() << ret;
        return ret;
    }
