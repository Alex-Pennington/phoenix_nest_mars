#include "tabtraffic.h"
#include "debug.h"
#include "main.h"

#include <QDebug>
#include <QHeaderView>
#include <QMenu>


QTableWidget * dlgTrafficList::TrafficTable = 0;
QTableWidget * dlgQSLTab::QSLTable = 0;

tabtraffic::tabtraffic(QWidget *parent): QWidget (parent) {
    if ((DebugFunctions > 1) || (DebugProp > 0)) {qDebug() << Q_FUNC_INFO;}

    tabWidget = new QTabWidget;
    ptrdlgTrafficList = new dlgTrafficList();
    tabWidget->addTab(ptrdlgTrafficList,"Traffic");
    QVBoxLayout *mainLayout = new QVBoxLayout;
    mainLayout->addWidget(tabWidget);
    mainLayout->setContentsMargins(0,0,0,0);
    setLayout(mainLayout);

}

dlgTrafficList::dlgTrafficList(QWidget *parent) : QWidget(parent) {
    if ((DebugFunctions > 1) || (DebugNCS > 0)) {qDebug() << Q_FUNC_INFO;}
    mainLayout = new QGridLayout;
    mainLayout->setContentsMargins(0,0,0,0);

    TrafficTable = new QTableWidget(0,4);
    QStringList TFClabels; TFClabels << "Listed By" << "QP" << "Disposition" << "Addressees";
    TrafficTable->setHorizontalHeaderLabels(TFClabels);
    TrafficTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    TrafficTable->horizontalHeader()->resizeSections(QHeaderView::ResizeToContents);
    TrafficTable->setColumnWidth(0,75);
    TrafficTable->horizontalHeader()->setStretchLastSection(true);
    //TrafficTable->verticalHeader()->hide();
    TrafficTable->setShowGrid(true);
    TrafficTable->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(TrafficTable, SIGNAL( customContextMenuRequested(const QPoint &) ), this, SLOT(on_TrafficTable_ContextMenuRequest(const QPoint &)));

    TabMap.append(NULL);

    mainLayout->addWidget(TrafficTable);
    setLayout(mainLayout);
    installEventFilter(this);
}

void dlgTrafficList::on_TrafficTable_ContextMenuRequest(const QPoint &pos) {
    if ((DebugFunctions > 1) || (DebugNCS > 0)) {qDebug() << Q_FUNC_INFO;}
    int row = TrafficTable->rowAt(pos.y());
    if (row == -1) {return;}
    QMenu contextMenu(tr("Context menu"), this);
    contextMenu.setToolTipsVisible(true);
    QAction QSL("QSL", this);
    QSL.setToolTip("Set Disposition to QSL");
    QAction AsListed("As Listed", this);
    AsListed.setToolTip("QSL Assistant Tab - Addressees As Listed");
    QAction AllStations("All Stations", this);
    AllStations.setToolTip("QSL Assistant Tab - Addressees as all stations in roster.");
    QAction RemoveTraffic("Remove",this);
    RemoveTraffic.setToolTip("Remove Traffic Item");

    contextMenu.addAction(&QSL);
    connect(&QSL, SIGNAL(triggered()), this, SLOT(on_QSL()));
    contextMenu.addSection("QSL Assistants");
    contextMenu.addAction(&AsListed);
    connect(&AsListed, SIGNAL(triggered()), this, SLOT(on_QSLTabAsListed()));
    contextMenu.addAction(&AllStations);
    connect(&AllStations, SIGNAL(triggered()), this, SLOT(on_QSLTabAllStations()));
    contextMenu.addSeparator();
    contextMenu.addAction(&RemoveTraffic);
    connect(&RemoveTraffic, SIGNAL(triggered()), this, SLOT(on_RemoveTraffic()));

    contextMenu.exec(TrafficTable->mapToGlobal(pos));
}

void dlgTrafficList::on_QSL() {
    const QSignalBlocker blocker(TrafficTable);
    QList<QModelIndex> list = TrafficTable->selectionModel()->selectedRows();
    for(int i=0; i < list.size();i++) {
        QModelIndex item = list.at(i);
        if (item.column() == 0) {
            int row = item.row();
            QTableWidgetItem *StatusItem = new QTableWidgetItem;
            StatusItem->setText("QSL");
            TrafficTable->setItem(row, 2, StatusItem);
         }
    }
}

void dlgTrafficList::on_QSLTabAsListed() {
    int row = TrafficTable->currentRow();
    QStringList QSLlist = TrafficTable->item(row,3)->text().split(" ");
    if (QSLlist.size() > 1) {
        dlgQSLTab * ptrQSLTab = new dlgQSLTab(this,row,QSLlist);
        bool ok;
        QString MSGnum = QInputDialog::getText(this,"Traffic Description","Message Number:", QLineEdit::Normal,QString::number(tabIndex),&ok);
        ptrWindow->wMainWindowTab->ptrdlgNCS->ptrTabTraffic_NCS->tabWidget->addTab(ptrQSLTab, MSGnum);
        tabIndex++;
        TabMap.append(ptrQSLTab);
        TrafficMap[ptrQSLTab] = row;
        qDebug() << ptrQSLTab;
    } else {
        qDebug() << "Only one entry found in QSL lsit.";
    }
}

