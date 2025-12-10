#include "dlgdatabaseeditor.h"

dlgDatabaseEditor::dlgDatabaseEditor(QWidget *parent, clsDB * ptrWorkingDB) : QWidget(parent)
{
    mainLayout = new QGridLayout;

    WorkingDBTable = new QTableWidget(0,4);
    QStringList labels; labels << "Callsign" << "AbrCallSign" << "WGS84_Lat" << "WGS84_Lon";
    WorkingDBTable->setHorizontalHeaderLabels(labels);
    WorkingDBTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    WorkingDBTable->horizontalHeader()->resizeSections(QHeaderView::ResizeToContents);
    WorkingDBTable->setColumnWidth(0,75);
    WorkingDBTable->horizontalHeader()->setStretchLastSection(true);
    WorkingDBTable->verticalHeader()->hide();
    WorkingDBTable->setShowGrid(true);
    WorkingDBTable->setContextMenuPolicy(Qt::CustomContextMenu);
    //connect(WorkingDBTable, SIGNAL( customContextMenuRequested(const QPoint &) ), this, SLOT(on_RosterTable_ContextMenuRequest(const QPoint &)));
    //connect(WorkingDBTable, SIGNAL(cellChanged(int,int)), this, SLOT(RosterCellChanged(int, int)));

    mainLayout->addWidget(WorkingDBTable,0,0);

    setLayout(mainLayout);

    load_WorkingDB(ptrWorkingDB);


}

void dlgDatabaseEditor::load_WorkingDB(clsDB * WorkingDB) {
    const QSignalBlocker blocker(WorkingDBTable);

    for (int i = 0 ; i <= WorkingDB->size() ; i++) {

        int row = WorkingDBTable->rowCount();
        WorkingDBTable->insertRow(row);
        QTableWidgetItem * vCallSignItem = new QTableWidgetItem;
        vCallSignItem->setText(WorkingDB->operator[](i).CallSign);
        WorkingDBTable->setItem(row, 0, vCallSignItem);
        QTableWidgetItem * AbrCallSign = new QTableWidgetItem;
        AbrCallSign->setText(WorkingDB->operator[](i).AbrCallSign);
        WorkingDBTable->setItem(row, 1, AbrCallSign);
        QTableWidgetItem * WGSLat = new QTableWidgetItem;
        WGSLat->setText(WorkingDB->operator[](i).WGSLat);
        WorkingDBTable->setItem(row, 2, WGSLat);
        QTableWidgetItem * WGSLon = new QTableWidgetItem;
        WGSLon->setText(WorkingDB->operator[](i).WGSLon);
        WorkingDBTable->setItem(row, 3, WGSLon);

    }

}
