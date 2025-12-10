#include "classroster.h"
#include "main.h"
#include "debug.h"

#include <QComboBox>

QColor clsRoster::Color() {
    WorkingVars.RelayColor++;
    if (WorkingVars.RelayColor > 8) { WorkingVars.RelayColor = 1;}
    switch (WorkingVars.RelayColor) {
    case 1 :
        return Qt::black;
    case 2 :
        return Qt::gray;
    case 3 :
        return Qt::blue;
    case 4 :
        return Qt::darkRed;
    case 5 :
        return Qt::lightGray;
    case 6 :
        return Qt::magenta;
    case 7 :
        return Qt::darkBlue;
    case 8 :
        return Qt::cyan;
    }
    return Qt::black;
}

clsRoster::clsRoster() {
    if ((DebugFunctions > 2) || (Debug_clsRoster > 1)) {qDebug() << Q_FUNC_INFO;}
    pa = new sRoster[10];
    for (int i = 0; i < 10; i++) {
        pa[i].CallSign = "";
        pa[i].AbrCallSign = "";
        pa[i].Lat = "";
        pa[i].Lon = "";
        pa[i].LocDupe = false;
        pa[i].isMerged = 0;
        pa[i].bounds = QRect(0,0,0,0);
        pa[i].Status = "";
        pa[i].rColor = Color();
        pa[i].relDB = 0;
        pa[i].HowCopy = "";
    }
    length = 10;
    nextIndex = 0;
}

clsRoster::~clsRoster() {
    delete [] pa;
}

sRoster& clsRoster::operator[](int index) {
    if (Debug_clsRoster > 8) {qDebug() << Q_FUNC_INFO;}
    sRoster *pnewa; // pointer for new array
    if (index >= length) { // is element in the array?
        pnewa = new sRoster[index + 10]; // allocate a bigger array
        for (int i = 0; i < nextIndex; i++) // copy old values
            pnewa[i] = pa[i];

        for (int j = nextIndex; j < index + 10; j++) { // initialize remainder
            qDebug() << "clsRoster::operator[](int index)" << j;
            pa[j].CallSign = "";
            pa[j].AbrCallSign = "";
            pa[j].Lat = "";
            pa[j].Lon = "";
            pa[j].LocDupe = false;
            pa[j].isMerged = 0;
            pa[j].bounds = QRect(0,0,0,0);
            pa[j].Status = "";
            pa[j].rColor = Color();
            pa[j].relDB = 0;
            pa[j].HowCopy = "";
        }
        length = index + 10; // set length to bigger size
        qDebug() << "clsRoster::operator[](int index)";
        delete [] pa; // delete the old array
        qDebug() << "clsRoster::operator[](int index)";
        pa = pnewa; // reassign the new array
    }
    if (index > nextIndex) // set nextIndex past index
        nextIndex = index + 1;

    return *(pa + index); // a reference to the element

}

void clsRoster::add(QString vCallSign, QString vAbrCallSign, QString vLat, QString vLon, bool vLocDupe, int vIsMerged, int relDB) {
    if ((DebugFunctions > 8) || (Debug_clsRoster > 1)) {qDebug() << "clsRoster::add" << vCallSign << vAbrCallSign << vLat<< vLon << vLocDupe << vIsMerged << relDB;}
    sRoster *pnewa;
    if (nextIndex == (length - 1) ) {
        length = length + 10;
        pnewa = new sRoster[length];
        for (int i = 0; i < nextIndex; i++) {
            pnewa[i] = pa[i];
        }
        for (int j = nextIndex; j < length; j++) {
            pnewa[j].CallSign = "";
            pnewa[j].AbrCallSign = "";
            pnewa[j].Lat = "";
            pnewa[j].Lon = "";
            pnewa[j].LocDupe = false;
            pnewa[j].isMerged = 0;
            pnewa[j].bounds = QRect(0,0,0,0);
            pnewa[j].Status = "";
            pnewa[j].rColor = Color();
            pnewa[j].relDB = 0;
            pnewa[j].HowCopy = "";
        }
        delete [] pa;
        pa = pnewa;
    }
    pa[nextIndex].CallSign = vCallSign;
    pa[nextIndex].AbrCallSign = vAbrCallSign;
    pa[nextIndex].Lat = vLat;
    pa[nextIndex].Lon = vLon;
    pa[nextIndex].LocDupe = vLocDupe;
    pa[nextIndex].isMerged = vIsMerged;
    pa[nextIndex].Status = "";
    pa[nextIndex].rColor = Color();
    pa[nextIndex].relDB = relDB;

    if (SettingsVars.ToolsAreaisShown) {
        ptrWindow->cmboStation1->addItem(vAbrCallSign,nextIndex);
        ptrWindow->cmboStation2->addItem(vAbrCallSign,nextIndex);
    }

    nextIndex++;
}