void dlgTrafficList::on_QSLTabAllStations() {
    int row = TrafficTable->currentRow();
    dlgQSLTab * ptrQSLTab = new dlgQSLTab(this,row,RosterVar.list());
    bool ok;
    QString MSGnum = QInputDialog::getText(this,"Traffic Description","Message Number:", QLineEdit::Normal,QString::number(tabIndex),&ok);
    ptrWindow->wMainWindowTab->ptrdlgNCS->ptrTabTraffic_NCS->tabWidget->addTab(ptrQSLTab, MSGnum);
    tabIndex++;
    TabMap.append(ptrQSLTab);
    TrafficMap[ptrQSLTab] = row;
    qDebug() << ptrQSLTab;
}

void dlgTrafficList::on_RemoveTraffic() {
    if (DebugFunctions > 1) {qDebug() << Q_FUNC_INFO;}
    QItemSelectionModel *select = TrafficTable->selectionModel();
    QList<QModelIndex> list = select->selectedRows();
    for(int i=0; i < list.size();i++) {
        QModelIndex item = list.at(i);
        int row = item.row();
        {
            const QSignalBlocker blocker(TrafficTable);
            TrafficTable->removeRow(row);
        }
    }
}

dlgQSLTab::dlgQSLTab(QWidget *parent, int parentRow, QStringList QSLList): QWidget (parent) {
    if ((DebugFunctions > 1) || (DebugNCS > 0)) {qDebug() << Q_FUNC_INFO  << QSLList;}

    QGridLayout * mainLayout = new QGridLayout;

    QSLTable = new QTableWidget(0,2);
    QStringList labels; labels << "Callsign" << "Disposition (QSL)";
    QSLTable->setHorizontalHeaderLabels(labels);
    QSLTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    QSLTable->horizontalHeader()->resizeSections(QHeaderView::ResizeToContents);
    QSLTable->setColumnWidth(0,75);
    QSLTable->horizontalHeader()->setStretchLastSection(true);
    QSLTable->verticalHeader()->hide();
    QSLTable->setShowGrid(true);
    QSLTable->setContextMenuPolicy(Qt::CustomContextMenu);
    for (int i = 0; i < QSLList.size() ; i++) {
        int row = QSLTable->rowCount();
        QSLTable->insertRow(row);
        QTableWidgetItem * vCallSignItem = new QTableWidgetItem;
        vCallSignItem->setText(QSLList.at(i));
        QSLTable->setItem(row, 0, vCallSignItem);
        QCheckBox * chkBox = new QCheckBox;
        QSLTable->setCellWidget(row,1,chkBox);
    }

    mainLayout->addWidget(QSLTable);
    setLayout(mainLayout);
    connect(QSLTable, SIGNAL( customContextMenuRequested(const QPoint &) ), this, SLOT(on_dlgQSLTab_ContextMenuRequest(const QPoint &)));

}

void dlgQSLTab::on_dlgQSLTab_ContextMenuRequest(const QPoint &pos) {
    if ((DebugFunctions > 1) || (DebugNCS > 0)) {qDebug() << Q_FUNC_INFO;}
    QMenu contextMenu(tr("Context menu"), this);
    QAction QSL("QSL All", this);
    contextMenu.addAction(&QSL);
    connect(&QSL, SIGNAL(triggered()), this, SLOT(on_QSLAll()));
    QTableWidget *tmpPtr = (QTableWidget *)sender();
    tmpPTRdlgQSLTab = (dlgQSLTab *) tmpPtr->parent();
    contextMenu.exec(tmpPtr->mapToGlobal(pos));
}

void dlgQSLTab::on_QSLAll() {
    int row = ptrWindow->wMainWindowTab->ptrdlgNCS->ptrTabTraffic_NCS->ptrdlgTrafficList->TrafficMap[tmpPTRdlgQSLTab];
    QTableWidgetItem * vQSLItem = new QTableWidgetItem;
    vQSLItem->setText("QSL All");
    ptrWindow->wMainWindowTab->ptrdlgNCS->ptrTabTraffic_NCS->ptrdlgTrafficList->TrafficTable->setItem(row,2,vQSLItem);
    int idx = ptrWindow->wMainWindowTab->ptrdlgNCS->ptrTabTraffic_NCS->ptrdlgTrafficList->TabMap.indexOf(tmpPTRdlgQSLTab);
    ptrWindow->wMainWindowTab->ptrdlgNCS->ptrTabTraffic_NCS->ptrdlgTrafficList->TabMap.removeAt(idx);
    ptrWindow->wMainWindowTab->ptrdlgNCS->ptrTabTraffic_NCS->tabWidget->removeTab(idx);
}
