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
#include <QApplication>
#include <QLockFile>
#include <QSplashScreen>

#include "mainwindow.h"
#include "globals.h"

int main(int argc, char *argv[])
{
    // Qt6: AA_EnableHighDpiScaling is always enabled, removed deprecated call
    QGuiApplication::setHighDpiScaleFactorRoundingPolicy(Qt::HighDpiScaleFactorRoundingPolicy::PassThrough);

//    QGuiApplication::setHighDpiScaleFactorRoundingPolicy(Qt::HighDpiScaleFactorRoundingPolicy::Round);
    QApplication a(argc, argv);

    //check to see if app is already running
    //    qDebug() << "Creating lock file...";
    QLockFile applicationLockFile(QCoreApplication::applicationDirPath()+"/"+qAppName()+".lock");
    //    qDebug() << "Checking to see if lock file is locked...";
    if(!applicationLockFile.tryLock(100)){
        //        qDebug() << "LOCK FILE LOCKED!!!";

        QMessageBox msgBox;
        msgBox.setIcon(QMessageBox::Warning);
        msgBox.setText("This application is already running.");
        msgBox.exec();
        return 1;
    }
    else {
        QSplashScreen *splash = new QSplashScreen();
        splash->setPixmap(QPixmap(":/images/RMIIcon.png")); // splash picture
        splash->show();
//        splash->showMessage("Application MAY be sent to system tray depending on settings!",Qt::AlignTop,Qt::white);
        QTimer::singleShot(4000, splash,SLOT(close()));
        MainWindow w;//create MainWindow
        w.show();//show the MainWindow
        return a.exec(); //execute the application then return the application's return
    }//else no lock file
}//main