bool clsRoster::searchCS ( QString searchTerm ) {
    if ((DebugFunctions > 8) || (Debug_clsRoster > 1)) {qDebug() << Q_FUNC_INFO;}
    for ( int i = 0 ; i < nextIndex ; i++ ) {
        if ( pa[i].CallSign == searchTerm ) {
            searchResult = i;
            return true;
        }
    }
    return false;
}

bool clsRoster::searchAbrCS ( QString searchTerm ) {
    if ((DebugRenderAreaPaintEvent > 0) || (Debug_clsRoster > 7)) {qDebug() << Q_FUNC_INFO << searchTerm;}
    for ( int i = 0 ; i < nextIndex ; i++ ) {
        if ( pa[i].AbrCallSign == searchTerm ) {
            searchResult = i;
            return true;
        }
    }
    return false;
}

bool clsRoster::searchAbrCSContains (QString searchTerm , int exclude) {
    if ((DebugRenderAreaPaintEvent > 0) || (Debug_clsRoster > 7)) {qDebug() << Q_FUNC_INFO;}
    for ( int i = 0 ; i < nextIndex ; i++ ) {
        if (i != exclude) {
            if ((pa[i].AbrCallSign.contains(searchTerm, Qt::CaseInsensitive))) {
                searchResult = i;
                return true;
            }
        }
    }
    return false;
}

int clsRoster::size() {
    if (Debug_clsRoster > 8) {qDebug() << Q_FUNC_INFO;}
    return (nextIndex - 1);
}

void clsRoster::reset() {
    if ((DebugFunctions > 2) || (Debug_clsRoster > 1)) {qDebug() << Q_FUNC_INFO;}
    delete [] pa;
    pa = new sRoster[10];
    for (int i = 0; i < 10; i++) {
        pa[i].CallSign = "";
        pa[i].AbrCallSign = "";
        pa[i].Lat = "";
        pa[i].Lon = "";
        pa[i].LocDupe = false;
        pa[i].isMerged = 0;
        pa[i].bounds = QRect(0,0,0,0);
        pa[i].Status = "";
        pa[i].rColor = Color();
        pa[i].HowCopy = "";
    }
    length = 10;
    nextIndex = 0;
    if (SettingsVars.ToolsAreaisShown) {
        ptrWindow->cmboStation1->clear();
        ptrWindow->cmboStation2->clear();
    }
}

QStringList clsRoster::list() {
    if ((DebugFunctions > 2) || (Debug_clsRoster > 1)) {qDebug() << Q_FUNC_INFO;}
    QStringList tmpList = {};
    for (int i = 0; i < nextIndex ; ++i) {
        tmpList << pa[i].CallSign;
    }
    return tmpList;
}

void clsRoster::addBounds (int index, QRect bounds) {
    if (Debug_clsRoster > 9) {qDebug() << Q_FUNC_INFO;}
    if (index <= (nextIndex -1)) {
        pa[index].bounds = bounds;
    }
}

