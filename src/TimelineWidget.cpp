#include "TimelineWidget.h"

#include "EditProject.h"

#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>

#include <algorithm>
#include <cmath>

namespace {
const QColor kClipColors[] = {QColor("#3E63DD"), QColor("#2F9E6B"), QColor("#D6409F"),
			      QColor("#E5932E"), QColor("#12A594"), QColor("#8E4EC6"),
			      QColor("#E5484D"), QColor("#0090FF")};

QColor clipColor(int source) { return kClipColors[std::abs(source) % 8]; }

QString transitionIcon(Transition t)
{
	switch (t) {
	case Transition::Flash: return "⚡";
	case Transition::BlackDip: return "◐";
	case Transition::ZoomPunch: return "⤢";
	case Transition::Glitch: return "▦";
	default: return {};
	}
}
} // namespace

TimelineWidget::TimelineWidget(QWidget *parent) : QWidget(parent)
{
	setMouseTracking(true);
	setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
}

void TimelineWidget::setProject(EditProject *p)
{
	m_p = p;
	update();
}

void TimelineWidget::setPosition(double t)
{
	m_pos = t;
	update();
}

void TimelineWidget::setSelectedSegment(int i)
{
	m_selSeg = i;
	update();
}

void TimelineWidget::setSelectedSubtitle(int i)
{
	m_selSub = i;
	update();
}

QRect TimelineWidget::rulerRect() const { return {kMargin, 4, width() - 2 * kMargin, 18}; }
QRect TimelineWidget::videoRect() const { return {kMargin, 26, width() - 2 * kMargin, 48}; }
QRect TimelineWidget::musicRect() const { return {kMargin, 78, width() - 2 * kMargin, 20}; }
QRect TimelineWidget::subRect() const { return {kMargin, 102, width() - 2 * kMargin, 28}; }

double TimelineWidget::viewDuration() const
{
	if (m_fixedViewDuration > 0)
		return m_fixedViewDuration;
	return m_p ? std::max(5.0, m_p->totalDuration() * 1.08) : 5.0;
}

double TimelineWidget::xToTime(double x) const
{
	const QRect r = videoRect();
	return std::max(0.0, (x - r.left()) / r.width() * viewDuration());
}

double TimelineWidget::timeToX(double t) const
{
	const QRect r = videoRect();
	return r.left() + t / viewDuration() * r.width();
}

int TimelineWidget::segmentAtX(double x) const
{
	if (!m_p)
		return -1;
	const auto loc = m_p->locate(xToTime(x));
	return loc.kind == EditProject::Locate::Segment ? loc.seg : -1;
}

bool TimelineWidget::nearEdge(double x, bool *isIn) const
{
	if (!m_p || m_selSeg < 0 || m_selSeg >= m_p->segments.size())
		return false;
	const double s = m_p->segmentStart(m_selSeg);
	const double e = s + m_p->segments[m_selSeg].outDuration();
	if (std::abs(x - timeToX(e)) <= 7) {
		*isIn = false;
		return true;
	}
	if (std::abs(x - timeToX(s)) <= 7) {
		*isIn = true;
		return true;
	}
	return false;
}

