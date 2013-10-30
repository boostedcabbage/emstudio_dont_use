/************************************************************************************
 * EMStudio - Open Source ECU tuning software                                       *
 * Copyright (C) 2013  Michael Carpenter (malcom2073@gmail.com)                     *
 *                                                                                  *
 * This file is a part of EMStudio                                                  *
 *                                                                                  *
 * EMStudio is free software; you can redistribute it and/or                        *
 * modify it under the terms of the GNU Lesser General Public                       *
 * License as published by the Free Software Foundation, version                    *
 * 2.1 of the License.                                                              *
 *                                                                                  *
 * EMStudio is distributed in the hope that it will be useful,                      *
 * but WITHOUT ANY WARRANTY; without even the implied warranty of                   *
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU                *
 * Lesser General Public License for more details.                                  *
 *                                                                                  *
 * You should have received a copy of the GNU Lesser General Public                 *
 * License along with this program; if not, write to the Free Software              *
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA   *
 ************************************************************************************/

#include "freeemscomms.h"
#include <QDebug>
#include <QDir>
#include <QCoreApplication>
#include <QXmlStreamReader>
#include <qjson/parser.h>
#include "fetable2ddata.h"
#include "fetable3ddata.h"
#include "QsLog.h"

FreeEmsComms::FreeEmsComms(QObject *parent) : EmsComms(parent)
{
	qRegisterMetaType<QList<unsigned short> >("QList<unsigned short>");
	qRegisterMetaType<QList<LocationIdFlags> >("QList<LocationIdFlags>");
	qRegisterMetaType<SerialPortStatus>("SerialPortStatus");
	serialPort = new SerialPort(this);
	connect(serialPort,SIGNAL(dataWritten(QByteArray)),this,SLOT(dataLogWrite(QByteArray)));
	m_isConnected = false;

	dataPacketDecoder = new FEDataPacketDecoder();
	m_metaDataParser = new FEMemoryMetaData();
	m_metaDataParser->loadMetaDataFromFile("freeems.config.json");
	m_packetDecoder = new PacketDecoder(this);
	connect(m_packetDecoder,SIGNAL(locationIdInfo(MemoryLocationInfo)),this,SLOT(locationIdInfoRec(MemoryLocationInfo)));
	connect(m_packetDecoder,SIGNAL(packetAcked(unsigned short,QByteArray,QByteArray)),this,SLOT(packetAckedRec(unsigned short,QByteArray,QByteArray)));
	connect(m_packetDecoder,SIGNAL(packetNaked(unsigned short,QByteArray,QByteArray,unsigned short)),this,SLOT(packetNakedRec(unsigned short,QByteArray,QByteArray,unsigned short)));
	connect(m_packetDecoder,SIGNAL(locationIdList(QList<unsigned short>)),this,SLOT(locationIdListRec(QList<unsigned short>)));
	connect(m_packetDecoder,SIGNAL(ramBlockUpdatePacket(QByteArray,QByteArray)),this,SLOT(ramBlockUpdateRec(QByteArray,QByteArray)));
	connect(m_packetDecoder,SIGNAL(flashBlockUpdatePacket(QByteArray,QByteArray)),this,SLOT(flashBlockUpdateRec(QByteArray,QByteArray)));

	m_lastdatalogTimer = new QTimer(this);
	connect(m_lastdatalogTimer,SIGNAL(timeout()),this,SLOT(datalogTimerTimeout()));
	m_lastdatalogTimer->start(500); //Every half second, check to see if we've timed out on datalogs.

	m_waitingForResponse = false;
	m_logsEnabled = false;
	m_lastDatalogUpdateEnabled = false;
	m_logInFile=0;
	m_logOutFile=0;
	m_logInOutFile=0;
	m_debugLogsEnabled = false;
	m_waitingForRamWrite=false;
	m_waitingForFlashWrite=false;
	m_sequenceNumber = 1;
	m_blockFlagList.append(BLOCK_HAS_PARENT);
	m_blockFlagList.append(BLOCK_IS_RAM);
	m_blockFlagList.append(BLOCK_IS_FLASH);
	m_blockFlagList.append(BLOCK_IS_INDEXABLE);
	m_blockFlagList.append(BLOCK_IS_READ_ONLY);
	m_blockFlagList.append(BLOCK_GETS_VERIFIED);
	m_blockFlagList.append(BLOCK_FOR_BACKUP_RESTORE);
	m_blockFlagList.append(BLOCK_SPARE_7);
	m_blockFlagList.append(BLOCK_SPARE_8);
	m_blockFlagList.append(BLOCK_SPARE_9);
	m_blockFlagList.append(BLOCK_SPARE_10);
	m_blockFlagList.append(BLOCK_IS_2D_SIGNED_TABLE);
	m_blockFlagList.append(BLOCK_IS_2D_TABLE);
	m_blockFlagList.append(BLOCK_IS_MAIN_TABLE);
	m_blockFlagList.append(BLOCK_IS_LOOKUP_DATA);
	m_blockFlagList.append(BLOCK_IS_CONFIGURATION);


	m_blockFlagToNameMap[BLOCK_HAS_PARENT] = "Parent";
	m_blockFlagToNameMap[BLOCK_IS_RAM] = "Is Ram";
	m_blockFlagToNameMap[BLOCK_IS_FLASH] = "Is Flash";
	m_blockFlagToNameMap[BLOCK_IS_INDEXABLE] = "Is Indexable";
	m_blockFlagToNameMap[BLOCK_IS_READ_ONLY] = "Is Read Only";
	m_blockFlagToNameMap[BLOCK_FOR_BACKUP_RESTORE] = "For Backup";
	m_blockFlagToNameMap[BLOCK_GETS_VERIFIED] = "Is Verified";
	m_blockFlagToNameMap[BLOCK_IS_2D_TABLE] = "2D Table";
	m_blockFlagToNameMap[BLOCK_IS_2D_SIGNED_TABLE] = "2D Signed Table";
	m_blockFlagToNameMap[BLOCK_IS_MAIN_TABLE] = "3D Table";
	m_blockFlagToNameMap[BLOCK_IS_LOOKUP_DATA] = "Lookup Table";
	m_blockFlagToNameMap[BLOCK_IS_CONFIGURATION] = "Configuration";
	m_interrogateInProgress = false;
	m_interogateComplete = false;
	m_interrogateIdListComplete = false;
	m_interrogateIdInfoComplete = false;
	m_interrogateTotalCount=0;
	emsData.setMetaData(m_metaDataParser);
	connect(&emsData,SIGNAL(updateRequired(unsigned short)),this,SIGNAL(deviceDataUpdated(unsigned short)));
	connect(&emsData,SIGNAL(ramBlockUpdateRequest(unsigned short,unsigned short,unsigned short,QByteArray)),this,SLOT(updateBlockInRam(unsigned short,unsigned short,unsigned short,QByteArray)));
	connect(&emsData,SIGNAL(flashBlockUpdateRequest(unsigned short,unsigned short,unsigned short,QByteArray)),this,SLOT(updateBlockInFlash(unsigned short,unsigned short,unsigned short,QByteArray)));
	connect(&emsData,SIGNAL(updateRequired(unsigned short)),this,SLOT(locationIdUpdate(unsigned short)));


	QFile dialogFile("menuconfig.json");
	if (!dialogFile.open(QIODevice::ReadOnly))
	{
		return;
	}
	QByteArray dialogfiledata = dialogFile.readAll();
	dialogFile.close();
	QJson::Parser parser;
	bool ok = false;
	QVariant resultvariant = parser.parse(dialogfiledata,&ok);

	QVariantMap topmap = resultvariant.toMap();
	QVariantList dialogslist = topmap["dialogs"].toList();
	MenuSetup menu;
	for (int i=0;i<dialogslist.size();i++)
	{
		QVariantMap dialogitemmap = dialogslist[i].toMap();

		QVariantList fieldlist = dialogitemmap["fieldlist"].toList();
		DialogItem item;
		item.variable = dialogitemmap["variable"].toString();
		item.title = dialogitemmap["title"].toString();
		for (int j=0;j<fieldlist.size();j++)
		{
			QVariantMap fieldmap = fieldlist[j].toMap();
			DialogField field;
			field.title = fieldmap["title"].toString();
			field.variable = fieldmap["variable"].toString();
			field.condition = fieldmap["condition"].toString();
			item.fieldList.append(field);
		}
		menu.dialoglist.append(item);
	}

	QVariantList menulist = topmap["menu"].toList();
	for (int i=0;i<menulist.size();i++)
	{
		QVariantMap menuitemmap = menulist[i].toMap();
		MenuItem menuitem;
		menuitem.title = menuitemmap["title"].toString();
		QVariantList submenuitemlist = menuitemmap["subitems"].toList();
		for (int j=0;j<submenuitemlist.size();j++)
		{
			QVariantMap submenuitemmap = submenuitemlist[j].toMap();
			SubMenuItem submenuitem;
			submenuitem.title = submenuitemmap["title"].toString();
			submenuitem.variable = submenuitemmap["variable"].toString();
			menuitem.subMenuList.append(submenuitem);
		}
		menu.menulist.append(menuitem);
	}

	QVariantList configlist = topmap["config"].toList();
	QMap<QString,QList<ConfigBlock> > configmap;
	for (int i=0;i<configlist.size();i++)
	{
		QVariantMap configitemmap = configlist[i].toMap();
		ConfigBlock block;
		block.setName(configitemmap["name"].toString());
		block.setType(configitemmap["type"].toString());
		block.setElementSize(configitemmap["sizeofelement"].toInt());
		block.setSize(configitemmap["size"].toInt());
		block.setOffset(configitemmap["offset"].toInt());
		//configitemmap["calc"];
		block.setSizeOverride(configitemmap["sizeoverride"].toString());
		bool ok = false;
		block.setLocationId(configitemmap["locationid"].toString().toInt(&ok,16));
		QString locid = configitemmap["locationid"].toString();
		if (!configmap.contains(locid.mid(2)))
		{
			configmap[locid.mid(2)] = QList<ConfigBlock>();
		}
		configmap[locid.mid(2)].append(block);
	}

	m_metaDataParser->passConfigData(configmap);
	m_metaDataParser->setMenuMetaData(menu);


}
void FreeEmsComms::passLogger(QsLogging::Logger *log)
{
	//Set the internal instance.
	QsLogging::Logger::instance(log);
	QLOG_DEBUG() << "Logging from the plugin!!!";
}

