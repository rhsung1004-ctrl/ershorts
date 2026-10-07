#include "EditProject.h"

#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>

#include <algorithm>
#include <cmath>

QString SourceClip::name() const { return QFileInfo(path).completeBaseName(); }

QString transitionName(Transition t)
{
	switch (t) {
	case Transition::None: return "없음 (바로 컷)";
	case Transition::Flash: return "화이트 플래시";
	case Transition::BlackDip: return "블랙 페이드";
	case Transition::ZoomPunch: return "줌 펀치";
	case Transition::Glitch: return "글리치";
	}
	return {};
}

// ─── 속도 램프 계산 ──────────────────────────────────────
// 램프 구간에서는 속도가 직선으로 변함: v(x) = 1 + (S-1)·x/Ra
// 결과 시간 = ∫ dx / v(x) = Ra/(S-1) · ln(v)  (닫힌 식이라 ffmpeg setpts 수식으로도 그대로 씀)
bool Segment::hasRamp() const
{
	double ra = 0, rb = 0;
	rampLengths(&ra, &rb);
	return ra > 0 || rb > 0;
}

void Segment::rampLengths(double *ra, double *rb) const
{
	*ra = *rb = 0.0;
	if (std::abs(speed - 1.0) < 1e-3)
		return;
	const double L = srcLength();
	const double lim = (rampIn && rampOut) ? L / 2 : L;
	if (rampIn)
		*ra = std::min(rampLen, lim);
	if (rampOut)
		*rb = std::min(rampLen, lim);
}

double Segment::srcToOut(double x) const
{
	const double L = srcLength();
	const double S = speed;
	x = std::clamp(x, 0.0, L);
	double Ra, Rb;
	rampLengths(&Ra, &Rb);
	if (Ra <= 0 && Rb <= 0)
		return x / S;

	const double TA = Ra > 0 ? Ra / (S - 1) * std::log(S) : 0.0;
	if (x < Ra)
		return Ra / (S - 1) * std::log(1 + (S - 1) * x / Ra);
	const double H = L - Ra - Rb;
	if (x < L - Rb || Rb <= 0)
		return TA + (x - Ra) / S;
	const double y = x - (L - Rb);
	return TA + H / S + Rb / (1 - S) * std::log((S + (1 - S) * y / Rb) / S);
}

double Segment::outToSrc(double t) const
{
	const double L = srcLength();
	const double S = speed;
	if (t <= 0)
		return 0.0;
	double Ra, Rb;
	rampLengths(&Ra, &Rb);
	if (Ra <= 0 && Rb <= 0)
		return std::min(L, t * S);

	const double TA = Ra > 0 ? Ra / (S - 1) * std::log(S) : 0.0;
	const double H = L - Ra - Rb;
	if (t < TA)
		return Ra / (S - 1) * (std::exp(t * (S - 1) / Ra) - 1);
	if (t < TA + H / S || Rb <= 0)
		return std::min(L, Ra + (t - TA) * S);
	const double v = S * std::exp((t - TA - H / S) * (1 - S) / Rb);
	return std::min(L, L - Rb + (v - S) * Rb / (1 - S));
}

double Segment::speedAt(double x) const
{
	double Ra, Rb;
	rampLengths(&Ra, &Rb);
	const double L = srcLength();
	if (Ra > 0 && x < Ra)
		return 1 + (speed - 1) * std::max(0.0, x) / Ra;
	if (Rb > 0 && x > L - Rb)
		return speed + (1 - speed) * std::min(Rb, x - (L - Rb)) / Rb;
	return speed;
}

double Segment::srcLengthForOutDuration(double target, double maxSrc) const
{
	// 결과 길이는 원본 길이에 대해 증가함수 → 이분 탐색
	Segment probe = *this;
	double lo = 0.05, hi = std::max(lo, maxSrc);
	probe.out = probe.in + hi;
	if (probe.outDuration() <= target)
		return hi;
	for (int i = 0; i < 60; ++i) {
		const double mid = (lo + hi) / 2;
		probe.out = probe.in + mid;
		(probe.outDuration() < target ? lo : hi) = mid;
	}
	return (lo + hi) / 2;
}

QStringList Segment::effectNames() const
{
	QStringList n;
	if (zoom) n << "줌인";
	if (shake) n << "흔들림";
	if (gray) n << "흑백";
	if (vivid) n << "선명";
	if (vignette) n << "비네팅";
	if (hasRamp())
		n << QStringLiteral("램프") + (rampIn ? QStringLiteral("↘") : QString()) + (rampOut ? QStringLiteral("↗") : QString());
	return n;
}