void TimelineWidget::paintEvent(QPaintEvent *)
{
	QPainter p(this);
	p.setRenderHint(QPainter::Antialiasing);
	p.fillRect(rect(), QColor("#18181C"));

	if (!m_p || m_p->segments.isEmpty()) {
		p.setPen(QColor("#888"));
		p.drawText(rect(), Qt::AlignCenter, "클립을 추가하세요");
		return;
	}

	QFont small = font();
	small.setPixelSize(10);
	QFont label = font();
	label.setPixelSize(11);
	label.setBold(true);

	// ── 눈금 ─────────────────────
	const QRect ruler = rulerRect();
	const double pps = ruler.width() / viewDuration();
	double step = 0.5;
	for (double c : {0.5, 1.0, 2.0, 5.0, 10.0, 30.0, 60.0})
		if (c * pps >= 56) {
			step = c;
			break;
		}
	p.setFont(small);
	for (double t = 0; t <= viewDuration(); t += step) {
		const double x = timeToX(t);
		p.setPen(QColor("#555"));
		p.drawLine(QPointF(x, ruler.bottom() - 4), QPointF(x, ruler.bottom()));
		p.setPen(QColor("#9A9AA5"));
		p.drawText(QPointF(x + 3, ruler.bottom() - 5),
			   QString("%1:%2").arg(int(t) / 60).arg(int(t) % 60, 2, 10, QChar('0')));
	}

	// ── 비트 선 ───────────────────
	const QVector<double> beats = m_p->beatTimes();
	const double total = m_p->totalDuration();
	p.setPen(QPen(QColor(255, 210, 80, 70), 1));
	for (double b : beats) {
		if (b > viewDuration())
			break;
		const double x = timeToX(b);
		p.drawLine(QPointF(x, videoRect().top()), QPointF(x, musicRect().bottom()));
	}

	// ── 영상 줄 ───────────────────
	const QRect vr = videoRect();
	p.fillRect(vr, QColor("#202026"));
	p.setFont(label);

	auto drawCard = [&](double start, double dur, const QString &text) {
		QRectF r(timeToX(start), vr.top(), timeToX(start + dur) - timeToX(start), vr.height());
		r.adjust(1, 1, -1, -1);
		p.setPen(Qt::NoPen);
		p.setBrush(QColor("#3A3A44"));
		p.drawRoundedRect(r, 4, 4);
		p.setPen(QColor("#DDD"));
		p.drawText(r, Qt::AlignCenter, p.fontMetrics().elidedText(text, Qt::ElideRight, int(r.width()) - 4));
	};
	if (m_p->intro.enabled)
		drawCard(0, m_p->introDuration(), "인트로");

	for (int i = 0; i < m_p->segments.size(); ++i) {
		const Segment &s = m_p->segments[i];
		const double st = m_p->segmentStart(i);
		QRectF r(timeToX(st), vr.top(), timeToX(st + s.outDuration()) - timeToX(st), vr.height());
		r.adjust(1, 1, -1, -1);
		if (r.width() < 1)
			continue;

		QColor c = clipColor(s.source);
		if (m_drag == Drag::Move && i == m_selSeg)
			c.setAlpha(110);
		p.setPen(Qt::NoPen);
		p.setBrush(c);
		p.drawRoundedRect(r, 4, 4);

		QStringList parts;
		parts << QString("#%1").arg(s.source + 1);
		if (std::abs(s.speed - 1.0) > 0.01)
			parts << QString("%1x").arg(s.speed);
		parts << s.effectNames();
		p.setPen(Qt::white);
		p.drawText(r.adjusted(5, 2, -3, 0), Qt::AlignTop | Qt::AlignLeft,
			   p.fontMetrics().elidedText(parts.join(" · "), Qt::ElideRight, int(r.width()) - 8));

		const QString icon = transitionIcon(s.transIn);
		if (!icon.isEmpty()) {
			QRectF badge(r.left() - 7, r.bottom() - 18, 16, 16);
			p.setPen(Qt::NoPen);
			p.setBrush(QColor(0, 0, 0, 170));
			p.drawEllipse(badge);
			p.setPen(QColor("#FFE14D"));
			p.drawText(badge, Qt::AlignCenter, icon);
		}

		if (i == m_selSeg) {
			p.setPen(QPen(Qt::white, 2));
			p.setBrush(Qt::NoBrush);
			p.drawRoundedRect(r.adjusted(1, 1, -1, -1), 4, 4);
			p.setPen(Qt::NoPen);
			p.setBrush(Qt::white);
			p.drawRect(QRectF(r.left(), r.top() + 12, 4, r.height() - 24));
			p.drawRect(QRectF(r.right() - 4, r.top() + 12, 4, r.height() - 24));
		}
	}

	if (m_p->outro.enabled)
		drawCard(m_p->segmentsEnd(), m_p->outroDuration(), "아웃트로");

	// 순서 바꾸기 드래그 중: 놓을 위치 표시
	if (m_drag == Drag::Move) {
		const int target = segmentAtX(m_dragX);
		if (target >= 0 && target != m_selSeg) {
			const double st = m_p->segmentStart(target);
			const double x = target > m_selSeg ? timeToX(st + m_p->segments[target].outDuration())
							   : timeToX(st);
			p.setPen(QPen(QColor("#FFE14D"), 3));
			p.drawLine(QPointF(x, vr.top() - 2), QPointF(x, vr.bottom() + 2));
		}
	}

	// ── 음악 줄 ───────────────────
	const QRect mr = musicRect();
	p.fillRect(mr, QColor("#1E1E24"));
	p.setFont(small);
	if (!m_p->music.path.isEmpty()) {
		QRectF r(timeToX(0), mr.top() + 2, timeToX(total) - timeToX(0), mr.height() - 4);
		p.setPen(Qt::NoPen);
		p.setBrush(QColor("#2B6F5E"));
		p.drawRoundedRect(r, 3, 3);
		p.setPen(QColor("#CFF5E7"));
		QString txt = "♪ BGM";
		if (m_p->music.bpm > 0)
			txt += QString("  %1 BPM").arg(m_p->music.bpm, 0, 'f', 1);
		p.drawText(r.adjusted(6, 0, 0, 0), Qt::AlignVCenter | Qt::AlignLeft, txt);
		p.setPen(QPen(QColor("#FFD24F"), 2));
		for (double b : beats) {
			if (b > total)
				break;
			const double x = timeToX(b);
			p.drawLine(QPointF(x, mr.bottom() - 6), QPointF(x, mr.bottom() - 1));
		}
	} else {
		p.setPen(QColor("#666"));
		p.drawText(mr.adjusted(6, 0, 0, 0), Qt::AlignVCenter | Qt::AlignLeft, "♪ BGM 없음 (음악 탭에서 추가)");
	}

	// ── 자막 줄 ───────────────────
	const QRect tr = subRect();
	p.fillRect(tr, QColor("#202026"));
	p.setFont(label);
	for (int i = 0; i < m_p->subtitles.size(); ++i) {
		const Subtitle &s = m_p->subtitles[i];
		QRectF r(timeToX(s.start), tr.top() + 3, timeToX(s.end) - timeToX(s.start), tr.height() - 6);
		p.setPen(i == m_selSub ? QPen(Qt::white, 2) : Qt::NoPen);
		p.setBrush(QColor("#8E4EC6"));
		p.drawRoundedRect(r, 3, 3);
		p.setPen(Qt::white);
		p.drawText(r.adjusted(4, 0, -2, 0), Qt::AlignVCenter | Qt::AlignLeft,
			   p.fontMetrics().elidedText(s.text.simplified(), Qt::ElideRight, int(r.width()) - 8));
	}

	// ── 재생 위치 ─────────────────
	const double px = timeToX(m_pos);
	p.setPen(QPen(QColor("#FF4D4F"), 2));
	p.drawLine(QPointF(px, ruler.top()), QPointF(px, tr.bottom()));
	QPainterPath tri;
	tri.moveTo(px - 6, ruler.top());
	tri.lineTo(px + 6, ruler.top());
	tri.lineTo(px, ruler.top() + 8);
	tri.closeSubpath();
	p.fillPath(tri, QColor("#FF4D4F"));
}

