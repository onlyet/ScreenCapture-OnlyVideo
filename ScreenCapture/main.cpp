#include <QApplication>
#include "ScreenRecordTest.h"

int main(int argc, char *argv[])
{
	QApplication a(argc, argv);

	ScreenRecord sr;

	return a.exec();
}
