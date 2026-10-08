#include "ShortsExporter.h"

#include "BandLayout.h"
#include "FontManager.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMap>
#include <QPair>
#include <QRegularExpression>
#include <QVector>

#include <algorithm>
#include <cmath>

namespace {
// ffmpeg 필터 안에서 쓸 경로: 역슬래시 → 슬래시, 콜론 이스케이프, 작은따옴표로 감쌈
QString filterPath(const QString &p)
{
	QString s = QDir::fromNativeSeparators(p);
	s.replace(":", "\\:");
	return "'" + s + "'";
}

QString num(double v) { return QString::number(v, 'f', 4); }
QString hexColor(const QColor &c) { return "0x" + c.name().mid(1); }

// atempo는 한 번에 0.5~2.0 배만 가능 → 여러 개 이어 붙임
QString atempoChain(double speed)
{
	QStringList parts;
	double s = speed;
	while (s > 2.0 + 1e-6) {
		parts << "atempo=2.0";
		s /= 2.0;
	}
	while (s < 0.5 - 1e-6) {
		parts << "atempo=0.5";
		s /= 0.5;
	}
	if (std::abs(s - 1.0) > 1e-3)
		parts << "atempo=" + num(s);
	return parts.join(',');
}

const QString kAudioFmt = "aformat=sample_rates=48000:channel_layouts=stereo";

QString n6(double v) { return QString::number(v, 'f', 6); }

// 구간의 시간 늘이기/줄이기. 속도 램프가 있으면 Segment::srcToOut 과 같은 식을 ffmpeg 수식으로 씀
QString segmentSetpts(const Segment &s)
{
	double Ra, Rb;
	s.rampLengths(&Ra, &Rb);
	const double S = s.speed;
	if (Ra <= 0 && Rb <= 0)
		return QString("setpts=(PTS-STARTPTS)/%1").arg(num(S));

	const double L = s.srcLength();
	const double TA = Ra > 0 ? Ra / (S - 1) * std::log(S) : 0.0;
	const double H = L - Ra - Rb;
	const QString X = "(PTS-STARTPTS)*TB"; // 구간 시작부터의 원본 시간(초)
	const QString a = Ra > 0 ? QString("%1/(%2)*log(1+(%2)*%3/%1)").arg(n6(Ra), n6(S - 1), X) : QString("0");
	const QString m = QString("%1+(%2-%3)/%4").arg(n6(TA), X, n6(Ra), n6(S));
	const QString b = Rb > 0 ? QString("%1+%2/(%3)*log((%4+(%3)*(%5-%6)/%2)/%4)")
					   .arg(n6(TA + H / S), n6(Rb), n6(1 - S), n6(S), X, n6(L - Rb))
				 : m;
	return QString("setpts='if(lt(%1,%2),%3,if(lt(%1,%4),%5,%6))/TB'").arg(X, n6(Ra), a, n6(L - Rb), m, b);
}

// 속도 램프 구간의 소리: atempo는 시간에 따라 못 바꾸므로 작은 조각으로 나눠 조각마다 평균 속도 적용
QVector<QPair<double, double>> rampAudioChunks(const Segment &s, QVector<double> *speeds)
{
	constexpr int K = 8;
	double Ra, Rb;
	s.rampLengths(&Ra, &Rb);
	const double L = s.srcLength();
	QVector<double> pts{0.0};
	if (Ra > 0)
		for (int k = 1; k <= K; ++k)
			pts << Ra * k / K;
	if (L - Rb > pts.last() + 1e-6)
		pts << L - Rb;
	if (Rb > 0)
		for (int k = 1; k <= K; ++k)
			pts << L - Rb + Rb * k / K;
	if (pts.last() < L - 1e-6)
		pts << L;

	QVector<QPair<double, double>> chunks;
	for (int k = 0; k + 1 < pts.size(); ++k) {
		const double x0 = pts[k], x1 = pts[k + 1];
		if (x1 - x0 < 1e-4)
			continue;
		chunks.push_back({x0, x1});
		speeds->push_back((x1 - x0) / (s.srcToOut(x1) - s.srcToOut(x0)));
	}
	return chunks;
}
constexpr double kDip = 0.15; // 블랙 페이드 절반 길이
} // namespace