void TimelineWidget::mousePressEvent(QMouseEvent *e)
{
	if (!m_p || e->button() != Qt::LeftButton)
		return;
	const QPointF pos = e->position();
	const double t = xToTime(pos.x());
	m_pressX = m_dragX = pos.x();
	m_fixedViewDuration = viewDuration();

	if (videoRect().contains(pos.toPoint())) {
		bool isIn = false;
		if (nearEdge(pos.x(), &isIn)) {
			m_drag = isIn ? Drag::SegIn : Drag::SegOut;
			m_origIn = m_p->segments[m_selSeg].in;
			return;
		}
		const auto loc = m_p->locate(t);
		if (loc.kind == EditProject::Locate::Segment) {
			if (loc.seg == m_selSeg) {
				m_drag = Drag::Move; // 선택된 구간을 다시 잡으면 순서 바꾸기
				emit seekRequested(t);
				return;
			}
			m_selSeg = loc.seg;
			emit segmentSelected(loc.seg);
		} else if (loc.kind == EditProject::Locate::Intro) {
			emit cardClicked(1);
		} else if (loc.kind == EditProject::Locate::Outro) {
			emit cardClicked(2);
		}
	} else if (subRect().contains(pos.toPoint())) {
		for (int i = int(m_p->subtitles.size()) - 1; i >= 0; --i) {
			const Subtitle &s = m_p->subtitles[i];
			if (t >= s.start && t <= s.end) {
				m_selSub = i;
				emit subtitleSelected(i);
				break;
			}
		}
	}

	m_drag = Drag::Playhead;
	emit seekRequested(t);
	update();
}

