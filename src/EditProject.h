#pragma once

#include <QColor>
#include <QJsonObject>
#include <QRect>
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

	// 속도 램프: 1x 에서 speed 로 부드럽게 들어가고(rampIn) / speed 에서 1x 로 빠져나옴(rampOut)
	bool rampIn = false;
	bool rampOut = false;
	double rampLen = 0.5; // 램프 구간 길이 (원본 영상 기준 초)

	// ── 시간 변환 (x = 구간 시작부터의 원본 시간, t = 구간 시작부터의 결과 시간) ──
	double srcLength() const { return out > in ? out - in : 0.0; }
	bool hasRamp() const;
	void rampLengths(double *ra, double *rb) const;
	double srcToOut(double x) const;
	double outToSrc(double t) const;
	double speedAt(double x) const;
	double outDuration() const { return srcToOut(srcLength()); }
	// 결과 길이가 target 이 되도록 하는 원본 길이 (길이 조절/비트 맞춤용)
	double srcLengthForOutDuration(double target, double maxSrc) const;

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
	QString font;    // 글꼴 파일 경로 (빈 문자열 = 기본 맑은 고딕 Bold)
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

// 비트마다 화면이 툭 튀는 효과 (인트로/아웃트로 제외, 영상 구간에만)
struct BeatFx {
	bool zoom = false;  // 줌 펄스
	bool shake = false; // 흔들림
	int strength = 1;   // 0 = 약하게, 1 = 보통, 2 = 강하게
	int every = 1;      // 1 = 매 박, 2 = 2박마다, 4 = 마디마다
	bool enabled() const { return zoom || shake; }
	double zoomAmount() const { return strength == 0 ? 0.04 : strength == 1 ? 0.08 : 0.14; }
	double shakeAmount() const { return strength == 0 ? 0.01 : strength == 1 ? 0.02 : 0.035; }
	static constexpr double kDecay = 0.12; // 펄스가 사라지는 속도(초)
};

enum class ShortsLayout { CenterCrop = 0, CropWithMinimap = 1, BlurBackground = 2, TitleBands = 3 };

// "제목 띠" 레이아웃: 위/아래에 단색 띠를 두고 글씨를 넣음, 가운데는 게임 화면
struct TitleBands {
	int topHeight = 330;    // 1080x1920 캔버스 기준 px
	int bottomHeight = 330;
	QColor background = Qt::black;

	QString title;          // 위 띠 큰 글씨 (여러 줄 가능)
	int titleSize = 110;
	QColor titleColor = Qt::white;
	QString subtitle;       // 위 띠 작은 글씨
	int subtitleSize = 60;
	QColor subtitleColor = QColor("#FFE14D");
	QString bottomText;     // 아래 띠 글씨
	int bottomSize = 64;
	QColor bottomColor = Qt::white;
	QString titleFont;      // 글꼴 파일 경로 (빈 문자열 = 기본)
	QString subtitleFont;
	QString bottomFont;

	double zoom = 1.0;      // 가운데 영상 확대 (1.0 ~ 2.0)
	double offsetY = 0.0;   // 가운데 영상 세로 위치 (-1 = 위쪽, 0 = 가운데, 1 = 아래쪽)

	int middleHeight() const { return 1920 - topHeight - bottomHeight; }
};

struct EditProject {
	QString filePath; // 프로젝트 저장 위치 (.json)

	QVector<SourceClip> sources;
	QVector<Segment> segments; // 타임라인 순서
	QVector<Subtitle> subtitles;
	TitleCard intro;
	TitleCard outro;
	MusicTrack music;
	BeatFx beatFx;
	double gameVolume = 1.0;

	ShortsLayout layout = ShortsLayout::TitleBands;
	TitleBands bands;
	// 제목 띠 레이아웃에서 원본 영상 중 가운데 영역으로 쓸 부분 (원본 픽셀, 짝수)
	QRect bandCropRect(QSize source) const;
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

	// 비트 효과: 펄스 주기/첫 펄스 시각(결과 시간). 꺼져 있거나 BPM이 없으면 false
	bool beatPulseGrid(double *period, double *first) const;
	// 시각 t 의 펄스 세기(0~1)와 마지막 펄스 이후 경과 시간
	double beatPulseAt(double t, double *phase = nullptr) const;

	QJsonObject toJson() const;
	void fromJson(const QJsonObject &o);
	bool save() const;
	bool load(const QString &path);
};
