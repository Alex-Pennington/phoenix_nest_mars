/****************************************************************************
**
** Copyright (C) 2016 - 2024 Timothy Millea <timothy.j.millea@gmail.com>
**
    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.

****************************************************************************/
#ifndef DIALOGRETRYQUEUE_H
#define DIALOGRETRYQUEUE_H

#include <QDialog>
#include <QFileSystemModel>
#include <QItemSelectionModel>
#include <QAbstractButton>

namespace Ui {
class DialogRetryQueue;
}

class DialogRetryQueue : public QDialog
{
    Q_OBJECT

public:
    explicit DialogRetryQueue(QWidget *parent = nullptr);
    ~DialogRetryQueue();

private:
    Ui::DialogRetryQueue *ui;
    QFileSystemModel *dirModel, *fileModel;
    QItemSelectionModel *itemModel;
    QString selectedFile;
    
    void setupDialogUI();
    
    void setupDirectoryModel();
    
    void setupItemModel();
    
    void deleteSelectedFiles();
    
signals:
    void signalUpdateLog(QString logData);
    void signalTXRetryQueue();
    void signalReadRetryQueue();
    void signalUpdateStatusBar();

private slots:
    void on_treeViewDirectory_clicked(const QModelIndex &index);
    void on_pushButtonDeleteFiles_clicked();
    void on_pushButtonTXRetryQueue_clicked();
    void on_buttonBox_accepted();
//    void on_treeViewDirectory_doubleClicked(const QModelIndex &index);
};

#endif // DIALOGRETRYQUEUE_H
