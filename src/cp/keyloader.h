/* Stub KeyLoader for GPL build
 * Key management not available without CUI components
 */

#ifndef KEYLOADER_H
#define KEYLOADER_H

#include <QDialog>
#include <QVBoxLayout>
#include <QLabel>
#include <QPushButton>

class KeyLoader : public QDialog
{
public:
    explicit KeyLoader(QWidget *parent = nullptr) : QDialog(parent) {
        setWindowTitle("Key Loader");
        setMinimumSize(300, 100);
        
        QVBoxLayout *layout = new QVBoxLayout(this);
        QLabel *label = new QLabel("Key management is not available in this GPL build.\n"
                                   "Encryption features require the full MSC Suite.", this);
        label->setWordWrap(true);
        layout->addWidget(label);
        
        QPushButton *okButton = new QPushButton("OK", this);
        connect(okButton, &QPushButton::clicked, this, &QDialog::accept);
        layout->addWidget(okButton);
    }
};

#endif // KEYLOADER_H