MemoryMetaData *FreeEmsComms::getMetaParser()
{
	return m_metaDataParser;
}

DataPacketDecoder *FreeEmsComms::getDecoder()
{
	return dataPacketDecoder;
}
Table3DData *FreeEmsComms::getNew3DTableData()
{
	return new FETable3DData();
}

Table2DData *FreeEmsComms::getNew2DTableData()
{
	return new FETable2DData();
}

FreeEmsComms::~FreeEmsComms()
{
}

void FreeEmsComms::disconnectSerial()
{
	RequestClass req;
	req.type = SERIAL_DISCONNECT;
	m_reqListMutex.lock();
	m_reqList.append(req);
	m_reqListMutex.unlock();
}
void FreeEmsComms::startInterrogation()
{
	if (!m_interrogateInProgress)
	{
		RequestClass req;
		req.type = INTERROGATE_START;
		m_reqListMutex.lock();
		m_reqList.append(req);
		m_reqListMutex.unlock();
		m_interrogatePacketList.clear();
		m_interrogateInProgress = true;
	}
}

void FreeEmsComms::openLogs()
{
	QLOG_INFO() << "Open logs:" << m_logsDirectory + "/" + m_logsFilename + ".bin";
	if (!QDir(m_logsDirectory).exists())
	{
		QDir dir(QCoreApplication::instance()->applicationDirPath());
		if (!dir.mkpath(m_logsDirectory))
		{
			emit error("Unable to create log directory. Data will NOT be logged until this is fixed!");
		}
	}
	m_logInFile = new QFile(m_logsDirectory + "/" + m_logsFilename + ".bin");
	m_logInFile->open(QIODevice::ReadWrite | QIODevice::Truncate);
	if (m_debugLogsEnabled)
	{
		m_logInOutFile = new QFile(m_logsDirectory + "/" + m_logsFilename + ".both.bin");
		m_logInOutFile->open(QIODevice::ReadWrite | QIODevice::Truncate);
		m_logOutFile = new QFile(m_logsDirectory + "/" + m_logsFilename + ".toecu.bin");
		m_logOutFile->open(QIODevice::ReadWrite | QIODevice::Truncate);
	}
}

void FreeEmsComms::connectSerial(QString port,int baud)
{
	RequestClass req;
	req.type = SERIAL_CONNECT;
	req.addArg(port);
	req.addArg(baud,sizeof(baud));
	m_reqListMutex.lock();
	m_reqList.append(req);
	m_reqListMutex.unlock();
}

void FreeEmsComms::loadLog(QString filename)
{
	Q_UNUSED(filename);
}

void FreeEmsComms::playLog()
{
}
void FreeEmsComms::setLogsEnabled(bool enabled)
{
	if (m_logsEnabled && !enabled)
	{
		m_logInFile->close();
		delete m_logInFile;
		m_logInFile=0;

		if (m_debugLogsEnabled)
		{
			m_logInOutFile->close();
			delete m_logInOutFile;
			m_logInOutFile=0;

			m_logOutFile->close();
			delete m_logOutFile;
			m_logOutFile=0;
		}
	}
	else if (!m_logsEnabled && enabled)
	{
		if (m_isConnected)
		{
			//If we're connected, open logs. Otherwise, don't as they will be open next time we connect.
			openLogs();
		}
	}
	m_logsEnabled = enabled;
}
void FreeEmsComms::setlogsDebugEnabled(bool enabled)
{
	if (m_logsEnabled && enabled && !m_debugLogsEnabled)
	{
		if (!QDir(m_logsDirectory).exists())
		{
			QDir dir(QCoreApplication::instance()->applicationDirPath());
			if (!dir.mkpath(m_logsDirectory))
			{
				emit error("Unable to create log directory. Data will NOT be logged until this is fixed!");
			}
		}
		m_logInOutFile = new QFile(m_logsDirectory + "/" + m_logsFilename + ".both.bin");
		m_logInOutFile->open(QIODevice::ReadWrite | QIODevice::Truncate);
		m_logOutFile = new QFile(m_logsDirectory + "/" + m_logsFilename + ".toecu.bin");
		m_logOutFile->open(QIODevice::ReadWrite | QIODevice::Truncate);
	}
	else if (m_logsEnabled && !enabled && m_debugLogsEnabled)
	{
		m_logInOutFile->close();
		m_logInOutFile->deleteLater();
		m_logInOutFile=0;
		m_logOutFile->close();
		m_logOutFile->deleteLater();
		m_logOutFile=0;
	}
	m_debugLogsEnabled = enabled;
}

void FreeEmsComms::setLogDirectory(QString dir)
{
	m_logsDirectory = dir;
}

void FreeEmsComms::setPort(QString portname)
{
	serialPort->setPort(portname);
}

void FreeEmsComms::setBaud(int baudrate)
{
	serialPort->setBaud(baudrate);
}
int FreeEmsComms::burnBlockFromRamToFlash(unsigned short location,unsigned short offset, unsigned short size)
{
	m_reqListMutex.lock();
	RequestClass req;
	req.type = BURN_BLOCK_FROM_RAM_TO_FLASH;
	req.addArg(location,sizeof(location));
	req.addArg(offset,sizeof(offset));
	req.addArg(size,sizeof(size));
	req.sequencenumber = m_sequenceNumber;
	m_sequenceNumber++;
	m_reqList.append(req);
	m_reqListMutex.unlock();
	return m_sequenceNumber-1;
}
int FreeEmsComms::enableDatalogStream()
{
	m_reqListMutex.lock();
	RequestClass req;
	req.type = UPDATE_BLOCK_IN_RAM;
	req.addArg(0x9000,2);
	req.addArg(0,2);
	req.addArg(1,2);
	req.addArg(QByteArray().append((char)0x01));
	req.sequencenumber = m_sequenceNumber;
	m_sequenceNumber++;
	m_reqList.append(req);
	m_reqListMutex.unlock();
	return m_sequenceNumber-1;
}

int FreeEmsComms::disableDatalogStream()
{
	m_reqListMutex.lock();
	RequestClass req;
	req.type = UPDATE_BLOCK_IN_RAM;
	req.addArg(0x9000,2);
	req.addArg(0,2);
	req.addArg(1,2);
	req.addArg(QByteArray().append((char)0x00));
	req.sequencenumber = m_sequenceNumber;
	m_sequenceNumber++;
	m_reqList.append(req);
	m_reqListMutex.unlock();
	return m_sequenceNumber-1;
}

int FreeEmsComms::updateBlockInRam(unsigned short location,unsigned short offset, unsigned short size,QByteArray data)
{
	m_reqListMutex.lock();
	RequestClass req;
	req.type = UPDATE_BLOCK_IN_RAM;
	req.addArg(location,sizeof(location));
	req.addArg(offset,sizeof(offset));
	req.addArg(size,sizeof(size));
	req.addArg(data);
	req.sequencenumber = m_sequenceNumber;
	m_sequenceNumber++;
	m_reqList.append(req);
	m_reqListMutex.unlock();
	return m_sequenceNumber-1;
}
int FreeEmsComms::updateBlockInFlash(unsigned short location,unsigned short offset, unsigned short size,QByteArray data)
{
	m_reqListMutex.lock();
	RequestClass req;
	req.type = UPDATE_BLOCK_IN_FLASH;
	req.addArg(location,sizeof(location));
	req.addArg(offset,sizeof(offset));
	req.addArg(size,sizeof(size));
	req.addArg(data);
	req.sequencenumber = m_sequenceNumber;
	m_sequenceNumber++;
	m_reqList.append(req);
	m_reqListMutex.unlock();
	return m_sequenceNumber-1;
}