// 비트마다 줌/흔들림. timeShift: 이 스트림의 0초가 결과 영상의 몇 초인지 (인트로 길이 등)
static QString beatFxChain(const EditProject &p, int w, int h, double timeShift)
{
	double P, O;
	if (!p.beatPulseGrid(&P, &O))
		return {};
	const double A = p.introDuration();
	const double B = p.segmentsEnd();
	// 마지막 비트 이후 경과 시간 = mod(t-O, P), 세기 = exp(-경과/감쇠)
	const QString gate = QString("between(%1,%2,%3)*gte(%1,%4)").arg(QStringLiteral("%T"), n6(A), n6(B), n6(O));
	const QString env = QString("exp(-mod(%T-%1,%2)/%3)").arg(n6(O), n6(P), n6(BeatFx::kDecay));
	auto expr = [&](const QString &var, const QString &body) {
		const QString v = timeShift > 0 ? QString("(%1+%2)").arg(var, n6(timeShift)) : var;
		return QString(body).replace("%T", v);
	};
	QStringList fx;
	if (p.beatFx.shake) {
		const QString amp = n6(p.beatFx.shakeAmount());
		fx << expr("t", QString("crop=w=iw*0.94:h=ih*0.94:"
					"x='(iw-ow)/2+%1*iw*%2*sin(mod(%T-%3,%4)*70)*%5':"
					"y='(ih-oh)/2+%1*ih*%2*0.5*cos(mod(%T-%3,%4)*55)*%5',scale=%6:%7")
					.arg(gate, amp, n6(O), n6(P), env, QString::number(w), QString::number(h)));
	}
	if (p.beatFx.zoom) {
		fx << expr("it", QString("zoompan=z='1+%1*%2*%3':d=1:x='iw/2-iw/zoom/2':y='ih/2-ih/zoom/2':"
					 "s=%4x%5:fps=60")
					 .arg(gate, n6(p.beatFx.zoomAmount()), env, QString::number(w), QString::number(h)));
	}
	return fx.join(',');
}

ShortsExporter::ShortsExporter(QObject *parent) : QObject(parent)
{
	m_proc.setProcessChannelMode(QProcess::SeparateChannels);
	connect(&m_proc, &QProcess::readyReadStandardError, this, &ShortsExporter::onStdErr);
	connect(&m_proc, &QProcess::finished, this, &ShortsExporter::onFinished);
}

ShortsExporter::~ShortsExporter() { cancel(); }

QString ShortsExporter::ffmpegPath()
{
	const QString bundled = QCoreApplication::applicationDirPath() + "/ffmpeg.exe";
	return QFileInfo::exists(bundled) ? bundled : QStringLiteral("ffmpeg");
}

QString ShortsExporter::writeTextFile(const QString &name, const QString &text)
{
	const QString path = m_tmp->filePath(name);
	QFile f(path);
	if (f.open(QIODevice::WriteOnly | QIODevice::Truncate))
		f.write(text.trimmed().toUtf8());
	return path;
}

