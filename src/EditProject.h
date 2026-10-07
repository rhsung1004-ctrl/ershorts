#pragma once

#include <QColor>
#include <QJsonObject>
#include <QRectF>
#include <QSize>
#include <QString>
#include <QStringList>
#include <QVector>

// ─────────────────────────────────────────────────────────────
// 매드무비 프로젝트: 여러 클립의 구간을 한 타임라인(결과 시간)에 이어 붙임
// ─────────────────────────────────────────────────────────────

struct SourceClip {
	QString path;
	QSize size{1920, 1080};
	double duration = 0.0; // 0 = 아직 모름 (플레이어가 로드하면 채워짐)
	bool hasAudio = true;
	QString name() const;
};

// 이전 컷 → 이 구간으로 넘어올 때의 전환
enum class Transition { None = 0, Flash, BlackDip, ZoomPunch, Glitch };
QString transitionName(Transition t);
constexpr int kTransitionCount = 5;

struct Segment {
	int source = 0;
	double in = 0.0;
	double out = -1.0; // -1 = 클립 끝까지 (길이를 알게 되면 확정)
	double speed = 1.0;
	Transition transIn = Transition::None;

	// 구간 효과
	bool zoom = false;     // 1.3배 확대
	bool shake = false;    // 화면 흔들림
	bool gray = false;     // 흑백
	bool vivid = false;    // 색감 강조 + 선명
	bool vignette = false; // 가장자리 어둡게

	double outDuration() const { return out > in ? (out - in) / speed : 0.0; }
	QStringList effectNames() const;
};

// 자막: 결과 영상 시간 기준
struct Subtitle {
	double start = 0.0;
	double end = 2.0;
	QString text;
	int fontSize = 72;
	QColor color = Qt::white;
	double y = 0.72; // 0 = 위, 1 = 아래 (글자 중심)
	bool box = true;
};

// 인트로/아웃트로 화면
struct TitleCard {
	bool enabled = false;
	double duration = 2.0;
	QString title;
	QString subtitle;
	QColor background = Qt::black;
	QColor color = Qt::white;
};

struct MusicTrack {
	QString path;            // 비어 있으면 BGM 없음
	double fileOffset = 0.0; // 음악 파일의 몇 초부터 쓸지
	double volume = 0.9;
	double bpm = 0.0;        // 0 = 비트 정보 없음
	double firstBeat = 0.0;  // 음악 파일 기준 첫 박 위치(초)
	int beatEvery = 1;       // 1 = 매 박, 2 = 2박마다, 4 = 마디마다
	bool fadeOut = true;
};

enum class ShortsLayout { CenterCrop = 0, CropWithMinimap = 1, BlurBackground = 2 };

struct EditProject {
	QString filePath; // 프로젝트 저장 위치 (.json)

	QVector<SourceClip> sources;
	QVector<Segment> segments; // 타임라인 순서
	QVector<Subtitle> subtitles;
	TitleCard intro;
	TitleCard outro;
	MusicTrack music;
	double gameVolume = 1.0;

	ShortsLayout layout = ShortsLayout::CenterCrop;
	QRectF minimapRect{0.80, 0.63, 0.19, 0.35};

	// 클립 추가 (전체 길이를 하나의 구간으로 타임라인 끝에 붙임)
	int addSource(const QString &path);
	void setSourceInfo(int index, double duration, QSize size, bool hasAudio);
	bool allSourcesReady() const;

	// 시간 계산 (결과 영상 기준)
	double introDuration() const { return intro.enabled ? intro.duration : 0.0; }
	double outroDuration() const { return outro.enabled ? outro.duration : 0.0; }
	double segmentStart(int i) const;
	double segmentsEnd() const { return segmentStart(int(segments.size())); }
	double totalDuration() const { return segmentsEnd() + outroDuration(); }

	struct Locate {
		enum Kind { End, Intro, Segment, Outro } kind = End;
		int seg = -1;
		double local = 0.0;   // 해당 구간/카드 안에서의 결과 시간
		double srcTime = 0.0; // 구간이면 원본 클립 시간
	};
	Locate locate(double t) const;

	bool splitAt(double t);
	void moveSegment(int from, int to);

	// 비트 (결과 영상 기준 시간)
	double beatInterval() const;
	QVector<double> beatTimes() const;
	double nearestBeat(double t) const; // 없으면 -1
	int snapCutsToBeats();             // 각 구간 끝을 가장 가까운 비트로, 바뀐 컷 수 반환

	QJsonObject toJson() const;
	void fromJson(const QJsonObject &o);
	bool save() const;
	bool load(const QString &path);
};
