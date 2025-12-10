#include "mainwindowtab.h"
#include "main.h"
#include "debug.h"
//#include "geocalc.h"
#include "globals.h"

//#include "classspelchecker.h"
#include "currentinfo.h"

#include <QVBoxLayout>
#include <QDebug>
#include <QDialog>

QTextEdit * dlgLog::Log_textEdit = 0;
QTextEdit * dlgNotes::Notes_textEdit = 0;
QTableWidget * dlgNCS::RosterTable = 0;


mainwindowtab::mainwindowtab(QWidget *parent): QWidget (parent) {
    if ((DebugFunctions > 1) || (DebugProp > 0)) {qDebug() << Q_FUNC_INFO;}

    tabWidget = new QTabWidget;
    tabWidget->addTab(ptrRenderArea, tr("Map"));

    ptrdlgProp = new dlgProp();
    tabWidget->addTab(ptrdlgProp, tr("Propagation"));

    if (SettingsVars.NCSTab == true) {
        ptrdlgNCS = new dlgNCS();
        ptrExternDlgNCS = ptrdlgNCS;
        tabWidget->addTab(ptrdlgNCS, tr("NCS"));
    }

    ptrdlgNotes = new dlgNotes();
    tabWidget->addTab(ptrdlgNotes, tr("Scratchpad"));
    if (SettingsVars.TerminalTab) {
        ptrdlgterminal = new dlgterminal();
        tabWidget->addTab(ptrdlgterminal, tr("Terminal"));
    }

    tabWidgetLibrary = new QTabWidget;
    tabWidget->addTab(tabWidgetLibrary,"Library");

    ptrdlgPoppler = new dlgpoppler();
    tabWidgetLibrary->addTab(ptrdlgPoppler, tr("Documents"));

    ptrCurrentInfo = new currentinfo();
    tabWidgetLibrary->addTab(ptrCurrentInfo, tr("NOAA"));

    ptrMetar = new classmetar();
    tabWidgetLibrary->addTab(ptrMetar, tr("METAR"));

    ptrdlgLog = new dlgLog();
    tabWidgetLibrary->addTab(ptrdlgLog, tr("Log"));

    QVBoxLayout *mainLayout = new QVBoxLayout;
    mainLayout->addWidget(tabWidget);
    mainLayout->setContentsMargins(0,0,0,0);
    setLayout(mainLayout);

    connect(tabWidget, SIGNAL(currentChanged(int)), this, SLOT(tabChanged(int)));
}

void mainwindowtab::tabChanged(int tabIdx) {
    if (DebugFunctions > 1) {qDebug() << Q_FUNC_INFO;}
    if (tabIdx == 0){
        ptrWindow->showToolBars();
    } else if (tabIdx == 1){
        ptrWindow->showToolBars();
    } else if ((tabIdx == 2) && (SettingsVars.NCSTab == true)){
        ptrdlgNCS->leCommand->setFocus();
        ptrWindow->hideToolBars();
    } else if (tabIdx == 3){
        ptrWindow->hideToolBars();
    } else if (tabIdx == 4){
        ptrWindow->hideToolBars();
    } else if (tabIdx == 5){
        ptrWindow->hideToolBars();
    } else if (tabIdx == 6){
        ptrWindow->hideToolBars();
    } else if (tabIdx == 7){
        ptrWindow->hideToolBars();
    }

}

dlgProp::dlgProp(QWidget *parent) : QWidget(parent) {
    if ((DebugFunctions > 1) || (DebugProp > 0)) {qDebug() << Q_FUNC_INFO;}
    mainLayout = new QVBoxLayout;
    mainLayout->setContentsMargins(0,0,0,0);
    setLayout(mainLayout);
}

dlgNCS::dlgNCS(QWidget *parent) : QWidget(parent) {
    if ((DebugFunctions > 1) || (DebugNCS > 0)) {qDebug() << Q_FUNC_INFO;}
    mainLayout = new QGridLayout;
    mainLayout->setContentsMargins(0,0,0,0);

    splitter = new QSplitter;
    splitter->setContentsMargins(0,0,0,0);

    splitter2 = new QSplitter;
    splitter2->setContentsMargins(0,5,0,0);
    splitter2->setOrientation(Qt::Vertical);

/*    QString fileNameSettings = QCoreApplication::applicationDirPath()+"/"+qAppName()+".ini";
    QSettings settings(fileNameSettings,QSettings::IniFormat);
    if (settings.contains("splitterState"))
        splitter->restoreState(settings.value("splitterState").toByteArray());
    if (settings.contains("splitter2State"))
        splitter2->restoreState(settings.value("splitter2State").toByteArray());
    if (settings.contains("splitterGeo"))
        splitter->restoreGeometry(settings.value("splitterGeo").toByteArray());
    if (settings.contains("splitter2Geo"))
        splitter2->restoreGeometry(settings.value("splitter2Geo").toByteArray());*/

    RosterTable = new QTableWidget(0,4);
    QStringList labels; labels << "Callsign" << "How Copy" << "Task" << "Status";
    RosterTable->setHorizontalHeaderLabels(labels);
    RosterTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    RosterTable->horizontalHeader()->resizeSections(QHeaderView::ResizeToContents);
    RosterTable->setColumnWidth(0,75);
    RosterTable->horizontalHeader()->setStretchLastSection(true);
    RosterTable->verticalHeader()->hide();
    RosterTable->setShowGrid(true);
    RosterTable->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(RosterTable, SIGNAL( customContextMenuRequested(const QPoint &) ), this, SLOT(on_RosterTable_ContextMenuRequest(const QPoint &)));
    connect(RosterTable, SIGNAL(cellChanged(int,int)), this, SLOT(RosterCellChanged(int, int)));

    QGroupBox * commandBox = new QGroupBox();
    commandBox->setContentsMargins(0,0,0,0);
    commandBoxLayout = new QGridLayout;
    commandBoxLayout->setContentsMargins(0,0,0,0);

    leCommand = new QTextEdit;
    leCommand->setSizePolicy(QSizePolicy::Expanding,QSizePolicy ::Expanding);
    leCommand->setPlaceholderText("Type command and press F6 to execute.\nZZZ4AZ GR 1R XZZ [EC1130Z]");


    commandBoxLayout->addWidget(leCommand,0,1);

    commandBox->setLayout(commandBoxLayout);

    ptrTabTraffic_NCS = new tabtraffic(this);

    splitter->addWidget(RosterTable);
    splitter->addWidget(ptrTabTraffic_NCS);
    splitter2->addWidget(splitter);
    splitter2->addWidget(commandBox);
    splitter2->setSizes({700,5});
    mainLayout->addWidget(splitter2,0,0,10,2);
    //mainLayout->addWidget(commandBox,12,0,3,2);
    setLayout(mainLayout);

    RosterTable->installEventFilter(this);
    leCommand->installEventFilter(this);
    installEventFilter(this);

}

