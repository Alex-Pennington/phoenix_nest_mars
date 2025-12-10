#ifndef CLASSMODEM_H
#define CLASSMODEM_H

#include <QObject>

class ClassModem : public QObject
{
    Q_OBJECT
public:
    explicit ClassModem(QObject *parent = nullptr);

signals:

public slots:
};

#endif // CLASSMODEM_H