#ifndef MAINWINDOWTAB_H
#define MAINWINDOWTAB_H
#include "renderarea.h"
#include "tabtraffic.h"
#include "dlgterminal.h"
#include "highlighter.h"
#include "dlgsettingstab.h"
#include "currentinfo.h"
#include "dlgpoppler.h"
#include "classmetar.h"

#include <QDialog>
#include <QTabWidget>
#include <QVBoxLayout>
#include <QTextEdit>
#include <QTableWidget>
#include <QSplitter>
#include <QComboBox>
#include <QMap>
#include <QListWidget>
#include <QFileSystemWatcher>
#include <QFileSystemModel>
#include <QTreeView>
#include <QMouseEvent>


class MyTreeView: public QTreeView
{
  Q_OBJECT
public:
  MyTreeView(QWidget *parent = 0);
  QVariant treeData;
  QVariant treeDataParent;

protected:
  virtual void mouseDoubleClickEvent(QMouseEvent * event);
  virtual void mousePressEvent(QMouseEvent * event);
  virtual void mouseReleaseEvent(QMouseEvent * event);
  Qt::MouseButton mbuttons;
  QPoint pos;
  QMouseEvent * lastEvent;

private:
  QTimer timer;
  bool contextMenuActive = false;

private slots:
  void onSingleClick();

signals:
 void loadFile(QString);

};

class dlgProp : public QWidget {
    Q_OBJECT
public:
    explicit dlgProp(QWidget *parent = 0);
    QVBoxLayout *mainLayout;

};

class dlgNCS : public QWidget {
    Q_OBJECT
public:
    explicit dlgNCS(QWidget *parent = 0);
    tabtraffic * ptrTabTraffic_NCS;
    QGridLayout *mainLayout;
    static QTableWidget * RosterTable;
    QGridLayout * commandBoxLayout;
    QHBoxLayout * commandButtonsLayout;
    QSplitter * splitter;
    QSplitter * splitter2;
    QPushButton * pbCkOut;
    QPushButton * pbListTfc;
    QPushButton * pbXlcTfc;
    QPushButton * pbCorrection;
    QPushButton * pbReqCom;
    QPushButton * pbQSL;
    QPushButton * pbQSY;
    QPushButton * pbReturn;
    QPushButton * pbStatus;
    QPushButton * pbTasking;
    void CreateCommandPB();
    QTextEdit * leCommand;
    void commandExec();
    QComboBox * cmbxCmd;
    void listTraffic(QString input, QString CallSign);

public slots:
    void RosterCellChanged(int row, int column);
    void on_RosterTable_ContextMenuRequest(const QPoint &pos);
    void on_Remove();
    void on_RemoveAll();
    void on_Closed();
    void on_Returned();
    void on_NCS();
    void on_ANCS();
    void on_MoveUp();
    void on_MoveDown();
    void on_AmmendTraffic();
    void on_SendRoster();
    void on_SendRosterTerminal();
    void on_SendComspot();

protected:
    bool eventFilter(QObject *obj, QEvent *event) override;
};

class dlgNotes : public QWidget {
    Q_OBJECT
public:
//    classspelchecker Notes_SpellChecker;
    QMap<QAction*, QString> SuggestionMap;
    explicit dlgNotes(QWidget *parent = 0);
    static QTextEdit * Notes_textEdit;
    static QTextEdit * Rec_textEdit;
    QGridLayout *mainLayout;
    QBoxLayout *toolLayout;
    QToolBar *toolbar;
    QString currFilename = "";
    QString currWatchDir = "";
    Highlighter *highlighter;
    QTextCursor prev;
    QTextCursor csr;
    QSplitter * splitter;
    QListWidget *lboxFile;
    QFileSystemWatcher dirWatcher;
    QLabel * lblFileName;
    QStatusBar *statusBar;
    QFileSystemModel *model;
    MyTreeView *tree;
    QAction *actFloat;
    QDialog * widget = nullptr;
    void closeEvent(QCloseEvent *e) override;

public slots:
    void on_Notes_textEdit_ContextMenuRequest(const QPoint &pos);
    void on_SelectSuggestion();
    void on_copy();
    void on_paste();
    void on_cut();
    void on_AddWord();
    void on_SendToTerminal();
    void on_SendSelected();
    void newFile();
    bool maybeSave();
    void open();
    bool loadFile(QString);
    bool save();
    bool saveFile(QString);
    bool saveAs();
    void QSL();
    void calldirWatcherChecker(QString);
    void DELAct();
    void ARCHAct();
    void on_tree_itemDoubleClicked(QModelIndex index);
    void on_menu_Tree(const QPoint &);
    void on_selected_Open();
    void FontChanged(const QFont &font);
    void selectedFontSizeInc();
    void selectedFontSizeDec();
    void selectedLastRX();
    void selectedFloat();
    void selectedInsertTab();
    void selectedACPD();

};

class dlgLog : public QWidget {
    Q_OBJECT
public:
    explicit dlgLog(QWidget *parent = 0);
    QVBoxLayout *mainLayout;
    static QTextEdit * Log_textEdit;

};

class mainwindowtab : public QWidget
{
    Q_OBJECT

public:
    explicit mainwindowtab(QWidget *parent = 0);
    QTabWidget *tabWidget;
    QTabWidget *tabWidgetLibrary;
    dlgProp * ptrdlgProp;
    dlgNCS * ptrdlgNCS;
    dlgNotes * ptrdlgNotes;
    dlgLog * ptrdlgLog;
    dlgterminal * ptrdlgterminal;
    dlgSettingsTab * ptrdlgSettingsTab;
    dlgpoppler * ptrdlgPoppler;
    currentinfo * ptrCurrentInfo;
    classmetar * ptrMetar;

private:

private slots:
    void tabChanged(int);

};


#endif // MAINWINDOWTAB_H