void dlgNCS::commandExec() {
    if ((DebugFunctions > 1) || (DebugNCS > 0)) {qDebug() << Q_FUNC_INFO;}
    QString data = leCommand->toPlainText();
    if (data == "") {return;}
    if (data == "1") {
    qDebug() << "here";
        QStringList tmpCSlst = ptrWindow->wMainWindowTab->ptrdlgterminal->configSettings.generalSettings.heardCallSignList;
        for (int j= 0 ; j < tmpCSlst.size() ; j++) {
            QString tmpCS = tmpCSlst.at(j);
            if (!RosterVar.searchCS(tmpCS)){
                int row;
                if (WorkingDB.searchCS(tmpCS))  {
                    if (!RosterVar.searchCS(tmpCS)) {
                        RosterVar.add(WorkingDB[WorkingDB.searchResult].CallSign
                                ,WorkingDB[WorkingDB.searchResult].AbrCallSign
                                ,WorkingDB[WorkingDB.searchResult].Lat
                                ,WorkingDB[WorkingDB.searchResult].Lon
                                ,false
                                ,WorkingDB[WorkingDB.searchResult].isMerged
                                ,WorkingDB.searchResult);
                        const QSignalBlocker blocker(RosterTable);
                        row = RosterTable->rowCount();
                        RosterTable->insertRow(row);
                        QTableWidgetItem * vCallSignItem = new QTableWidgetItem;
                        vCallSignItem->setText(tmpCS);
                        RosterTable->setItem(row, 0, vCallSignItem);
                    }
                }
            }
        }
        leCommand->clear();
    }

    QStringList LineList = data.split("\n",QString::SkipEmptyParts);
    //FILE:///C:/MSC/STATIONMAPPER/MESSAGES/1569867845.TXT
    if (!LineList.at(0).contains("file:///")) {
        for (int ln = 0; ln < LineList.size(); ln++) {
            QString tmpLine = LineList.at(ln).toUpper();
            //qDebug() << ln << tmpLine;
            QStringList Line = LineList.at(ln).split(" ",QString::SkipEmptyParts);
            if (Line.size() > 0 ) {
                QString tmpCS = Line.at(0).toUpper();
                if (!RosterVar.searchCS(tmpCS)){
                    int row;
                    if (WorkingDB.searchCS(tmpCS))  {
                        if (!RosterVar.searchCS(tmpCS)) {
                            RosterVar.add(WorkingDB[WorkingDB.searchResult].CallSign
                                    ,WorkingDB[WorkingDB.searchResult].AbrCallSign
                                    ,WorkingDB[WorkingDB.searchResult].Lat
                                    ,WorkingDB[WorkingDB.searchResult].Lon
                                    ,false
                                    ,WorkingDB[WorkingDB.searchResult].isMerged
                                    ,WorkingDB.searchResult);
                        }
                    } else { //Not Found in WorkingDB - Place in Gulf
                        RosterVar.add(tmpCS
                                      ,tmpCS
                                      ,"3250000"
                                      ,"-9900000"
                                      ,false
                                      ,false
                                      ,0);
                    }

                    const QSignalBlocker blocker(RosterTable);
                    row = RosterTable->rowCount();
                    RosterTable->insertRow(row);
                    QTableWidgetItem * vCallSignItem = new QTableWidgetItem;
                    vCallSignItem->setText(tmpCS);
                    RosterTable->setItem(row, 0, vCallSignItem);
                    tmpLine = tmpLine.remove(0,Line.at(0).size()+1);
                    //qDebug() << ln << tmpLine;
                    if (Line.size() > 1) {
                        QString term = Line.at(1).trimmed().toUpper();
                        if ( term.contains("T=") || (term == "WBR") || (term == "WR") || (term == "R") || (term == "GR") || (term == "LC") ) {
                            const QSignalBlocker blocker(RosterTable);
                            QTableWidgetItem *HowCopyItem = new QTableWidgetItem;
                            HowCopyItem->setText(term);
                            RosterTable->setItem(row, 1, HowCopyItem);
                            RosterVar[row].HowCopy = term;
                            tmpLine = tmpLine.remove(0,Line.at(1).size()+1);
                            //qDebug() << ln << tmpLine;
                        }
                    }
                    if (tmpLine.contains("[") & tmpLine.contains("]")) {
                        int start = tmpLine.indexOf("[");
                        int end = tmpLine.indexOf("]");
                        QString tmpStatus = tmpLine.mid(start+1,end-start-1).toUpper();
                        const QSignalBlocker blocker(RosterTable);
                        QTableWidgetItem *Status = new QTableWidgetItem;
                        Status->setText(tmpStatus);
                        RosterTable->setItem(row, 3, Status);
                        RosterVar[row].Status = tmpStatus;
                        tmpLine = tmpLine.remove(start,end+1);
                        //qDebug() << ln << tmpLine;
                    }
                    if (tmpLine.contains("(") & tmpLine.contains(")")) {
                        int start = tmpLine.indexOf("(");
                        int end = tmpLine.indexOf(")");
                        QString tmpTasking = tmpLine.mid(start+1,end-start-1).toUpper();
                        const QSignalBlocker blocker(RosterTable);
                        QTableWidgetItem *Tasking = new QTableWidgetItem;
                        Tasking->setText(tmpTasking);
                        RosterTable->setItem(row, 2, Tasking);
                        //RosterVar[row].Status = tmpTasking;
                        tmpLine = tmpLine.remove(start,end+1);
                        //qDebug() << ln << tmpLine;
                    }
                    if (Line.size() > 2) {
                        //qDebug() << ln << tmpLine.trimmed();
                        listTraffic(tmpLine.trimmed(), tmpCS);
                    }
                } else {//Already in roster

                }
            }
        }
    } else {
        QString tmpFilename = LineList.at(0);
        QFile file(tmpFilename.remove("file:///"));
        file.open(QFile::ReadOnly);
        QTextStream in(&file);
        QString receivedMessage = in.readAll();
        if (receivedMessage.contains("ZKS")) {
            bool ZKSfound = false;
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
                    int leftParen = fileLine.indexOf("(");
                    int rightParen = fileLine.indexOf(")");
                    if ((leftParen != -1) && (rightParen != -1)) {
                        status = "[" + fileLine.mid((leftParen+1),(rightParen-leftParen-1)) + "]";
                    }
                    QString tmpCS = line.at(0);
                    ptrNCS->leCommand->append(tmpCS+" "+status);
                    ptrNCS->commandExec();
                }
            }
        } else if (receivedMessage.contains("NET:") && receivedMessage.contains("NCS:")) {
            bool NCSfound = false;
            auto ptrNCS = ptrWindow->wMainWindowTab->ptrdlgNCS;
            QTextStream in(&receivedMessage, QIODevice::ReadOnly);
            while (!in.atEnd()) {
                QString fileLine = in.readLine();
                if (fileLine.contains("NCS:")) {
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
                        ptrNCS->leCommand->append(line.at(1));
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
                    ptrNCS->leCommand->append(tmpCS + status);
                    ptrNCS->commandExec();
                }
            }

        }
    }


    leCommand->clear();
    RosterTable->resizeRowsToContents();
    RosterVar.dupeCheck();

    ptrWindow->updateStatusBar();
}

bool dlgNCS::eventFilter(QObject *obj, QEvent *event) {
//    if ((DebugFunctions > 5) || (DebugNCS > 0)) {qDebug() << Q_FUNC_INFO;}

    if (event->type () == QEvent::KeyPress) {
        QKeyEvent *keyEvent = static_cast<QKeyEvent *>(event);
        if ((obj == leCommand) & (keyEvent->key() == 16777269)) { //F6 Exec
            if ((DebugNCS > 7)) {qDebug() << "Ate Key Press" << keyEvent->key();}
             commandExec();
             return true;
        } else if (keyEvent->key() == 16777269) { //F6 Return to leCommand
            if ((DebugNCS > 7)) {qDebug() << "Ate Key Press" << keyEvent->key();}
            leCommand->setFocus();
            return true;
        } else if (keyEvent->key() == 16777266) { //F3 Find
            if ((DebugNCS > 7)) {qDebug() << "Ate Key Press" << keyEvent->key();}
            bool ok;
            QString tmpCS = QInputDialog::getText(this,"Find Station","Call Sign:", QLineEdit::Normal,leCommand->toPlainText(),&ok).toUpper().remove(" ");
            if (ok == true) {
                if (RosterVar.searchCS(tmpCS)) {
                    leCommand->clear();
                    RosterTable->setFocus();
                    RosterTable->selectRow(RosterVar.searchResult);
                } else {
                    QMessageBox msgBox;
                    msgBox.setText("Attention");
                    QString msg = tmpCS + " Not Found in Roster";
                    msgBox.setInformativeText(msg);
                    msgBox.setStandardButtons(QMessageBox::Ok);
                    msgBox.setDefaultButton(QMessageBox::Ok);
                    msgBox.exec();
                }
            }
            return true;
        }
        if ((obj == RosterTable) & (keyEvent->key() == Qt::Key_Up)  & (keyEvent->modifiers().testFlag(Qt::ShiftModifier)) & (keyEvent->modifiers().testFlag(Qt::ControlModifier)) ) {
            if ((DebugNCS > 7)) {qDebug() << "Ate Key Press" << keyEvent->key();}
            int row = RosterTable->currentRow();
            RosterVar.moveUp(row);
        }
        if ((obj == RosterTable) & (keyEvent->key() == Qt::Key_Down) & (keyEvent->modifiers().testFlag(Qt::ShiftModifier)) & (keyEvent->modifiers().testFlag(Qt::ControlModifier)) ) {
            if ((DebugNCS > 7)) {qDebug() << "Ate Key Press" << keyEvent->key();}
            int row = RosterTable->currentRow();
            RosterVar.moveDown(row);
        }
        return false;
    } else {
        return false;
    }
}

void dlgNCS::on_RosterTable_ContextMenuRequest(const QPoint &pos) {
    if ((DebugFunctions > 1) || (DebugNCS > 0)) {qDebug() << Q_FUNC_INFO;}
    int row = RosterTable->rowAt(pos.y());
    if (row == -1) {return;}
    QMenu contextMenu(tr("Context menu"), this);
    contextMenu.setToolTipsVisible(true);
    QAction SendRoster("Send Roster to Scratchpad", this);
    SendRoster.setToolTip("Send Roster to scratchpad");
    QAction SendRosterTerminal("Send Roster to Terminal", this);
    SendRosterTerminal.setToolTip("Send Roster to notes tab");
    QAction SendCOMSPOT("Send COMSPOT", this);
    SendRoster.setToolTip("Send COMSPOT to notes tab");
    QAction Remove("Remove", this);
    Remove.setToolTip("Remove this station");
    QAction RemoveAll("Remove All", this);
    Remove.setToolTip("Remove all Stations");
    QAction AmmendTraffic("Ammend Traffic",this);
    AmmendTraffic.setToolTip("List traffic from this station");
    QAction Closed("Closed", this);
    Closed.setToolTip("Set status for this station to closed");
    QAction Returned("Returned", this);
    Returned.setToolTip("Clear status for this station");
    QAction NCS("NCS", this);
    NCS.setToolTip("Set tasking to NCS");
    QAction ANCS("ANCS", this);
    ANCS.setToolTip("Set tasking to ANCS");
    QAction MoveUp("Move Up (Ctrl+Shft Up)", this);
    MoveUp.setToolTip("Move the position of this station up in the roster");
    QAction MoveDown("Move Down (Ctrl+Shft Down)", this);
    MoveDown.setToolTip("Move the position of this station up in the roster");

    contextMenu.addAction(&SendRoster);
    connect(&SendRoster, SIGNAL(triggered()), this, SLOT(on_SendRoster()));

    if (SettingsVars.TerminalTab) {
        contextMenu.addAction(&SendRosterTerminal);
        connect(&SendRosterTerminal, SIGNAL(triggered()), this, SLOT(on_SendRosterTerminal()));
    }

    contextMenu.addAction(&SendCOMSPOT);
    connect(&SendCOMSPOT, SIGNAL(triggered()), this, SLOT(on_SendComspot()));

    contextMenu.addSeparator();

    contextMenu.addAction(&Remove);
    connect(&Remove, SIGNAL(triggered()), this, SLOT(on_Remove()));

    contextMenu.addAction(&RemoveAll);
    connect(&RemoveAll, SIGNAL(triggered()), this, SLOT(on_RemoveAll()));

    contextMenu.addSeparator();

    contextMenu.addAction(&AmmendTraffic);
    connect(&AmmendTraffic, SIGNAL(triggered()), this, SLOT(on_AmmendTraffic()));

    contextMenu.addSeparator();

    contextMenu.addAction(&Closed);
    connect(&Closed, SIGNAL(triggered()), this, SLOT(on_Closed()));

    contextMenu.addAction(&Returned);
    connect(&Returned, SIGNAL(triggered()), this, SLOT(on_Returned()));

    contextMenu.addSeparator();

    contextMenu.addAction(&NCS);
    connect(&NCS, SIGNAL(triggered()), this, SLOT(on_NCS()));

    contextMenu.addAction(&ANCS);
    connect(&ANCS, SIGNAL(triggered()), this, SLOT(on_ANCS()));

    contextMenu.addSeparator();

    contextMenu.addAction(&MoveUp);
    connect(&MoveUp, SIGNAL(triggered()), this, SLOT(on_MoveUp()));

    contextMenu.addAction(&MoveDown);
    connect(&MoveDown, SIGNAL(triggered()), this, SLOT(on_MoveDown()));

    contextMenu.exec(RosterTable->mapToGlobal(pos));
}

