#include "classdb.h"
#include "debug.h"

#include <QDebug>

clsDB::clsDB() {
    if ((DebugFunctions > 1) || (Debug_clsDB > 0)){qDebug() << "clsDB::clsDB()";}
    pa = new sDB[10];
    for (int i = 0; i < 10; i++) {
        pa[i].CallSign = "";
        pa[i].AbrCallSign = "";
        pa[i].Lat = "";
        pa[i].Lon = "";
        pa[i].isMerged = 0;
        pa[i].WGSLat = "";
        pa[i].WGSLon = "";
    }
    length = 10;
    nextIndex = 0;
}

clsDB::~clsDB() {
    if ((DebugFunctions > 1) || (Debug_clsDB > 0)){qDebug() << "clsDB::~clsDB()";}
    delete [] pa;
}

sDB& clsDB::operator[](int index) {
    if (Debug_clsDB > 8) {qDebug() << Q_FUNC_INFO;}
    if ((index == 9999) or (index > length)) {
        index = 0;
    }
    sDB *pnewa; // pointer for new array
    if (index >= length) { // is element in the array?
        pnewa = new sDB[index + 10]; // allocate a bigger array
        for (int i = 0; i < nextIndex; i++) // copy old values
            pnewa[i] = pa[i];
        for (int j = nextIndex; j < index + 10; j++) { // initialize remainder
            pa[j].CallSign = "";
            pa[j].AbrCallSign = "";
            pa[j].Lat = "";
            pa[j].Lon = "";
            pa[j].isMerged = 0;
            pa[j].WGSLat = "";
            pa[j].WGSLon = "";
        }
        length = index + 10; // set length to bigger size
        delete [] pa; // delete the old array
        pa = pnewa; // reassign the new array
    }
    if (index > nextIndex) // set nextIndex past index
        nextIndex = index + 1;
    return *(pa + index); // a reference to the element
}

void clsDB::add(QString vCallSign, QString vAbrCallSign, QString vLat, QString vLon, int vIsMerged, QString vWGSLat,QString vWGSLon) {
    if (Debug_clsDB > 6) {qDebug() << "clsDB::add" << nextIndex << vCallSign << vAbrCallSign << vLat << vLon << vIsMerged << vWGSLat << vWGSLon;}
    sDB *pnewa;
    if (nextIndex == (length - 1) ) {
        length = length + 10;
        pnewa = new sDB[length];
        for (int i = 0; i < nextIndex; i++) {
            pnewa[i] = pa[i];
        }
        for (int j = nextIndex; j < length; j++) {
            pnewa[j].CallSign = "";
            pnewa[j].AbrCallSign = "";
            pnewa[j].Lat = "";
            pnewa[j].Lon = "";
            pnewa[j].isMerged = 0;
            pnewa[j].WGSLat = "";
            pnewa[j].WGSLon = "";
        }
        delete [] pa;
        pa = pnewa;
    }
    pa[nextIndex].CallSign = vCallSign;
    pa[nextIndex].AbrCallSign = vAbrCallSign;
    pa[nextIndex].Lat = vLat;
    pa[nextIndex].Lon = vLon;
    pa[nextIndex].isMerged = vIsMerged;
    pa[nextIndex].WGSLat = vWGSLat;
    pa[nextIndex].WGSLon = vWGSLon;

    nextIndex++;
}

bool clsDB::searchCS ( QString searchTerm ) {
    if ((DebugFunctions > 8) || (Debug_clsDB > 2)) {qDebug() << Q_FUNC_INFO;}
    for ( int i = 0 ; i < nextIndex ; i++ ) {
        if ( (pa[i].CallSign == searchTerm)  || (pa[i].CallSign == searchTerm + "/T")) {
            searchResult = i;
            return true;
        }
    }
    return false;
}

bool clsDB::searchAbrCS (QString searchTerm) {
    if (Debug_clsDB > 0) {qDebug() << Q_FUNC_INFO << searchTerm;}
    for ( int i = 0 ; i < nextIndex ; i++ ) {
        if ( pa[i].AbrCallSign == searchTerm ) {
            searchResult = i;
            return true;
        }
    }
    return false;
}

int clsDB::size() {
    if ((DebugFunctions > 1) || (Debug_clsDB > 0)) {qDebug() << Q_FUNC_INFO;}
    return (nextIndex - 1);
}

void clsDB::reset() {
    if ((DebugFunctions > 1) || (Debug_clsDB > 0)) {qDebug() << Q_FUNC_INFO;}
    delete [] pa;
    pa = new sDB[10];
    for (int i = 0; i < 10; i++) {
        pa[i].CallSign = "";
        pa[i].AbrCallSign = "";
        pa[i].Lat = "";
        pa[i].Lon = "";
        pa[i].isMerged = 0;
        pa[i].WGSLat = "";
        pa[i].WGSLon = "";
    }
    length = 10;
    nextIndex = 0;
}
