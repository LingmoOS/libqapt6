/***************************************************************************
 *   Copyright © 2010 Jonathan Thomas <echidnaman@kubuntu.org>             *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or         *
 *   modify it under the terms of the GNU General Public License as        *
 *   published by the Free Software Foundation; either version 2 of        *
 *   the License or (at your option) version 3 or any later version        *
 *   accepted by the membership of KDE e.V. (or its successor approved     *
 *   by the membership of KDE e.V.), which shall act as a proxy            *
 *   defined in Section 14 of version 3 of the license.                    *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program.  If not, see <http://www.gnu.org/licenses/>. *
 ***************************************************************************/

#include "config.h"

// Qt includes
#include <QtCore/QByteArray>
#include <QtCore/QFile>
#include <QtCore/QHash>
#include <QDebug>

// APT includes
#include <apt-pkg/configuration.h>

namespace QApt {

class ConfigPrivate
{
    public:
        QByteArray buffer;
        bool newFile;

        void writeBufferEntry(const QByteArray &key, const QByteArray &value);
};

void ConfigPrivate::writeBufferEntry(const QByteArray &key, const QByteArray &value)
{
    unsigned int lineIndex = 0;
    bool changed = false;

    QList<QByteArray> lines = buffer.split('\n');

    while (lineIndex < lines.size()) {
        QByteArray line = lines[lineIndex];
        // skip empty lines and lines beginning with '#'
        if (line.isEmpty() || line.at(0) == '#') {
            lineIndex++;
            continue;
        }

        QByteArray aKey;
        int eqpos = line.indexOf(' ');

        if (eqpos < 0) {
            // Invalid
            lineIndex++;
            continue;
        } else {
            aKey = line.left(eqpos);

            if (aKey == key) {
                lines[lineIndex] = QByteArray(key + ' ' + value);
                changed = true;
            }
        }

        lineIndex++;
    }

    if (changed) {
        QByteArray tempBuffer;

        foreach (const QByteArray &line, lines) {
            tempBuffer += QByteArray(line + '\n');
        }

        buffer = tempBuffer;
    }
}

Config::Config()
       : d(new ConfigPrivate())
{
    QFile file("/etc/apt/apt.conf");

    if (file.exists()) {
        file.open(QIODevice::ReadOnly | QIODevice::Text);

        d->buffer = file.readAll();
        d->newFile = false;
    } else {
        d->newFile = true;
    }
}

Config::~Config()
{
}

bool Config::readEntry(const QString &key, const bool defaultValue) const
{
    return _config->FindB(key.toStdString(), defaultValue);
}

int Config::readEntry(const QString &key, const int defaultValue) const
{
    return _config->FindI(key.toStdString(), defaultValue);
}

QString Config::readEntry(const QString &key, const QString &defaultValue) const
{
    return QString::fromStdString(_config->Find(key.toStdString(), defaultValue.toStdString()));
}

void Config::writeEntry(const QString &key, const bool value)
{
    QByteArray boolString;

    if (value == true) {
        boolString = "\"true\";";
    } else {
        boolString = "\"false\";";
    }

    if (d->newFile) {
        d->buffer.append(key + ' ' + boolString);
        d->newFile = false;
    } else {
        d->writeBufferEntry(key.toAscii(), boolString);
        qDebug() << d->buffer;
    }

    // writeToDisk();
}

void Config::writeEntry(const QString &key, const int value)
{
    QByteArray intString;

    intString = '\"' + QString::number(value).toAscii() + "\";";

    if (d->newFile) {
        d->buffer.append(key + ' ' + intString);
        d->newFile = false;
    } else {
        d->writeBufferEntry(key.toAscii(), intString);
        qDebug() << d->buffer;
    }

    // writeToDisk();
}

void Config::writeEntry(const QString &key, const QString &value)
{
    QByteArray valueString;

    valueString = '\"' + value.toAscii() + "\";";

    if (d->newFile) {
        d->buffer.append(key + ' ' + valueString);
        d->newFile = false;
    } else {
        d->writeBufferEntry(key.toAscii(), valueString);
        qDebug() << d->buffer;
    }

    // writeToDisk();
}

}