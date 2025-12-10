#ifndef CLASSCONVERTBAUDOT_H
#define CLASSCONVERTBAUDOT_H

#include <QObject>
#include <QDebug>

class ClassConvertBaudot : public QObject
{
    Q_OBJECT
    bool isFigures = false;
    bool figureChange = false;

public:
    explicit ClassConvertBaudot(QObject *parent = nullptr);
    QByteArray convertBaudottoASCII(QByteArray baudotMessage);
    QByteArray convertASCIItoBaudot(QByteArray ASCIIMessage);

signals:

public slots:
};

#endif // CLASSCONVERTBAUDOT_H