QString ShortsExporter::cardFilter(const TitleCard &c, const QString &tag, int frames)
{
	const QString font = filterPath("C:/Windows/Fonts/malgunbd.ttf");
	const double d = frames / 60.0; // 프레임 단위로 딱 맞춘 길이
	QString v = QString("color=c=%1:s=1080x1920:r=60:d=%2,format=yuv420p,setsar=1")
			    .arg(hexColor(c.background), num(d));
	const bool hasSub = !c.subtitle.trimmed().isEmpty();
	if (!c.title.trimmed().isEmpty())
		v += QString(",drawtext=fontfile=%1:textfile=%2:fontsize=110:fontcolor=%3:line_spacing=12:"
			     "borderw=4:bordercolor=black@0.6:x=(w-text_w)/2:y=%4")
			     .arg(font, filterPath(writeTextFile(tag + "_title.txt", c.title)), hexColor(c.color),
				  // 부제가 있으면 제목은 가운데 위로, 부제는 가운데 아래로 (여러 줄 제목도 겹치지 않게)
				  hasSub ? QStringLiteral("h/2-text_h-20") : QStringLiteral("(h-text_h)/2"));
	if (hasSub)
		v += QString(",drawtext=fontfile=%1:textfile=%2:fontsize=54:fontcolor=%3@0.85:"
			     "x=(w-text_w)/2:y=h/2+30")
			     .arg(font, filterPath(writeTextFile(tag + "_sub.txt", c.subtitle)), hexColor(c.color));
	const double fd = std::min(0.3, d / 3);
	v += QString(",fade=t=in:st=0:d=%1,fade=t=out:st=%2:d=%1").arg(num(fd), num(d - fd));
	return v + QString("[%1v];anullsrc=r=48000:cl=stereo,atrim=duration=%2[%1a]").arg(tag, num(d));
}

