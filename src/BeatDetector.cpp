#include "BeatDetector.h"

#include "ShortsExporter.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace {
constexpr int kSampleRate = 11025;
constexpr int kHop = 64;  // 약 5.8ms 단위
constexpr int kWin = 256; // 에너지 계산 창
constexpr double kMinBpm = 70.0;
constexpr double kMaxBpm = 180.0;
} // namespace

BeatDetector::BeatDetector(QObject *parent) : QObject(parent)
{
	connect(&m_proc, &QProcess::readyReadStandardOutput, this,
		[this] { m_pcm += m_proc.readAllStandardOutput(); });
	connect(&m_proc, &QProcess::finished, this, [this](int code, QProcess::ExitStatus st) {
		m_pcm += m_proc.readAllStandardOutput();
		if (st != QProcess::NormalExit || code != 0 || m_pcm.size() < int(sizeof(float)) * kSampleRate * 5) {
			emit finished(false, 0, 0, "음악 파일을 읽지 못했습니다 (5초 이상인 오디오 파일인지 확인)");
			return;
		}
		std::vector<float> mono(size_t(m_pcm.size()) / sizeof(float));
		std::memcpy(mono.data(), m_pcm.constData(), mono.size() * sizeof(float));
		m_pcm.clear();

		const Result r = analyze(mono, kSampleRate);
		if (!r.ok)
			emit finished(false, 0, 0, "박자를 찾지 못했습니다. BPM을 직접 입력해 주세요.");
		else
			emit finished(true, r.bpm, r.firstBeat,
				      QString("BPM %1 감지 (신뢰도 %2)")
					      .arg(r.bpm, 0, 'f', 1)
					      .arg(r.confidence > 2.0 ? "높음" : r.confidence > 1.4 ? "보통" : "낮음"));
	});
}

BeatDetector::~BeatDetector()
{
	if (isRunning()) {
		m_proc.kill();
		m_proc.waitForFinished(1000);
	}
}

void BeatDetector::start(const QString &audioPath)
{
	if (isRunning())
		return;
	m_pcm.clear();
	m_proc.start(ShortsExporter::ffmpegPath(),
		     {"-hide_banner", "-loglevel", "error", "-i", audioPath, "-t", "90", "-vn", "-ac", "1",
		      "-ar", QString::number(kSampleRate), "-f", "f32le", "-"});
}

BeatDetector::Result BeatDetector::analyze(const std::vector<float> &x, int sr)
{
	Result res;
	const int frames = int((x.size() - kWin) / kHop);
	if (frames < 400)
		return res;
	const double rate = double(sr) / kHop;

	// 1) 프레임별 로그 에너지 → 증가량(온셋)
	std::vector<double> le(frames);
	for (int i = 0; i < frames; ++i) {
		double e = 0.0;
		const float *p = x.data() + size_t(i) * kHop;
		for (int k = 0; k < kWin; ++k)
			e += double(p[k]) * p[k];
		le[i] = std::log(e + 1e-6);
	}
	std::vector<double> on(frames, 0.0);
	for (int i = 1; i < frames; ++i)
		on[i] = std::max(0.0, le[i] - le[i - 1]);

	// 2) 느린 변화 제거(로컬 평균 빼기) + 살짝 부드럽게
	std::vector<double> hp(frames, 0.0);
	{
		const int R = 16;
		double sum = 0.0;
		int cnt = 0;
		int lo = 0, hi = -1;
		for (int i = 0; i < frames; ++i) {
			while (hi < std::min(frames - 1, i + R)) sum += on[++hi], ++cnt;
			while (lo < i - R) sum -= on[lo++], --cnt;
			hp[i] = std::max(0.0, on[i] - sum / cnt);
		}
	}
	std::vector<double> o(frames, 0.0);
	for (int i = 2; i < frames - 2; ++i)
		o[i] = (hp[i - 2] + 2 * hp[i - 1] + 3 * hp[i] + 2 * hp[i + 1] + hp[i + 2]) / 9.0;

	double mean = 0.0;
	for (double v : o) mean += v;
	mean /= frames;
	if (mean <= 1e-9)
		return res;

	// 3) 자기상관 + 템포 선호도(120 BPM 근처)로 대략적인 박 간격 찾기
	const int lagMin = int(std::floor(60.0 * rate / kMaxBpm));
	const int lagMax = int(std::ceil(60.0 * rate / kMinBpm));
	double bestScore = -1.0;
	int bestLag = lagMin;
	for (int L = lagMin; L <= lagMax; ++L) {
		double ac = 0.0;
		for (int i = 0; i + L < frames; ++i)
			ac += o[i] * o[i + L];
		ac /= (frames - L);
		const double bpm = 60.0 * rate / L;
		const double z = std::log2(bpm / 120.0) / 0.7;
		const double score = ac * std::exp(-0.5 * z * z);
		if (score > bestScore) {
			bestScore = score;
			bestLag = L;
		}
	}

	// 4) 콤 필터로 박 간격(소수점)과 위상을 함께 정밀 탐색
	double bestP = bestLag, bestPhase = 0.0, bestComb = -1.0;
	for (double P = bestLag - 1.5; P <= bestLag + 1.5; P += 0.02) {
		if (P < 2)
			continue;
		for (double ph = 0.0; ph < P; ph += 0.5) {
			double s = 0.0;
			int n = 0;
			for (double t = ph; t < frames; t += P) {
				s += o[size_t(t + 0.5) < size_t(frames) ? size_t(t + 0.5) : size_t(frames - 1)];
				++n;
			}
			s /= std::max(1, n);
			if (s > bestComb) {
				bestComb = s;
				bestP = P;
				bestPhase = ph;
			}
		}
	}

	res.ok = true;
	res.bpm = 60.0 * rate / bestP;
	// 온셋은 소리가 에너지 창에 처음 들어오는 프레임에서 최대 → 창 길이만큼 보정
	double first = (bestPhase * kHop + kWin * 0.85) / sr;
	const double period = bestP / rate;
	while (first >= period)
		first -= period;
	res.firstBeat = std::max(0.0, first);
	res.confidence = bestComb / mean;
	return res;
}