int FreeEmsComms::getDecoderName()
{
	m_reqListMutex.lock();
	RequestClass req;
	req.type = GET_DECODER_NAME;
	req.sequencenumber = m_sequenceNumber;
	m_sequenceNumber++;
	m_reqList.append(req);
	m_reqListMutex.unlock();
	return m_sequenceNumber-1;
}
int FreeEmsComms::getFirmwareBuildDate()
{
	m_reqListMutex.lock();
	RequestClass req;
	req.type = GET_FIRMWARE_BUILD_DATE;
	req.sequencenumber = m_sequenceNumber;
	m_sequenceNumber++;
	m_reqList.append(req);
	m_reqListMutex.unlock();
	return m_sequenceNumber-1;
}
int FreeEmsComms::getCompilerVersion()
{
	m_reqListMutex.lock();
	RequestClass req;
	req.type = GET_COMPILER_VERSION;
	req.sequencenumber = m_sequenceNumber;
	m_sequenceNumber++;
	m_reqList.append(req);
	m_reqListMutex.unlock();
	return m_sequenceNumber-1;
}
int FreeEmsComms::getOperatingSystem()
{
	m_reqListMutex.lock();
	RequestClass req;
	req.type = GET_OPERATING_SYSTEM;
	req.sequencenumber = m_sequenceNumber;
	m_sequenceNumber++;
	m_reqList.append(req);
	m_reqListMutex.unlock();
	return m_sequenceNumber-1;
}
int FreeEmsComms::retrieveBlockFromFlash(unsigned short location, unsigned short offset, unsigned short size)
{
	m_reqListMutex.lock();
	RequestClass req;
	req.type = RETRIEVE_BLOCK_IN_FLASH;
	req.addArg(location,sizeof(location));
	req.addArg(offset,sizeof(offset));
	req.addArg(size,sizeof(size));
	req.sequencenumber = m_sequenceNumber;
	m_sequenceNumber++;
	m_reqList.append(req);
	m_reqListMutex.unlock();
	return m_sequenceNumber-1;
}
int FreeEmsComms::retrieveBlockFromRam(unsigned short location, unsigned short offset, unsigned short size)
{
	m_reqListMutex.lock();
	RequestClass req;
	req.type = RETRIEVE_BLOCK_IN_RAM;
	req.addArg(location,sizeof(location));
	req.addArg(offset,sizeof(offset));
	req.addArg(size,sizeof(size));
	req.sequencenumber = m_sequenceNumber;
	m_sequenceNumber++;
	m_reqList.append(req);
	m_reqListMutex.unlock();
	return m_sequenceNumber-1;
}
int FreeEmsComms::getInterfaceVersion()
{
	m_reqListMutex.lock();
	RequestClass req;
	req.type = GET_INTERFACE_VERSION;
	req.sequencenumber = m_sequenceNumber;
	m_sequenceNumber++;
	m_reqList.append(req);
	m_reqListMutex.unlock();
	return m_sequenceNumber-1;
}
int FreeEmsComms::getFirmwareVersion()
{
	m_reqListMutex.lock();
	RequestClass req;
	req.type = GET_FIRMWARE_VERSION;
	req.sequencenumber = m_sequenceNumber;
	m_sequenceNumber++;
	m_reqList.append(req);
	m_reqListMutex.unlock();
	return m_sequenceNumber-1;
}
int FreeEmsComms::getMaxPacketSize()
{
	m_reqListMutex.lock();
	RequestClass req;
	req.type = GET_MAX_PACKET_SIZE;
	req.sequencenumber = m_sequenceNumber;
	m_sequenceNumber++;
	m_reqList.append(req);
	m_reqListMutex.unlock();
	return m_sequenceNumber-1;
}
int FreeEmsComms::echoPacket(QByteArray packet)
{
	m_reqListMutex.lock();
	RequestClass req;
	req.type = ECHO_PACKET;
	req.sequencenumber = m_sequenceNumber;
	req.addArg(packet);
	m_sequenceNumber++;
	m_reqList.append(req);
	m_reqListMutex.unlock();
	return m_sequenceNumber-1;
}
int FreeEmsComms::getLocationIdInfo(unsigned short locationid)
{
	m_reqListMutex.lock();
	RequestClass req;
	req.type = GET_LOCATION_ID_INFO;
	req.sequencenumber = m_sequenceNumber;
	req.addArg(locationid,sizeof(locationid));
	m_sequenceNumber++;
	m_reqList.append(req);
	m_reqListMutex.unlock();
	return m_sequenceNumber-1;
}

int FreeEmsComms::getLocationIdList(unsigned char listtype, unsigned short listmask)
{
	m_reqListMutex.lock();
	RequestClass req;
	req.type = GET_LOCATION_ID_LIST;
	req.sequencenumber = m_sequenceNumber;
	req.addArg(listtype,sizeof(listtype));
	req.addArg(listmask,sizeof(listmask));
	m_sequenceNumber++;
	m_reqList.append(req);
	m_reqListMutex.unlock();
	return m_sequenceNumber-1;
}

int FreeEmsComms::softReset()
{
	m_reqListMutex.lock();
	RequestClass req;
	req.type = SOFT_RESET;
	req.sequencenumber = m_sequenceNumber;
	m_sequenceNumber++;
	m_reqList.append(req);
	m_reqListMutex.unlock();
	return m_sequenceNumber-1;
}
int FreeEmsComms::hardReset()
{
	m_reqListMutex.lock();
	RequestClass req;
	req.type = HARD_RESET;
	req.sequencenumber = m_sequenceNumber;
	m_sequenceNumber++;
	m_reqList.append(req);
	m_reqListMutex.unlock();
	return m_sequenceNumber-1;
}
bool FreeEmsComms::sendPacket(RequestClass request,bool haslength)
{
	if (!sendPacket(request.type,request.args,request.argsize,haslength))
	{
		QLOG_ERROR() << "sendPacket failed";
		return false;
	}
	return true;
}

bool FreeEmsComms::sendPacket(unsigned short payloadid,QList<QVariant> arglist,QList<int> argsizelist,bool haslength)
{
	if (arglist.size() != argsizelist.size())
	{
		return false;
	}
	QByteArray header;
	QByteArray payload;
	for (int i=0;i<arglist.size();i++)
	{
		if (arglist[i].type() == QVariant::Int)
		{
			if (argsizelist[i] == 1)
			{
				unsigned char arg = arglist[i].toInt();
				payload.append((unsigned char)((arg) & 0xFF));
			}
			else if (argsizelist[i] == 2)
			{
				unsigned short arg = arglist[i].toInt();
				payload.append((unsigned char)((arg >> 8) & 0xFF));
				payload.append((unsigned char)((arg) & 0xFF));
			}
			else if (argsizelist[i] == 4)
			{
				unsigned int arg = arglist[i].toInt();
				payload.append((unsigned char)((arg >> 24) & 0xFF));
				payload.append((unsigned char)((arg >> 16) & 0xFF));
				payload.append((unsigned char)((arg >> 8) & 0xFF));
				payload.append((unsigned char)((arg) & 0xFF));
			}
		}
		else if (arglist[i].type() == QVariant::ByteArray)
		{
			//Data packet
			QByteArray arg = arglist[i].toByteArray();
			payload.append(arg);
		}
		else if (arglist[i].type() == QVariant::String)
		{
			QByteArray arg = arglist[i].toString().toAscii();
			payload.append(arg);
		}
	}
	if (haslength)
	{
		header.append((unsigned char)0x01); //Length, no seq no nak
		header.append((unsigned char)((payloadid >> 8) & 0xFF));
		header.append((unsigned char)((payloadid) & 0xFF));
		header.append((char)(payload.length() >> 8) & 0xFF);
		header.append((char)(payload.length()) & 0xFF);
	}
	else
	{
		header.append((char)0x00); //No Length, no seq no nak
		header.append((char)((payloadid >> 8) & 0xFF));
		header.append((char)((payloadid) & 0xFF));
	}
	QLOG_TRACE() << "About to send packet";
    if (serialPort->writeBytes(generatePacket(header,payload)) < 0)
	{
		return false;
	}
	QLOG_TRACE() << "Sent packet" << "0x" + QString::number(payloadid,16).toUpper();
	emit packetSent(payloadid,header,payload);
	return true;
}