QString ShortsExporter::buildFilter()
{
	const EditProject &p = m_project;
	QStringList g;

	// 해상도 통일 기준: 첫 구간의 원본 크기
	const QSize base = p.sources.value(p.segments.first().source).size;
	const int W = std::max(2, base.width()) & ~1;
	const int H = std::max(2, base.height()) & ~1;

	// ── 1) 구간: 자르기 → 배속 → 크기 통일 → 효과 → 전환 ─────
	QString concatIn;
	for (int i = 0; i < p.segments.size(); ++i) {
		const Segment &s = p.segments[i];
		const SourceClip &src = p.sources[s.source];
		const int in = m_inputOf.value(s.source);
		const double dur = s.outDuration();
		// 누적 시간을 프레임으로 반올림해서 구간 길이를 정함 → 컷이 비트에서 밀리지 않음
		const int frames = int(std::lround(p.segmentStart(i + 1) * 60.0) - std::lround(p.segmentStart(i) * 60.0));
		const double exactDur = frames / 60.0;
		const bool nextDips = (i + 1 < p.segments.size() && p.segments[i + 1].transIn == Transition::BlackDip);

		QString v = QString("[%1:v]trim=start=%2:end=%3,").arg(in).arg(num(s.in), num(s.out)) + segmentSetpts(s);
		if (s.reverse) // 프레임 수를 먼저 줄인 뒤 뒤집음 (메모리 절약)
			v += ",fps=60,reverse";
		if (s.freeze > 0) // 마지막 장면을 그대로 늘려 멈춤
			v += QString(",tpad=stop_mode=clone:stop_duration=%1").arg(num(s.freeze));
		if (s.zoom) // 미리보기에서 고른 영역을 확대 (중심이 화면 밖으로 나가지 않게 고정)
			v += QString(",crop=w='floor(iw/%1/2)*2':h='floor(ih/%1/2)*2':x='min(max(iw*%2-ow/2,0),iw-ow)':"
				     "y='min(max(ih*%3-oh/2,0),ih-oh)'")
				     .arg(num(std::clamp(s.zoomScale, 1.05, 4.0)), num(std::clamp(s.zoomCX, 0.0, 1.0)),
					  num(std::clamp(s.zoomCY, 0.0, 1.0)));
		if (s.shake)
			v += ",crop=iw*0.92:ih*0.92:(iw-ow)/2+sin(t*47)*iw*0.025:(ih-oh)/2+cos(t*41)*ih*0.025";
		v += QString(",scale=%1:%2:force_original_aspect_ratio=decrease,pad=%1:%2:(ow-iw)/2:(oh-ih)/2,"
			     "setsar=1,fps=60,format=yuv420p")
			     .arg(W)
			     .arg(H);
		if (s.gray)
			v += ",hue=s=0";
		if (s.vivid)
			v += ",eq=contrast=1.08:saturation=1.35,unsharp=5:5:0.7";
		if (s.vignette)
			v += ",vignette=PI/4";
		if (s.reverse) // 되감기 느낌: 색 번짐 + 노이즈
			v += ",rgbashift=rh=-6:bh=6,noise=alls=18:allf=t";
		if (s.freeze > 0) {
			const QString t0 = num(s.movingDuration());
			if (s.freezeGray)
				v += QString(",hue=s=0:enable='gte(t,%1)'").arg(t0);
			if (s.freezeFlash)
				v += QString(",eq=brightness='if(gte(t,%1),max(0,0.6-(t-%1)*2.4),0)':eval=frame").arg(t0);
		}

		// 영상 맨 처음(인트로 없음)은 흰 플래시 대신 검은 화면에서 서서히 밝아지게
		const bool opening = (i == 0 && !p.intro.enabled);
		const Transition trans = (opening && s.transIn == Transition::Flash) ? Transition::BlackDip : s.transIn;
		const double dipLen = opening ? 0.4 : kDip;
		switch (trans) {
		case Transition::Flash:
			v += ",fade=t=in:st=0:d=0.25:color=white";
			break;
		case Transition::BlackDip:
			v += QString(",fade=t=in:st=0:d=%1").arg(num(std::min(dipLen, dur / 3)));
			break;
		case Transition::ZoomPunch:
			v += QString(",zoompan=z='max(1,1.35-0.35*it/0.3)':d=1:x='iw/2-iw/zoom/2':"
				     "y='ih/2-ih/zoom/2':s=%1x%2:fps=60")
				     .arg(W)
				     .arg(H);
			break;
		case Transition::Glitch:
			v += ",rgbashift=rh=-14:bh=14:enable='lt(t,0.2)',noise=alls=40:allf=t:enable='lt(t,0.2)'";
			break;
		case Transition::None:
			break;
		}
		if (nextDips)
			v += QString(",fade=t=out:st=%1:d=%2")
				     .arg(num(std::max(0.0, dur - std::min(kDip, dur / 3))), num(std::min(kDip, dur / 3)));
		v += QString(",trim=end_frame=%1,setpts=PTS-STARTPTS").arg(frames);
		g << v + QString("[v%1]").arg(i);

		QString a;
		if (src.hasAudio && s.hasRamp()) {
			QVector<double> speeds;
			const auto chunks = rampAudioChunks(s, &speeds);
			QString labels;
			for (int k = 0; k < chunks.size(); ++k) {
				QString c = QString("[%1:a]atrim=start=%2:end=%3,asetpts=PTS-STARTPTS")
						    .arg(in)
						    .arg(n6(s.in + chunks[k].first), n6(s.in + chunks[k].second));
				const QString tempo = atempoChain(speeds[k]);
				if (!tempo.isEmpty())
					c += "," + tempo;
				g << c + QString("[a%1_%2]").arg(i).arg(k);
				labels += QString("[a%1_%2]").arg(i).arg(k);
			}
			a = labels + QString("concat=n=%1:v=0:a=1,").arg(chunks.size()) + kAudioFmt +
			    QString(",apad=whole_dur=%1,atrim=end=%1").arg(num(exactDur));
		} else if (src.hasAudio) {
			a = QString("[%1:a]atrim=start=%2:end=%3,asetpts=PTS-STARTPTS").arg(in).arg(num(s.in), num(s.out));
			if (s.reverse)
				a += ",areverse";
			const QString tempo = atempoChain(s.speed);
			if (!tempo.isEmpty())
				a += "," + tempo;
			if (s.reverse)
				a += ",volume=0.6";
			a += "," + kAudioFmt + QString(",apad=whole_dur=%1,atrim=end=%1").arg(num(exactDur));
		} else {
			a = QString("anullsrc=r=48000:cl=stereo,atrim=duration=%1").arg(num(exactDur));
		}
		g << a + QString("[a%1]").arg(i);
		concatIn += QString("[v%1][a%1]").arg(i);
	}
	g << concatIn + QString("concat=n=%1:v=1:a=1[cv][ca]").arg(p.segments.size());

	// ── 2) 9:16 레이아웃 ──────────────────────────────
	switch (p.layout) {
	case ShortsLayout::CenterCrop:
		g << "[cv]crop=ih*9/16:ih,scale=1080:1920:flags=lanczos,setsar=1[base]";
		break;
	case ShortsLayout::CropWithMinimap: {
		const QRectF r = p.minimapRect;
		g << QString("[cv]split=2[m0][mm0];"
			     "[m0]crop=ih*9/16:ih,scale=1080:1920:flags=lanczos,setsar=1[m];"
			     "[mm0]crop=iw*%1:ih*%2:iw*%3:ih*%4,scale=380:-2,drawbox=c=white@0.85:t=4[mm];"
			     "[m][mm]overlay=main_w-overlay_w-28:150[base]")
			     .arg(num(r.width()), num(r.height()), num(r.x()), num(r.y()));
		break;
	}
	case ShortsLayout::TitleBands: {
		// 가운데: 원본에서 잘라낸 영역 → 1080 x (1920 - 위 띠 - 아래 띠), 비트 효과는 여기에만
		const QRect c = p.bandCropRect(QSize(W, H));
		const int mid = p.bands.middleHeight();
		QString m = QString("[cv]crop=%1:%2:%3:%4,scale=1080:%5:flags=lanczos,setsar=1")
				    .arg(c.width())
				    .arg(c.height())
				    .arg(c.x())
				    .arg(c.y())
				    .arg(mid);
		const QString fx = beatFxChain(p, 1080, mid, p.introDuration());
		if (!fx.isEmpty())
			m += "," + fx;
		g << m + "[mid]";

		// 위/아래 띠 + 글씨 (줄마다 가운데 정렬)
		QString b = QString("[mid]pad=1080:1920:0:%1:color=%2").arg(p.bands.topH()).arg(hexColor(p.bands.background));
		int k = 0;
		for (const BandLine &l : BandLayout::lines(p)) {
			b += QString(",drawtext=fontfile=%1:textfile=%2:fontsize=%3:fontcolor=%4:x=(w-text_w)/2:y=%5+(%6-text_h)/2")
				     .arg(filterPath(FontManager::renderFile(l.fontPath)),
					  filterPath(writeTextFile(QString("band_%1.txt").arg(k++), l.text)))
				     .arg(l.size)
				     .arg(hexColor(l.color), n6(l.slotTop), n6(l.slotHeight));
		}
		g << b + "[base]";
		break;
	}
	case ShortsLayout::BlurBackground:
		g << "[cv]split=2[bg0][fg0];"
		     "[bg0]scale=1080:1920:force_original_aspect_ratio=increase,crop=1080:1920,"
		     "boxblur=20:2,eq=brightness=-0.12[bg];"
		     "[fg0]scale=1080:-2:flags=lanczos[fg];"
		     "[bg][fg]overlay=(main_w-overlay_w)/2:(main_h-overlay_h)/2,setsar=1[base]";
		break;
	}

	// ── 3) 인트로 / 아웃트로 ─────────────────────────
	if (p.intro.enabled || p.outro.enabled) {
		QString parts;
		int n = 0;
		if (p.intro.enabled) {
			g << cardFilter(p.intro, "intro", int(std::lround(p.introDuration() * 60.0)));
			parts += "[introv][introa]";
			++n;
		}
		parts += "[base][ca]";
		++n;
		if (p.outro.enabled) {
			g << cardFilter(p.outro, "outro",
					int(std::lround(p.totalDuration() * 60.0) - std::lround(p.segmentsEnd() * 60.0)));
			parts += "[outrov][outroa]";
			++n;
		}
		g << parts + QString("concat=n=%1:v=1:a=1[allv][alla]").arg(n);
	} else {
		g << "[base]null[allv]" << "[ca]anull[alla]";
	}

	// ── 4) 게임 소리 + BGM ────────────────────────────
	const double total = p.totalDuration();
	const bool duck = m_bgmInput >= 0 && p.music.duck;
	if (duck) // 게임 소리를 하나 더 갈라서 음악을 줄이는 신호로 사용
		g << QString("[alla]volume=%1,asplit=2[game][duckkey]").arg(num(p.gameVolume));
	else
		g << QString("[alla]volume=%1[game]").arg(num(p.gameVolume));
	if (m_bgmInput >= 0) {
		QString b = QString("[%1:a]atrim=start=%2,asetpts=PTS-STARTPTS,volume=%3,%4")
				    .arg(m_bgmInput)
				    .arg(num(p.music.fileOffset), num(p.music.volume), kAudioFmt);
		if (p.music.fadeOut && total > 3.0)
			b += QString(",afade=t=out:st=%1:d=1.5").arg(num(total - 1.5));
		if (duck) {
			g << b + "[bgmraw]";
			// 게임 소리가 기준보다 크면 음악을 압축(줄임), 작아지면 0.4초에 걸쳐 돌아옴
			g << QString("[bgmraw][duckkey]sidechaincompress=threshold=%1:ratio=%2:attack=15:release=400:"
				     "makeup=1:detection=rms[bgm]")
				     .arg(num(p.music.duckThreshold()), num(p.music.duckRatio()));
		} else {
			g << b + "[bgm]";
		}
	}
	// 효과음: 지정한 시각에 한 번씩
	QString mixIn = "[game]";
	int mixN = 1;
	if (m_bgmInput >= 0) {
		mixIn += "[bgm]";
		++mixN;
	}
	for (int k = 0; k < p.sfx.size() && k < m_sfxInput.size(); ++k) {
		if (m_sfxInput[k] < 0 || p.sfx[k].start >= total)
			continue;
		const qint64 ms = qint64(std::llround(p.sfx[k].start * 1000.0));
		g << QString("[%1:a]%2,volume=%3,adelay=delays=%4:all=1[sfx%5]")
			     .arg(m_sfxInput[k])
			     .arg(kAudioFmt, num(p.sfx[k].volume))
			     .arg(ms)
			     .arg(k);
		mixIn += QString("[sfx%1]").arg(k);
		++mixN;
	}
	if (mixN > 1)
		g << mixIn + QString("amix=inputs=%1:duration=first:normalize=0[aout]").arg(mixN);
	else
		g << "[game]anull[aout]";

	// ── 5) 자막 (결과 시간 기준) ──────────────────────
	QStringList texts;
	for (int i = 0; i < p.subtitles.size(); ++i) {
		const Subtitle &s = p.subtitles[i];
		if (s.text.trimmed().isEmpty() || s.end <= s.start)
			continue;
		QString dt = QString("drawtext=fontfile=%1:textfile=%2:fontsize=%3:fontcolor=%4:"
				     "line_spacing=10:borderw=5:bordercolor=black:"
				     "x=(w-text_w)/2:y=h*%5-text_h/2:enable='between(t,%6,%7)'")
				     .arg(filterPath(FontManager::renderFile(s.font)),
					  filterPath(writeTextFile(QString("sub_%1.txt").arg(i), s.text)))
				     .arg(s.fontSize)
				     .arg(hexColor(s.color), num(s.y), num(s.start), num(s.end));
		if (s.box)
			dt += ":box=1:boxcolor=black@0.4:boxborderw=22";
		texts << dt;
	}
	// ── 6) 비트 효과 (자막보다 먼저 → 자막은 흔들리지 않음) ──
	//    제목 띠 레이아웃은 가운데 영상에만 이미 적용했으므로 여기서는 건너뜀
	QString vlabel = "[allv]";
	if (p.layout != ShortsLayout::TitleBands) {
		const QString fx = beatFxChain(p, 1080, 1920, 0.0);
		if (!fx.isEmpty()) {
			g << vlabel + fx + "[fxv]";
			vlabel = "[fxv]";
		}
	}

	// ── 7) 자막 → 이미지(로고·스티커) → (미리보기 품질이면) 축소 ──
	g << vlabel + (texts.isEmpty() ? QStringLiteral("null") : texts.join(',')) + "[subv]";
	QString cur = "[subv]";
	for (int k = 0; k < p.images.size() && k < m_imgInput.size(); ++k) {
		if (m_imgInput[k] < 0)
			continue;
		const ImageOverlay &im = p.images[k];
		const int w = std::max(2, int(std::lround(1080.0 * im.width)) & ~1);
		g << QString("[%1:v]scale=%2:-2,format=rgba,colorchannelmixer=aa=%3[img%4]")
			     .arg(m_imgInput[k])
			     .arg(w)
			     .arg(num(im.opacity))
			     .arg(k);
		QString ov = QString("%1[img%2]overlay=x=%3-overlay_w/2:y=%4-overlay_h/2")
				     .arg(cur)
				     .arg(k)
				     .arg(num(im.x * 1080.0), num(im.y * 1920.0));
		if (!im.whole)
			ov += QString(":enable='between(t,%1,%2)'").arg(num(im.start), num(im.end));
		g << ov + QString("[ov%1]").arg(k);
		cur = QString("[ov%1]").arg(k);
	}
	if (m_previewQuality)
		g << cur + "scale=540:960:flags=bilinear,fps=30[v]";
	else
		g << cur + "null[v]";

	return g.join(';');
}

