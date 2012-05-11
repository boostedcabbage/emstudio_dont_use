/***************************************************************************
*   Copyright (C) 2012  Michael Carpenter (malcom2073)                     *
*                                                                          *
*   This file is a part of FreeTune                                        *
*                                                                          *
*   FreeTune is free software: you can redistribute it and/or modify       *
*   it under the terms of the GNU General Public License version 2 as      *
*   published by the Free Software Foundation.                             *
*                                                                          *
*   FreeTune is distributed in the hope that it will be useful,            *
*   but WITHOUT ANY WARRANTY; without even the implied warranty of         *
*   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the          *
*   GNU General Public License for more details.                           *
									   *
*   You should have received a copy of the GNU General Public License      *
*   along with this program.  If not, see <http://www.gnu.org/licenses/>.  *
****************************************************************************/

#ifndef DATAPACKETDECODER_H
#define DATAPACKETDECODER_H

#include <QObject>
#include <QMap>
#include "datafield.h"
class DataPacketDecoder : public QObject
{
    Q_OBJECT
public:
	DataPacketDecoder(QObject *parent = 0);
	void populateDataFields();
	QList<DataField> m_dataFieldList;
signals:
	void payloadDecoded(QMap<QString,double> data);
public slots:
	void decodePayload(QByteArray payload);
};

#endif // DATAPACKETDECODER_H