void dlgNCS::RosterCellChanged(int row, int col) {
    if ((DebugFunctions > 1) || (DebugNCS > 0)) {qDebug() << Q_FUNC_INFO;}
    //qDebug() << "Row:" << row << "    Col:" << col;
    if (col == 0) {
        QString term = RosterTable->item(row,col)->text().toUpper();
        {
            const QSignalBlocker blocker(ptrExternDlgNCS->RosterTable);
            RosterTable->item(row,col)->setText(term);
        }
        if (WorkingDB.searchCS(term)) {
            int wIdx = WorkingDB.searchResult;
            if (SettingsVars.ToolsAreaisShown) {
                ptrWindow->cmboStation1->removeItem(row);
                ptrWindow->cmboStation2->removeItem(row);
                ptrWindow->cmboStation1->addItem(WorkingDB[wIdx].AbrCallSign,row);
                ptrWindow->cmboStation2->addItem(WorkingDB[wIdx].AbrCallSign,row);
            }
            if (DebugCheckLogLevel > 8) {qDebug() << row << "WorkingDB[wIdx].CallSign = " << WorkingDB[wIdx].CallSign;}
            RosterVar[row].CallSign = WorkingDB[wIdx].CallSign;
            RosterVar[row].AbrCallSign = WorkingDB[wIdx].AbrCallSign;
            RosterVar[row].Lat = WorkingDB[wIdx].Lat;
            RosterVar[row].Lon = WorkingDB[wIdx].Lon;
            RosterVar[row].isMerged = WorkingDB[wIdx].isMerged;
            RosterVar[row].relDB = wIdx;
            RosterVar.dupeCheck();
        } else {
            if (SettingsVars.ToolsAreaisShown) {
                ptrWindow->cmboStation1->removeItem(row);
                ptrWindow->cmboStation2->removeItem(row);
                ptrWindow->cmboStation1->addItem(term,row);
                ptrWindow->cmboStation2->addItem(term,row);
            }
            if (DebugCheckLogLevel > 8) {qDebug() << row << term;}
            RosterVar[row].CallSign = term;
            RosterVar[row].AbrCallSign = term;
            RosterVar[row].Lat = "3250000";
            RosterVar[row].Lon = "-9900000";
            RosterVar[row].isMerged = 0;
            RosterVar[row].relDB = 0;
            RosterVar.dupeCheck();
        }
    } else if (col == 1) {
        QString term = RosterTable->item(row,col)->text().toUpper();
        {
            const QSignalBlocker blocker(RosterTable);
            RosterTable->item(row,col)->setText(term);
        }
        RosterVar[row].HowCopy = term;
    } else if (col == 2) {
        QString term = RosterTable->item(row,col)->text().toUpper();
        {
            const QSignalBlocker blocker(RosterTable);
            RosterTable->item(row,col)->setText(term);
        }
    } else if (col == 3) {
        QString term = RosterTable->item(row,col)->text().toUpper();
        {
            const QSignalBlocker blocker(RosterTable);
            RosterTable->item(row,col)->setText(term);
        }
        RosterVar[row].Status = term;
    }
}

void dlgNCS::CreateCommandPB() {
    if ((DebugFunctions > 1) || (DebugNCS > 0)) {qDebug() << Q_FUNC_INFO;}
//    pbCkOut = new QPushButton("CK Out");
//    pbCkOut->setSizePolicy(QSizePolicy::Minimum,QSizePolicy::Minimum);
//    pbListTfc = new QPushButton("List Tfc");
//    pbListTfc->setSizePolicy(QSizePolicy::Minimum,QSizePolicy::Minimum);
//    pbXlcTfc = new QPushButton("Xcl Tfc");
//    pbXlcTfc->setSizePolicy(QSizePolicy::Minimum,QSizePolicy::Minimum);
//    pbCorrection = new QPushButton("Correction");
//    pbCorrection->setSizePolicy(QSizePolicy::Minimum,QSizePolicy::Minimum);
//    pbReqCom = new QPushButton("Req Com");
//    pbReqCom->setSizePolicy(QSizePolicy::Minimum,QSizePolicy::Minimum);
//    pbQSL = new QPushButton("QSL");
//    pbQSL->setSizePolicy(QSizePolicy::Minimum,QSizePolicy::Minimum);
//    pbQSY = new QPushButton("QSY");
//    pbQSY->setSizePolicy(QSizePolicy::Minimum,QSizePolicy::Minimum);
//    pbReturn = new QPushButton("Return");
//    pbReturn->setSizePolicy(QSizePolicy::Minimum,QSizePolicy::Minimum);
//    pbStatus = new QPushButton("Status");
//    pbStatus->setSizePolicy(QSizePolicy::Minimum,QSizePolicy::Minimum);
//    pbTasking = new QPushButton("Tasking");
//    pbTasking->setSizePolicy(QSizePolicy::Minimum,QSizePolicy::Minimum);


//    commandButtonsLayout = new QHBoxLayout;
//    commandButtonsLayout->addWidget(pbCkOut);
//    commandButtonsLayout->addWidget(pbListTfc);
//    commandButtonsLayout->addWidget(pbXlcTfc);
//    commandButtonsLayout->addWidget(pbCorrection);
//    commandButtonsLayout->addWidget(pbReqCom);
//    commandButtonsLayout->addWidget(pbQSL);
//    commandButtonsLayout->addWidget(pbQSY);
//    commandButtonsLayout->addWidget(pbReturn);
//    commandButtonsLayout->addWidget(pbStatus);
//    commandButtonsLayout->addWidget(pbTasking);

//    commandBoxLayout->addLayout(commandButtonsLayout,3,1,1,1);
}

void dlgNCS::on_AmmendTraffic() {
    if ((DebugFunctions > 1) || (DebugNCS > 0)) {qDebug() << Q_FUNC_INFO;}
    QItemSelectionModel *select = RosterTable->selectionModel();
    QList<QModelIndex> list = select->selectedRows();
    for(int i=0; i < list.size();i++) {
        QModelIndex item = list.at(i);
        if (item.column() == 0) {
            int row = item.row();
            bool ok;
            QString ret  = QInputDialog::getText(this,RosterVar[row].CallSign,"List Traffic:", QLineEdit::Normal,"",&ok).toUpper();
            if (ok) {listTraffic(ret,RosterVar[row].CallSign);}
        }
    }
}

void dlgNCS::on_MoveUp() {
    int row = RosterTable->currentRow();
    RosterVar.moveUp(row);
}

void dlgNCS::on_MoveDown() {
    int row = RosterTable->currentRow();
    RosterVar.moveDown(row);
}

void dlgNCS::on_Remove() {
    if ((DebugFunctions > 1) || (DebugNCS > 0)) {qDebug() << Q_FUNC_INFO;}
    QItemSelectionModel *select = RosterTable->selectionModel();
    QList<QModelIndex> list = select->selectedRows();
    for(int i=0; i < list.size();i++) {
        QModelIndex item = list.at(i);
        int row = item.row();
        RosterVar.remove(row);
        {
            const QSignalBlocker blocker(RosterTable);
            RosterTable->removeRow(row);
        }
    }
    RosterVar.dupeCheck();
    ptrWindow->updateStatusBar();
}

void dlgNCS::on_RemoveAll() {
    if ((DebugFunctions > 1) || (DebugNCS > 0)) {qDebug() << Q_FUNC_INFO;}
    QMessageBox msgBox;
    msgBox.setText("Attention");
    msgBox.setInformativeText("Confirm roster reset (Remove All)");
    msgBox.setStandardButtons(QMessageBox::Ok | QMessageBox::Cancel);
    msgBox.setDefaultButton(QMessageBox::Cancel);
    int ret = msgBox.exec();
    if (ret == QMessageBox::Ok) {
        const QSignalBlocker blocker(RosterTable);
        for(int i=RosterTable->rowCount(); i >= 0 ;i--) {
            RosterTable->removeRow(i);
        }
        RosterVar.reset();
        ptrWindow->updateStatusBar();
    }
}