QStringList ShortsExporter::buildArgs(bool useHw)
{
	QStringList a;
	a << "-hide_banner" << "-y";
	for (const QString &in : m_inputs)
		a << "-i" << in;
	a << "-filter_complex" << m_filter << "-map" << "[v]" << "-map" << "[aout]";

	if (m_previewQuality) {
		// 확인용: 작고 빠르게
		if (useHw)
			a << "-c:v" << "h264_nvenc" << "-preset" << "p1" << "-rc" << "vbr" << "-cq" << "30" << "-b:v" << "0";
		else
			a << "-c:v" << "libx264" << "-preset" << "ultrafast" << "-crf" << "30";
		a << "-pix_fmt" << "yuv420p" << "-r" << "30" << "-c:a" << "aac" << "-b:a" << "128k";
	} else {
		if (useHw)
			a << "-c:v" << "h264_nvenc" << "-preset" << "p5" << "-rc" << "vbr" << "-cq" << "20" << "-b:v" << "0";
		else
			a << "-c:v" << "libx264" << "-preset" << "medium" << "-crf" << "19";
		a << "-pix_fmt" << "yuv420p" << "-r" << "60" << "-c:a" << "aac" << "-b:a" << "192k";
	}
	a << "-movflags" << "+faststart" << m_output;
	return a;
}

