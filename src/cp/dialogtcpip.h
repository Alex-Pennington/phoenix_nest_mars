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
#ifndef DIALOGTCPIP_H
#define DIALOGTCPIP_H

//#include <QDialog>
#include "globals.h"
namespace Ui {
class DialogTCPIP;
}

class DialogTCPIP : public QDialog
{
    Q_OBJECT

protected:
    virtual void moveEvent(QMoveEvent *event);
    virtual void resizeEvent(QResizeEvent *event);

public:
    explicit DialogTCPIP(QWidget *parent = nullptr);
    ~DialogTCPIP();

signals:
    void signalUpdateMainWindow();
    void signalWriteTCPLogFile(QByteArray);


public slots:
    void slotUpdateBrowser(QByteArray data);

private slots:
    void on_checkBoxHideHandshakePackets_clicked();

private:
    Ui::DialogTCPIP *ui;
//    QByteArray printables(QByteArray byteArray);
};

#endif // DIALOGTCPIP_H