QByteArray FreeEmsComms::generatePacket(QByteArray header,QByteArray payload)
{
	QByteArray packet;
	packet.append((char)0xAA);
	unsigned char checksum = 0;
	for (int i=0;i<header.size();i++)
	{
		checksum += header[i];
	}
	for (int i=0;i<payload.size();i++)
	{
		checksum += payload[i];
	}
	payload.append(checksum);
	for (int j=0;j<header.size();j++)
	{
		if (header[j] == (char)0xAA)
		{
			packet.append((char)0xBB);
			packet.append((char)0x55);
		}
		else if (header[j] == (char)0xBB)
		{
			packet.append((char)0xBB);
			packet.append((char)0x44);
		}
		else if (header[j] == (char)0xCC)
		{
			packet.append((char)0xBB);
			packet.append((char)0x33);
		}
		else
		{
			packet.append(header[j]);
		}
	}
	for (int j=0;j<payload.size();j++)
	{
		if (payload[j] == (char)0xAA)
		{
			packet.append((char)0xBB);
			packet.append((char)0x55);
		}
		else if (payload[j] == (char)0xBB)
		{
			packet.append((char)0xBB);
			packet.append((char)0x44);
		}
		else if (payload[j] == (char)0xCC)
		{
			packet.append((char)0xBB);
			packet.append((char)0x33);
		}
		else
		{
			packet.append(payload[j]);
		}
	}
	packet.append((char)0xCC);
	return packet;
}
void FreeEmsComms::setInterByteSendDelay(int milliseconds)
{
	serialPort->setInterByteSendDelay(milliseconds);
}

