/**************************************************************************************
 * Copyright (c) 2013, Michael Carpenter  (malcom2073@gmail.com)                      *
 * All rights reserved.                                                               *
 *                                                                                    *
 * Redistribution and use in source and binary forms, with or without                 *
 * modification, are permitted provided that the following conditions are met:        *
 *     * Redistributions of source code must retain the above copyright               *
 *       notice, this list of conditions and the following disclaimer.                *
 *     * Redistributions in binary form must reproduce the above copyright            *
 *       notice, this list of conditions and the following disclaimer in the          *
 *       documentation and/or other materials provided with the distribution.         *
 *     * Neither the name EMStudio nor the                                            *
 *       names of its contributors may be used to endorse or promote products         *
 *       derived from this software without specific prior written permission.        *
 *                                                                                    *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND    *
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED      *
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE             *
 * DISCLAIMED. IN NO EVENT SHALL MICHAEL CARPENTER BE LIABLE FOR ANY                  *
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES         *
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;       *
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND        *
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT         *
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS      *
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.                       *
 **************************************************************************************/

#ifndef O5EDATAPACKETDECODER_H
#define O5EDATAPACKETDECODER_H
#include "datapacketdecoder.h"
#include "o5eheaders.h"
class O5EDataPacketDecoder : public DataPacketDecoder
{
	Q_OBJECT
public:
	O5EDataPacketDecoder();
	void populateDataFields();
	int fieldSize();
	DataField getField(int num);
	void setDataDefinitions(QList<DataField> datamap);
private:
	QList<DataField> m_dataList;
signals:
	void payloadDecoded(QVariantMap data);
public slots:
	void decodePayload(QByteArray payload);

};

#endif // O5EDATAPACKETDECODER_H