#include "KillSoundDetector.h"

#include "KillSoundTemplates.h"

#include <algorithm>
#include <cmath>

namespace {
constexpr double kPi = 3.14159265358979323846;
constexpr int kWinFrames = KillSoundTemplates::kFrames;
constexpr int kBands = KillSoundTemplates::kBands;

// 템플릿마다 제곱합 (피어슨 상관 분모용) — 템플릿은 이미 평균 0
struct TemplateNorms {
	double v[KillSoundTemplates::kCount];
	TemplateNorms()
	{
		for (int k = 0; k < KillSoundTemplates::kCount; ++k) {
			double s = 0;
			for (int f = 0; f < kWinFrames; ++f)
				for (int b = 0; b < kBands; ++b)
					s += double(KillSoundTemplates::kData[k][f][b]) * KillSoundTemplates::kData[k][f][b];
			v[k] = std::sqrt(s);
		}
	}
};
const TemplateNorms &norms()
{
	static const TemplateNorms n;
	return n;
}

void fftInPlace(std::vector<std::complex<double>> &a)
{
	const int n = int(a.size());
	for (int i = 1, j = 0; i < n; ++i) {
		int bit = n >> 1;
		for (; j & bit; bit >>= 1)
			j ^= bit;
		j ^= bit;
		if (i < j)
			std::swap(a[i], a[j]);
	}
	for (int len = 2; len <= n; len <<= 1) {
		const double ang = -2 * kPi / len;
		const std::complex<double> wl(std::cos(ang), std::sin(ang));
		for (int i = 0; i < n; i += len) {
			std::complex<double> w(1);
			for (int j = 0; j < len / 2; ++j) {
				const std::complex<double> u = a[i + j], v = a[i + j + len / 2] * w;
				a[i + j] = u + v;
				a[i + j + len / 2] = u - v;
				w *= wl;
			}
		}
	}
}
} // namespace

const char *KillSoundDetector::kindName(int kind)
{
	return (kind >= 0 && kind < KillSoundTemplates::kCount) ? KillSoundTemplates::kNames[kind] : "?";
}

KillSoundDetector::KillSoundDetector()
{
	// 7kHz 저역 통과 FIR (해밍 창) — 분석 스크립트와 같은 계수
	const double fc = 7000.0 / 48000.0;
	const double M = (kTaps - 1) / 2.0;
	double sum = 0;
	for (int n = 0; n < kTaps; ++n) {
		const double t = n - M;
		const double sinc = (t == 0) ? 1.0 : std::sin(kPi * 2 * fc * t) / (kPi * 2 * fc * t);
		m_fir[n] = 2 * fc * sinc * (0.54 - 0.46 * std::cos(2 * kPi * n / (kTaps - 1)));
		sum += m_fir[n];
	}
	for (double &h : m_fir)
		h /= sum;
	for (int i = 0; i < kN; ++i)
		m_window[i] = 0.5 - 0.5 * std::cos(2 * kPi * i / kN);
	m_fft.resize(kN);
	const int ring = (kWinFrames - 1) * kStride + 1;
	m_frames.assign(ring, std::vector<float>(kBands, 0.0f));
	m_rms.assign(ring, 0.0);
	reset();
}

void KillSoundDetector::reset()
{
	m_hist.fill(0.0f);
	m_histPos = 0;
	m_inCount = 0;
	m_buf16.clear();
	m_buf16Start = 0;
	m_frameCount = 0;
	m_peakScore = 0.0;
	m_peakFrame = -1;
	m_lastHitFrame = -1000000;
	m_maxScore = 0.0;
}