void FreeEmsComms::run()
{
	rxThread = new SerialRXThread(this);
	connect(rxThread,SIGNAL(incomingPacket(QByteArray)),this,SLOT(parseEverything(QByteArray)));
	connect(rxThread,SIGNAL(dataRead(QByteArray)),this,SLOT(dataLogRead(QByteArray)));
	m_terminateLoop = false;
	bool serialconnected = false;

	while (!m_terminateLoop)
	{
		m_reqListMutex.lock();
		m_threadReqList.append(m_reqList);
		m_reqList.clear();
		m_reqListMutex.unlock();
		for (int i=0;i<m_threadReqList.size();i++)
		{
			if (m_threadReqList[i].type == SERIAL_CONNECT)
			{
				SerialPortStatus errortype = serialPort->isSerialMonitor(m_threadReqList[i].args[0].toString());
				if (errortype != NONE)
				{
					QLOG_ERROR() << "Unable to verify ECU";
					QString errorstr = "UNKNOWN ERROR";
					if (errortype == UNABLE_TO_CONNECT)
					{
						errorstr = "Unable to open serial port " + m_threadReqList[i].args[0].toString() + " Please ensure no other application has the port open and that the port exists!";
					}
					else if (errortype == UNABLE_TO_LOCK)
					{
						errorstr = "Unable to open serial port " + m_threadReqList[i].args[0].toString() + " because another compatible application has locked it. Please close all serial port related applications and try again.";
					}
					else if (errortype == UNABLE_TO_WRITE)
					{
						errorstr = "Unable to open serial port " + m_threadReqList[i].args[0].toString() + " Please ensure no other application has the port open and that the port exists!";
					}
					else if (errortype == UNABLE_TO_READ)
					{
						errorstr = "Unable to read from serial port " + m_threadReqList[i].args[0].toString() + ". This is likely an error with your serial port driver. Please disconnect and reconnect the device and try again";
					}
					else if (errortype == SM_MODE)
					{
						//TODO Fix this when we have the ability to reset SM mode
						errorstr = "Unable to connect to ECU. SerialMonitor mode detected! Please remove SM jumper, reset the ECU and click retry!";
					}
					emit error(errortype,errorstr);
					serialconnected = false;
					emit disconnected();
					//On a disconnect, we are going to be deleting this thread, so go ahead and quit out;
					return;
					m_threadReqList.removeAt(i);
					i--;
					continue;
				}
				emit debugVerbose("SERIAL_CONNECT");
				int errornum = 0;
				if ((errornum = serialPort->openPort(m_threadReqList[i].args[0].toString(),m_threadReqList[i].args[1].toInt())))
				{
					if (errornum == -1)
					{
						emit error(UNABLE_TO_CONNECT,"Unable to open serial port " + m_threadReqList[i].args[0].toString() + " Please ensure no other application has the port open and that the port exists!");
						QLOG_ERROR() << "Unable to connect to COM port";
					}
					else if (errornum == -2)
					{
						emit error(UNABLE_TO_LOCK,"Unable to open serial port " + m_threadReqList[i].args[0].toString() + " due to another freeems application locking the port. Please close all other freeems related applications and try again.");
						QLOG_ERROR() << "Unable to connect to COM port due to process lock";
					}
					m_threadReqList.removeAt(i);
					i--;
					emit disconnected();
					continue;
				}
				QLOG_INFO() << "Serial connected!";
				//Before we finish emitting the fact that we are connected, let's verify this is a freeems system we are talking to.
				if (!sendPacket(GET_FIRMWARE_VERSION))
				{
					QLOG_FATAL() << "Error writing packet. Quitting thread";
					return;
				}
				int dataattempts = 0;
				int nodataattempts=0;
				bool good = false;
				bool nodata = true;
				while (dataattempts < 50 && !good && nodataattempts < 4)
				{
					QByteArray result = rxThread->readSinglePacket(serialPort);
					if (result.size() > 0)
					{
						dataattempts++;
						nodata = false;

						Packet p = m_packetDecoder->parseBuffer(result);
						if (!p.isValid)
						{
							emit decoderFailure(result);
						}
						if (p.isValid && p.payloadid == GET_FIRMWARE_VERSION+1)
						{
							//We're good!
							good = true;
						}
					}
					else
					{
						nodataattempts++;
					}
				}
				if (!good)
				{
					QString errorstr = "";
					SerialPortStatus errortype = NONE;
					if (nodata)
					{
						errorstr = "Unable to communicate with ECU, Serial port is unresponsive. Please verify your FreeEMS Board is plugged in, powered up, and all serial settings are properly set.";
						errortype = (SerialPortStatus)NO_DAT;
					}
					else
					{
						errorstr = "Unable to communicate with FreeEMS, corrupt data received. Please verify serial settings, in particular double check the baud rate.";
						errortype = (SerialPortStatus)INVALID_DATA;
					}
					emit error(errortype,errorstr);
					serialconnected = false;
					serialPort->closePort();
					emit disconnected();
					//On a disconnect, we are going to be deleting this thread, so go ahead and quit out;
					return;
					m_threadReqList.removeAt(i);
					i--;
					continue;
				}

				serialconnected = true;
				emit debug("Connected to serial port");
				m_isConnected = true;
				openLogs();
				emit connected();
				m_threadReqList.removeAt(i);
				i--;
				rxThread->start(serialPort);

			}
			else if (!serialconnected)
			{
				continue;
			}
			else if (m_threadReqList[i].type == SERIAL_DISCONNECT)
			{
				emit debugVerbose("SERIAL_DISCONNECT");
				rxThread->stop();
				rxThread->wait(500);
				rxThread->terminate();
				rxThread->deleteLater();

				serialPort->closePort();
				serialconnected = false;
				emit disconnected();
			}
			else if (m_threadReqList[i].type == INTERROGATE_START)
			{
				int seq = getFirmwareVersion();
				emit interrogateTaskStart("Ecu Info FW Version",seq);
				m_interrogatePacketList.append(seq);

				seq = getInterfaceVersion();
				emit interrogateTaskStart("Ecu Info Interface Version",seq);
				m_interrogatePacketList.append(seq);

				seq = getCompilerVersion();
				emit interrogateTaskStart("Ecu Info Compiler Version",seq);
				m_interrogatePacketList.append(seq);

				seq = getDecoderName();
				emit interrogateTaskStart("Ecu Info Decoder Name",seq);
				m_interrogatePacketList.append(seq);

				seq = getFirmwareBuildDate();
				emit interrogateTaskStart("Ecu Info Firmware Build Date",seq);
				m_interrogatePacketList.append(seq);

				seq = getMaxPacketSize();
				emit interrogateTaskStart("Ecu Info Max Packet Size",seq);
				m_interrogatePacketList.append(seq);

				seq = getOperatingSystem();
				emit interrogateTaskStart("Ecu Info Operating System",seq);
				m_interrogatePacketList.append(seq);

				seq = getLocationIdList(0x00,0x00);
				emit interrogateTaskStart("Ecu Info Location ID List",seq);
				m_interrogatePacketList.append(seq);

				m_interrogateTotalCount=8;
				m_threadReqList.removeAt(i);
				i--;
				continue;
			}
			else if (m_threadReqList[i].type == GET_LOCATION_ID_LIST)
			{
				m_waitingInfoMutex.lock();
				if (!m_waitingForResponse)
				{
					m_waitingForResponse = true;
					m_timeoutMsecs = QDateTime::currentDateTime().currentMSecsSinceEpoch();
					emit debugVerbose("GET_LOCATION_ID_LIST");
					m_currentWaitingRequest = m_threadReqList[i];
					m_payloadWaitingForResponse = 0xDA5E;
					if (!sendPacket(m_threadReqList[i],true))
					{
						QLOG_FATAL() << "Error writing packet. Quitting thread";
						return;
					}
					m_threadReqList.removeAt(i);
					i--;
				}
				m_waitingInfoMutex.unlock();
			}
			else if (m_threadReqList[i].type == GET_DECODER_NAME)
			{
				m_waitingInfoMutex.lock();
				if (!m_waitingForResponse)
				{
					m_waitingForResponse = true;
					m_timeoutMsecs = QDateTime::currentDateTime().currentMSecsSinceEpoch();
					m_currentWaitingRequest = m_threadReqList[i];
					m_payloadWaitingForResponse = 0xEEEE;
					if (!sendPacket(GET_DECODER_NAME))
					{
						QLOG_FATAL() << "Error writing packet. Quitting thread";
						return;
					}
					m_threadReqList.removeAt(i);
					i--;
				}
				m_waitingInfoMutex.unlock();
			}
			else if (m_threadReqList[i].type == GET_FIRMWARE_BUILD_DATE)
			{
				m_waitingInfoMutex.lock();
				if (!m_waitingForResponse)
				{
					m_waitingForResponse = true;
					m_timeoutMsecs = QDateTime::currentDateTime().currentMSecsSinceEpoch();
					m_currentWaitingRequest = m_threadReqList[i];
					m_payloadWaitingForResponse = 0xEEF0;
					if (!sendPacket(GET_FIRMWARE_BUILD_DATE))
					{
						QLOG_FATAL() << "Error writing packet. Quitting thread";
						return;
					}
					m_threadReqList.removeAt(i);
					i--;
				}
				m_waitingInfoMutex.unlock();
			}
			else if (m_threadReqList[i].type == GET_COMPILER_VERSION)
			{
				m_waitingInfoMutex.lock();
				if (!m_waitingForResponse)
				{
					m_waitingForResponse = true;
					m_timeoutMsecs = QDateTime::currentDateTime().currentMSecsSinceEpoch();
					m_currentWaitingRequest = m_threadReqList[i];
					m_payloadWaitingForResponse = 0xEEF2;
					if (!sendPacket(GET_COMPILER_VERSION))
					{
						QLOG_FATAL() << "Error writing packet. Quitting thread";
						return;
					}
					m_threadReqList.removeAt(i);
					i--;
				}
				m_waitingInfoMutex.unlock();
			}
			else if (m_threadReqList[i].type == GET_OPERATING_SYSTEM)
			{
				m_waitingInfoMutex.lock();
				if (!m_waitingForResponse)
				{
					m_waitingForResponse = true;
					m_timeoutMsecs = QDateTime::currentDateTime().currentMSecsSinceEpoch();
					m_currentWaitingRequest = m_threadReqList[i];
					m_payloadWaitingForResponse = 0xEEF4;
					if (!sendPacket(GET_OPERATING_SYSTEM))
					{
						QLOG_FATAL() << "Error writing packet. Quitting thread";
						return;
					}
					m_threadReqList.removeAt(i);
					i--;
				}
				m_waitingInfoMutex.unlock();
			}
			else if (m_threadReqList[i].type == BURN_BLOCK_FROM_RAM_TO_FLASH)
			{
				m_waitingInfoMutex.lock();
				if (!m_waitingForResponse)
				{
					m_waitingForResponse = true;
					m_timeoutMsecs = QDateTime::currentDateTime().currentMSecsSinceEpoch();
					m_currentWaitingRequest = m_threadReqList[i];
					m_payloadWaitingForResponse = 0x0108;
					if (!sendPacket(m_threadReqList[i],false))
					{
						QLOG_FATAL() << "Error writing packet. Quitting thread";
						return;
					}
					m_threadReqList.removeAt(i);
					i--;
				}
				m_waitingInfoMutex.unlock();
			}
			else if (m_threadReqList[i].type == GET_LOCATION_ID_INFO)
			{
				m_waitingInfoMutex.lock();
				if (!m_waitingForResponse)
				{
					m_waitingForResponse = true;
					m_timeoutMsecs = QDateTime::currentDateTime().currentMSecsSinceEpoch();
					m_currentWaitingRequest = m_threadReqList[i];
					m_payloadWaitingForResponse = 0xF8E0;
					if (!sendPacket(m_threadReqList[i],false))
					{
						QLOG_FATAL() << "Error writing packet. Quitting thread";
						return;
					}
					m_threadReqList.removeAt(i);
					i--;
				}
				m_waitingInfoMutex.unlock();
			}
			else if (m_threadReqList[i].type == UPDATE_BLOCK_IN_RAM)
			{
				m_waitingInfoMutex.lock();
				if (!m_waitingForResponse)
				{
					m_waitingForResponse = true;
					m_timeoutMsecs = QDateTime::currentDateTime().currentMSecsSinceEpoch();
					m_currentWaitingRequest = m_threadReqList[i];
					m_payloadWaitingForResponse = 0x0100;
					m_waitingForRamWrite = true;
					if (!sendPacket(m_threadReqList[i],true))
					{
						QLOG_FATAL() << "Error writing packet. Quitting thread";
						return;
					}
					m_threadReqList.removeAt(i);
					i--;
				}
				m_waitingInfoMutex.unlock();

			}
			else if (m_threadReqList[i].type == RETRIEVE_BLOCK_IN_RAM)
			{
				m_waitingInfoMutex.lock();
				if (!m_waitingForResponse)
				{
					m_waitingForResponse = true;
					m_timeoutMsecs = QDateTime::currentDateTime().currentMSecsSinceEpoch();
					m_currentWaitingRequest = m_threadReqList[i];
					m_payloadWaitingForResponse = 0x0104;
					if (!sendPacket(m_threadReqList[i],false))
					{
						QLOG_FATAL() << "Error writing packet. Quitting thread";
						return;
					}
					m_threadReqList.removeAt(i);
					i--;
				}
				m_waitingInfoMutex.unlock();
			}
			else if (m_threadReqList[i].type == UPDATE_BLOCK_IN_FLASH)
			{
				m_waitingInfoMutex.lock();
				if (!m_waitingForResponse)
				{
					m_waitingForResponse = true;
					m_timeoutMsecs = QDateTime::currentDateTime().currentMSecsSinceEpoch();
					m_currentWaitingRequest = m_threadReqList[i];
					m_payloadWaitingForResponse = 0x0102;
					m_waitingForFlashWrite = true;
					if (!sendPacket(m_threadReqList[i],true))
					{
						QLOG_FATAL() << "Error writing packet. Quitting thread";
						return;
					}
					m_threadReqList.removeAt(i);
					i--;
				}
				m_waitingInfoMutex.unlock();
			}
			else if (m_threadReqList[i].type == RETRIEVE_BLOCK_IN_FLASH)
			{
				m_waitingInfoMutex.lock();
				if (!m_waitingForResponse)
				{
					m_waitingForResponse = true;
					m_timeoutMsecs = QDateTime::currentDateTime().currentMSecsSinceEpoch();
					m_currentWaitingRequest = m_threadReqList[i];
					m_payloadWaitingForResponse = 0x0106;
					if (!sendPacket(m_threadReqList[i],false))
					{
						QLOG_FATAL() << "Error writing packet. Quitting thread";
						return;
					}
					m_threadReqList.removeAt(i);
					i--;
				}
				m_waitingInfoMutex.unlock();
			}
			else if (m_threadReqList[i].type == GET_INTERFACE_VERSION)
			{
				m_waitingInfoMutex.lock();
				if (!m_waitingForResponse)
				{
					m_waitingForResponse = true;
					m_timeoutMsecs = QDateTime::currentDateTime().currentMSecsSinceEpoch();
					m_currentWaitingRequest = m_threadReqList[i];
					m_payloadWaitingForResponse = 0x0000;
					if (!sendPacket(GET_INTERFACE_VERSION))
					{
						QLOG_FATAL() << "Error writing packet. Quitting thread";
						return;
					}
					m_threadReqList.removeAt(i);
					i--;
				}
				m_waitingInfoMutex.unlock();
			}
			else if (m_threadReqList[i].type == GET_FIRMWARE_VERSION)
			{
				m_waitingInfoMutex.lock();
				if (!m_waitingForResponse)
				{
					m_waitingForResponse = true;
					m_timeoutMsecs = QDateTime::currentDateTime().currentMSecsSinceEpoch();
					m_currentWaitingRequest = m_threadReqList[i];
					m_payloadWaitingForResponse = 0x0002;
					if (!sendPacket(GET_FIRMWARE_VERSION))
					{
						QLOG_FATAL() << "Error writing packet. Quitting thread";
						return;
					}
					m_threadReqList.removeAt(i);
					i--;
				}
				m_waitingInfoMutex.unlock();
			}
			else if (m_threadReqList[i].type == GET_MAX_PACKET_SIZE)
			{
				m_waitingInfoMutex.lock();
				if (!m_waitingForResponse)
				{
					m_waitingForResponse = true;
					m_timeoutMsecs = QDateTime::currentDateTime().currentMSecsSinceEpoch();
					m_currentWaitingRequest = m_threadReqList[i];
					m_payloadWaitingForResponse = 0x0004;
					if (!sendPacket(GET_MAX_PACKET_SIZE))
					{
						QLOG_FATAL() << "Error writing packet. Quitting thread";
						return;
					}
					m_threadReqList.removeAt(i);
					i--;
				}
				m_waitingInfoMutex.unlock();
			}
			else if (m_threadReqList[i].type == ECHO_PACKET)
			{
				m_waitingInfoMutex.lock();
				if (!m_waitingForResponse)
				{
					m_waitingForResponse = true;
					m_timeoutMsecs = QDateTime::currentDateTime().currentMSecsSinceEpoch();
					m_currentWaitingRequest = m_threadReqList[i];
					m_payloadWaitingForResponse = 0x0006;
					if (!sendPacket(m_threadReqList[i],true))
					{
						QLOG_FATAL() << "Error writing packet. Quitting thread";
						return;
					}
					m_threadReqList.removeAt(i);
					i--;
				}
				m_waitingInfoMutex.unlock();
			}
			else if (m_threadReqList[i].type == SOFT_RESET)
			{
				m_waitingInfoMutex.lock();
				if (!m_waitingForResponse)
				{
					m_currentWaitingRequest = m_threadReqList[i];
					if (!sendPacket(SOFT_RESET))
					{
						QLOG_FATAL() << "Error writing packet. Quitting thread";
						return;
					}
					m_threadReqList.removeAt(i);
					i--;
					emit packetSent(SOFT_RESET,QByteArray(),QByteArray());
				}
				m_waitingInfoMutex.unlock();
			}
			else if (m_threadReqList[i].type == HARD_RESET)
			{
				m_waitingInfoMutex.lock();
				if (!m_waitingForResponse)
				{
					m_currentWaitingRequest = m_threadReqList[i];
					if (!sendPacket(HARD_RESET))
					{
						QLOG_FATAL() << "Error writing packet. Quitting thread";
						return;
					}
					m_threadReqList.removeAt(i);
					i--;
					emit packetSent(HARD_RESET,QByteArray(),QByteArray());
				}
				m_waitingInfoMutex.unlock();
			}
			else if (false)
			{
                serialPort->writeBytes(QByteArray());
				break;
			}
		}
		if (m_threadReqList.size() == 0)
		{
			msleep(10);
		}

		m_waitingInfoMutex.lock();
		if (QDateTime::currentDateTime().currentMSecsSinceEpoch() - m_timeoutMsecs > 500 && m_waitingForResponse)
		{
			//5 seconds
			QLOG_WARN() << "TIMEOUT waiting for response to payload:" << "0x" + QString::number(m_payloadWaitingForResponse,16).toUpper() << "Sequence:" << m_currentWaitingRequest.sequencenumber;
			if (m_currentWaitingRequest.retryCount >= 2)
			{
				QLOG_ERROR() << "No retries left!";
				emit commandTimedOut(m_currentWaitingRequest.sequencenumber);
				m_waitingForResponse = false;
			}
			else
			{
				QLOG_WARN() << "Retrying";
				m_waitingForResponse = false;
				m_currentWaitingRequest.retryCount++;
				m_threadReqList.insert(0,m_currentWaitingRequest);
			}
			//TODO: Requeue the command for retry.
		}
		m_waitingInfoMutex.unlock();
	}
	rxThread->stop();
	rxThread->wait(500);
}

