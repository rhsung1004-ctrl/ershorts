#pragma once

#include <QWidget>

struct EditProject;
class ThumbnailCache;

// 결과 영상 기준 타임라인
//  ─ 영상 줄: 인트로 | 구간들(클립별 색) | 아웃트로, 컷 앞의 전환 표시
//  ─ 음악 줄: BGM + 비트 위치(세로선)
//  ─ 자막 줄
// 조작: 클릭 = 이동/선택, 선택한 구간의 양 끝 드래그 = 길이 조절(비트에 자석처럼 붙음),
//       선택한 구간 가운데를 드래그 = 순서 바꾸기
class TimelineWidget : public QWidget {
	Q_OBJECT
public:
	explicit TimelineWidget(QWidget *parent = nullptr);

	void setProject(EditProject *p);
	void setPosition(double t);
	void setSelectedSegment(int i);
	void setSelectedSubtitle(int i);
	void setSnapToBeats(bool on) { m_snap = on; }
	void setThumbnails(ThumbnailCache *cache) { m_thumbs = cache; }

	// 확대/축소: 1 = 전체가 한 화면, 최대 64배. Ctrl+휠로 커서 위치 기준 확대, 휠로 좌우 이동
	void setZoom(double zoom, double anchorTime = -1.0);
	double zoom() const { return m_zoom; }
	void zoomToFit() { setZoom(1.0); }
	void setFollowPlayhead(bool on) { m_follow = on; } // 재생 중 화면이 재생 위치를 따라감
	int selectedSegment() const { return m_selSeg; }
	int selectedSubtitle() const { return m_selSub; }

	QSize sizeHint() const override { return {900, 146}; }
	QSize minimumSizeHint() const override { return {300, 146}; }

signals:
	void seekRequested(double t);
	void segmentSelected(int index);
	void subtitleSelected(int index);
	void cardClicked(int which); // 1 = 인트로, 2 = 아웃트로
	void segmentsEdited();
	void zoomChanged(double zoom);

protected:
	void paintEvent(QPaintEvent *) override;
	void mousePressEvent(QMouseEvent *e) override;
	void mouseMoveEvent(QMouseEvent *e) override;
	void mouseReleaseEvent(QMouseEvent *e) override;
	void wheelEvent(QWheelEvent *e) override;

private:
	enum class Drag { None, Playhead, SegIn, SegOut, Move };

	QRect rulerRect() const;
	QRect videoRect() const;
	QRect musicRect() const;
	QRect subRect() const;
	double xToTime(double x) const;
	double timeToX(double t) const;
	double fitDuration() const;     // 확대 1배일 때 화면에 보이는 길이
	double visibleDuration() const; // 지금 화면에 보이는 길이
	void clampView();
	int segmentAtX(double x) const;
	bool nearEdge(double x, bool *isIn) const;

	EditProject *m_p = nullptr;
	double m_pos = 0.0;
	int m_selSeg = -1;
	int m_selSub = -1;
	bool m_snap = true;
	Drag m_drag = Drag::None;
	double m_pressX = 0.0;
	double m_dragX = 0.0;
	double m_origIn = 0.0;
	double m_fixedViewDuration = 0.0; // 드래그 중 화면 배율 고정
	ThumbnailCache *m_thumbs = nullptr;
	double m_zoom = 1.0;
	double m_viewStart = 0.0; // 화면 왼쪽 끝의 시각
	bool m_follow = false;
	static constexpr int kMargin = 12;
};
