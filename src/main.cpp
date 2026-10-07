#include <QApplication>
#include <QMessageBox>

#include "Diagnostics.h"
#include "MainWindow.h"

int main(int argc, char *argv[])
{
	// 미리보기 재생은 Windows 기본 코덱(Media Foundation)을 사용
	// (Qt의 FFmpeg 백엔드 DLL이 libobs의 FFmpeg DLL과 이름이 겹치는 문제를 피함)
	qputenv("QT_MEDIA_BACKEND", "windows");

	QApplication app(argc, argv);
	QApplication::setApplicationName("ERShorts");
	QApplication::setOrganizationName("ERShorts");

	// 로그/충돌 기록은 가장 먼저 (libobs 초기화 로그까지 남기기 위해)
	Diagnostics::init();

	bool safeMode = app.arguments().contains("--safe-mode");
	if (!safeMode && Diagnostics::previousRunCrashed()) {
		safeMode = QMessageBox::question(
				   nullptr, "ERShorts",
				   "지난번 실행이 정상적으로 종료되지 않았습니다.\n\n"
				   "안전 모드로 시작할까요?\n"
				   "(자동 녹화와 실시간 미리보기를 끄고 시작합니다. 편집기는 그대로 쓸 수 있어요.)",
				   QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes) == QMessageBox::Yes;
	}
	if (safeMode)
		Diagnostics::write("안전 모드로 시작");

	int ret = 1;
	{
		MainWindow w(safeMode);
		QString error;
		if (!w.initialize(&error)) {
			Diagnostics::write("초기화 실패: " + error);
			QMessageBox::critical(nullptr, "ERShorts 초기화 실패",
					      error + "\n\nOBS 런타임 파일(data, obs-plugins)이 올바른 위치에 있는지 확인하세요."
						      "\n\n로그: " + Diagnostics::currentLogPath());
			Diagnostics::markCleanExit();
			return 1;
		}
		w.show();
		ret = app.exec();
	}
	Diagnostics::markCleanExit();
	return ret;
}