bool FreeEmsComms::sendSimplePacket(unsigned short payloadid)
{
	m_waitingForResponse = true;
	m_timeoutMsecs = QDateTime::currentDateTime().currentMSecsSinceEpoch();
	QByteArray header;
	QByteArray payload;
	header.append((char)0x00);
	header.append((char)((payloadid << 8) & 0xFF));
	header.append((char)(payloadid & 0xFF));
	m_payloadWaitingForResponse = payloadid;
    if (serialPort->writeBytes(generatePacket(header,payload)) < 0)
	{
		QLOG_FATAL() << "Error writing packet. Quitting thread";
		return false;
	}
	return true;
}

void FreeEmsComms::packetNakedRec(unsigned short payloadid,QByteArray header,QByteArray payload,unsigned short errornum)
{
	if (m_waitingForResponse)
	{
		if (m_interrogateInProgress)
		{
			if (m_interrogatePacketList.contains(m_currentWaitingRequest.sequencenumber))
			{
				//Interrogate command failed!!
				/*emit interrogateTaskSucceed(m_currentWaitingRequest.sequencenumber);
				m_interrogatePacketList.removeOne(m_currentWaitingRequest.sequencenumber);
				emit interrogationProgress(m_interrogateTotalCount - m_interrogatePacketList.size(),m_interrogateTotalCount);
				if (m_interrogatePacketList.size() == 0)
				{
					if (!m_interrogateIdListComplete)
					{
						m_interrogateIdListComplete = true;
						m_interrogateTotalCount += m_locationIdList.size();
						for (int i=0;i<m_locationIdList.size();i++)
						{
							int task = getLocationIdInfo(m_locationIdList[i]);
							m_interrogatePacketList.append(task);
							emit interrogateTaskStart("Location ID Info: 0x" + QString::number(m_locationIdList[i],16),task);
							//void interrogateTaskStart(QString task, int sequence);
							//void interrogateTaskFail(int sequence);
						}
						emit interrogationProgress(m_interrogateTotalCount - m_interrogatePacketList.size(),m_interrogateTotalCount);
					}
					else if (!m_interrogateIdInfoComplete)
					{
						//Fill out the parent information for both device, and local ram.
						emsData.populateDeviceRamAndFlashParents();
						emsData.populateLocalRamAndFlash();
						m_interrogateIdInfoComplete = true;
						QList<unsigned short> ramlist = emsData.getTopLevelDeviceRamLocations();
						QList<unsigned short> flashlist = emsData.getTopLevelDeviceFlashLocations();
						m_interrogateTotalCount += ramlist.size() + flashlist.size();
						for (int i=0;i<ramlist.size();i++)
						{
							int task = retrieveBlockFromRam(ramlist[i],0,0);
							m_interrogatePacketList.append(task);
							emit interrogateTaskStart("Ram Location: 0x" + QString::number(ramlist[i],16),task);
						}
						for (int i=0;i<flashlist.size();i++)
						{
							int task = retrieveBlockFromFlash(flashlist[i],0,0);
							m_interrogatePacketList.append(task);
							emit interrogateTaskStart("Flash Location: 0x" + QString::number(flashlist[i],16),task);
						}
						emit interrogationProgress(m_interrogateTotalCount - m_interrogatePacketList.size(),m_interrogateTotalCount);
					}
					else
					{
						//Interrogation complete.
						emit interrogationProgress(m_interrogateTotalCount - m_interrogatePacketList.size(),m_interrogateTotalCount);
						emit interrogationComplete();
						m_interrogateInProgress = false;
						for (int i=0;i<emsData.getUniqueLocationIdList().size();i++)
						{
							locationIdUpdate(emsData.getUniqueLocationIdList()[i]);
						}
						//deviceDataUpdated(unsigned short)
					}
				}
				else
				{
					if (m_payloadWaitingForResponse == GET_LOCATION_ID_LIST)
					{

					}
				}*/
			}
			else
			{
			}
		}
		if (m_waitingForFlashWrite)
		{
			m_waitingForFlashWrite = false;
			unsigned short locid = m_currentWaitingRequest.args[0].toUInt();
			emsData.setLocalFlashBlock(locid,emsData.getDeviceFlashBlock(locid));
			if (m_2dTableMap.contains(locid))
			{
				m_2dTableMap[locid]->setData(locid,!emsData.hasLocalRamBlock(locid),emsData.getLocalFlashBlock(locid),m_metaDataParser->get2DMetaData(locid),false);
			}
			if (m_3dTableMap.contains(locid))
			{
				m_3dTableMap[locid]->setData(locid,!emsData.hasLocalRamBlock(locid),emsData.getLocalFlashBlock(locid),m_metaDataParser->get3DMetaData(locid));
			}
			if (m_rawDataMap.contains(locid))
			{
				if (emsData.hasLocalRamBlock(locid))
				{
					m_rawDataMap[locid]->setData(locid,false,emsData.getLocalRamBlock(locid));
				}
				else
				{
					m_rawDataMap[locid]->setData(locid,true,emsData.getLocalFlashBlock(locid));
				}
			}
		}
		if (m_waitingForRamWrite)
		{
			m_waitingForRamWrite = false;
			unsigned short locid = m_currentWaitingRequest.args[0].toUInt();
			emsData.setLocalRamBlock(locid,emsData.getDeviceRamBlock(locid));
			if (m_2dTableMap.contains(locid))
			{
				m_2dTableMap[locid]->setData(locid,!emsData.hasLocalRamBlock(locid),emsData.getLocalRamBlock(locid),m_metaDataParser->get2DMetaData(locid),false);
			}
			if (m_3dTableMap.contains(locid))
			{
				m_3dTableMap[locid]->setData(locid,!emsData.hasLocalRamBlock(locid),emsData.getLocalRamBlock(locid),m_metaDataParser->get3DMetaData(locid));
			}
			if (m_rawDataMap.contains(locid))
			{
				if (emsData.hasLocalRamBlock(locid))
				{
					m_rawDataMap[locid]->setData(locid,false,emsData.getLocalRamBlock(locid));
				}
				else
				{
					m_rawDataMap[locid]->setData(locid,true,emsData.getLocalFlashBlock(locid));
				}
			}
		}
		if (payloadid == m_payloadWaitingForResponse+1)
		{
			QLOG_TRACE() << "Recieved Negative Response" << "0x" + QString::number(m_payloadWaitingForResponse+1,16).toUpper() << "For Payload:" << "0x" + QString::number(m_payloadWaitingForResponse+1,16).toUpper()<< "Sequence Number:" << m_currentWaitingRequest.sequencenumber;
			QLOG_TRACE() << "Currently waiting for:" << QString::number(m_currentWaitingRequest.type,16).toUpper();
			//NAK to our packet
			//unsigned short errornum = parsedPacket.payload[0] << 8;
			//errornum += parsedPacket.payload[1];
			emit commandFailed(m_currentWaitingRequest.sequencenumber,errornum);
			emit packetNaked(m_currentWaitingRequest.type,header,payload,errornum);
			m_waitingForResponse = false;
		}
		else
		{
			QLOG_ERROR() << "ERROR! Invalid packet:" << "0x" + QString::number(payloadid,16).toUpper();
		}
	}
}