// ─── 클립 ───────────────────────────────────────────────
int EditProject::addSource(const QString &path)
{
	SourceClip c;
	c.path = path;
	sources.push_back(c);
	const int idx = int(sources.size()) - 1;

	Segment s;
	s.source = idx;
	s.in = 0.0;
	s.out = -1.0;
	if (!segments.isEmpty())
		s.transIn = Transition::Flash; // 매드무비 기본: 컷마다 플래시
	segments.push_back(s);
	return idx;
}

void EditProject::setSourceInfo(int index, double duration, QSize size, bool hasAudio)
{
	if (index < 0 || index >= sources.size())
		return;
	SourceClip &c = sources[index];
	if (duration > 0)
		c.duration = duration;
	if (size.isValid() && size.width() > 0)
		c.size = size;
	c.hasAudio = hasAudio;

	if (c.duration <= 0)
		return;
	for (Segment &s : segments) {
		if (s.source != index)
			continue;
		if (s.out < 0 || s.out > c.duration)
			s.out = c.duration;
		s.in = std::clamp(s.in, 0.0, std::max(0.0, s.out - 0.1));
	}
}

bool EditProject::allSourcesReady() const
{
	for (const Segment &s : segments)
		if (s.out <= s.in)
			return false;
	return true;
}

// ─── 시간 ───────────────────────────────────────────────
double EditProject::segmentStart(int i) const
{
	double t = introDuration();
	for (int k = 0; k < i && k < segments.size(); ++k)
		t += segments[k].outDuration();
	return t;
}

EditProject::Locate EditProject::locate(double t) const
{
	Locate r;
	if (t < 0)
		t = 0;
	const double introD = introDuration();
	if (t < introD) {
		r.kind = Locate::Intro;
		r.local = t;
		return r;
	}
	double acc = introD;
	for (int i = 0; i < segments.size(); ++i) {
		const double d = segments[i].outDuration();
		if (d > 0 && t < acc + d) {
			r.kind = Locate::Segment;
			r.seg = i;
			r.local = t - acc;
			r.srcTime = segments[i].in + segments[i].outToSrc(r.local);
			return r;
		}
		acc += d;
	}
	if (t < acc + outroDuration()) {
		r.kind = Locate::Outro;
		r.local = t - acc;
	}
	return r;
}

bool EditProject::splitAt(double t)
{
	const Locate loc = locate(t);
	if (loc.kind != Locate::Segment)
		return false;
	Segment &s = segments[loc.seg];
	if (loc.srcTime - s.in < 0.1 || s.out - loc.srcTime < 0.1)
		return false;
	Segment right = s;
	right.in = loc.srcTime;
	right.transIn = Transition::None;
	right.rampIn = false; // 램프는 바깥쪽 끝에만 남김
	s.out = loc.srcTime;
	s.rampOut = false;
	segments.insert(loc.seg + 1, right);
	return true;
}

void EditProject::moveSegment(int from, int to)
{
	if (from < 0 || from >= segments.size() || to < 0 || to >= segments.size() || from == to)
		return;
	segments.move(from, to);
}

// ─── 비트 ───────────────────────────────────────────────
double EditProject::beatInterval() const
{
	if (music.path.isEmpty() || music.bpm <= 0)
		return 0.0;
	return 60.0 / music.bpm * std::max(1, music.beatEvery);
}

QVector<double> EditProject::beatTimes() const
{
	QVector<double> beats;
	const double iv = beatInterval();
	if (iv <= 0)
		return beats;
	// 결과 시간 t 에서 재생되는 음악 위치 = fileOffset + t
	const double limit = totalDuration() + 30.0;
	double t = music.firstBeat - music.fileOffset;
	if (t < 0)
		t += std::ceil(-t / iv) * iv;
	for (; t <= limit && beats.size() < 5000; t += iv)
		beats.push_back(t);
	return beats;
}

double EditProject::nearestBeat(double t) const
{
	double best = -1.0;
	for (double b : beatTimes())
		if (best < 0 || std::abs(b - t) < std::abs(best - t))
			best = b;
	return best;
}

