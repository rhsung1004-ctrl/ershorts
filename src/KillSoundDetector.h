#pragma once

#include <array>
#include <complex>
#include <cstdint>
#include <deque>
#include <vector>

// 게임 소리에서 이터널리턴 킬(빈사) 효과음을 찾는 감지기 (Qt/OBS와 무관한 순수 C++)
//  - 48kHz 모노 샘플을 조금씩 넣으면, 효과음과 모양이 같은 소리가 지나간 순간을 알려 줌
//  - 실시간(OBS 오디오 스레드)과 저장된 클립 테스트에 똑같이 사용
class KillSoundDetector {
public:
	struct Hit {
		double time = 0.0;  // 효과음이 시작된 시각 (넣은 소리 기준, 초)
		double score = 0.0; // 일치도 (0~1)
		int kind = 0;       // 0 = 1킬, 1 = 2킬, 2 = 3킬
	};

	// 민감도별 기준값 (높을수록 덜 민감)
	static constexpr double kThresholdHigh = 0.24;   // 민감하게
	static constexpr double kThresholdNormal = 0.30; // 보통
	static constexpr double kThresholdLow = 0.38;    // 덜 민감하게

	KillSoundDetector();
	void reset();
	void setThreshold(double t) { m_threshold = t; }
	double threshold() const { return m_threshold; }
	double maxScore() const { return m_maxScore; } // 지금까지 가장 높았던 일치도 (테스트용)

	// 48kHz 모노 float 샘플 (-1~1)
	void process(const float *samples, int count, std::vector<Hit> *hits);

	static const char *kindName(int kind);

private:
	static constexpr int kDecim = 3;   // 48k → 16k
	static constexpr int kTaps = 63;
	static constexpr int kN = 1024;    // FFT 크기 (16kHz 기준 64ms)
	static constexpr int kHop = 128;   // 8ms 간격으로 분석 (비교는 16ms 간격 20프레임)
	static constexpr int kStride = 2;  // 템플릿 프레임 간격 = kHop * kStride = 256
	static constexpr int kLo = 19;     // 300Hz 근처 FFT 칸
	static constexpr int kPool = 4;

	void analyzeFrame();
	double matchTemplates(int *kind);

	double m_threshold = kThresholdNormal;
	std::array<double, kTaps> m_fir{};
	std::array<float, kTaps> m_hist{}; // 최근 입력 (원형)
	int m_histPos = 0;
	int64_t m_inCount = 0;              // 지금까지 받은 48k 샘플 수
	std::vector<float> m_buf16;         // 16k 샘플 (분석 대기)
	int64_t m_buf16Start = 0;           // m_buf16[0] 의 16k 샘플 번호
	std::array<double, kN> m_window{};
	std::vector<std::vector<float>> m_frames; // 최근 프레임 특징 (원형)
	int64_t m_frameCount = 0;
	std::vector<double> m_rms;          // 프레임별 소리 크기 (원형)
	std::vector<std::complex<double>> m_fft;

	// 감지 상태
	double m_maxScore = 0.0;
	double m_peakScore = 0.0;
	int m_peakKind = 0;
	int64_t m_peakFrame = -1;
	int64_t m_lastHitFrame = -1000000;
};
