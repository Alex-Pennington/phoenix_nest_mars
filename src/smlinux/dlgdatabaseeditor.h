#ifndef DLGDATABASEEDITOR_H
#define DLGDATABASEEDITOR_H

#include <QObject>
#include <QWidget>
#include <QTableWidget>
#include <QHeaderView>
#include <QGridLayout>

#include "classdb.h"

class dlgDatabaseEditor : public QWidget
{
    Q_OBJECT
public:
    explicit dlgDatabaseEditor(QWidget *parent = nullptr, clsDB * WorkingDB = nullptr);
    QTableWidget * WorkingDBTable;
    QGridLayout * mainLayout;

signals:

private:
    void load_WorkingDB(clsDB * WorkingDB);

};

#endif // DLGDATABASEEDITOR_H
