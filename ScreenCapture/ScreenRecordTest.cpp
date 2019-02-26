#include "ScreenRecordTest.h"
#include "ScreenRecordImpl.h"
#include <QTimer>

ScreenRecord::ScreenRecord(QObject *parent) :
	QObject(parent)
{
	ScreenRecordImpl *sr = new ScreenRecordImpl(this);
	//connect(this, SIGNAL(StartRecord()), sr, SLOT(Start()));
	//connect(this, SIGNAL(FinishRecord()), sr, SLOT(Finish()));
	/*QTimer::singleShot(1000, this, SLOT(Start()));
	QTimer::singleShot(9000, this, SLOT(Finish()));*/
	QTimer::singleShot(1000, sr, SLOT(Start()));
	QTimer::singleShot(9000, sr, SLOT(Finish()));
}

void ScreenRecord::Start()
{
	emit StartRecord();
}

void ScreenRecord::Stop()
{
}

void ScreenRecord::Finish()
{
	emit FinishRecord();
}