int EditProject::snapCutsToBeats()
{
	const QVector<double> beats = beatTimes();
	if (beats.isEmpty())
		return 0;

	int changed = 0;
	for (int i = 0; i < segments.size(); ++i) {
		Segment &s = segments[i];
		if (s.out <= s.in)
			continue;
		const double start = segmentStart(i);
		const double end = start + s.outDuration();
		const double srcMax = sources.value(s.source).duration > 0 ? sources[s.source].duration : s.out;

		const double maxSrc = srcMax - s.in;
		Segment full = s;
		full.out = srcMax;
		const double maxOutDur = full.outDuration();

		double best = -1.0;
		for (double b : beats) {
			if (b - start < 0.25) // 너무 짧은 컷 방지
				continue;
			if (b - start > maxOutDur + 1e-6)
				break;
			if (best < 0 || std::abs(b - end) < std::abs(best - end))
				best = b;
		}
		if (best >= 0 && std::abs(best - end) > 0.005) {
			s.out = s.in + s.srcLengthForOutDuration(best - start, maxSrc);
			++changed;
		}
	}
	return changed;
}

bool EditProject::beatPulseGrid(double *period, double *first) const
{
	if (!beatFx.enabled() || music.path.isEmpty() || music.bpm <= 0)
		return false;
	*period = 60.0 / music.bpm * std::max(1, beatFx.every);
	double t = music.firstBeat - music.fileOffset;
	if (t < 0)
		t += std::ceil(-t / *period) * *period;
	*first = t;
	return true;
}

double EditProject::beatPulseAt(double t, double *phase) const
{
	double P, O;
	if (!beatPulseGrid(&P, &O) || t < O || t < introDuration() || t >= segmentsEnd())
		return 0.0;
	const double ph = std::fmod(t - O, P);
	if (phase)
		*phase = ph;
	return std::exp(-ph / BeatFx::kDecay);
}

QRect EditProject::bandCropRect(QSize source) const
{
	const double W = std::max(2, source.width());
	const double H = std::max(2, source.height());
	const double ar = 1080.0 / std::max(2, bands.middleHeight()); // 가운데 영역의 가로/세로 비율
	const double z = std::clamp(bands.zoom, 1.0, 3.0);
	double cw = std::min(W, H * ar) / z;
	double ch = cw / ar;
	if (ch > H) { // 띠가 얇아 가운데가 원본보다 세로로 길면 세로 기준으로 맞춤
		ch = H / z;
		cw = ch * ar;
	}
	const int iw = std::max(2, int(cw) & ~1);
	const int ih = std::max(2, int(ch) & ~1);
	const int x = int((W - iw) / 2) & ~1;
	const double free = (H - ih) / 2;
	const int y = std::clamp(int(free * (1.0 + std::clamp(bands.offsetY, -1.0, 1.0))) & ~1, 0, int(H) - ih);
	return QRect(x, y, iw, ih);
}

// ─── 저장/불러오기 ─────────────────────────────────────
static QJsonObject cardToJson(const TitleCard &c)
{
	return QJsonObject{{"enabled", c.enabled}, {"duration", c.duration}, {"title", c.title},
			   {"subtitle", c.subtitle}, {"background", c.background.name()},
			   {"color", c.color.name()}};
}

static TitleCard cardFromJson(const QJsonObject &o)
{
	TitleCard c;
	c.enabled = o.value("enabled").toBool();
	c.duration = std::clamp(o.value("duration").toDouble(2.0), 0.5, 10.0);
	c.title = o.value("title").toString();
	c.subtitle = o.value("subtitle").toString();
	c.background = QColor(o.value("background").toString("#000000"));
	c.color = QColor(o.value("color").toString("#ffffff"));
	return c;
}

// ── 자막 모양 ──
QJsonObject Subtitle::styleToJson() const
{
	return QJsonObject{{"fontSize", fontSize}, {"color", color.name()}, {"y", y}, {"box", box}, {"font", font}};
}

void Subtitle::styleFromJson(const QJsonObject &j)
{
	fontSize = std::clamp(j.value("fontSize").toInt(72), 12, 300);
	color = QColor(j.value("color").toString("#ffffff"));
	y = std::clamp(j.value("y").toDouble(0.72), 0.0, 1.0);
	box = j.value("box").toBool(true);
	font = j.value("font").toString();
}

void Subtitle::copyStyleFrom(const Subtitle &o)
{
	fontSize = o.fontSize;
	color = o.color;
	y = o.y;
	box = o.box;
	font = o.font;
}