void KillSoundDetector::process(const float *x, int count, std::vector<Hit> *hits)
{
	for (int i = 0; i < count; ++i) {
		m_hist[m_histPos] = x[i];
		// 3개마다 하나: y[m] = Σ h[k]·x[3m-k]
		if (m_inCount % kDecim == 0) {
			double acc = 0;
			int p = m_histPos;
			for (int k = 0; k < kTaps; ++k) {
				acc += m_fir[k] * m_hist[p];
				p = (p == 0) ? kTaps - 1 : p - 1;
			}
			m_buf16.push_back(float(acc));
		}
		m_histPos = (m_histPos + 1) % kTaps;
		++m_inCount;
	}

	// 다음 프레임 시작 위치 = m_frameCount * kHop (16k 샘플 번호)
	while (true) {
		const int64_t start = m_frameCount * kHop;
		const int64_t off = start - m_buf16Start;
		if (off < 0 || off + kN > int64_t(m_buf16.size()))
			break;
		analyzeFrame();

		int kind = 0;
		const double score = matchTemplates(&kind);
		const int64_t f = m_frameCount; // 방금 분석한 프레임 번호
		++m_frameCount;
		m_maxScore = std::max(m_maxScore, score);

		const int64_t refractory = int64_t(0.7 * 16000 / kHop); // 한 번 감지하면 0.7초 쉼
		if (f - m_lastHitFrame < refractory)
			continue;
		if (score >= m_threshold && score > m_peakScore) {
			m_peakScore = score;
			m_peakKind = kind;
			m_peakFrame = f;
		}
		// 기준을 넘은 뒤 가장 높은 점을 잡고 50ms 안에 더 높아지지 않으면 확정
		if (m_peakFrame >= 0 && f - m_peakFrame >= 6) {
			Hit h;
			h.score = m_peakScore;
			h.kind = m_peakKind;
			// 창(20프레임 = 약 0.32초)의 시작이 효과음 시작
			const int64_t firstFrame = m_peakFrame - int64_t(kWinFrames - 1) * kStride;
			h.time = std::max<int64_t>(0, firstFrame + 1) * double(kHop) / 16000.0;
			if (hits)
				hits->push_back(h);
			m_lastHitFrame = m_peakFrame;
			m_peakFrame = -1;
			m_peakScore = 0.0;
		}
	}

	// 다 쓴 16k 샘플 정리
	const int64_t keepFrom = m_frameCount * kHop;
	const int64_t drop = keepFrom - m_buf16Start;
	if (drop > 4096) {
		m_buf16.erase(m_buf16.begin(), m_buf16.begin() + drop);
		m_buf16Start = keepFrom;
	}
}

void KillSoundDetector::analyzeFrame()
{
	const int64_t off = m_frameCount * kHop - m_buf16Start;
	const float *s = m_buf16.data() + off;
	double energy = 0;
	for (int i = 0; i < kN; ++i) {
		m_fft[i] = std::complex<double>(s[i] * m_window[i], 0.0);
		energy += double(s[i]) * s[i];
	}
	fftInPlace(m_fft);
	const int slot = int(m_frameCount % int64_t(m_frames.size()));
	std::vector<float> &out = m_frames[slot];
	for (int b = 0; b < kBands; ++b) {
		double e = 0;
		for (int j = 0; j < kPool; ++j)
			e += std::norm(m_fft[kLo + b * kPool + j]);
		out[b] = float(std::log10(e + 1e-9));
	}
	m_rms[slot] = std::sqrt(energy / kN);
}

double KillSoundDetector::matchTemplates(int *kind)
{
	const int ring = int(m_frames.size());
	if (m_frameCount + 1 < ring)
		return 0.0;

	// 창: 지금 프레임에서 kStride 간격으로 20개 (가장 오래된 것부터)
	double W[kWinFrames][kBands];
	double loud = 0;
	for (int f = 0; f < kWinFrames; ++f) {
		const int64_t idx = m_frameCount - int64_t(kWinFrames - 1 - f) * kStride;
		const int slot = int(idx % ring);
		loud = std::max(loud, m_rms[slot]);
		for (int b = 0; b < kBands; ++b)
			W[f][b] = m_frames[slot][b];
	}
	if (loud < 0.0005) // 거의 무음이면 판단하지 않음
		return 0.0;

	// 이중 중심화: 프레임 평균·대역 평균을 빼서 "소리 크기"와 "음색 기울기" 차이를 없앰
	double rowMean[kWinFrames] = {}, colMean[kBands] = {}, all = 0;
	for (int f = 0; f < kWinFrames; ++f)
		for (int b = 0; b < kBands; ++b) {
			rowMean[f] += W[f][b];
			colMean[b] += W[f][b];
			all += W[f][b];
		}
	for (double &v : rowMean)
		v /= kBands;
	for (double &v : colMean)
		v /= kWinFrames;
	all /= double(kWinFrames) * kBands;
	double ss = 0;
	for (int f = 0; f < kWinFrames; ++f)
		for (int b = 0; b < kBands; ++b) {
			W[f][b] = W[f][b] - rowMean[f] - colMean[b] + all;
			ss += W[f][b] * W[f][b];
		}
	if (ss <= 1e-12)
		return 0.0;
	const double wn = std::sqrt(ss);

	double best = -1;
	for (int k = 0; k < KillSoundTemplates::kCount; ++k) {
		double dot = 0;
		for (int f = 0; f < kWinFrames; ++f)
			for (int b = 0; b < kBands; ++b)
				dot += W[f][b] * KillSoundTemplates::kData[k][f][b];
		const double r = dot / (wn * norms().v[k]);
		if (r > best) {
			best = r;
			*kind = k;
		}
	}
	return best;
}
