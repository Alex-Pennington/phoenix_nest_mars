#ifndef CLASSRADIO_H
#define CLASSRADIO_H

#include <QObject>

class ClassRadio : public QObject
{
    Q_OBJECT
public:
    explicit ClassRadio(QObject *parent = nullptr);

signals:

public slots:
};

#endif // CLASSRADIO_H