void dlgNCS::on_Closed() {
    if ((DebugFunctions > 1) || (DebugNCS > 0)) {qDebug() << Q_FUNC_INFO;}
    QItemSelectionModel *select = RosterTable->selectionModel();
    QList<QModelIndex> list = select->selectedRows();
    for(int i=0; i < list.size();i++) {
        QModelIndex item = list.at(i);
        if (item.column() == 0) {
            int row = item.row();
            RosterVar[row].Status = "closed";
            QTableWidgetItem *StatusItem = new QTableWidgetItem;
            StatusItem->setText("closed");
            {
                const QSignalBlocker blocker(RosterTable);
                RosterTable->setItem(row, 3, StatusItem);
            }
         }
    }
}

void dlgNCS::on_Returned() {
    if ((DebugFunctions > 1) || (DebugNCS > 0)) {qDebug() << Q_FUNC_INFO;}
    QItemSelectionModel *select = RosterTable->selectionModel();
    QList<QModelIndex> list = select->selectedRows();
    for(int i=0; i < list.size();i++) {
        QModelIndex item = list.at(i);
        if (item.column() == 0) {
            int row = item.row();
            RosterVar[row].Status = "";
            QTableWidgetItem *StatusItem = new QTableWidgetItem;
            StatusItem->setText("");
            {
                const QSignalBlocker blocker(RosterTable);
                RosterTable->setItem(row, 3, StatusItem);
            }
         }
    }
}

void dlgNCS::on_NCS() {
    if ((DebugFunctions > 1) || (DebugNCS > 0)) {qDebug() << Q_FUNC_INFO;}
    QItemSelectionModel *select = RosterTable->selectionModel();
    QList<QModelIndex> list = select->selectedRows();
    for(int i=0; i < list.size();i++) {
        QModelIndex item = list.at(i);
        if (item.column() == 0) {
            int row = item.row();
            QTableWidgetItem *TaskItem = new QTableWidgetItem;
            TaskItem->setText("NCS");
            {
                const QSignalBlocker blocker(RosterTable);
                RosterTable->setItem(row, 2, TaskItem);
            }
         }
    }
}

void dlgNCS::on_ANCS() {
    if ((DebugFunctions > 1) || (DebugNCS > 0)) {qDebug() << Q_FUNC_INFO;}
    QItemSelectionModel *select = RosterTable->selectionModel();
    QList<QModelIndex> list = select->selectedRows();
    for(int i=0; i < list.size();i++) {
        QModelIndex item = list.at(i);
        if (item.column() == 0) {
            int row = item.row();
            QTableWidgetItem *TaskItem = new QTableWidgetItem;
            TaskItem->setText("ANCS");
            {
                const QSignalBlocker blocker(RosterTable);
                RosterTable->setItem(row, 2, TaskItem);
            }
         }
    }
}

void dlgNCS::listTraffic(QString input, QString CallSign) {
    if ((DebugFunctions > 1) || (DebugNCS > 0)) {qDebug() << Q_FUNC_INFO;}
    QRegularExpression re("\\b[0-9][OIPR]\\b");
    QStringList TrafficList = input.split(re,QString::SplitBehavior::SkipEmptyParts);

    QRegularExpressionMatchIterator qpIt = re.globalMatch(input);

    for (int i = 0; i < TrafficList.size(); i++) {
        QRegularExpressionMatch match = qpIt.next();
        const QSignalBlocker blocker(ptrTabTraffic_NCS->ptrdlgTrafficList->TrafficTable);
        int row = ptrTabTraffic_NCS->ptrdlgTrafficList->TrafficTable->rowCount();
        ptrTabTraffic_NCS->ptrdlgTrafficList->TrafficTable->insertRow(row);
        QTableWidgetItem * vCallSignItem = new QTableWidgetItem;
        vCallSignItem->setText(CallSign);
        ptrTabTraffic_NCS->ptrdlgTrafficList->TrafficTable->setItem(row, 0, vCallSignItem);
        QTableWidgetItem * vQPItem = new QTableWidgetItem;
        vQPItem->setText(match.captured(0));
        ptrTabTraffic_NCS->ptrdlgTrafficList->TrafficTable->setItem(row, 1, vQPItem);
        QTableWidgetItem * vAddresseesItem = new QTableWidgetItem;
        vAddresseesItem->setText(TrafficList.at(i).trimmed());
        ptrTabTraffic_NCS->ptrdlgTrafficList->TrafficTable->setItem(row, 3, vAddresseesItem);
    }
}

void dlgNCS::on_SendRoster() {
    bool ok;
    QString net = QInputDialog::getText(NULL,"Enter NET Designator","NET:", QLineEdit::Normal,"M4A",&ok).toUpper();
    QString tmpDE = "";

    if (SettingsVars.MyCallSign == 9999) {
        //ask for callsign
    } else {
        tmpDE = WorkingDB[SettingsVars.MyCallSign].CallSign;
    }
    QTextEdit * ptrNotesTextrEdit = ptrWindow->wMainWindowTab->ptrdlgNotes->Notes_textEdit;
    QLabel * ptrlblFileName =  ptrWindow->wMainWindowTab->ptrdlgNotes->lblFileName;
    ptrlblFileName->setText("");
    ptrWindow->wMainWindowTab->ptrdlgNotes->currFilename = "";

    ptrNotesTextrEdit->clear();
    ptrNotesTextrEdit->append(net +" DE " + tmpDE);
    ptrNotesTextrEdit->append("//");
    ptrNotesTextrEdit->append("NET: " + net);
    ptrNotesTextrEdit->append("NCS: " + RosterVar[0].CallSign);
    for (int idx = 0; idx <= RosterVar.size(); idx++) {
        ptrNotesTextrEdit->append(RosterVar[idx].CallSign + ",,," + RosterVar[idx].Status + ",,,");
    }

    QString min = QString::number(QDateTime::currentDateTimeUtc().time().minute());
    QString UTC = "";
    if (QDateTime::currentDateTimeUtc().time().hour() <= 9) {
        UTC = "0" + QString::number(QDateTime::currentDateTimeUtc().time().hour());
    } else {
        UTC = QString::number(QDateTime::currentDateTimeUtc().time().hour());
    }

    ptrNotesTextrEdit->append("// " + UTC + min + "Z AR");

    if (!WorkingVars.NotesIsFloat) {ptrWindow->wMainWindowTab->tabWidget->setCurrentIndex(3);}

}

void dlgNCS::on_SendRosterTerminal() {
    bool ok;
    QString net = QInputDialog::getText(NULL,"Enter NET Designator","NET:", QLineEdit::Normal,"M4A",&ok).toUpper();
    QString tmpDE = "";

    if (SettingsVars.MyCallSign == 9999) {
        //ask for callsign
    } else {
        tmpDE = WorkingDB[SettingsVars.MyCallSign].CallSign;
    }
    QPlainTextEdit * plainTextEditSendData = ptrWindow->wMainWindowTab->ptrdlgterminal->findChild<QPlainTextEdit*>("plainTextEditSendData");

    plainTextEditSendData->clear();
    plainTextEditSendData->appendPlainText(net +" DE " + tmpDE);
    plainTextEditSendData->appendPlainText("//");
    plainTextEditSendData->appendPlainText("NET: " + net);
    plainTextEditSendData->appendPlainText("NCS: " + RosterVar[0].CallSign);
    for (int idx = 0; idx <= RosterVar.size(); idx++) {
        plainTextEditSendData->appendPlainText(RosterVar[idx].CallSign + ",,,,,,");
    }

    QString min = QString::number(QDateTime::currentDateTimeUtc().time().minute());
    QString UTC = "";
    if (QDateTime::currentDateTimeUtc().time().hour() <= 9) {
        UTC = "0" + QString::number(QDateTime::currentDateTimeUtc().time().hour());
    } else {
        UTC = QString::number(QDateTime::currentDateTimeUtc().time().hour());
    }

    plainTextEditSendData->appendPlainText("// " + UTC + min + "Z AR");
    if (WorkingVars.NotesIsFloat) {ptrWindow->wMainWindowTab->tabWidget->setCurrentIndex(3);} else {ptrWindow->wMainWindowTab->tabWidget->setCurrentIndex(4);}
    plainTextEditSendData->setFocus();

}

