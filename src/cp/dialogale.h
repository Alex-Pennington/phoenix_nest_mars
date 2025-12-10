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
#ifndef DIALOGALE_H
#define DIALOGALE_H

#include "globals.h"

namespace Ui {
class DialogAle;
}

class DialogAle : public QDialog
{
    Q_OBJECT

protected:
    virtual void moveEvent(QMoveEvent *event);
    virtual void resizeEvent(QResizeEvent *event);

public:
    explicit DialogAle(QWidget *parent = nullptr);
    ~DialogAle();

signals:
    void signalSendALECommand(QByteArray aleCommand);

public slots:
    void slotUpdateBrowser(QByteArray dataByte);
    void slotUpdateAMDBrowser(QByteArray dataByte);
    void slotUpdateHeardListBrowser(QStringList ALEHeardList);
    void slotUpdateALEAddressSelect(QStringList knowAddressList);

private slots:
    void on_pushButtonSendAMD_clicked();

    void on_splitter_splitterMoved(int pos, int index);

    void on_splitter_2_splitterMoved(int pos, int index);

private:
    Ui::DialogAle *ui;
    void writeALELogFile(QString data);
    void rotateLog();
    QFile aleLogFile;
    QStringList ALEHeardList;

};

#endif // DIALOGALE_H