void TimelineWidget::mouseMoveEvent(QMouseEvent *e)
{
	if (!m_p)
		return;
	const QPointF pos = e->position();

	if (m_drag == Drag::None) {
		bool isIn = false;
		const bool edge = videoRect().contains(pos.toPoint()) && nearEdge(pos.x(), &isIn);
		setCursor(edge ? Qt::SizeHorCursor : Qt::ArrowCursor);
		return;
	}

	m_dragX = pos.x();
	double t = xToTime(pos.x());

	switch (m_drag) {
	case Drag::Playhead:
		emit seekRequested(t);
		break;

	case Drag::Move:
		setCursor(Qt::ClosedHandCursor);
		update();
		break;

	case Drag::SegOut: {
		Segment &s = m_p->segments[m_selSeg];
		const double start = m_p->segmentStart(m_selSeg);
		if (m_snap) {
			const double b = m_p->nearestBeat(t);
			if (b >= 0 && std::abs(timeToX(b) - pos.x()) <= 10)
				t = b; // 비트에 자석처럼 붙기
		}
		const double srcMax = m_p->sources.value(s.source).duration > 0 ? m_p->sources[s.source].duration
										   : s.out;
		s.out = std::clamp(s.in + (t - start) * s.speed, s.in + 0.2 * s.speed, srcMax);
		emit seekRequested(std::max(start, start + s.outDuration() - 0.03));
		update();
		break;
	}

	case Drag::SegIn: {
		Segment &s = m_p->segments[m_selSeg];
		const double start = m_p->segmentStart(m_selSeg);
		// 앞쪽 끝을 오른쪽으로 끌면 앞부분이 잘리고, 왼쪽으로 끌면 다시 늘어남
		s.in = std::clamp(m_origIn + (t - xToTime(m_pressX)) * s.speed, 0.0, s.out - 0.2 * s.speed);
		emit seekRequested(start);
		update();
		break;
	}

	case Drag::None:
		break;
	}
}

void TimelineWidget::mouseReleaseEvent(QMouseEvent *)
{
	if (m_drag == Drag::Move) {
		const int target = segmentAtX(m_dragX);
		if (target >= 0 && target != m_selSeg && std::abs(m_dragX - m_pressX) > 6) {
			m_p->moveSegment(m_selSeg, target);
			m_selSeg = target;
			emit segmentsEdited();
			emit segmentSelected(target);
		}
	} else if (m_drag == Drag::SegIn || m_drag == Drag::SegOut) {
		emit segmentsEdited();
	}
	m_drag = Drag::None;
	m_fixedViewDuration = 0.0;
	setCursor(Qt::ArrowCursor);
	update();
}
