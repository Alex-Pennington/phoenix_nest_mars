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
#ifndef DIALOGMODEM_H
#define DIALOGMODEM_H

#include "globals.h"

#define TIMESTAMP_FORMAT "hh:mm:ss.zzzZ"

namespace Ui {
class DialogModem;
}

class DialogModem : public QDialog
{
    Q_OBJECT

protected:
    virtual void moveEvent(QMoveEvent *event);
    virtual void resizeEvent(QResizeEvent *event);

public:
    explicit DialogModem(QWidget *parent = nullptr);
    ~DialogModem();

signals:
    void signalSendDataToModem(QByteArray dataByte);
    void signalWriteModemLogFile(QByteArray dataByte);

public slots:
    void slotUpdateBrowser(QByteArray dataByte, bool received, bool hexCode);
    void slotUpdateModemStatus(QByteArray statusByteArray);
//    void slotUpdateProgressBarSNR(double amount);
//    void slotUpdateProgressBarBER(double amount);
//    void slotUpdateProgressBarFER(double amount);

private slots:
//    void on_pushButtonSendData_clicked();
    void on_lineEditOrderwireData_returnPressed();
    void on_lineEditRawData_returnPressed();

    void on_splitter_splitterMoved(int pos, int index);


private:
    Ui::DialogModem *ui;
//    QByteArray printables(QByteArray byteArray);
    QString humanReadableHex(QByteArray hexCode);
    void setuDialogUI();
    void displayReceivedData(const QByteArray &dataByte, bool hexCode);
    void displaySentData(const QByteArray &dataByte, bool hexCode);
    void displayHexCode(const QByteArray &header, const QByteArray &dataByte);
    void displayASCIIText(const QByteArray &dataByte);
};

#endif // DIALOGMODEM_H
