/*
    KSysGuard, the KDE System Guard

	Copyright (c) 1999 - 2001 Chris Schlaeger <cs@kde.org>

    This program is free software; you can redistribute it and/or
    modify it under the terms of version 2 of the GNU General Public
    License as published by the Free Software Foundation.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.

	KSysGuard is currently maintained by Chris Schlaeger
	<cs@kde.org>. Please do not commit any changes without consulting
	me first. Thanks!

	$Id$
*/

#include <qdragobject.h>
#include <qspinbox.h>
#include <qlabel.h>
#include <qfile.h>
#include <qdom.h>
#include <qtextstream.h>

#include <kglobal.h>
#include <klocale.h>
#include <kmessagebox.h>
#include <kstddirs.h>
#include <kdebug.h>

#include "SensorManager.h"
#include "FancyPlotter.h"
#include "KSysGuardAppletSettings.h"
#include "KSysGuardApplet.moc"

extern "C"
{
	KPanelApplet* init(QWidget *parent, const QString& configFile)
	{
		KGlobal::locale()->insertCatalogue("ksysguardapplet");
		return new KSysGuardApplet(configFile, KPanelApplet::Normal,
								   KPanelApplet::Preferences, parent,
								   "ksysguardapplet");
    }
}

KSysGuardApplet::KSysGuardApplet(const QString& configFile, Type t,
								 int actions, QWidget *parent,
								 const char *name)
    : KPanelApplet(configFile, t, actions, parent, name)
{
	ksgas = 0;

	SensorMgr = new SensorManager();
	CHECK_PTR(SensorMgr);

	dockCnt = 1;
	docks = new SensorDisplay*[dockCnt];
	docks[0] = 0;

	load();

	setAcceptDrops(TRUE);
}

KSysGuardApplet::~KSysGuardApplet()
{
	delete ksgas;
	delete SensorMgr;
}

int
KSysGuardApplet::widthForHeight(int h) const
{
	return (h * dockCnt);
}

int
KSysGuardApplet::heightForWidth(int w) const
{
	return (w * dockCnt);
}

void
KSysGuardApplet::resizeEvent(QResizeEvent*)
{
	layout();
}

void
KSysGuardApplet::preferences()
{
	ksgas = new KSysGuardAppletSettings(
		this, "KSysGuardAppletSettings", true);
	CHECK_PTR(ksgas);
																
	connect(ksgas->applyButton, SIGNAL(clicked()),
			this, SLOT(applySettings()));

	ksgas->dockCnt->setValue(dockCnt);
	if (ksgas->exec())
		applySettings();

	delete ksgas;
	ksgas = 0;

	save();
}

void
KSysGuardApplet::applySettings()
{
	resizeDocks(ksgas->dockCnt->text().toUInt());
}

void
KSysGuardApplet::layout()
{
	int w = width();
	int h = height();

	if (orientation() == Horizontal)
	{
		for (uint i = 0; i < dockCnt; ++i)
			if (docks[i])
				docks[i]->setGeometry(i * h, 0, h, h);
	}
	else
	{
		for (uint i = 0; i < dockCnt; ++i)
			if (docks[i])
				docks[i]->setGeometry(0, i * w, w, w);
	}
}

int
KSysGuardApplet::findDock(const QPoint& p)
{
	if (orientation() == Horizontal)
		return (p.x() / height());
	else
		return (p.y() / width());
}

void
KSysGuardApplet::dragEnterEvent(QDragEnterEvent* ev)
{
    ev->accept(QTextDrag::canDecode(ev));
}

void
KSysGuardApplet::dropEvent(QDropEvent* ev)
{
	QString dObj;

	if (QTextDrag::decode(ev, dObj))
	{
		// The host name, sensor name and type are seperated by a ' '.
		QString hostName = dObj.left(dObj.find(' '));
		dObj.remove(0, dObj.find(' ') + 1);
		QString sensorName = dObj.left(dObj.find(' '));
		dObj.remove(0, dObj.find(' ') + 1);
		QString sensorType = dObj.left(dObj.find(' '));
		dObj.remove(0, dObj.find(' ') + 1);
		QString sensorDescr = dObj;

		if (hostName.isEmpty() || sensorName.isEmpty() ||
			sensorType.isEmpty())
		{
			return;
		}

		if (!SensorMgr->engageHost(hostName))
		{
			/* TODO: This error message is wrong. It needs to be changed
			 * to "Impossible to connect to ..." after the message freeze. */
			QString msg = i18n("Unknown hostname \'%1\'!").arg(hostName);
			KMessageBox::error(this, msg);
			return;
		}

		int dock = findDock(ev->pos());
		if (docks[dock] == 0)
		{
			docks[dock] = new FancyPlotter(this, "FancyPlotter",
										   sensorDescr, 100.0, 100.0, true);
			layout();
			docks[dock]->show();
		}
		docks[dock]->addSensor(hostName, sensorName, sensorDescr);
	}

	save();
}

void
KSysGuardApplet::customEvent(QCustomEvent* ev)
{
	if (ev->type() == QEvent::User)
	{
		// SensorDisplays send out this event if they want to be removed.
		removeDisplay((SensorDisplay*) ev->data());
		save();
	}
}

void
KSysGuardApplet::removeDisplay(SensorDisplay* sd)
{
	for (uint i = 0; i < dockCnt; ++i)
		if (sd == docks[i])
		{
			delete docks[i];
			docks[i] = 0;
			return;
		}
}