// ── 제목 띠 ──
QJsonObject TitleBands::toJson(bool withText) const
{
	QJsonObject o{{"top", topHeight},
		      {"bottom", bottomHeight},
		      {"background", background.name()},
		      {"titleSize", titleSize},
		      {"titleColor", titleColor.name()},
		      {"subtitleSize", subtitleSize},
		      {"subtitleColor", subtitleColor.name()},
		      {"bottomSize", bottomSize},
		      {"bottomColor", bottomColor.name()},
		      {"zoom", zoom},
		      {"offsetY", offsetY},
		      {"titleFont", titleFont},
		      {"subtitleFont", subtitleFont},
		      {"bottomFont", bottomFont}};
	if (withText) {
		o["title"] = title;
		o["subtitle"] = subtitle;
		o["bottomText"] = bottomText;
	}
	return o;
}

void TitleBands::fromJson(const QJsonObject &b, bool withText)
{
	const TitleBands def;
	topHeight = std::clamp(b.value("top").toInt(def.topHeight), 0, 800) & ~1;
	bottomHeight = std::clamp(b.value("bottom").toInt(def.bottomHeight), 0, 800) & ~1;
	background = QColor(b.value("background").toString(def.background.name()));
	titleSize = std::clamp(b.value("titleSize").toInt(def.titleSize), 20, 220);
	titleColor = QColor(b.value("titleColor").toString(def.titleColor.name()));
	subtitleSize = std::clamp(b.value("subtitleSize").toInt(def.subtitleSize), 20, 160);
	subtitleColor = QColor(b.value("subtitleColor").toString(def.subtitleColor.name()));
	bottomSize = std::clamp(b.value("bottomSize").toInt(def.bottomSize), 20, 160);
	bottomColor = QColor(b.value("bottomColor").toString(def.bottomColor.name()));
	zoom = std::clamp(b.value("zoom").toDouble(1.0), 1.0, 3.0);
	offsetY = std::clamp(b.value("offsetY").toDouble(0.0), -1.0, 1.0);
	titleFont = b.value("titleFont").toString();
	subtitleFont = b.value("subtitleFont").toString();
	bottomFont = b.value("bottomFont").toString();
	if (withText) {
		title = b.value("title").toString();
		subtitle = b.value("subtitle").toString();
		bottomText = b.value("bottomText").toString();
	}
}

QJsonObject EditProject::toJson() const
{
	QJsonArray src;
	for (const SourceClip &c : sources)
		src.append(QJsonObject{{"path", c.path}, {"duration", c.duration}, {"w", c.size.width()},
				       {"h", c.size.height()}, {"hasAudio", c.hasAudio}});

	QJsonArray segs;
	for (const Segment &s : segments)
		segs.append(QJsonObject{
			{"source", s.source}, {"in", s.in}, {"out", s.out}, {"speed", s.speed},
			{"transIn", int(s.transIn)}, {"zoom", s.zoom}, {"shake", s.shake}, {"gray", s.gray},
			{"vivid", s.vivid}, {"vignette", s.vignette}, {"rampIn", s.rampIn},
			{"rampOut", s.rampOut}, {"rampLen", s.rampLen}});

	QJsonArray subs;
	for (const Subtitle &s : subtitles)
		subs.append(QJsonObject{{"start", s.start}, {"end", s.end}, {"text", s.text},
					{"fontSize", s.fontSize}, {"color", s.color.name()}, {"y", s.y},
					{"box", s.box}, {"font", s.font}});

	const QJsonObject mus{{"path", music.path},       {"fileOffset", music.fileOffset},
			      {"volume", music.volume},   {"bpm", music.bpm},
			      {"firstBeat", music.firstBeat}, {"beatEvery", music.beatEvery},
			      {"fadeOut", music.fadeOut},
			      {"duck", music.duck},           {"duckStrength", music.duckStrength}};

	return QJsonObject{
		{"version", 2},
		{"sources", src},
		{"segments", segs},
		{"subtitles", subs},
		{"intro", cardToJson(intro)},
		{"outro", cardToJson(outro)},
		{"music", mus},
		{"gameVolume", gameVolume},
		{"beatFx", QJsonObject{{"zoom", beatFx.zoom}, {"shake", beatFx.shake},
				       {"strength", beatFx.strength}, {"every", beatFx.every}}},
		{"layout", int(layout)},
		{"bands", bands.toJson()},
		{"subStyle", subStyle.styleToJson()},
		{"minimap", QJsonArray{minimapRect.x(), minimapRect.y(), minimapRect.width(), minimapRect.height()}},
	};
}