void FreeEmsComms::packetAckedRec(unsigned short payloadid,QByteArray header,QByteArray payload)
{
	if (m_waitingForResponse)
	{
		if (m_interrogateInProgress)
		{
			if (m_interrogatePacketList.contains(m_currentWaitingRequest.sequencenumber))
			{
				emit interrogateTaskSucceed(m_currentWaitingRequest.sequencenumber);
				m_interrogatePacketList.removeOne(m_currentWaitingRequest.sequencenumber);
				emit interrogationProgress(m_interrogateTotalCount - m_interrogatePacketList.size(),m_interrogateTotalCount);
				if (m_interrogatePacketList.size() == 0)
				{
					if (!m_interrogateIdListComplete)
					{
						QLOG_DEBUG() << "Interrogation ID List complete" << m_locationIdList.size() << "entries";
						m_interrogateIdListComplete = true;
						m_interrogateTotalCount += m_locationIdList.size();
						for (int i=0;i<m_locationIdList.size();i++)
						{
							int task = getLocationIdInfo(m_locationIdList[i]);
							m_interrogatePacketList.append(task);
							emit interrogateTaskStart("Location ID Info: 0x" + QString::number(m_locationIdList[i],16),task);
							//void interrogateTaskStart(QString task, int sequence);
							//void interrogateTaskFail(int sequence);
						}
						emit interrogationProgress(m_interrogateTotalCount - m_interrogatePacketList.size(),m_interrogateTotalCount);
					}
					else if (!m_interrogateIdInfoComplete)
					{
						//Fill out the parent information for both device, and local ram.
						emsData.populateDeviceRamAndFlashParents();
						emsData.populateLocalRamAndFlash();
						m_interrogateIdInfoComplete = true;
						QList<unsigned short> ramlist = emsData.getTopLevelDeviceRamLocations();
						QList<unsigned short> flashlist = emsData.getTopLevelDeviceFlashLocations();
						m_interrogateTotalCount += ramlist.size() + flashlist.size();
						for (int i=0;i<ramlist.size();i++)
						{
							int task = retrieveBlockFromRam(ramlist[i],0,0);
							m_interrogatePacketList.append(task);
							emit interrogateTaskStart("Ram Location: 0x" + QString::number(ramlist[i],16),task);
						}
						for (int i=0;i<flashlist.size();i++)
						{
							int task = retrieveBlockFromFlash(flashlist[i],0,0);
							m_interrogatePacketList.append(task);
							emit interrogateTaskStart("Flash Location: 0x" + QString::number(flashlist[i],16),task);
						}
						emit interrogationProgress(m_interrogateTotalCount - m_interrogatePacketList.size(),m_interrogateTotalCount);
					}
					else
					{
						QLOG_DEBUG() << "Interrogation complete";
						//Interrogation complete.
						emit interrogationProgress(m_interrogateTotalCount - m_interrogatePacketList.size(),m_interrogateTotalCount);
						emit interrogationComplete();
						m_interrogateInProgress = false;
						for (int i=0;i<emsData.getUniqueLocationIdList().size();i++)
						{
							locationIdUpdate(emsData.getUniqueLocationIdList()[i]);
						}
						//deviceDataUpdated(unsigned short)
					}
				}
				else
				{
					if (m_payloadWaitingForResponse == GET_LOCATION_ID_LIST)
					{

					}
				}
			}
			else
			{
			}
		}
		if (m_waitingForFlashWrite)
		{
			m_waitingForFlashWrite = false;
			unsigned short locid = m_currentWaitingRequest.args[0].toUInt();
			emsData.setDeviceFlashBlock(locid,emsData.getLocalFlashBlock(locid));
			emit locationIdUpdate(locid);
		}
		if (m_waitingForRamWrite)
		{
			m_waitingForRamWrite = false;
			unsigned short locid = m_currentWaitingRequest.args[0].toUInt();
			//Change has been accepted, copy local ram to device ram
			emsData.setDeviceRamBlock(locid,emsData.getLocalRamBlock(locid));
			emit locationIdUpdate(locid);
		}
		if (payloadid == m_payloadWaitingForResponse+1)
		{
			QLOG_TRACE() << "Recieved Response" << "0x" + QString::number(m_payloadWaitingForResponse+1,16).toUpper() << "For Payload:" << "0x" + QString::number(m_payloadWaitingForResponse+1,16).toUpper()<< "Sequence Number:" << m_currentWaitingRequest.sequencenumber;
			QLOG_TRACE() << "Currently waiting for:" << QString::number(m_currentWaitingRequest.type,16).toUpper();
			//Packet is good.
			emit commandSuccessful(m_currentWaitingRequest.sequencenumber);
			emit packetAcked(m_currentWaitingRequest.type,header,payload);
			m_waitingForResponse = false;
		}
		else if (payloadid != 0x0191)
		{
			QLOG_ERROR() << "ERROR! Invalid packet:" << "0x" + QString::number(payloadid,16).toUpper();
		}
	}
}

void FreeEmsComms::setLogFileName(QString filename)
{
	m_logsFilename = filename;
}
void FreeEmsComms::datalogTimerTimeout()
{
	if (!m_lastDatalogUpdateEnabled)
	{
		return;
	}
	quint64 current = QDateTime::currentMSecsSinceEpoch() - m_lastDatalogTime;
	if (current > 5000)
	{
		//It's been 5 seconds since our last datalog. We've likely either reset, or stopped responding.
		m_isSilent = true;
		m_lastDatalogUpdateEnabled = false;
		emit emsSilenceStarted();
	}
}
Table2DData* FreeEmsComms::get2DTableData(unsigned short locationid)
{
	if (!m_2dTableMap.contains(locationid))
	{
		//This is an error condition
		return 0;
	}
	return m_2dTableMap[locationid];

}

Table3DData* FreeEmsComms::get3DTableData(unsigned short locationid)
{
	if (!m_3dTableMap.contains(locationid))
	{
		//This is an error condition
		return 0;
	}
	return m_3dTableMap[locationid];
}
RawData* FreeEmsComms::getRawData(unsigned short locationid)
{
	if (!m_rawDataMap.contains(locationid))
	{
		//This is an error condition
		return 0;
	}
	return m_rawDataMap[locationid];
}
void FreeEmsComms::locationIdListRec(QList<unsigned short> locationidlist)
{
	m_locationIdList.clear();
	for (int i=0;i<locationidlist.size();i++)
	{
		m_locationIdList.append(locationidlist[i]);
	}
	QLOG_DEBUG() << m_locationIdList.size() << "Locations loaded";
}

