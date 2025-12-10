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

#include "classmessagequeue.h"

bool ClassMessageQueue::insertMessage(ClassMessage *message)
{
    qDebug() << "Function Name: " + tr(Q_FUNC_INFO);
    emit signalUpdateDebugLog("Function Name: " + tr(Q_FUNC_INFO));
    emit signalUpdateDebugLog("Locking messageQueueMutex");
    QMutexLocker mutexLocker(&messageQueueMutex);
    emit signalUpdateDebugLog("Past messageQueueMutex");

    int routineInsertionPoint = 0, priorityInsertionPoint = 0,
            immediateInsertionPoint = 0, flashInsertionPoint = 0;
    if(messageQueue->size() == 0)
    {
        messageQueue->enqueue(message);
        emit signalUpdateDebugLog("Un-Locking messageQueueMutex");
        return true;
    }

    //foreach message in messageQueue
    for(int i = 0; i < this->messageQueue->size(); i++)
    {
        if(messageQueue->at(i)->getMessagePriority() == 4)
            routineInsertionPoint = i+1;
        else if(messageQueue->at(i)->getMessagePriority() == 3)
            priorityInsertionPoint = i+1;
        else if(messageQueue->at(i)->getMessagePriority() == 2)
            immediateInsertionPoint = i+1;
        else if(messageQueue->at(i)->getMessagePriority() == 1)
            flashInsertionPoint = i+1;
    }

    switch (message->getMessagePriority()) {
    case 4:
        messageQueue->insert(routineInsertionPoint,message);
        emit signalUpdateDebugLog("Un-Locking messageQueueMutex");
        return true;
        break;
    case 3:
        messageQueue->insert(priorityInsertionPoint,message);
        emit signalUpdateDebugLog("Un-Locking messageQueueMutex");
        return true;
        break;
    case 2:
        messageQueue->insert(immediateInsertionPoint,message);
        emit signalUpdateDebugLog("Un-Locking messageQueueMutex");
        return true;
        break;
    case 1:
        messageQueue->insert(flashInsertionPoint,message);
        emit signalUpdateDebugLog("Un-Locking messageQueueMutex");
        return true;
        break;
    case -1:
        emit signalUpdateDebugLog("Un-Locking messageQueueMutex");
        return false;
        break;
    default:
        messageQueue->insert(messageQueue->count()+1,message);
        emit signalUpdateDebugLog("Un-Locking messageQueueMutex");
        return true;
        break;
    }//switch
    emit signalUpdateDebugLog("Un-Locking messageQueueMutex");
    return false;
}//insertMessage

bool ClassMessageQueue::isEmpty()
{
    return this->messageQueue->isEmpty();
}

int ClassMessageQueue::getMessageCount()
{
    return messageQueue->count();
}

void ClassMessageQueue::clearMessageQueue()
{
    if(g_ExitApplication)
        return;

    emit signalUpdateDebugLog("Function Name: " + tr(Q_FUNC_INFO));
    while (!messageQueue->isEmpty()) {
        ClassMessage *message = messageQueue->dequeue();
        delete message;
        message = nullptr;
    }
}

bool ClassMessageQueue::getB_isQueueCurrentlyProcessing() const
{
    return b_isQueueCurrentlyProcessing;
}

void ClassMessageQueue::setB_isQueueCurrentlyProcessing(bool newB_isQueueCurrentlyProcessing)
{
    emit signalUpdateDebugLog("Function Name: " + tr(Q_FUNC_INFO));
    b_isQueueCurrentlyProcessing = newB_isQueueCurrentlyProcessing;
}

ClassMessageQueue::ClassMessageQueue(QObject *parent) : QObject(parent)
{
    emit signalUpdateDebugLog("Function Name: " + tr(Q_FUNC_INFO));
    messageQueue = new QQueue<ClassMessage *>;
}

ClassMessageQueue::~ClassMessageQueue()
{
    emit signalUpdateDebugLog("Function Name: " + tr(Q_FUNC_INFO));
    clearMessageQueue();
    messageQueue = nullptr;
}

ClassMessage *ClassMessageQueue::getNextMessage()
{
    emit signalUpdateDebugLog("Function Name: " + tr(Q_FUNC_INFO));
    QMutexLocker mutexLocker(&messageQueueMutex);
    return this->messageQueue->dequeue();
}