void ShortsExporter::start(const EditProject &project, const QString &output, bool previewQuality)
{
	m_previewQuality = previewQuality;
	if (isRunning())
		return;
	if (project.segments.isEmpty() || !project.allSourcesReady()) {
		emit logMessage(project.segments.isEmpty() ? "타임라인이 비어 있습니다"
							  : "클립 정보를 아직 불러오는 중입니다. 잠시 후 다시 시도하세요.");
		emit finished(false, output);
		return;
	}

	m_project = project;
	m_output = output;
	m_cancelled = false;
	m_tmp = std::make_unique<QTemporaryDir>();

	// 타임라인에서 실제로 쓰는 클립만 입력으로 넣음
	m_inputs.clear();
	m_inputOf.clear();
	for (const Segment &s : project.segments) {
		if (!m_inputOf.contains(s.source)) {
			m_inputOf.insert(s.source, int(m_inputs.size()));
			m_inputs << project.sources[s.source].path;
		}
	}
	m_bgmInput = -1;
	if (!project.music.path.isEmpty() && QFileInfo::exists(project.music.path)) {
		m_bgmInput = int(m_inputs.size());
		m_inputs << project.music.path;
	}
	m_sfxInput.clear();
	for (const SoundFx &f : project.sfx) {
		if (QFileInfo::exists(f.path)) {
			m_sfxInput << int(m_inputs.size());
			m_inputs << f.path;
		} else {
			m_sfxInput << -1;
			emit logMessage("효과음 파일이 없어 건너뜀: " + f.path);
		}
	}
	m_imgInput.clear();
	for (const ImageOverlay &im : project.images) {
		if (QFileInfo::exists(im.path)) {
			m_imgInput << int(m_inputs.size());
			m_inputs << im.path;
		} else {
			m_imgInput << -1;
			emit logMessage("이미지 파일이 없어 건너뜀: " + im.path);
		}
	}

	m_filter = buildFilter();
	run(true);
}

