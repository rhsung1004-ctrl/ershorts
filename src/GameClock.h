#pragma once

#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

// 화면 가운데 위 "N일 차" + 해/달 아이콘을 보고 지금 게임 시간("4일차 낮")을 알아냄
//  - 낮/밤: 아이콘 색 (해 = 노랑, 달 = 보라)
//  - 며칠째: Windows 글자 인식(별도 스레드)으로 읽고, 못 읽으면 밤→낮으로 바뀔 때 하루씩 셈
//  - 입력: 게임 화면 높이 720 기준 (가운데-84, 2)부터 48x28 영역을 4배로 키운 RGB 그림
class GameClock {
public:
	static constexpr int kW = 192;
	static constexpr int kH = 112;
	static constexpr double kLeft = -84.0; // 화면 가운데 기준 왼쪽 끝 (720 기준 px)
	static constexpr double kTop = 2.0;
	static constexpr double kUnitW = 48.0;
	static constexpr double kUnitH = 28.0;

	enum Phase { None = 0, Day = 1, Night = 2 };

	GameClock();
	~GameClock();

	// 1초에 한 번쯤 호출 (영상 스레드)
	void feed(const uint8_t *rgb, int64_t nowMs);
	// "4일차 낮" / 며칠째 모르면 "낮" / 게임 화면이 아니면 빈 문자열 (UTF-8)
	std::string label() const;
	std::string lastOcrText() const;

	// 한 장의 그림만 보고 판단 (저장된 클립 이름 바꾸기용, UI 스레드에서 불러도 됨)
	static Phase phaseOf(const uint8_t *rgb);
	static int readDay(const uint8_t *rgb, std::wstring *raw = nullptr); // 0 = 못 읽음
	static std::string makeLabel(int day, Phase phase);

private:
	static std::vector<uint8_t> ocrImage(const uint8_t *rgb, int *w, int *h);
	static int parseDay(const std::wstring &text);
	void workerLoop();

	mutable std::mutex m_mutex;
	int m_day = 0;
	Phase m_phase = None;
	int64_t m_lastSeenMs = -1;   // 아이콘이 마지막으로 보인 시각
	int64_t m_absentSinceMs = -1; // 게임 화면이 안 보이기 시작한 시각
	bool m_sawAbsence = false;    // 게임 밖(로비 등)을 충분히 본 적 있음 → 새 판이면 1일차
	int64_t m_lastOcrMs = -100000;
	int m_ocrCandidate = 0;       // 같은 숫자가 두 번 읽히면 확정
	std::string m_lastOcrText;

	// 글자 인식 작업 (최신 1장만)
	std::thread m_worker;
	std::condition_variable m_cv;
	bool m_stop = false;
	bool m_hasJob = false;
	std::vector<uint8_t> m_job;
};