bool clsRoster::searchBounds (QPoint searchTerm){
    if ((DebugFunctions > 2) || (Debug_clsRoster > 1)) {qDebug() << Q_FUNC_INFO;}
    int bpX = 0;
    int bpY = 0;
    if (SettingsVars.PreserveAspectRatio) {
        bpX = int((searchTerm.x() - Xoff) / ScaleFactor);
        bpY = int((searchTerm.y() - Yoff) / ScaleFactor);
    } else {
        bpX = int((searchTerm.x() - Xoff) / ScaleFactor);
        bpY = int((searchTerm.y() - Yoff) / ScaleFactor);
    }
    for ( int i = 0 ; i < nextIndex ; i++ ) {
        if ( pa[i].bounds.contains(QPoint(bpX,bpY),false) ) {
            searchResult = i;
            return true;
        }
    }
    return false;
}

void clsRoster::updateStatus(int index, QString Status) {
    if ((DebugFunctions > 2) || (Debug_clsRoster > 1)) {qDebug() << Q_FUNC_INFO;}
    if (index <= (nextIndex -1)) {
        pa[index].Status = Status;
    }
}

void clsRoster::remove(int rIdx) {
    if ((DebugFunctions > 2) || (Debug_clsRoster > 1)) {qDebug() << Q_FUNC_INFO;}
    if ((rIdx <0) || (rIdx > length)) {return;}
    if (SettingsVars.ToolsAreaisShown) {
        ptrWindow->cmboStation1->removeItem(rIdx);
        ptrWindow->cmboStation2->removeItem(rIdx);
    }
    int tmpNextIndex = 0;
    sRoster * pnewa = new sRoster[length];
    for (int i = 0; i < length; i++) {
        if (i != rIdx) {
            pnewa[tmpNextIndex].Lat = pa[i].Lat;
            pnewa[tmpNextIndex].Lon = pa[i].Lon;
            pnewa[tmpNextIndex].relDB = pa[i].relDB;
            pnewa[tmpNextIndex].Status = pa[i].Status;
            pnewa[tmpNextIndex].bounds = pa[i].bounds;
            pnewa[tmpNextIndex].rColor = pa[i].rColor;
            pnewa[tmpNextIndex].LocDupe = pa[i].LocDupe;
            pnewa[tmpNextIndex].CallSign = pa[i].CallSign;
            pnewa[tmpNextIndex].isMerged = pa[i].isMerged;
            pnewa[tmpNextIndex].AbrCallSign = pa[i].AbrCallSign;
            pnewa[tmpNextIndex].HowCopy = pa[i].HowCopy;

            tmpNextIndex++;
        }
    }
    delete [] pa;
    nextIndex--;
    pa = pnewa;

}

void clsRoster::updateTable(int rIdx) {
//    if ((SettingsVars.NCSTab == true) & (ptrExternDlgNCS != 0)) {
//        const QSignalBlocker blocker(ptrExternDlgNCS->RosterTable);
//        QTableWidgetItem * vCallSignItem = new QTableWidgetItem;
//        vCallSignItem->setText(pa[rIdx].CallSign);
//        QTableWidgetItem *StatusItem = new QTableWidgetItem;
//        StatusItem->setText(pa[rIdx].Status);
//        ptrExternDlgNCS->RosterTable->setItem(rIdx, 0, vCallSignItem);
//        ptrExternDlgNCS->RosterTable->setItem(rIdx, 3, StatusItem);
//        //ptrExternDlgNCS->RosterTable->resizeColumnsToContents();
//        ptrExternDlgNCS->RosterTable->resizeRowsToContents();
//    }
}

