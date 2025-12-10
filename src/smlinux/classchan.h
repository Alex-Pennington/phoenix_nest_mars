#ifndef CLASSCHAN_H
#define CLASSCHAN_H
#include <QString>

extern QString WChanIdx[];

struct chan {
    QString ChIdx;
    double Freq;
};

struct relFreq {
    QString ChanId;
    QString padFreq;
    double Mhz;
    double rel;
};

extern relFreq varRelFreq[25];

class classchan {
private:
    chan *pa; // points to the array
    int length; // the # elements
    int nextIndex; // the next highest index value
public:
    classchan(); // the constructor
    ~classchan(); // the destructor
    chan& operator[](int index); // the indexing operation
    void add(QString vChIdx, double vFreq); // add a new value to the end
    int size(); // return length
    bool searchIdx(QString);
    int searchResult;
    bool searchClosestFreq(QString searchTerm);
    void print();
    int parseWChanCSV (QString vWorkingChanCSV, relFreq prtVarRelFreq[]);
    QString pad(double input, int beforePoint, int afterPoint);
};

#endif // CLASSCHAN_H
