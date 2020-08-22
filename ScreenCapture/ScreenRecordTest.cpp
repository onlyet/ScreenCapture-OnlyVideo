#include "ScreenRecordTest.h"
#include "ScreenRecordImpl.h"
#include <QTimer>
#include <QApplication>
#include <QDesktopWidget>

ScreenRecord::ScreenRecord(QObject *parent) :
	QObject(parent)
{
	ScreenRecordImpl *sr = new ScreenRecordImpl(this);

	//m_params["width"] = 1920;
	//m_params["height"] = 1080;
    m_params["width"] = QApplication::desktop()->screenGeometry().width();
    m_params["height"] = QApplication::desktop()->screenGeometry().height();

	m_params["fps"] = 30;
	m_params["filePath"] = QStringLiteral("test.mp4");

	sr->Init(m_params);

	QTimer::singleShot(1000, sr, SLOT(Start()));
	//QTimer::singleShot(5000, sr, SLOT(Pause()));
	//QTimer::singleShot(6000, sr, SLOT(Start()));
	QTimer::singleShot(11000, sr, SLOT(Stop()));
}