void dlgNCS::on_SendComspot() {

    QTextEdit * ptrNotesTextrEdit = ptrWindow->wMainWindowTab->ptrdlgNotes->Notes_textEdit;
    QLabel * ptrlblFileName =  ptrWindow->wMainWindowTab->ptrdlgNotes->lblFileName;
    ptrlblFileName->setText("");
    ptrWindow->wMainWindowTab->ptrdlgNotes->currFilename = "";
    QString MIN;
    QString HR;
    QString DOM = QString::number(QDateTime::currentDateTime().date().day());
    int iMonth = QDateTime::currentDateTime().date().month();
    QStringList Months = {"","JAN","FEB","MAR","APR","MAY","JUN","JUL","AUG","SEP","OCT","NOV","DEC"};
    QString MONTH = Months.at(iMonth);
    QString YEAR = QString::number(QDateTime::currentDateTime().date().year());
    if (QDateTime::currentDateTimeUtc().time().hour() < 9) {
        HR = "0" + QString::number(QDateTime::currentDateTimeUtc().time().hour());
    } else {
        HR = QString::number(QDateTime::currentDateTimeUtc().time().hour());
    }
    if (QString::number(QDateTime::currentDateTimeUtc().time().minute()) < 9) {
        MIN = "0" + QString::number(QDateTime::currentDateTimeUtc().time().minute());
    } else {
        MIN = QString::number(QDateTime::currentDateTimeUtc().time().minute());
    }

    QString DOY = QString::number(QDateTime::currentDateTime().date().dayOfYear());

    bool ok;
    QString NET = QInputDialog::getItem(this,"Comspot Input","NET:",SettingsVars.DefaultNetList,SettingsVars.DefaultNet,true,&ok).toUpper();
    if (!SettingsVars.DefaultNetList.contains(NET)) {
        SettingsVars.DefaultNetList << NET;
        SettingsVars.DefaultNet = SettingsVars.DefaultNetList.size() - 1;
        QString fileNameSettings = "/home/user/MSC/StationMapper/StationMapper.ini";
        QSettings settings(fileNameSettings,QSettings::IniFormat);
        settings.setValue("DefaultNetList", SettingsVars.DefaultNetList);
        settings.setValue("DefaultNet", SettingsVars.DefaultNet);
    } else {
        SettingsVars.CoverageDefaultChan = SettingsVars.DefaultNetList.indexOf(NET);
        QString fileNameSettings = "/home/user/MSC/StationMapper/StationMapper.ini";
        QSettings settings(fileNameSettings,QSettings::IniFormat);
        settings.setValue("DefaultNet", SettingsVars.DefaultNet);
    }

    QString tmpDE = "";

    if (SettingsVars.MyCallSign == 9999) {
        tmpDE = QInputDialog::getText(this,"Comspot Input","Enter Your Callsign", QLineEdit::Normal,"",&ok).trimmed().toUpper();
    } else {
        tmpDE = WorkingDB[SettingsVars.MyCallSign].CallSign;
    }
    QStringList SvTo = SettingsVars.ComspotFM.split("/");
    QString FL3DE;
    QString FMPLA;
    if (SvTo.size() < 2) {

    } else {
        FL3DE = SvTo.at(0).trimmed().toUpper();
        FMPLA = SvTo.at(1).trimmed().toUpper();
    }

    ptrNotesTextrEdit->clear();
    ptrNotesTextrEdit->append("VZCZCMMM001");
    ptrNotesTextrEdit->append("RR " + SettingsVars.ComspotRI.trimmed().toUpper());
    QString MSGNR = "";
    if (SettingsVars.ACPDMsgNR) {
        QString fileNameSettings = SettingsVars.ACPDDir + "/acpdeluxe.ini";
        QFile ACPDsettings(fileNameSettings);
        ACPDsettings.open(QFile::ReadOnly);
        QTextStream settingsStream(ACPDsettings.readAll());
        QTextStream settingsStreamOUT;
        ACPDsettings.close();
        ACPDsettings.open(QFile::WriteOnly);
        while (!settingsStream.atEnd()) {
            QString fileLine = settingsStream.readLine();
            QStringList lineToken = fileLine.split("=");
            if (lineToken.size() == 2) {
                if (lineToken.at(0) == "nxtMsgNo") {
                    MSGNR = QString::number(lineToken.at(1).toInt());
                    QString tmpOp = "nxtMsgNo=" + QString::number(lineToken.at(1).toInt() + 1);
                    tmpOp.append("\n");
                    ACPDsettings.write(tmpOp.toUtf8());
                }
                else {
                    fileLine.append("\n");
                    ACPDsettings.write(fileLine.toUtf8());
                }
            }
        }
        ACPDsettings.close();
    }
    else {
        MSGNR = QInputDialog::getText(this,"Comspot Input","Message Number:", QLineEdit::Normal,"",&ok);
    }
    if (MSGNR.size() < 4) {
        for (int i = 0; i <= (4 - MSGNR.size()); i++) {
            MSGNR = "0" + MSGNR;
        }
    }
    ptrNotesTextrEdit->append("DE " + FL3DE + " #" + MSGNR + " " + DOY + HR + MIN);
    ptrNotesTextrEdit->append("ZNY EEEEE");
    ptrNotesTextrEdit->append("R " + DOM + HR + MIN + "Z " + MONTH + " " + YEAR);
    ptrNotesTextrEdit->append("FM " + SettingsVars.ComspotFM.trimmed().toUpper());
    ptrNotesTextrEdit->append("TO " + SettingsVars.ComspotTO.trimmed().toUpper());
    ptrNotesTextrEdit->append("INFO " + SettingsVars.ComspotINFO.trimmed().toUpper());
    ptrNotesTextrEdit->append("BT");
    ptrNotesTextrEdit->append("UNCLAS");
    ptrNotesTextrEdit->append("MSGID/COMSPOT/" + FMPLA + "/" + MSGNR + "//");
    QString START = QInputDialog::getText(this,"Comspot Input","Start Time", QLineEdit::Normal,"0001Z",&ok).trimmed().toUpper();
    QString STOP = QInputDialog::getText(this,"Comspot Input","Stop Time", QLineEdit::Normal,"0059Z",&ok).trimmed().toUpper();
    ptrNotesTextrEdit->append("COMEV/ACTIVATION/" + START + "/" + STOP + "/CCN:" + NET);
    ptrNotesTextrEdit->append("LOCN/" + SettingsVars.ComspotLOC.trimmed().toUpper() + "//");
    QString FREQ = QInputDialog::getText(this,"Comspot Input","Frequency Designator", QLineEdit::Normal,"M037",&ok).trimmed().toUpper();
    ptrNotesTextrEdit->append("RMKS/1. FREQ " + FREQ);
    QString MODES = QInputDialog::getText(this,"Comspot Input","Modes: ", QLineEdit::Normal,"SSB M110A",&ok).trimmed().toUpper();
    ptrNotesTextrEdit->append("2. MODES: " + MODES);
    QStringList lstConditions;
    lstConditions << "BAD" << "POOR" << "FAIR" << "GOOD";
    QString COND = QInputDialog::getItem(this, "Comspot Input", "App:", lstConditions,2,true,&ok,Qt::Window).toUpper();
    ptrNotesTextrEdit->append("3. BAND CONDITIONS: " + COND);
    ptrNotesTextrEdit->append("4. TOTAL STATIONS: " + QString::number(RosterVar.size()+1));
    for (int idx = 0; idx <= RosterVar.size(); idx++) {
        ptrNotesTextrEdit->append(RosterVar[idx].CallSign);
    }
    ptrNotesTextrEdit->append("5. TRAFFIC: " + QString::number(ptrWindow->wMainWindowTab->ptrdlgNCS->ptrTabTraffic_NCS->ptrdlgTrafficList->TrafficTable->rowCount()));
    QString TRAINING = QInputDialog::getText(this,"Comspot Input","Traning: ", QLineEdit::Normal,"NONE",&ok).trimmed().toUpper();
    ptrNotesTextrEdit->append("6. TRAINING: " + TRAINING);
    QString REMARKS = QInputDialog::getText(this,"Comspot Input","Remarks: ", QLineEdit::Normal,"NONE",&ok).trimmed().toUpper();
    ptrNotesTextrEdit->append("7 REMARKS: " + REMARKS + "//");
    ptrNotesTextrEdit->append("BT");
    ptrNotesTextrEdit->append("#" + MSGNR);
    ptrNotesTextrEdit->append("");
    ptrNotesTextrEdit->append("");
    ptrNotesTextrEdit->append("");
    ptrNotesTextrEdit->append("");
    ptrNotesTextrEdit->append("");
    ptrNotesTextrEdit->append("NNNN");

    if (!WorkingVars.NotesIsFloat) {ptrWindow->wMainWindowTab->tabWidget->setCurrentIndex(3);}

}

