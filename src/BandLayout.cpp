#include "BandLayout.h"

#include "EditProject.h"
#include "FontManager.h"

#include <QFont>
#include <QFontMetrics>
#include <QStringList>

namespace BandLayout {

int fitSize(const QString &line, int size, const QString &fontPath, int maxWidth)
{
	// 실제로 쓸 글꼴로 폭을 재서, 넘치면 글자 크기를 줄임
	while (size > 24) {
		if (QFontMetrics(FontManager::qfont(fontPath, size)).horizontalAdvance(line) <= maxWidth)
			break;
		size -= 2;
	}
	return size;
}

static void stack(QVector<BandLine> &out, QVector<BandLine> group, double bandTop, double bandHeight)
{
	if (group.isEmpty() || bandHeight <= 0)
		return;
	double total = 0;
	for (BandLine &l : group) {
		l.slotHeight = l.size * 1.3;
		total += l.slotHeight;
	}
	double y = bandTop + (bandHeight - total) / 2; // 띠 안에서 세로 가운데
	for (BandLine &l : group) {
		l.slotTop = y;
		y += l.slotHeight;
		out.push_back(l);
	}
}

static void addLines(QVector<BandLine> &group, const QString &text, int size, const QColor &color,
		     const QString &fontPath)
{
	for (const QString &raw : text.split('\n')) {
		const QString line = raw.trimmed();
		if (line.isEmpty())
			continue;
		BandLine l;
		l.text = line;
		l.size = fitSize(line, size, fontPath);
		l.color = color;
		l.fontPath = fontPath;
		group.push_back(l);
	}
}

QVector<BandLine> lines(const EditProject &p)
{
	QVector<BandLine> out;
	const TitleBands &b = p.bands;

	QVector<BandLine> top;
	addLines(top, b.title, b.titleSize, b.titleColor, b.titleFont);
	addLines(top, b.subtitle, b.subtitleSize, b.subtitleColor, b.subtitleFont);
	stack(out, top, 0, b.topH()); // 꺼진 띠(높이 0)의 글씨는 그리지 않음

	QVector<BandLine> bottom;
	addLines(bottom, b.bottomText, b.bottomSize, b.bottomColor, b.bottomFont);
	stack(out, bottom, 1920 - b.bottomH(), b.bottomH());
	return out;
}

} // namespace BandLayout
