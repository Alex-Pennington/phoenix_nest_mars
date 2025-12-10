#include "dlgpoppler.h"
#include <QDesktopServices>

//ImageViewer * dlgpoppler::myImageViewer = nullptr;

void MyLibraryTreeView::mouseDoubleClickEvent(QMouseEvent * event)
{
  Q_UNUSED(event)
  QVariant tmpVfileName = MyLibraryTreeView::indexAt(pos).data();
  QVariant tmpVpath = MyLibraryTreeView::indexAt(pos).parent().data();
  QModelIndex tempMidx =  MyLibraryTreeView::indexAt(pos);

  //qDebug() << tmpVfileName.toString() << tmpVpath.toString();
  if (!(QDir(tmpVpath.toString()).path() == QDir("C:/").path())) {
      loadFile(tempMidx);
  }
  timer.stop();
}

void MyLibraryTreeView::mousePressEvent(QMouseEvent * event) {
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

void MyLibraryTreeView::onSingleClick()
{
    if (mbuttons == Qt::RightButton) {
        treeData = this->indexAt(this->pos).data();
        treeDataParent = this->indexAt(this->pos).parent().data();
        //qDebug() << treeData.toString() << treeDataParent.toString();
        timer.stop();
    } else if (mbuttons == Qt::LeftButton) {
        if (MyLibraryTreeView::isExpanded(MyLibraryTreeView::indexAt(pos))) {
            MyLibraryTreeView::collapse(MyLibraryTreeView::indexAt(pos));
        } else {
            MyLibraryTreeView::expand(MyLibraryTreeView::indexAt(pos));
        }
        timer.stop();
    }
}

void MyLibraryTreeView::mouseReleaseEvent( QMouseEvent * event ) {

}

MyLibraryTreeView::MyLibraryTreeView(QWidget *parent) : QTreeView(parent)
{
  connect(&timer,SIGNAL(timeout()),this,SLOT(onSingleClick()));
}

dlgpoppler::dlgpoppler(QWidget *parent): QWidget(parent)
{
    mainLayout = new QGridLayout();
    //myImageViewer = new ImageViewer();
    mySplitter = new QSplitter();
    mySplitter->setContentsMargins(0,0,0,0);

    //mySplitter->addWidget(myImageViewer);



    const QString rootPath = "/home/user/MSC/StationMapper/LIBRARY/";
    model = new QFileSystemModel;
    tree = new MyLibraryTreeView;
    model->setRootPath("");
    model->setReadOnly(false);
    //model->setNameFilters(QStringList({"*.pdf"}));
    //qDebug() << model->supportedDragActions();
    tree->setModel(model);
    if (!rootPath.isEmpty()) {
        const QModelIndex rootIndex = model->index(QDir::cleanPath(rootPath));
        if (rootIndex.isValid())
            tree->setRootIndex(rootIndex);
    }
    tree->setColumnWidth(0, tree->width());
    connect(tree,&MyLibraryTreeView::loadFile,this,&dlgpoppler::loadFile);

    // Demonstrating look and feel features
    tree->setAnimated(false);
    tree->setIndentation(20);
    tree->setSortingEnabled(true);
    tree->sortByColumn(3,Qt::SortOrder::DescendingOrder);
//    const QSize availableSize = QApplication::desktop()->availableGeometry(tree).size();
//    tree->resize(availableSize / 2);
//    tree->setColumnWidth(0, tree->width() / 3);

    tree->setDragEnabled(true);
    tree->setAcceptDrops(true);
    tree->setDropIndicatorShown(true);

    mySplitter->addWidget(tree);
    mySplitter->setSizes({600,200});
    mainLayout->addWidget(mySplitter);
    setLayout(mainLayout);

}

void dlgpoppler::loadFile(QModelIndex tempMidx) {
    qDebug() << model->filePath(tempMidx);
    QDesktopServices::openUrl(QUrl::fromLocalFile(model->filePath(tempMidx)));
}
