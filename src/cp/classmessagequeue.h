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
#ifndef CLASSMESSAGEQUEUE_H
#define CLASSMESSAGEQUEUE_H


#include "globals.h"
#include "classmessage.h"

class ClassMessageQueue : public QObject
{
    Q_OBJECT
    QQueue<ClassMessage *> *messageQueue = nullptr;
    //mutexes for QMutexLocker
    QMutex messageQueueMutex;

    bool b_isQueueCurrentlyProcessing = false;


public:
    explicit ClassMessageQueue(QObject *parent = nullptr);
    ~ClassMessageQueue();
    ClassMessage *getNextMessage();
    bool insertMessage(ClassMessage *message);
    bool isEmpty();
    int getMessageCount();
    void clearMessageQueue();

    bool getB_isQueueCurrentlyProcessing() const;
    void setB_isQueueCurrentlyProcessing(bool newB_isQueueCurrentlyProcessing);

signals:
void signalUpdateDebugLog(QString logString);

public slots:
};

#endif // CLASSMESSAGEQUEUE_H
