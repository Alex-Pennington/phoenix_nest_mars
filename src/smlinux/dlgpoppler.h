#ifndef DLGPOPPLER_H
#define DLGPOPPLER_H

//#include "imageviewer.h"

#include <QWidget>
#include <QLabel>
#include <QPixmap>
#include <QGridLayout>
#include <QDebug>
#include <QTreeView>
#include <QFileSystemModel>
#include <QSplitter>
#include <QTimer>
#include <QMouseEvent>

class MyLibraryTreeView: public QTreeView
{
  Q_OBJECT
public:
  MyLibraryTreeView(QWidget *parent = nullptr);
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
 void loadFile(QModelIndex);

};

class dlgpoppler  : public QWidget
{
    Q_OBJECT

public:
    dlgpoppler(QWidget *parent = nullptr);
//    static ImageViewer * myImageViewer;
    QGridLayout * mainLayout;
    MyLibraryTreeView * tree;
    QFileSystemModel * model;
    QSplitter * mySplitter;
    void uploadFile();

public slots:
    void loadFile(QModelIndex tempMidx);
};


#endif // DLGPOPPLER_H