dlgNotes::dlgNotes(QWidget *parent) : QWidget(parent) {
    if (DebugFunctions > 1) {qDebug() << Q_FUNC_INFO;}

    mainLayout = new QGridLayout;
    //toolLayout = new QBoxLayout(QBoxLayout::TopToBottom, this);

    splitter = new QSplitter;
    splitter->setContentsMargins(0,0,0,0);

    toolbar = new QToolBar;
    toolbar->setContentsMargins(0,0,0,0);
    toolbar->setMaximumHeight(23);

    lblFileName = new QLabel();
    lblFileName->setContentsMargins(0,0,0,0);

    statusBar = new QStatusBar;
    statusBar->setSizeGripEnabled(false);
    statusBar->addPermanentWidget(lblFileName,1);
    statusBar->setContentsMargins(0,0,0,0);
    statusBar->setMaximumHeight(20);

    Notes_textEdit = new QTextEdit;
    Notes_textEdit->setReadOnly(false);
    Notes_textEdit->setAcceptDrops(true);
    Notes_textEdit->setContextMenuPolicy(Qt::CustomContextMenu);

    connect(Notes_textEdit, SIGNAL( customContextMenuRequested(const QPoint &) ), this, SLOT(on_Notes_textEdit_ContextMenuRequest(const QPoint &)));

    Notes_textEdit->setFont(SettingsVars.NotesFont);
    Notes_textEdit->update();

    highlighter = new Highlighter(Notes_textEdit->document());

    const QString rootPath = SettingsVars.MessagesFolderRoot;
    model = new QFileSystemModel;
    tree = new MyTreeView;
    model->setRootPath("");
    model->setReadOnly(false);
    qDebug() << model->supportedDragActions();
    tree->setModel(model);
    if (!rootPath.isEmpty()) {
        const QModelIndex rootIndex = model->index(QDir::cleanPath(rootPath));
        if (rootIndex.isValid())
            tree->setRootIndex(rootIndex);
    }

    // Demonstrating look and feel features
    tree->setAnimated(false);
    tree->setIndentation(20);
    tree->setSortingEnabled(true);
    tree->sortByColumn(3,Qt::SortOrder::DescendingOrder);
    const QSize availableSize = QApplication::desktop()->availableGeometry(tree).size();
    tree->resize(availableSize / 2);
    tree->setColumnWidth(0, tree->width() / 3);
    connect(tree,&MyTreeView::loadFile,this,&dlgNotes::loadFile);

    tree->setDragEnabled(true);
    tree->setAcceptDrops(true);
    tree->setDropIndicatorShown(true);
    tree->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(tree, SIGNAL( customContextMenuRequested(const QPoint &) ), this, SLOT(on_menu_Tree(const QPoint &)));

    QAction *newAct = new QAction("NEW", this);
    newAct->setShortcuts(QKeySequence::New);
    newAct->setStatusTip(tr("Create a new file"));
    connect(newAct, &QAction::triggered, this, &dlgNotes::newFile);
    toolbar->addAction(newAct);

    QAction *openAct = new QAction("OPEN", this);
    openAct->setShortcuts(QKeySequence::Open);
    openAct->setStatusTip(tr("Open an existing file"));
    connect(openAct, &QAction::triggered, this, &dlgNotes::open);
    toolbar->addAction(openAct);

    QAction *saveAct = new QAction("SAVE", this);
    saveAct->setShortcuts(QKeySequence::Save);
    saveAct->setStatusTip(tr("Save the document to disk"));
    connect(saveAct, &QAction::triggered, this, &dlgNotes::save);
    toolbar->addAction(saveAct);

    QAction *acpdAct = new QAction("ACP/DLX", this);
    acpdAct->setShortcuts(QKeySequence::Save);
    acpdAct->setStatusTip(tr("Save the document to disk"));
    connect(acpdAct, &QAction::triggered, this, &dlgNotes::selectedACPD);
    toolbar->addAction(acpdAct);

    /*toolbar->addSeparator();

    if (SettingsVars.TerminalTab) {
        QAction *QSLAct = new QAction("QSL-RI", this);
        QSLAct->setStatusTip(tr("QSL for your Guard RIs"));
        connect(QSLAct, &QAction::triggered, this, &dlgNotes::QSL);
        toolbar->addAction(QSLAct);
    }*/

    toolbar->addSeparator();

//    QAction *DELAct = new QAction("TRASH", this);
//    DELAct->setStatusTip(tr("Move to TRASH"));
//    connect(DELAct, &QAction::triggered, this, &dlgNotes::DELAct);
//    toolbar->addAction(DELAct);

    QAction *ARCHAct = new QAction("Send to ARCHIVE", this);
    ARCHAct->setStatusTip(tr("Mode to the archive folder"));
    connect(ARCHAct, &QAction::triggered, this, &dlgNotes::ARCHAct);
    toolbar->addAction(ARCHAct);

    toolbar->addSeparator();

    QFontComboBox * cmbxFontFamily = new QFontComboBox();
    cmbxFontFamily->setCurrentFont(SettingsVars.NotesFont);
    connect(cmbxFontFamily, &QFontComboBox::currentFontChanged, this, &dlgNotes::FontChanged);
    toolbar->addWidget(cmbxFontFamily);

    QAction *actFontScalarDec = new QAction("-", this);
    actFontScalarDec->setStatusTip(tr("Reduce font scalar"));
    connect(actFontScalarDec, &QAction::triggered, this, &dlgNotes::selectedFontSizeDec);
    toolbar->addAction(actFontScalarDec);

    QAction *actFontScalarInc = new QAction("+", this);
    actFontScalarInc->setStatusTip(tr("Increase font scalar"));
    connect(actFontScalarInc, &QAction::triggered, this, &dlgNotes::selectedFontSizeInc);
    toolbar->addAction(actFontScalarInc);

    toolbar->addSeparator();

    QAction *actLastRX = new QAction("Last RX", this);
    actLastRX->setStatusTip(tr("Load last received message 'VZCZMMM'"));
    connect(actLastRX, &QAction::triggered, this, &dlgNotes::selectedLastRX);
    toolbar->addAction(actLastRX);

    toolbar->addSeparator();

    //actFloat = new QAction("FLOAT", this);
    //connect(actFloat, &QAction::triggered, this, &dlgNotes::selectedFloat);
    //toolbar->addAction(actFloat);

    splitter->addWidget(Notes_textEdit);
    splitter->addWidget(tree);
    splitter->setSizes({600,200});
    mainLayout->addWidget(toolbar,0,0,1,2);
    mainLayout->addWidget(statusBar,1,0,1,2);
    mainLayout->addWidget(splitter,2,0,20,2);
    mainLayout->setContentsMargins(0,0,0,0);

    setLayout(mainLayout);


    dirWatcher.addPath(SettingsVars.MessagesFolderRoot);
    currWatchDir = SettingsVars.MessagesFolderRoot;
    QObject::connect(&dirWatcher, SIGNAL(directoryChanged(QString)), this, SLOT(calldirWatcherChecker(QString)));
}

void dlgNotes::selectedACPD() {
    QProcess *process = new QProcess();
    process->setProgram(SettingsVars.ACPDDir + "ACPDeluxe.exe");
    process->setProcessChannelMode(QProcess::MergedChannels);
    //QObject::connect(process, &QProcess::readyRead, [&process](){qDebug() << process->readAll();});
    //QObject::connect(process , SIGNAL(finished(int,QProcess::ExitStatus)), this, SLOT(VOACAPAreaFinished(int, QProcess::ExitStatus)));
    //QObject::connect(process, &QObject::destroyed,[] { qDebug() << "Sender got deleted!"; });
    process->startDetached();
}

void dlgNotes::selectedLastRX() {
    if(maybeSave()) {
        currFilename = "";
        lblFileName->setText(currFilename);
        Notes_textEdit->clear();
        Notes_textEdit->setText(WorkingVars.lastRX);
    }
}

void dlgNotes::selectedFloat() {
    Qt::WindowFlags flags = 0;


    QString name = "Sratchpad"; // retrieve the name of the tab
    QLayout *layout = ptrWindow->wMainWindowTab->tabWidget->widget(3)->layout();
    ptrWindow->wMainWindowTab->tabWidget->removeTab(3); // remove the tab (now we have only the pointer above to its content)

    widget = new QDialog(); // create a new Dialog
    widget->setWindowTitle(name); // set the title like the name of the tab removed
    widget->setLayout(layout); // place here the content of the tab removed
    widget->setModal(false);
    flags |= Qt::CustomizeWindowHint;
    //flags |= Qt::WindowCloseButtonHint;
    flags |= Qt::WindowTitleHint;
    flags |= Qt::WindowMinMaxButtonsHint;
    flags |= Qt::WindowShadeButtonHint;
    //flags |= Qt::WindowCloseButtonHint;
    widget->setWindowFlags(flags);
    widget->show(); // show the dialog

    QString fileNameSettings = "/home/user/MSC/StationMapper/StationMapper.ini";
    QSettings settings(fileNameSettings,QSettings::IniFormat);
    const QByteArray geometry = settings.value("Notes_Float_Geometry", QByteArray()).toByteArray();
    if (geometry.isEmpty()) {
//        const QRect availableGeometry = QApplication::desktop()->availableGeometry(this);
//        resize(MAP_WIDTH/2, MAP_HEIGHT/2);
//        move((availableGeometry.width() - width()) / 2,
//             (availableGeometry.height() - height()) / 2);
    } else {
        widget->restoreGeometry(geometry);
    }

    actFloat->setText("X");
    disconnect(actFloat, &QAction::triggered, this, &dlgNotes::selectedFloat);
    connect(actFloat, &QAction::triggered, this, &dlgNotes::selectedInsertTab);
    WorkingVars.NotesIsFloat = true;

}

void dlgNotes::selectedInsertTab() {
    QString fileNameSettings = "/home/user/MSC/StationMapper/StationMapper.ini";
    QSettings settings(fileNameSettings,QSettings::IniFormat);
    settings.setValue("Notes_Float_Geometry", widget->saveGeometry());

    ptrWindow->wMainWindowTab->tabWidget->insertTab(3,ptrWindow->wMainWindowTab->ptrdlgNotes, tr("Scratchpad"));
    QLayout *layout = widget->layout();
    ptrWindow->wMainWindowTab->tabWidget->widget(3)->setLayout(layout);

    widget->close();

    actFloat->setText("FLOAT");
    disconnect(actFloat, &QAction::triggered, this, &dlgNotes::selectedInsertTab);
    connect(actFloat, &QAction::triggered, this, &dlgNotes::selectedFloat);
    WorkingVars.NotesIsFloat = false;
}