void FreeEmsComms::locationIdUpdate(unsigned short locationid)
{
	//emsData.getChildrenOfLocalRamLocation()
	QList<unsigned short> updatelist;
	updatelist.append(locationid);
	if (emsData.localFlashHasParent(locationid))
	{
		updatelist.append(emsData.getParentOfLocalFlashLocation(locationid));
	}
	if (emsData.localRamHasParent(locationid))
	{
		updatelist.append(emsData.getParentOfLocalRamLocation(locationid));
	}
	if (emsData.localFlashHasChildren(locationid))
	{
		updatelist.append(emsData.getChildrenOfLocalFlashLocation(locationid));
	}
	if (emsData.localRamHasChildren(locationid))
	{
		updatelist.append(emsData.getChildrenOfLocalRamLocation(locationid));
	}

	for (int i=0;i<updatelist.size();i++)
	{
		//QLOG_DEBUG() << "Updating location id:" << QString::number(updatelist[i],16);
		if (m_2dTableMap.contains(updatelist[i]))
		{
			m_2dTableMap[updatelist[i]]->setData(updatelist[i],!emsData.hasDeviceRamBlock(updatelist[i]),emsData.getDeviceRamBlock(updatelist[i]),m_metaDataParser->get2DMetaData(updatelist[i]),false);
		}
		if (m_3dTableMap.contains(updatelist[i]))
		{
			m_3dTableMap[updatelist[i]]->setData(updatelist[i],!emsData.hasDeviceRamBlock(updatelist[i]),emsData.getDeviceRamBlock(updatelist[i]),m_metaDataParser->get3DMetaData(updatelist[i]));
		}
		if (m_rawDataMap.contains(updatelist[i]))
		{
			if (emsData.hasLocalRamBlock(updatelist[i]))
			{
				m_rawDataMap[updatelist[i]]->setData(updatelist[i],false,emsData.getLocalRamBlock(updatelist[i]));
			}
			else
			{
				m_rawDataMap[updatelist[i]]->setData(updatelist[i],true,emsData.getLocalFlashBlock(updatelist[i]));
			}
		}
	}
}
void FreeEmsComms::copyFlashToRam(unsigned short locationid)
{
	emsData.setLocalRamBlock(locationid,emsData.getLocalFlashBlock(locationid));
	if (m_2dTableMap.contains(locationid))
	{
		m_2dTableMap[locationid]->setData(locationid,!emsData.hasLocalRamBlock(locationid),emsData.getLocalRamBlock(locationid),m_metaDataParser->get2DMetaData(locationid),false);
	}
	if (m_3dTableMap.contains(locationid))
	{
		m_3dTableMap[locationid]->setData(locationid,!emsData.hasLocalRamBlock(locationid),emsData.getLocalRamBlock(locationid),m_metaDataParser->get3DMetaData(locationid));
	}
	if (m_rawDataMap.contains(locationid))
	{
		if (emsData.hasLocalRamBlock(locationid))
		{
			m_rawDataMap[locationid]->setData(locationid,false,emsData.getLocalRamBlock(locationid));
		}
		else
		{
			m_rawDataMap[locationid]->setData(locationid,true,emsData.getLocalFlashBlock(locationid));
		}
	}
	updateBlockInRam(locationid,0,emsData.getLocalFlashBlock(locationid).size(),emsData.getLocalFlashBlock(locationid));
}

void FreeEmsComms::copyRamToFlash(unsigned short locationid)
{
	emsData.setLocalFlashBlock(locationid,emsData.getLocalRamBlock(locationid));
	emsData.setDeviceFlashBlock(locationid,emsData.getDeviceRamBlock(locationid));
	burnBlockFromRamToFlash(locationid,0,0);
}

void FreeEmsComms::dataLogWrite(QByteArray buffer)
{
	if (m_logsEnabled)
	{
		if (m_debugLogsEnabled)
		{
			m_logOutFile->write(buffer);
			m_logInOutFile->write(buffer);
		}
	}
}


void FreeEmsComms::dataLogRead(QByteArray buffer)
{
	if (m_logsEnabled)
	{
		m_logInFile->write(buffer);
		if (m_debugLogsEnabled)
		{
			m_logInOutFile->write(buffer);
		}
	}
}

void FreeEmsComms::parseEverything(QByteArray buffer)
{
	Packet p = m_packetDecoder->parseBuffer(buffer);
	if (!p.isValid)
	{
		emit decoderFailure(buffer);
	}
	else
	{
		m_packetDecoder->parsePacket(p);
		//parsePacket(p);
	}
}
void FreeEmsComms::ramBlockUpdateRec(QByteArray header,QByteArray payload)
{
	if (m_currentWaitingRequest.args.size() == 0)
	{
		QLOG_ERROR() << "ERROR! Current waiting packet's arg size is zero1!!";
		QLOG_ERROR() << "0x" + QString::number(m_currentWaitingRequest.type,16).toUpper();
		//QLOG_ERROR() << "0x" + QString::number(payloadid,16).toUpper();
	}
	unsigned short locationid = m_currentWaitingRequest.args[0].toInt();
	emsData.ramBlockUpdate(locationid,header,payload);
}

void FreeEmsComms::flashBlockUpdateRec(QByteArray header,QByteArray payload)
{
	if (m_currentWaitingRequest.args.size() == 0)
	{
		QLOG_ERROR() << "ERROR! Current waiting packet's arg size is zero1!!";
		QLOG_ERROR() << "0x" + QString::number(m_currentWaitingRequest.type,16).toUpper();
		//QLOG_ERROR() << "0x" + QString::number(payloadid,16).toUpper();
	}
	unsigned short locationid = m_currentWaitingRequest.args[0].toInt();
	emsData.flashBlockUpdate(locationid,header,payload);
}

void FreeEmsComms::locationIdInfoRec(MemoryLocationInfo info)
{
	if (m_currentWaitingRequest.args.size() == 0)
	{
		QLOG_ERROR() << "ERROR! Current waiting packet's arg size is zero1!!";
		QLOG_ERROR() << "0x" + QString::number(m_currentWaitingRequest.type,16).toUpper();
		//QLOG_ERROR() << "0x" + QString::number(payloadid,16).toUpper();
	}
	unsigned short locationid = m_currentWaitingRequest.args[0].toInt();
	info.locationid = locationid;
	emit locationIdInfo(locationid,info);
	emsData.passLocationInfo(locationid,info);
	QLOG_DEBUG() << "Got memory location:" << info.locationid;
	if (info.type == DATA_TABLE_2D)
	{
		Table2DData *data = new FETable2DData();
		connect(data,SIGNAL(saveSingleDataToRam(unsigned short,unsigned short,unsigned short,QByteArray)),&emsData,SLOT(ramBytesLocalUpdate(unsigned short,unsigned short,unsigned short,QByteArray)));
		connect(data,SIGNAL(saveSingleDataToFlash(unsigned short,unsigned short,unsigned short,QByteArray)),&emsData,SLOT(flashBytesLocalUpdate(unsigned short,unsigned short,unsigned short,QByteArray)));
		connect(data,SIGNAL(requestBlockFromRam(unsigned short,unsigned short,unsigned short)),this,SLOT(retrieveBlockFromRam(unsigned short,unsigned short,unsigned short)));
		connect(data,SIGNAL(requestBlockFromFlash(unsigned short,unsigned short,unsigned short)),this,SLOT(retrieveBlockFromFlash(unsigned short,unsigned short,unsigned short)));
		connect(data,SIGNAL(requestRamUpdateFromFlash(unsigned short)),this,SLOT(copyFlashToRam(unsigned short)));
		connect(data,SIGNAL(requestFlashUpdateFromRam(unsigned short)),this,SLOT(copyRamToFlash(unsigned short)));
		m_2dTableMap[locationid] = data;
	}
	else if (info.type == DATA_TABLE_3D)
	{
		Table3DData *data = new FETable3DData();
		connect(data,SIGNAL(saveSingleDataToRam(unsigned short,unsigned short,unsigned short,QByteArray)),&emsData,SLOT(ramBytesLocalUpdate(unsigned short,unsigned short,unsigned short,QByteArray)));
		connect(data,SIGNAL(saveSingleDataToFlash(unsigned short,unsigned short,unsigned short,QByteArray)),&emsData,SLOT(flashBytesLocalUpdate(unsigned short,unsigned short,unsigned short,QByteArray)));
		connect(data,SIGNAL(requestBlockFromRam(unsigned short,unsigned short,unsigned short)),this,SLOT(retrieveBlockFromRam(unsigned short,unsigned short,unsigned short)));
		connect(data,SIGNAL(requestBlockFromFlash(unsigned short,unsigned short,unsigned short)),this,SLOT(retrieveBlockFromFlash(unsigned short,unsigned short,unsigned short)));
		connect(data,SIGNAL(requestRamUpdateFromFlash(unsigned short)),this,SLOT(copyFlashToRam(unsigned short)));
		connect(data,SIGNAL(requestFlashUpdateFromRam(unsigned short)),this,SLOT(copyRamToFlash(unsigned short)));
		m_3dTableMap[locationid] = data;
	}
	else
	{
		RawData *data = new FERawData();
		connect(data,SIGNAL(saveSingleDataToRam(unsigned short,unsigned short,unsigned short,QByteArray)),&emsData,SLOT(ramBytesLocalUpdate(unsigned short,unsigned short,unsigned short,QByteArray)));
		connect(data,SIGNAL(saveSingleDataToFlash(unsigned short,unsigned short,unsigned short,QByteArray)),&emsData,SLOT(flashBytesLocalUpdate(unsigned short,unsigned short,unsigned short,QByteArray)));
		connect(data,SIGNAL(requestBlockFromRam(unsigned short,unsigned short,unsigned short)),this,SLOT(retrieveBlockFromRam(unsigned short,unsigned short,unsigned short)));
		connect(data,SIGNAL(requestBlockFromFlash(unsigned short,unsigned short,unsigned short)),this,SLOT(retrieveBlockFromFlash(unsigned short,unsigned short,unsigned short)));
		m_rawDataMap[locationid] = data;
	}
}

Q_EXPORT_PLUGIN2(FreeEmsPlugin, FreeEmsComms)
