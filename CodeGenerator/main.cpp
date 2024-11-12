//    Copyright (C) 2020-2021 Jakub Melka
//
//    This file is part of PDF4QT.
//
//    PDF4QT is free software: you can redistribute it and/or modify
//    it under the terms of the GNU Lesser General Public License as published by
//    the Free Software Foundation, either version 3 of the License, or
//    with the written consent of the copyright owner, any later version.
//
//    PDF4QT is distributed in the hope that it will be useful,
//    but WITHOUT ANY WARRANTY; without even the implied warranty of
//    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//    GNU Lesser General Public License for more details.
//
//    You should have received a copy of the GNU Lesser General Public License
//    along with PDF4QT. If not, see <https://www.gnu.org/licenses/>.

#include "generatormainwindow.h"

#include <QHash>
#include <QApplication>
#include <QTranslator>

int main(int argc, char *argv[])
{
    QHashSeed::globalSeed().setDeterministicGlobalSeed();

    QApplication a(argc, argv);

    QTranslator appTranslator, qtTranslator;
    QString transPath = QApplication::applicationDirPath() + "/translations";
    if (appTranslator.load(QLocale(), "PDF4QT", "_", transPath))
    {
        QApplication::installTranslator(&appTranslator);
    }
    if (qtTranslator.load(QLocale(), "qt", "_", transPath))
    {
        QApplication::installTranslator(&qtTranslator);
    }

    GeneratorMainWindow w;
    w.show();
    return a.exec();
}