void clsRoster::dupeCheck() {
    if ((DebugFunctions > 2) || (Debug_clsRoster > 1)) {qDebug() << Q_FUNC_INFO;}
    for (int pri = 0 ; pri <= RosterVar.size(); pri ++) {
        RosterVar[pri].LocDupe = false;
        if (RosterVar[pri].relDB !=0 ) {
            RosterVar[pri].AbrCallSign = WorkingDB[RosterVar[pri].relDB].AbrCallSign;
        } else {
            RosterVar[pri].AbrCallSign = RosterVar[pri].CallSign;
        }
    }
    for (int pri = 0 ; pri <= RosterVar.size(); pri ++) {
        if(!RosterVar[pri].LocDupe) {
            for (int snd=pri; snd <= RosterVar.size(); snd++) {
                if (pri != snd) {
                    if((RosterVar[pri].Lat == RosterVar[snd].Lat) & (RosterVar[pri].Lon == RosterVar[snd].Lon)){
                        if (!RosterVar[pri].AbrCallSign.contains(RosterVar[snd].AbrCallSign)){
                            if (DebugCheckLogLevel > 7) {qDebug() << "LocDupe: " << RosterVar[pri].AbrCallSign << " : " << RosterVar[snd].AbrCallSign;}
                            RosterVar[pri].AbrCallSign += "\n" + RosterVar[snd].AbrCallSign;
                            RosterVar[snd].LocDupe = true;
                        }
                    }
                }
            }
        }
    }
}

void clsRoster::moveUp(int rIdx) {
    if ((DebugFunctions > 1) || (Debug_clsRoster > 1)) {qDebug() << Q_FUNC_INFO;}
    if ((rIdx < 1) || (rIdx > nextIndex)) {return;}
    const QSignalBlocker blocker(ptrWindow->wMainWindowTab->ptrdlgNCS->RosterTable);
    sRoster tmpCell;
    tmpCell.CallSign = pa[rIdx-1].CallSign;
    tmpCell.AbrCallSign = pa[rIdx-1].AbrCallSign;
    tmpCell.Lat = pa[rIdx-1].Lat;
    tmpCell.Lon = pa[rIdx-1].Lon;
    tmpCell.LocDupe = pa[rIdx-1].LocDupe;
    tmpCell.isMerged = pa[rIdx-1].isMerged;
    tmpCell.Status = pa[rIdx-1].Status;
    tmpCell.bounds = pa[rIdx-1].bounds;
    tmpCell.rColor = pa[rIdx-1].rColor;
    tmpCell.relDB = pa[rIdx-1].relDB;
    tmpCell.HowCopy = pa[rIdx-1].HowCopy;

    pa[rIdx-1].CallSign = pa[rIdx].CallSign;
    pa[rIdx-1].AbrCallSign = pa[rIdx].AbrCallSign;
    pa[rIdx-1].Lat = pa[rIdx].Lat;
    pa[rIdx-1].Lon = pa[rIdx].Lon;
    pa[rIdx-1].LocDupe = pa[rIdx].LocDupe;
    pa[rIdx-1].isMerged = pa[rIdx].isMerged;
    pa[rIdx-1].Status = pa[rIdx].Status;
    pa[rIdx-1].bounds = pa[rIdx].bounds;
    pa[rIdx-1].rColor = pa[rIdx].rColor;
    pa[rIdx-1].relDB = pa[rIdx].relDB;
    pa[rIdx-1].HowCopy = pa[rIdx].HowCopy;

    pa[rIdx].CallSign = tmpCell.CallSign;
    pa[rIdx].AbrCallSign = tmpCell.AbrCallSign;
    pa[rIdx].Lat = tmpCell.Lat;
    pa[rIdx].Lon = tmpCell.Lon;
    pa[rIdx].LocDupe = tmpCell.LocDupe;
    pa[rIdx].isMerged = tmpCell.isMerged;
    pa[rIdx].Status = tmpCell.Status;
    pa[rIdx].bounds = tmpCell.bounds;
    pa[rIdx].rColor = tmpCell.rColor;
    pa[rIdx].relDB = tmpCell.relDB;
    pa[rIdx].HowCopy = tmpCell.HowCopy;

    QTableWidgetItem *CSItem = new QTableWidgetItem;
    QTableWidgetItem *HowCopyItem = new QTableWidgetItem;
    QTableWidgetItem *StatusItem = new QTableWidgetItem;
    QTableWidgetItem *TaskItem = new QTableWidgetItem;

    QTableWidgetItem *CSItem2 = new QTableWidgetItem;
    QTableWidgetItem *HowCopyItem2 = new QTableWidgetItem;
    QTableWidgetItem *StatusItem2 = new QTableWidgetItem;
    QTableWidgetItem *TaskItem2 = new QTableWidgetItem;

    CSItem = ptrWindow->wMainWindowTab->ptrdlgNCS->RosterTable->takeItem(rIdx-1,0);
    HowCopyItem = ptrWindow->wMainWindowTab->ptrdlgNCS->RosterTable->takeItem(rIdx-1,1);
    StatusItem = ptrWindow->wMainWindowTab->ptrdlgNCS->RosterTable->takeItem(rIdx-1,2);
    TaskItem = ptrWindow->wMainWindowTab->ptrdlgNCS->RosterTable->takeItem(rIdx-1,3);

    CSItem2 = ptrWindow->wMainWindowTab->ptrdlgNCS->RosterTable->takeItem(rIdx,0);
    HowCopyItem2 = ptrWindow->wMainWindowTab->ptrdlgNCS->RosterTable->takeItem(rIdx,1);
    StatusItem2 = ptrWindow->wMainWindowTab->ptrdlgNCS->RosterTable->takeItem(rIdx,2);
    TaskItem2 = ptrWindow->wMainWindowTab->ptrdlgNCS->RosterTable->takeItem(rIdx,3);

    ptrWindow->wMainWindowTab->ptrdlgNCS->RosterTable->setItem(rIdx-1, 0, CSItem2);
    ptrWindow->wMainWindowTab->ptrdlgNCS->RosterTable->setItem(rIdx-1, 1, HowCopyItem2);
    ptrWindow->wMainWindowTab->ptrdlgNCS->RosterTable->setItem(rIdx-1, 2, StatusItem2);
    ptrWindow->wMainWindowTab->ptrdlgNCS->RosterTable->setItem(rIdx-1, 3, TaskItem2);

    ptrWindow->wMainWindowTab->ptrdlgNCS->RosterTable->setItem(rIdx, 0, CSItem);
    ptrWindow->wMainWindowTab->ptrdlgNCS->RosterTable->setItem(rIdx, 1, HowCopyItem);
    ptrWindow->wMainWindowTab->ptrdlgNCS->RosterTable->setItem(rIdx, 2, StatusItem);
    ptrWindow->wMainWindowTab->ptrdlgNCS->RosterTable->setItem(rIdx, 3, TaskItem);

}

