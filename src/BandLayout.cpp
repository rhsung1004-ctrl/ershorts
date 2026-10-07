#include "BandLayout.h"

#include "EditProject.h"

#include <QFont>
#include <QFontMetrics>
#include <QStringList>

namespace BandLayout {

QString fontFamily() { return QStringLiteral("Malgun Gothic"); }

int fitSize(const QString &line, int size, int maxWidth)
{
	// 내보내기에 쓰는 맑은 고딕 Bold 기준으로 폭을 재서, 넘치면 글자 크기를 줄임
	QFont f(fontFamily());
	f.setBold(true);
	while (size > 24) {
		f.setPixelSize(size);
		if (QFontMetrics(f).horizontalAdvance(line) <= maxWidth)
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

static void addLines(QVector<BandLine> &group, const QString &text, int size, const QColor &color)
{
	for (const QString &raw : text.split('\n')) {
		const QString line = raw.trimmed();
		if (line.isEmpty())
			continue;
		BandLine l;
		l.text = line;
		l.size = fitSize(line, size);
		l.color = color;
		group.push_back(l);
	}
}

QVector<BandLine> lines(const EditProject &p)
{
	QVector<BandLine> out;
	const TitleBands &b = p.bands;

	QVector<BandLine> top;
	addLines(top, b.title, b.titleSize, b.titleColor);
	addLines(top, b.subtitle, b.subtitleSize, b.subtitleColor);
	stack(out, top, 0, b.topHeight);

	QVector<BandLine> bottom;
	addLines(bottom, b.bottomText, b.bottomSize, b.bottomColor);
	stack(out, bottom, 1920 - b.bottomHeight, b.bottomHeight);
	return out;
}

} // namespace BandLayout
