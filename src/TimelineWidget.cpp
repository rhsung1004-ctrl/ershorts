#include "TimelineWidget.h"

#include "EditProject.h"
#include "ThumbnailCache.h"

#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QWheelEvent>

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
	// 확대 상태에서 재생 위치가 화면 밖으로 나가면 따라감
	if (m_follow && m_zoom > 1.0 && m_drag == Drag::None) {
		const double vis = visibleDuration();
		if (t < m_viewStart || t > m_viewStart + vis * 0.92) {
			m_viewStart = t - vis * 0.1;
			clampView();
		}
	}
	update();
}

void TimelineWidget::setZoom(double zoom, double anchorTime)
{
	zoom = std::clamp(zoom, 1.0, 64.0);
	if (anchorTime < 0)
		anchorTime = m_pos; // 기본: 재생 위치 기준
	const double frac = (anchorTime - m_viewStart) / visibleDuration();
	m_zoom = zoom;
	m_viewStart = anchorTime - std::clamp(frac, 0.0, 1.0) * visibleDuration();
	clampView();
	emit zoomChanged(m_zoom);
	update();
}

void TimelineWidget::clampView()
{
	m_viewStart = std::clamp(m_viewStart, 0.0, std::max(0.0, fitDuration() - visibleDuration()));
}