void dlgNotes::closeEvent(QCloseEvent *e) {

}

void dlgNotes::FontChanged(const QFont &font) {
    if (DebugFunctions > 1) {qDebug() << Q_FUNC_INFO;}
    SettingsVars.NotesFont.setFamily(font.family());
    Notes_textEdit->setFont(SettingsVars.NotesFont);
    Notes_textEdit->update();
}

void dlgNotes::selectedFontSizeInc() {
    if (DebugFunctions > 1) {qDebug() << Q_FUNC_INFO;}
    if (SettingsVars.NotesFont.pointSize() < 20) {
        SettingsVars.NotesFont.setPointSize( SettingsVars.NotesFont.pointSize() + 1 );
        Notes_textEdit->setFont(SettingsVars.NotesFont);
        Notes_textEdit->update();
    }
}

void dlgNotes::selectedFontSizeDec() {
    if (DebugFunctions > 1) {qDebug() << Q_FUNC_INFO;}
    if (SettingsVars.NotesFont.pointSize() > 8) {
        SettingsVars.NotesFont.setPointSize( SettingsVars.NotesFont.pointSize() - 1 );
        Notes_textEdit->setFont(SettingsVars.NotesFont);
        Notes_textEdit->update();
    }
}

void dlgNotes::on_menu_Tree(const QPoint &tmpPos) {
    if (DebugFunctions > 1) {qDebug() << Q_FUNC_INFO;}
    QMenu menu;
    QAction * actnOpen = new QAction("Open", this);
    actnOpen->setToolTip("Open");
    menu.addAction(actnOpen);
    connect(actnOpen, SIGNAL(triggered()), this, SLOT(on_selected_Open()));
    menu.addSeparator();
    menu.exec(ptrWindow->wMainWindowTab->ptrdlgNotes->tree->mapToGlobal(tmpPos));
}

void dlgNotes::on_selected_Open() {
    if (DebugFunctions > 1) {qDebug() << Q_FUNC_INFO;}
    QVariant tmpVfileName = tree->treeData;
    QVariant tmpVpath = tree->treeDataParent;
    //qDebug() << tmpVfileName.toString() << tmpVpath.toString();
    if (!tmpVpath.toString().contains("MESSAGES")) {
        loadFile(SettingsVars.WorkingDir + "MESSAGES/" + tmpVpath.toString() + "/" + tmpVfileName.toString()); }
}

void dlgNotes::on_tree_itemDoubleClicked(QModelIndex index) {
    QString tmpFilename = model->fileName(index);
    qDebug() << tmpFilename;
}

void dlgNotes::calldirWatcherChecker(QString tmpStr) {
    if (DebugFunctions > 1) {qDebug() << Q_FUNC_INFO;}
    QDir directory(tmpStr);
    QStringList textFiles = directory.entryList(QStringList() << "*.txt" << "*.TXT",QDir::Files);
    lboxFile->clear();
    lboxFile->addItems(textFiles);
}

void dlgNotes::DELAct() {
    if (DebugFunctions > 1) {qDebug() << Q_FUNC_INFO << " Remarked Out";}
//    if (currFilename == "") return;
//    QFile file(currFilename);
//    QDir dir;
//    QString tmpDir = SettingsVars.ArchiveFolder;
//    if (!dir.exists(tmpDir)) {dir.mkdir(tmpDir);}
//    QStringList fileNameToken = file.fileName().split("/");
//    qDebug() << currFilename << fileNameToken.at((fileNameToken.size()-1));
//    if (file.copy(tmpDir + fileNameToken.at((fileNameToken.size()-1)))) {
//        file.remove();
//    }
//    Notes_textEdit->clear();
//    currFilename = "";
//    lblFileName->setText("");
}

void dlgNotes::ARCHAct() {
    if (DebugFunctions > 1) {qDebug() << Q_FUNC_INFO;}
    if (currFilename == "") return;
    QFile file(currFilename);
    QDir dir;
    QString tmpDir = SettingsVars.ArchiveFolder;
    if (!dir.exists(tmpDir)) {dir.mkdir(tmpDir);}
    QStringList fileNameToken = file.fileName().split("/");
    //qDebug() << currFilename << fileNameToken.at((fileNameToken.size()-1));
    file.copy(tmpDir + fileNameToken.at((fileNameToken.size()-1)));
    file.remove();
    lblFileName->setText(tmpDir + fileNameToken.at((fileNameToken.size()-1)));
    currFilename = tmpDir + fileNameToken.at((fileNameToken.size()-1));

}

void dlgNotes::QSL() {
    if (DebugFunctions > 1) {qDebug() << Q_FUNC_INFO;}
    QString tempDoc;
    tempDoc = Notes_textEdit->toPlainText();
    QTextStream file( &tempDoc, QIODevice::ReadOnly );
    QString fileLine ="";
    QString PRECEDENCE = "";
    QString DE = "";
    QString RI = "";
    QString NR = "";
    QString DTG = "";
    bool badFile = false;
    if (tempDoc == "")
        badFile = true;
    while (!file.atEnd() && !badFile) {
        fileLine = file.readLine();
        if (fileLine.contains("VZCZCMMM")) {
            fileLine = fileLine = file.readLine();
            PRECEDENCE = fileLine.at(0);
            fileLine = fileLine = file.readLine();
            if (fileLine.contains("DE")) {
                QStringList fileToken = fileLine.split(" ");
                if (fileToken.size() == 4) {
                    DE = fileToken.at(0);
                    RI = fileToken.at(1);
                    NR = fileToken.at(2);
                    DTG = fileToken.at(3);
                    badFile = false;
                    break;
                }
            } else {
                badFile = true;
            }
        } else {
            badFile = true;
        }
    }
    if (!badFile) {
        QMessageBox msgBox;
        if (settingsTermVars.GuardedRI == "") {
            msgBox.setText("Error");
            msgBox.setInformativeText("No Guarded RIs set.");
            msgBox.setStandardButtons(QMessageBox::Cancel);
            msgBox.setDefaultButton(QMessageBox::Cancel);
            int ret = msgBox.exec();
        } else {
            QString GuardRI = settingsTermVars.GuardedRI;
            QString tmpRI = "/" + WorkingDB[SettingsVars.MyCallSign].CallSign + " " + GuardRI + "/";
            QString tempStr ="ALL-CM DE "+ WorkingDB[SettingsVars.MyCallSign].CallSign +"-CM//QSL _" + PRECEDENCE + "_" + RI+ " " + NR + " " + DTG + tmpRI +  "//" + QDateTime::currentDateTimeUtc().toString("hhmm")+"K\n";
            qDebug() << tempStr;
            QByteArray buffer;
            buffer = buffer.append(tempStr);
            //ptrWindow->wMainWindowTab->ptrdlgterminal->sendDataToProcessingChain(buffer, ptrWindow->wMainWindowTab->ptrdlgterminal->ui->comboBoxKeyList->currentText(), ptrWindow->wMainWindowTab->ptrdlgterminal->ui->checkBoxCompress->isChecked(), ptrWindow->wMainWindowTab->ptrdlgterminal->ui->comboBoxSelectMachineAddress->currentText().trimmed());
        }
    } else if (badFile) {
        QMessageBox msgBox;
        msgBox.setText("Attention");
        msgBox.setInformativeText("File does contain the expected syntax. \nVZCZCMMM\nRR\n DE ");
        msgBox.setStandardButtons(QMessageBox::Cancel);
        msgBox.setDefaultButton(QMessageBox::Cancel);
        int ret = msgBox.exec();
        if (ret) { }
    }

    if (WorkingVars.NotesIsFloat) {ptrWindow->wMainWindowTab->tabWidget->setCurrentIndex(3);} else {ptrWindow->wMainWindowTab->tabWidget->setCurrentIndex(4);}
}

void dlgNotes::newFile() {
    if (DebugFunctions > 1) {qDebug() << Q_FUNC_INFO;}
    if (maybeSave()) {
        Notes_textEdit->clear();
        currFilename = "";
        lblFileName->setText("");
    }
}

bool dlgNotes::maybeSave() {
    if (DebugFunctions > 1) {qDebug() << Q_FUNC_INFO;}
    if (!Notes_textEdit->document()->isModified()) {
        return true;
    } else {
        const QMessageBox::StandardButton ret
                = QMessageBox::warning(this, tr("StationMapper"),
                                       tr("The document has been modified.\n"
                                          "Do you want to save your changes?"),
                                       QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);
        switch (ret) {
        case QMessageBox::Save:
            return save();
        case QMessageBox::Cancel:
            return false;
        default:
            break;
        }
        return true;
    }
}

void dlgNotes::open() {
    if (DebugFunctions > 1) {qDebug() << Q_FUNC_INFO;}
    if (maybeSave()) {
        QString fileName = QFileDialog::getOpenFileName(this,"Open File",SettingsVars.WorkingDir+"..");
        loadFile(fileName);
    }

}

bool dlgNotes::loadFile(QString fileName) {
    if (DebugFunctions > 1) {qDebug() << Q_FUNC_INFO;}
    if (maybeSave()) {
        QFile file(fileName);
        currFilename = fileName;
        lblFileName->setText(fileName);
        file.open(QFile::ReadOnly | QFile::Text);

        QTextStream ReadFile(&file);
        Notes_textEdit->setText(ReadFile.readAll());
        Notes_textEdit->setFont(SettingsVars.NotesFont);
        Notes_textEdit->update();
        file.close();
        return true;
    }
    return false;
}