void clsRoster::moveDown(int rIdx) {
    if ((DebugFunctions > 1) || (Debug_clsRoster > 1)) {qDebug() << Q_FUNC_INFO;}
    if ((rIdx < 0) || (rIdx > nextIndex - 2)) {return;}
    sRoster tmpCell;
    const QSignalBlocker blocker(ptrWindow->wMainWindowTab->ptrdlgNCS->RosterTable);
    tmpCell.CallSign = pa[rIdx+1].CallSign;
    tmpCell.AbrCallSign = pa[rIdx+1].AbrCallSign;
    tmpCell.Lat = pa[rIdx+1].Lat;
    tmpCell.Lon = pa[rIdx+1].Lon;
    tmpCell.LocDupe = pa[rIdx+1].LocDupe;
    tmpCell.isMerged = pa[rIdx+1].isMerged;
    tmpCell.Status = pa[rIdx+1].Status;
    tmpCell.bounds = pa[rIdx+1].bounds;
    tmpCell.rColor = pa[rIdx+1].rColor;
    tmpCell.relDB = pa[rIdx+1].relDB;
    tmpCell.HowCopy = pa[rIdx+1].HowCopy;

    pa[rIdx+1].CallSign = pa[rIdx].CallSign;
    pa[rIdx+1].AbrCallSign = pa[rIdx].AbrCallSign;
    pa[rIdx+1].Lat = pa[rIdx].Lat;
    pa[rIdx+1].Lon = pa[rIdx].Lon;
    pa[rIdx+1].LocDupe = pa[rIdx].LocDupe;
    pa[rIdx+1].isMerged = pa[rIdx].isMerged;
    pa[rIdx+1].Status = pa[rIdx].Status;
    pa[rIdx+1].bounds = pa[rIdx].bounds;
    pa[rIdx+1].rColor = pa[rIdx].rColor;
    pa[rIdx+1].relDB = pa[rIdx].relDB;
    pa[rIdx+1].HowCopy = pa[rIdx].HowCopy;

    pa[rIdx].CallSign = tmpCell.CallSign;
    pa[rIdx].AbrCallSign = tmpCell.AbrCallSign;
    pa[rIdx].Lat = tmpCell.Lat;
    pa[rIdx].Lon = tmpCell.Lon;
    pa[rIdx].LocDupe = tmpCell.LocDupe;
    pa[rIdx].isMerged = tmpCell.isMerged;
    pa[rIdx].Status = tmpCell.Status;
    pa[rIdx].bounds = tmpCell.bounds;
    pa[rIdx].rColor = tmpCell.rColor;
    pa[rIdx].relDB = tmpCell.relDB;
    pa[rIdx].HowCopy = tmpCell.HowCopy;

    QTableWidgetItem *CSItem = new QTableWidgetItem;
    QTableWidgetItem *HowCopyItem = new QTableWidgetItem;
    QTableWidgetItem *StatusItem = new QTableWidgetItem;
    QTableWidgetItem *TaskItem = new QTableWidgetItem;

    QTableWidgetItem *CSItem2 = new QTableWidgetItem;
    QTableWidgetItem *HowCopyItem2 = new QTableWidgetItem;
    QTableWidgetItem *StatusItem2 = new QTableWidgetItem;
    QTableWidgetItem *TaskItem2 = new QTableWidgetItem;

    CSItem = ptrWindow->wMainWindowTab->ptrdlgNCS->RosterTable->takeItem(rIdx+1,0);
    HowCopyItem = ptrWindow->wMainWindowTab->ptrdlgNCS->RosterTable->takeItem(rIdx+1,1);
    StatusItem = ptrWindow->wMainWindowTab->ptrdlgNCS->RosterTable->takeItem(rIdx+1,2);
    TaskItem = ptrWindow->wMainWindowTab->ptrdlgNCS->RosterTable->takeItem(rIdx+1,3);

    CSItem2 = ptrWindow->wMainWindowTab->ptrdlgNCS->RosterTable->takeItem(rIdx,0);
    HowCopyItem2 = ptrWindow->wMainWindowTab->ptrdlgNCS->RosterTable->takeItem(rIdx,1);
    StatusItem2 = ptrWindow->wMainWindowTab->ptrdlgNCS->RosterTable->takeItem(rIdx,2);
    TaskItem2 = ptrWindow->wMainWindowTab->ptrdlgNCS->RosterTable->takeItem(rIdx,3);

    ptrWindow->wMainWindowTab->ptrdlgNCS->RosterTable->setItem(rIdx+1, 0, CSItem2);
    ptrWindow->wMainWindowTab->ptrdlgNCS->RosterTable->setItem(rIdx+1, 1, HowCopyItem2);
    ptrWindow->wMainWindowTab->ptrdlgNCS->RosterTable->setItem(rIdx+1, 2, StatusItem2);
    ptrWindow->wMainWindowTab->ptrdlgNCS->RosterTable->setItem(rIdx+1, 3, TaskItem2);

    ptrWindow->wMainWindowTab->ptrdlgNCS->RosterTable->setItem(rIdx, 0, CSItem);
    ptrWindow->wMainWindowTab->ptrdlgNCS->RosterTable->setItem(rIdx, 1, HowCopyItem);
    ptrWindow->wMainWindowTab->ptrdlgNCS->RosterTable->setItem(rIdx, 2, StatusItem);
    ptrWindow->wMainWindowTab->ptrdlgNCS->RosterTable->setItem(rIdx, 3, TaskItem);

}