void TimelineWidget::wheelEvent(QWheelEvent *e)
{
	const double steps = e->angleDelta().y() / 120.0;
	if (e->modifiers() & Qt::ControlModifier) {
		setZoom(m_zoom * std::pow(1.25, steps), xToTime(e->position().x()));
	} else if (m_zoom > 1.0) {
		const double dx = (e->angleDelta().x() != 0 ? e->angleDelta().x() : e->angleDelta().y()) / 120.0;
		m_viewStart -= dx * visibleDuration() * 0.15;
		clampView();
		update();
	} else {
		e->ignore();
		return;
	}
	e->accept();
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
QRect TimelineWidget::videoRect() const { return {kMargin, 26, width() - 2 * kMargin, 58}; }
QRect TimelineWidget::musicRect() const { return {kMargin, 88, width() - 2 * kMargin, 20}; }
QRect TimelineWidget::subRect() const { return {kMargin, 112, width() - 2 * kMargin, 28}; }

double TimelineWidget::fitDuration() const
{
	if (m_fixedViewDuration > 0)
		return m_fixedViewDuration;
	return m_p ? std::max(5.0, m_p->totalDuration() * 1.08) : 5.0;
}

double TimelineWidget::visibleDuration() const { return fitDuration() / m_zoom; }

double TimelineWidget::xToTime(double x) const
{
	const QRect r = videoRect();
	return std::max(0.0, m_viewStart + (x - r.left()) / r.width() * visibleDuration());
}

double TimelineWidget::timeToX(double t) const
{
	const QRect r = videoRect();
	return r.left() + (t - m_viewStart) / visibleDuration() * r.width();
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

	// 확대했을 때 화면 밖 부분이 여백에 그려지지 않도록 트랙 영역으로 자름
	p.setClipRect(QRect(kMargin - 8, 0, width() - 2 * kMargin + 16, height()));
	const double viewEnd = m_viewStart + visibleDuration();

	// ── 눈금 ─────────────────────
	const QRect ruler = rulerRect();
	const double pps = ruler.width() / visibleDuration();
	double step = 60.0;
	for (double c : {0.1, 0.25, 0.5, 1.0, 2.0, 5.0, 10.0, 30.0, 60.0})
		if (c * pps >= 56) {
			step = c;
			break;
		}
	p.setFont(small);
	for (double t = std::floor(m_viewStart / step) * step; t <= viewEnd; t += step) {
		if (t < -1e-9)
			continue;
		const double x = timeToX(t);
		p.setPen(QColor("#555"));
		p.drawLine(QPointF(x, ruler.bottom() - 4), QPointF(x, ruler.bottom()));
		p.setPen(QColor("#9A9AA5"));
		const int m = int(t + 1e-6) / 60;
		const double sec = t - m * 60;
		const QString lbl = step < 1.0 ? QString("%1:%2").arg(m).arg(sec, 4, 'f', step < 0.25 ? 1 : 2, QChar('0'))
					       : QString("%1:%2").arg(m).arg(int(sec + 1e-6), 2, 10, QChar('0'));
		p.drawText(QPointF(x + 3, ruler.bottom() - 5), lbl);
	}

	// ── 비트 선 ───────────────────
	const QVector<double> beats = m_p->beatTimes();
	const double total = m_p->totalDuration();
	p.setPen(QPen(QColor(255, 210, 80, 70), 1));
	for (double b : beats) {
		if (b < m_viewStart)
			continue;
		if (b > viewEnd)
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

	// 앞부분을 자르는 중에는 선택 구간의 뒤쪽 끝(과 그 뒤 구간들)을 제자리에 두고 앞쪽만 움직여 보여 줌
	// → 손을 놓으면 앞으로 당겨짐
	const bool trimmingIn = (m_drag == Drag::SegIn && m_selSeg >= 0 && m_selSeg < m_p->segments.size());
	if (trimmingIn && m_trimShift > 1e-4) { // 잘려 나가는 부분
		const double os = m_p->segmentStart(m_selSeg);
		QRectF cut(timeToX(os), vr.top() + 1, timeToX(os + m_trimShift) - timeToX(os), vr.height() - 2);
		p.setPen(Qt::NoPen);
		p.setBrush(QColor(229, 72, 77, 70));
		p.drawRoundedRect(cut, 4, 4);
		p.setBrush(QBrush(QColor(229, 72, 77, 160), Qt::BDiagPattern));
		p.drawRoundedRect(cut, 4, 4);
		if (cut.width() > 30) {
			p.setPen(QColor("#FFB4B6"));
			p.drawText(cut, Qt::AlignCenter, cut.width() > 70 ? "✂ 잘림" : "✂");
		}
	}

	for (int i = 0; i < m_p->segments.size(); ++i) {
		const Segment &s = m_p->segments[i];
		const double st = m_p->segmentStart(i) + (trimmingIn && i >= m_selSeg ? m_trimShift : 0.0);
		QRectF r(timeToX(st), vr.top(), timeToX(st + s.outDuration()) - timeToX(st), vr.height());
		r.adjust(1, 1, -1, -1);
		if (r.width() < 1)
			continue;

		if (r.right() < 0 || r.left() > width())
			continue; // 화면 밖

		QColor c = clipColor(s.source);
		if (m_drag == Drag::Move && i == m_selSeg)
			c.setAlpha(110);
		p.setPen(Qt::NoPen);
		p.setBrush(c);
		p.drawRoundedRect(r, 4, 4);

		// 장면 썸네일: 구간 안을 칸으로 나눠 칸 가운데 시점의 장면을 그림
		const QString path = m_p->sources.value(s.source).path;
		if (m_thumbs && !path.isEmpty()) {
			const QRectF inner = r.adjusted(2, 2, -2, -6);
			const double tileW = inner.height() * 16.0 / 9.0;
			p.save();
			QPainterPath clipPath;
			clipPath.addRoundedRect(inner, 3, 3);
			p.setClipPath(clipPath, Qt::IntersectClip);
			const double x0 = std::max(inner.left(), -tileW);
			const double firstX = inner.left() + std::floor((x0 - inner.left()) / tileW) * tileW;
			for (double x = firstX; x < std::min(inner.right(), double(width())); x += tileW) {
				const double tLocal = xToTime(x + tileW / 2) - st;
				const double src = s.in + s.srcAt(std::clamp(tLocal, 0.0, s.outDuration()));
				const QImage img = m_thumbs->frameAt(path, src);
				if (!img.isNull())
					p.drawImage(QRectF(x, inner.top(), tileW, inner.height()), img);
			}
			p.fillRect(inner, QColor(0, 0, 0, 60)); // 글자가 잘 보이도록 살짝 어둡게
			p.restore();
		}

		if (s.freeze > 0) { // 멈춤 구간: 끝부분을 밝게 + 눈송이
			const double fx = timeToX(st + s.movingDuration());
			QRectF fr(std::max(fx, r.left()), r.top(), r.right() - std::max(fx, r.left()), r.height());
			p.fillRect(fr, QColor(255, 255, 255, 70));
			p.setPen(QColor("#E8F4FF"));
			if (fr.width() > 14)
				p.drawText(fr, Qt::AlignCenter, fr.width() > 44 ? "❄ 멈춤" : "❄");
		}

		QStringList parts;
		parts << QString("#%1").arg(s.source + 1);
		if (std::abs(s.speed - 1.0) > 0.01)
			parts << QString("%1x").arg(s.speed);
		parts << s.effectNames();
		const QString segText = p.fontMetrics().elidedText(parts.join(" · "), Qt::ElideRight, int(r.width()) - 8);
		if (!segText.isEmpty()) {
			const QRectF tb(r.left() + 3, r.top() + 2, p.fontMetrics().horizontalAdvance(segText) + 6, 15);
			p.fillRect(tb.intersected(r), QColor(c.red(), c.green(), c.blue(), 220));
			p.setPen(Qt::white);
			p.drawText(r.adjusted(5, 2, -3, 0), Qt::AlignTop | Qt::AlignLeft, segText);
		}

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

	// 효과음 위치 (음악 줄 위 주황색 표시)
	for (const SoundFx &f : m_p->sfx) {
		const double x = timeToX(f.start);
		if (x < kMargin - 8 || x > width())
			continue;
		p.setPen(QPen(QColor("#FF8A3D"), 2));
		p.drawLine(QPointF(x, mr.top()), QPointF(x, mr.bottom()));
		QPainterPath tri;
		tri.moveTo(x - 5, mr.top());
		tri.lineTo(x + 5, mr.top());
		tri.lineTo(x, mr.top() + 7);
		tri.closeSubpath();
		p.fillPath(tri, QColor("#FF8A3D"));
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
	const double px = timeToX(m_pos + (trimmingIn && m_pos >= m_p->segmentStart(m_selSeg) - 1e-6 ? m_trimShift : 0.0));
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
	m_fixedViewDuration = fitDuration();

	if (videoRect().contains(pos.toPoint())) {
		bool isIn = false;
		if (nearEdge(pos.x(), &isIn)) {
			m_drag = isIn ? Drag::SegIn : Drag::SegOut;
			m_origIn = m_p->segments[m_selSeg].in;
			m_origOutDur = m_p->segments[m_selSeg].outDuration();
			m_trimShift = 0.0;
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
		// 속도 램프가 있어도 "끈 위치 = 구간 끝"이 되도록 원본 길이를 역산
		const double len = s.srcLengthForOutDuration(std::max(0.1, t - start), srcMax - s.in);
		s.out = std::clamp(s.in + len, s.in + 0.1, srcMax);
		emit seekRequested(std::max(start, start + s.outDuration() - 0.03));
		update();
		break;
	}

	case Drag::SegIn: {
		Segment &s = m_p->segments[m_selSeg];
		const double start = m_p->segmentStart(m_selSeg);
		// 앞쪽 끝을 오른쪽으로 끌면 앞부분이 잘리고, 왼쪽으로 끌면 다시 늘어남
		s.in = std::clamp(m_origIn + (t - xToTime(m_pressX)) * s.speed, 0.0, s.out - 0.2 * s.speed);
		m_trimShift = m_origOutDur - s.outDuration(); // 뒤쪽 끝은 그대로 보이게
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
	m_trimShift = 0.0;
	m_fixedViewDuration = 0.0;
	setCursor(Qt::ArrowCursor);
	update();
}