void ShortsExporter::run(bool useHw)
{
	m_usingHw = useHw;
	m_errBuf.clear();
	emit logMessage(QString("%1 내보내기 시작 (%2, 결과 길이 %3초)")
				.arg(m_previewQuality ? QStringLiteral("미리보기(540x960)") : QStringLiteral("고화질"))
				.arg(useHw ? "NVENC" : "x264")
				.arg(m_project.totalDuration(), 0, 'f', 1));
	emit progress(0);
	m_proc.start(ffmpegPath(), buildArgs(useHw));
}

void ShortsExporter::cancel()
{
	if (isRunning()) {
		m_cancelled = true;
		m_proc.kill();
		m_proc.waitForFinished(2000);
	}
}

void ShortsExporter::onStdErr()
{
	const QByteArray chunk = m_proc.readAllStandardError();
	m_errBuf += chunk;
	if (m_errBuf.size() > 64 * 1024)
		m_errBuf = m_errBuf.right(32 * 1024);

	static const QRegularExpression reTime(R"(time=(\d+):(\d+):(\d+\.\d+))");
	auto it = reTime.globalMatch(QString::fromUtf8(chunk));
	double t = -1;
	while (it.hasNext()) {
		const auto m = it.next();
		t = m.captured(1).toDouble() * 3600 + m.captured(2).toDouble() * 60 + m.captured(3).toDouble();
	}
	const double total = m_project.totalDuration();
	if (t >= 0 && total > 0)
		emit progress(std::clamp(int(t / total * 100.0), 0, 99));
}

void ShortsExporter::onFinished(int code, QProcess::ExitStatus status)
{
	const bool ok = (status == QProcess::NormalExit && code == 0);

	if (!ok && !m_cancelled && m_usingHw && status == QProcess::NormalExit) {
		emit logMessage("NVENC 인코딩 실패 → CPU(x264)로 다시 시도합니다");
		run(false);
		return;
	}

	m_tmp.reset();
	if (ok) {
		emit progress(100);
		emit logMessage("내보내기 완료: " + QFileInfo(m_output).fileName());
	} else if (m_cancelled) {
		emit logMessage("내보내기 취소됨");
	} else {
		const QStringList lines = QString::fromUtf8(m_errBuf).trimmed().split('\n');
		emit logMessage("내보내기 실패:\n" + lines.mid(std::max(0, int(lines.size()) - 6)).join('\n'));
	}
	emit finished(ok, m_output);
}
