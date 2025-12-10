#include "classfile.h"

Dynarray::Dynarray() {
    pa = new FileVersions[10];
    for (int i = 0; i < 10; i++) {
        pa[i].FileName = "";
        pa[i].Version = "";
    }
    length = 10;
    nextIndex = 0;
}

Dynarray::~Dynarray() {
    delete [] pa;
}

FileVersions& Dynarray::operator[](int index) {
    FileVersions *pnewa; // pointer for new array
    if (index >= length) { // is element in the array?
        pnewa = new FileVersions[index + 10]; // allocate a bigger array
        for (int i = 0; i < nextIndex; i++) // copy old values
            pnewa[i] = pa[i];
        for (int j = nextIndex; j < index + 10; j++) { // initialize remainder
            pa[j].FileName = "";
            pa[j].Version = "";
        }
        length = index + 10; // set length to bigger size
        delete [] pa; // delete the old array
        pa = pnewa; // reassign the new array
    }
    if (index > nextIndex) // set nextIndex past index
        nextIndex = index + 1;
    return *(pa + index); // a reference to the element
}

void Dynarray::add(QString Fn, QString Ver) {
    FileVersions *pnewa;
    if (nextIndex == (length - 1) ) {
        length = length + 10;
        pnewa = new FileVersions[length];
        for (int i = 0; i < nextIndex; i++) {
            pnewa[i] = pa[i];
        }
        for (int j = nextIndex; j < length; j++) {
            pnewa[j].FileName = "";
            pnewa[j].Version = "";
        }
        delete [] pa;
        pa = pnewa;
    }
    pa[nextIndex].FileName = Fn;
    pa[nextIndex].Version = Ver;
    nextIndex++;
}

int Dynarray::size() {
    return (nextIndex - 1);
}

