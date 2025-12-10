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

#include "dialogretryqueue.h"
#include "ui_dialogretryqueue.h"

#include "globals.h"


void DialogRetryQueue::setupDialogUI()
{
    ui->setupUi(this);
    this->setWindowTitle("Retry Queue - "+globalConfigSettings.generalSettings.positionIdentifier);
    ui->checkBoxExpireImmediate->setChecked(globalConfigSettings.fileManagement.expireImmediateMessages);
    ui->checkBoxExpirePriority->setChecked(globalConfigSettings.fileManagement.expirePriorityMessages);
    ui->checkBoxExpireRoutine->setChecked(globalConfigSettings.fileManagement.expireRoutineMessages);
    
    ui->spinBoxExpireImmediate->setValue(globalConfigSettings.fileManagement.expireImmediateMessagesHours);
    ui->spinBoxExpirePriority->setValue(globalConfigSettings.fileManagement.expirePriorityMessagesHours);
    ui->spinBoxExpireRoutine->setValue(globalConfigSettings.fileManagement.expireRoutineMessagesHours);
}//setupDialogUI

void DialogRetryQueue::setupDirectoryModel()
{
    dirModel = new QFileSystemModel(this);
    dirModel->setFilter(QDir::Files);

    QString retryFolder = QApplication::applicationDirPath()+"/RETRY";
    dirModel->setRootPath(retryFolder);

    ui->treeViewDirectory->setModel(dirModel);
    ui->treeViewDirectory->setRootIndex(dirModel->index(retryFolder));
}//setupDirectoryModel

void DialogRetryQueue::setupItemModel()
{
    itemModel = new QItemSelectionModel(dirModel);

    ui->treeViewDirectory->setSelectionModel(itemModel);
    ui->treeViewDirectory->setSelectionBehavior(QAbstractItemView::SelectItems);
    ui->treeViewDirectory->setSelectionMode(QAbstractItemView::ExtendedSelection);

    ui->treeViewDirectory->hideColumn(1);
    ui->treeViewDirectory->hideColumn(2);
    ui->treeViewDirectory->hideColumn(3);
}//setupItemModel

DialogRetryQueue::DialogRetryQueue(QWidget *parent) :
    QDialog(parent),
    ui(new Ui::DialogRetryQueue)
{
    setupDialogUI();

    setupDirectoryModel();

    setupItemModel();
}//DialogRetryQueue

DialogRetryQueue::~DialogRetryQueue()
{
    delete ui;
}

void DialogRetryQueue::on_treeViewDirectory_clicked(const QModelIndex &index)
{
    emit signalUpdateLog("Function Name: " + tr(Q_FUNC_INFO));

    selectedFile = QApplication::applicationDirPath()+"/RETRY/"+index.data().toString();
    QFile inputFile(selectedFile);

    if(!inputFile.open(QFile::ReadOnly)){
        emit signalUpdateLog("File open error: " + inputFile.errorString());
        return;
    }

    QTextStream inStream(&inputFile);
    ui->textBrowserFileView->clear();
    ui->textBrowserFileView->append(inStream.readAll());
}//on_treeViewDirectory_clicked

void DialogRetryQueue::deleteSelectedFiles()
{
    QModelIndexList itemList = itemModel->selectedIndexes();

    foreach (QModelIndex index, itemList)
        dirModel->remove(index);
}//deleteSelectedFiles

void DialogRetryQueue::on_pushButtonDeleteFiles_clicked()
{
    emit signalUpdateLog("Function Name: " + tr(Q_FUNC_INFO));

    deleteSelectedFiles();

    emit signalReadRetryQueue();

    ui->textBrowserFileView->clear();
}//on_pushButtonDeleteFiles_clicked


void DialogRetryQueue::on_pushButtonTXRetryQueue_clicked()
{
    emit signalTXRetryQueue();
    ui->textBrowserFileView->clear();
}//on_pushButtonTXRetryQueue_clicked

void DialogRetryQueue::on_buttonBox_accepted()
{
    emit signalUpdateStatusBar();
}//on_buttonBox_accepted
