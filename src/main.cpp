#include <QApplication>
#include <QMessageBox>

#include "MainWindow.h"

int main(int argc, char *argv[])
{
	// 미리보기 재생은 Windows 기본 코덱(Media Foundation)을 사용
	// (Qt의 FFmpeg 백엔드 DLL이 libobs의 FFmpeg DLL과 이름이 겹치는 문제를 피함)
	qputenv("QT_MEDIA_BACKEND", "windows");

	QApplication app(argc, argv);
	QApplication::setApplicationName("ERShorts");
	QApplication::setOrganizationName("ERShorts");

	MainWindow w;
	QString error;
	if (!w.initialize(&error)) {
		QMessageBox::critical(nullptr, "ERShorts 초기화 실패",
				      error + "\n\nOBS 런타임 파일(data, obs-plugins)이 올바른 위치에 있는지 확인하세요.");
		return 1;
	}
	w.show();
	return app.exec();
}
