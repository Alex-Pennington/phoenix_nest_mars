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

#include "dialogprogressbar.h"
#include "ui_dialogprogressbar.h"

#include "globals.h"

DialogProgressBar::DialogProgressBar(QWidget *parent) :
    QDialog(parent),
    ui(new Ui::DialogProgressBar)
{
    ui->setupUi(this);
    this->setWindowTitle(globalConfigSettings.generalSettings.positionIdentifier);
}//DialogProgressBar

DialogProgressBar::~DialogProgressBar()
{
    delete ui;
}//DialogProgressBar

void DialogProgressBar::slotUpdateProgressBar(int currentDataReceived, int maxDataReceived)
{
    QMutexLocker mutexLocker(&progressBarMutex);

    ui->progressBarStatus->setMinimum(0);
    ui->progressBarStatus->setMaximum(maxDataReceived);
    ui->progressBarStatus->setValue(currentDataReceived);
}//updateProgressBar

void DialogProgressBar::slotUpdateStatusLabel(QString currentStatus)
{
    ui->labelStatusDisplay->setText(currentStatus);
}//updateStatusLabel

void DialogProgressBar::slotResetProgressBar()
{
    ui->progressBarStatus->setValue(0);
}//resetProgressBar