void EditProject::fromJson(const QJsonObject &o)
{
	sources.clear();
	for (const QJsonValue &v : o.value("sources").toArray()) {
		const QJsonObject j = v.toObject();
		SourceClip c;
		c.path = j.value("path").toString();
		c.duration = j.value("duration").toDouble();
		c.size = QSize(j.value("w").toInt(1920), j.value("h").toInt(1080));
		c.hasAudio = j.value("hasAudio").toBool(true);
		sources.push_back(c);
	}

	segments.clear();
	for (const QJsonValue &v : o.value("segments").toArray()) {
		const QJsonObject j = v.toObject();
		Segment s;
		s.source = j.value("source").toInt();
		if (s.source < 0 || s.source >= sources.size())
			continue;
		s.in = j.value("in").toDouble();
		s.out = j.value("out").toDouble(-1);
		s.speed = std::clamp(j.value("speed").toDouble(1.0), 0.25, 4.0);
		s.transIn = Transition(std::clamp(j.value("transIn").toInt(), 0, kTransitionCount - 1));
		s.zoom = j.value("zoom").toBool();
		s.shake = j.value("shake").toBool();
		s.gray = j.value("gray").toBool();
		s.vivid = j.value("vivid").toBool();
		s.vignette = j.value("vignette").toBool();
		s.rampIn = j.value("rampIn").toBool();
		s.rampOut = j.value("rampOut").toBool();
		s.rampLen = std::clamp(j.value("rampLen").toDouble(0.5), 0.1, 3.0);
		segments.push_back(s);
	}

	subtitles.clear();
	for (const QJsonValue &v : o.value("subtitles").toArray()) {
		const QJsonObject j = v.toObject();
		Subtitle s;
		s.start = j.value("start").toDouble();
		s.end = j.value("end").toDouble(s.start + 2.0);
		s.text = j.value("text").toString();
		s.styleFromJson(j);
		subtitles.push_back(s);
	}

	intro = cardFromJson(o.value("intro").toObject());
	outro = cardFromJson(o.value("outro").toObject());

	const QJsonObject m = o.value("music").toObject();
	music.path = m.value("path").toString();
	music.fileOffset = std::max(0.0, m.value("fileOffset").toDouble());
	music.volume = std::clamp(m.value("volume").toDouble(0.9), 0.0, 2.0);
	music.bpm = m.value("bpm").toDouble();
	music.firstBeat = m.value("firstBeat").toDouble();
	music.beatEvery = std::clamp(m.value("beatEvery").toInt(1), 1, 8);
	music.fadeOut = m.value("fadeOut").toBool(true);
	music.duck = m.value("duck").toBool(false);
	music.duckStrength = std::clamp(m.value("duckStrength").toInt(1), 0, 2);

	gameVolume = std::clamp(o.value("gameVolume").toDouble(1.0), 0.0, 2.0);
	const QJsonObject fx = o.value("beatFx").toObject();
	beatFx.zoom = fx.value("zoom").toBool();
	beatFx.shake = fx.value("shake").toBool();
	beatFx.strength = std::clamp(fx.value("strength").toInt(1), 0, 2);
	beatFx.every = std::clamp(fx.value("every").toInt(1), 1, 8);
	layout = ShortsLayout(std::clamp(o.value("layout").toInt(int(ShortsLayout::TitleBands)), 0, 3));
	bands = TitleBands();
	bands.fromJson(o.value("bands").toObject());
	subStyle = Subtitle();
	if (o.contains("subStyle"))
		subStyle.styleFromJson(o.value("subStyle").toObject());
	else if (!subtitles.isEmpty()) // 예전 프로젝트: 마지막 자막 모양을 기본으로
		subStyle.copyStyleFrom(subtitles.last());
	const QJsonArray mm = o.value("minimap").toArray();
	if (mm.size() == 4)
		minimapRect = QRectF(mm[0].toDouble(), mm[1].toDouble(), mm[2].toDouble(), mm[3].toDouble());
}

bool EditProject::save() const
{
	if (filePath.isEmpty())
		return false;
	QFile f(filePath);
	if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
		return false;
	f.write(QJsonDocument(toJson()).toJson(QJsonDocument::Indented));
	return true;
}

bool EditProject::load(const QString &path)
{
	QFile f(path);
	if (!f.open(QIODevice::ReadOnly))
		return false;
	const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
	if (!doc.isObject())
		return false;
	filePath = path;
	fromJson(doc.object());
	return true;
}