void
KSysGuardApplet::resizeDocks(uint newDockCnt)
{
	/* This function alters the number of available docks. The number of
	 * docks can be increased or decreased. We try to preserve existing
	 * docks if possible. */

	if (newDockCnt == dockCnt)
		return;

	// Create and initialize new dock array.
	SensorDisplay** tmp = new SensorDisplay*[newDockCnt];
	uint i;
	// Copy old docks into new.
	for (i = 0; (i < newDockCnt) && (i < dockCnt); ++i)
		tmp[i] = docks[i];
	// Destruct old docks that are not needed anymore.
	for (i = newDockCnt; i < dockCnt; ++i)
		delete docks[i];
	for (i = dockCnt; i < newDockCnt; ++i)
		tmp[i] = 0;
	// Destruct old dock.
	delete docks;

	docks = tmp;
	dockCnt = newDockCnt;

	emit updateLayout();
}

bool
KSysGuardApplet::load()
{
	KStandardDirs* kstd = KGlobal::dirs();
	kstd->addResourceType("data", "share/apps/ksysguard");
	QString fileName = kstd->findResource("data", "KSysGuardApplet.xml");

	QFile file(fileName);
	if (!file.open(IO_ReadOnly))
	{
		KMessageBox::sorry(this, i18n("Can't open the file %1")
						   .arg(fileName));
		return (FALSE);
	}

	// Parse the XML file.
	QDomDocument doc;
	// Read in file and check for a valid XML header.
	if (!doc.setContent(&file))
	{
		KMessageBox::sorry(
			this,
			i18n("The file %1 does not contain valid XML").arg(fileName));
		return (FALSE);
	}
	// Check for proper document type.
	if (doc.doctype().name() != "KSysGuardApplet")
	{
/* TODO: Enable this after message freeze
		KMessageBox::sorry(
			this,
			i18n("The file %1 does not contain a valid applet\n"
				 "definition, which must have a document type\n"
				 "'KSysGuardApplet'").arg(fileName));
*/
		return (FALSE);
	}

	QDomElement element = doc.documentElement();
	bool rowsOk;
	uint d = element.attribute("dockCnt").toUInt(&rowsOk);
	resizeDocks(d);

	/* Load lists of hosts that are needed for the work sheet and try
	 * to establish a connection. */
	QDomNodeList dnList = element.elementsByTagName("host");
	uint i;
	for (i = 0; i < dnList.count(); ++i)
	{
		QDomElement element = dnList.item(i).toElement();
		SensorMgr->engage(element.attribute("name"),
						  element.attribute("shell"),
						  element.attribute("command"));
	}

	// Load the displays and place them into the work sheet.
	dnList = element.elementsByTagName("display");
	for (i = 0; i < dnList.count(); ++i)
	{
		QDomElement element = dnList.item(i).toElement();
		uint dock = element.attribute("dock").toUInt();
		if (i >= dockCnt)
		{
			kdDebug () << "Dock number " << i << " out of range "
					   << dockCnt << endl;
			return (FALSE);
		}

		QString classType = element.attribute("class");
		SensorDisplay* newDisplay;
		if (classType == "FancyPlotter")
			newDisplay = new FancyPlotter(this, "Dummy", "Dummy",
										  100.0, 100.0, true);
#if 0
		else if (classType == "MultiMeter")
			newDisplay = new MultiMeter(this);
		else if (classType == "DancingBars")
			newDisplay = new DancingBars(this);
#endif
		else
		{
			kdDebug () << "Unkown class " <<  classType << endl;
			return (FALSE);
		}
		CHECK_PTR(newDisplay);

		// load display specific settings
		if (!newDisplay->load(element))
			return (FALSE);

		delete docks[dock];
		docks[dock] = newDisplay;
	}
	return (TRUE);
}

bool
KSysGuardApplet::save()
{
	// Parse the XML file.
	QDomDocument doc("KSysGuardApplet");
	doc.appendChild(doc.createProcessingInstruction(
		"xml", "version=\"1.0\" encoding=\"UTF-8\""));

	// save work sheet information
	QDomElement ws = doc.createElement("WorkSheet");
	doc.appendChild(ws);
	ws.setAttribute("dockCnt", dockCnt);

	QStringList hosts;
	uint i;
	for (i = 0; i < dockCnt; ++i)
		if (docks[i])
			docks[i]->collectHosts(hosts);

	// save host information (name, shell, etc.)
	QStringList::Iterator it;
	for (it = hosts.begin(); it != hosts.end(); ++it)
	{
		QString shell, command;

		if (SensorMgr->getHostInfo(*it, shell, command))
		{
			QDomElement host = doc.createElement("host");
			ws.appendChild(host);
			host.setAttribute("name", *it);
			host.setAttribute("shell", shell);
			host.setAttribute("command", command);
		}
	}
	
	for (i = 0; i < dockCnt; ++i)
		if (docks[i])
		{
			QDomElement display = doc.createElement("display");
			ws.appendChild(display);
			display.setAttribute("dock", i);
			display.setAttribute("class", docks[i]->className());

			docks[i]->save(doc, display);
		}	

	KStandardDirs* kstd = KGlobal::dirs();
	kstd->addResourceType("data", "share/apps/ksysguard");
	QString fileName = kstd->saveLocation("data", "ksysguard");
	fileName += "/KSysGuardApplet.xml";

	QFile file(fileName);
	if (!file.open(IO_WriteOnly))
	{
		KMessageBox::sorry(this, i18n("Can't save file %1")
						   .arg(fileName));
		return (FALSE);
	}
	QTextStream s(&file);
	s.setEncoding(QTextStream::UnicodeUTF8);
	s << doc;
	file.close();

	return (TRUE);
}
