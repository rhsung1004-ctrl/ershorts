#pragma once

#include <QColor>
#include <QString>
#include <QVector>

struct EditProject;

// 제목 띠 글씨 배치 (미리보기와 내보내기가 똑같이 쓰도록 한 곳에서 계산)
struct BandLine {
	QString text;
	int size = 60;    // 픽셀 (긴 줄은 화면 폭에 맞게 자동으로 줄어듦)
	QColor color;
	double slotTop = 0; // 1080x1920 캔버스에서 이 줄이 차지하는 칸의 위쪽
	double slotHeight = 0;
};

namespace BandLayout {
QVector<BandLine> lines(const EditProject &p);
int fitSize(const QString &line, int size, int maxWidth = 1000);
QString fontFamily(); // "Malgun Gothic"
}
