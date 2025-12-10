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

#ifndef DIALOGPROGRESSBAR_H
#define DIALOGPROGRESSBAR_H


#include <QDialog>
#include <QMutex>

namespace Ui {
class DialogProgressBar;
}

class DialogProgressBar : public QDialog
{
    Q_OBJECT

public:
    explicit DialogProgressBar(QWidget *parent = nullptr);
    ~DialogProgressBar();

public slots:
    void slotUpdateProgressBar(int currentDataReceived, int maxDataReceived);
    void slotUpdateStatusLabel(QString currentStatus);
    void slotResetProgressBar();
//    void slotUpdateModemStatus(QByteArray statusByteArray);
//    void slotUpdateProgressBarSNR(double amount);
//    void slotUpdateProgressBarBER(double amount);
//    void slotUpdateProgressBarFER(double amount);

private:
    Ui::DialogProgressBar *ui;
    QMutex progressBarMutex;

};

#endif // DIALOGPROGRESSBAR_H
