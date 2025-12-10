#ifndef HEATMAP_H
#define HEATMAP_H
#include <QString>
#include <QPoint>
#include <QColor>
#include <QObject>

struct heatpoint {
    QString lon;
    QString lat;
    float val;
    QPoint Point;
    QPoint Point2;
    QColor Color;
    bool draw;
};

struct hmArray {
    QString ChanID;
    heatpoint * ptrHM;
    QString size;
    int length = 10;
    int nextIndex = 0;
};

class heatmap : public QObject
{
    Q_OBJECT

public slots:
    void parseHeatmap() {}

public:
    heatmap(QObject* parent = 0) : QObject(parent) {
        for (int i = 0; i < 10; i++) {
            arrHM[i].ptrHM = new heatpoint[100];
        }
    }
    ~heatmap() {
        for (int i = 0; i < 10; i++) {
            delete[] arrHM[i].ptrHM;
        }
    }
    
    hmArray arrHM[10];
    int gridSize = 10;
};

extern heatmap varHeatMap;

#endif // HEATMAP_H