bool dlgNotes::save() {
    if (DebugFunctions > 1) {qDebug() << Q_FUNC_INFO;}
    if (currFilename == "") {
        return saveAs();
    } else {
        return saveFile(currFilename);
    }
}

bool dlgNotes::saveFile(QString fileName) {
    if (DebugFunctions > 1) {qDebug() << Q_FUNC_INFO;}
    QFile file(fileName);
    if (!file.open(QFile::WriteOnly | QFile::Text)) {
        QMessageBox::warning(this, tr("Application"),
                             tr("Cannot write file %1:\n%2.")
                             .arg(QDir::toNativeSeparators(fileName),
                                  file.errorString()));
        return false;
    } else {
        QTextStream out(&file);
        out << Notes_textEdit->document()->toPlainText();
        file.close();
        return true;
    }
}

bool dlgNotes::saveAs() {
    if (DebugFunctions > 1) {qDebug() << Q_FUNC_INFO;}
    QFileDialog dialog(this);
    dialog.setWindowModality(Qt::WindowModal);
    dialog.setAcceptMode(QFileDialog::AcceptSave);
    if (dialog.exec() != QDialog::Accepted) {
        return false;
    } else {
        return saveFile(dialog.selectedFiles().first());
    }
}

void dlgNotes::on_Notes_textEdit_ContextMenuRequest(const QPoint &pos) {
    if (DebugFunctions > 1) {qDebug() << Q_FUNC_INFO;}

    SuggestionMap.clear();

    QMenu contextMenu(tr("Context menu"), this);
    contextMenu.setToolTipsVisible(true);
    prev = Notes_textEdit->textCursor();
    csr = Notes_textEdit->cursorForPosition(pos);
    csr.select(QTextCursor::WordUnderCursor);
    csr.selectedText();
    //Notes_textEdit->setTextCursor(csr);

    if (csr.hasSelection()) {
        QString text = csr.selectedText();
        //qDebug() << text << Notes_SpellChecker.isSpellingCorrect(text);
        /*if (!Notes_SpellChecker.isSpellingCorrect(text)) {
            QAction * AddWord = new QAction("Add Word", this);
            AddWord->setToolTip("Add word to dictionary");
            contextMenu.addAction(AddWord);
            connect(AddWord, SIGNAL(triggered()), this, SLOT(on_AddWord()));
            contextMenu.addSeparator();

            QStringList suggestions = Notes_SpellChecker.suggestCorrections(text);
            //qDebug() << suggestions;
            for (int idx = 0; idx < suggestions.size() ; idx++) {
                    QAction * SelectSuggestion = new QAction(suggestions.at(idx), this);
                    SuggestionMap[SelectSuggestion] = suggestions.at(idx); //memory leak
                    connect(SelectSuggestion, SIGNAL(triggered()), this, SLOT(on_SelectSuggestion()));
                    contextMenu.addAction(SelectSuggestion);
            }
        }
        */
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
    contextMenu.addSeparator();
    QAction SendTerminal("Send to Terminal", this);
    connect(&SendTerminal, SIGNAL(triggered()), this, SLOT(on_SendToTerminal()));
    QAction SendSelected("Send Selected", this);
    connect(&SendSelected, SIGNAL(triggered()), this, SLOT(on_SendSelected()));
    if (SettingsVars.TerminalTab) {
        contextMenu.addAction(&SendTerminal);
        contextMenu.addAction(&SendSelected);
    }



    contextMenu.exec(Notes_textEdit->mapToGlobal(pos));
}

void dlgNotes::on_SelectSuggestion() {
    if (DebugFunctions > 1) {qDebug() << Q_FUNC_INFO;}
    QString word = SuggestionMap[(QAction*)sender()];

    csr.beginEditBlock();

    csr.removeSelectedText();
    csr.insertText(word);

    csr.endEditBlock();
    //Notes_textEdit->setTextCursor(prev);
}

void dlgNotes::on_AddWord() {/*
    if (DebugFunctions > 1) {qDebug() << Q_FUNC_INFO;}
    QString text = csr.selectedText();
    Notes_SpellChecker.addWord(text);
    highlighter->addWord(text);
    highlighter->rehighlight();*/
}

void dlgNotes::on_cut() {
    if (DebugFunctions > 1) {qDebug() << Q_FUNC_INFO;}
    QString selected = Notes_textEdit->textCursor().selectedText();
    QApplication::clipboard()->setText(selected);
    Notes_textEdit->textCursor().removeSelectedText();
}

void dlgNotes::on_copy() {
    if (DebugFunctions > 1) {qDebug() << Q_FUNC_INFO;}
    QString selected = Notes_textEdit->textCursor().selectedText();
    QApplication::clipboard()->setText(selected);
}

void dlgNotes::on_paste() {
    if (DebugFunctions > 1) {qDebug() << Q_FUNC_INFO;}
    QTextCursor selected = Notes_textEdit->textCursor();
    selected.beginEditBlock();

    selected.removeSelectedText();
    selected.insertText(QApplication::clipboard()->text());

    selected.endEditBlock();
}

void dlgNotes::on_SendToTerminal() {
    if (DebugFunctions > 1) {qDebug() << Q_FUNC_INFO;}
    QPlainTextEdit * plainTextEditSendData = ptrWindow->wMainWindowTab->ptrdlgterminal->findChild<QPlainTextEdit*>("plainTextEditSendData");

    plainTextEditSendData->clear();
    plainTextEditSendData->document()->setPlainText(Notes_textEdit->document()->toPlainText());
    int index;
    if (WorkingVars.NotesIsFloat) { index = 3; } else { index = 4; }


    ptrWindow->wMainWindowTab->tabWidget->setCurrentIndex(index);
    plainTextEditSendData->setFocus();
    if (WorkingVars.NotesIsFloat) {ptrWindow->wMainWindowTab->tabWidget->setCurrentIndex(3);} else {ptrWindow->wMainWindowTab->tabWidget->setCurrentIndex(4);}

}

void dlgNotes::on_SendSelected() {
    if (DebugFunctions > 1) {qDebug() << Q_FUNC_INFO;}
    QString text = Notes_textEdit->textCursor().selectedText();
    QPlainTextEdit * plainTextEditSendData = ptrWindow->wMainWindowTab->ptrdlgterminal->findChild<QPlainTextEdit*>("plainTextEditSendData");

    plainTextEditSendData->clear();
    plainTextEditSendData->document()->setPlainText(text);
    int index;
    if (WorkingVars.NotesIsFloat) { index = 3; } else { index = 4; }
    ptrWindow->wMainWindowTab->tabWidget->setCurrentIndex(index);
    plainTextEditSendData->setFocus();

}

dlgLog::dlgLog(QWidget *parent) : QWidget(parent) {
    if (DebugFunctions > 1) {qDebug() << Q_FUNC_INFO;}
    mainLayout = new QVBoxLayout;
    mainLayout->setContentsMargins(0,0,0,0);

    Log_textEdit = new QTextEdit;
    mainLayout->addWidget(Log_textEdit);
    Log_textEdit->setReadOnly(true);

    setLayout(mainLayout);
    if ((DebugFunctions > 1) || (DebugProp > 0)) {qDebug() << "Exit" << Q_FUNC_INFO;}
}

MyTreeView::MyTreeView(QWidget *parent) : QTreeView(parent)
{
  connect(&timer,SIGNAL(timeout()),this,SLOT(onSingleClick()));
}


void MyTreeView::mouseDoubleClickEvent(QMouseEvent * event)
{
  Q_UNUSED(event)
  QVariant tmpVfileName = MyTreeView::indexAt(pos).data();
  QVariant tmpVpath = MyTreeView::indexAt(pos).parent().data();
  //qDebug() << tmpVfileName.toString() << tmpVpath.toString();
  if (!(QDir(tmpVpath.toString()).path() == QDir(SettingsVars.MessagesFolderRoot).path())) {
      loadFile(SettingsVars.MessagesFolderRoot + tmpVpath.toString() + "/" + tmpVfileName.toString()); }
  timer.stop();
}

void MyTreeView::mousePressEvent(QMouseEvent * event)
{
    if (!contextMenuActive) {
        lastEvent = event;
        mbuttons = event->button();
        pos = event->pos();
        timer.start(250);
        QTreeView::mouseReleaseEvent(event);
    } else {
        event->ignore();
        timer.stop();
    }
}

void MyTreeView::onSingleClick()
{
    if (mbuttons == Qt::RightButton) {
        treeData = ptrWindow->wMainWindowTab->ptrdlgNotes->tree->indexAt(ptrWindow->wMainWindowTab->ptrdlgNotes->tree->pos).data();
        treeDataParent = ptrWindow->wMainWindowTab->ptrdlgNotes->tree->indexAt(ptrWindow->wMainWindowTab->ptrdlgNotes->tree->pos).parent().data();
        //qDebug() << treeData.toString() << treeDataParent.toString();
        timer.stop();
    } else if (mbuttons == Qt::LeftButton) {
        if (MyTreeView::isExpanded(MyTreeView::indexAt(pos))) {
            MyTreeView::collapse(MyTreeView::indexAt(pos));
        } else {
            MyTreeView::expand(MyTreeView::indexAt(pos));
        }
        timer.stop();
    }
}

void MyTreeView::mouseReleaseEvent( QMouseEvent * event ) {

}

