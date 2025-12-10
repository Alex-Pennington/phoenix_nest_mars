#ifndef TABTRAFFIC_H
#define TABTRAFFIC_H

#include <QDialog>
#include <QTabWidget>
#include <QVBoxLayout>
#include <QTextEdit>
#include <QTableWidget>
#include <QSplitter>
#include <QComboBox>

class dlgQSLTab : public QWidget {
    Q_OBJECT
public:
    explicit dlgQSLTab(QWidget *parent = 0, int parentRow = 0, QStringList QSLList = {""});
    QGridLayout *mainLayout;
    static QTableWidget * QSLTable;
    dlgQSLTab * tmpPTRdlgQSLTab;

public slots:
    void on_dlgQSLTab_ContextMenuRequest(const QPoint &pos);
    void on_QSLAll();

};

class dlgTrafficList : public QWidget {
    Q_OBJECT
public:
    explicit dlgTrafficList(QWidget *parent = 0);
    QGridLayout *mainLayout;
    static QTableWidget * TrafficTable;
    int tabIndex = 0;
    QMap<dlgQSLTab*, int> TrafficMap;
    QList<dlgQSLTab *> TabMap;

public slots:
    void on_TrafficTable_ContextMenuRequest(const QPoint &pos);
    void on_QSL();
    void on_QSLTabAsListed();
    void on_QSLTabAllStations();
    void on_RemoveTraffic();

};

class tabtraffic : public QWidget
{
    Q_OBJECT

public:
    explicit tabtraffic(QWidget *parent = 0);
    QTabWidget *tabWidget;
    dlgTrafficList * ptrdlgTrafficList;

private:

};



#endif // TABTRAFFIC_H
