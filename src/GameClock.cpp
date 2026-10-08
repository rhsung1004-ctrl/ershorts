#include "GameClock.h"

#include "GameClockOcr.h"

#include <algorithm>

namespace {
// 아이콘 영역 (4배 그림 기준)
constexpr int kIconX0 = 56, kIconX1 = 144, kIconY0 = 52, kIconY1 = 100;
// "N일 차" 글씨 영역
constexpr int kTextX0 = 24, kTextX1 = 132, kTextY0 = 12, kTextY1 = 54;
constexpr int kMinIconPixels = 60;
constexpr int64_t kNewGameAbsenceMs = 20000; // 20초 넘게 게임 화면이 없다가 낮으로 나타나면 새 판
constexpr int64_t kOcrEveryMs = 4000;
} // namespace

GameClock::GameClock() { m_worker = std::thread([this] { workerLoop(); }); }

GameClock::~GameClock()
{
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		m_stop = true;
	}
	m_cv.notify_all();
	if (m_worker.joinable())
		m_worker.join();
}

GameClock::Phase GameClock::phaseOf(const uint8_t *rgb)
{
	int sun = 0, moon = 0;
	for (int y = kIconY0; y < kIconY1; ++y)
		for (int x = kIconX0; x < kIconX1; ++x) {
			const uint8_t *p = rgb + (size_t(y) * kW + x) * 3;
			const int r = p[0], g = p[1], b = p[2];
			if (r > 200 && g > 150 && b < 110)
				++sun; // 노란 해
			else if (b > 170 && b > r + 10 && r > 120)
				++moon; // 보라 달
		}
	if (sun >= kMinIconPixels && sun > 2 * moon)
		return Day;
	if (moon >= kMinIconPixels && moon > 2 * sun)
		return Night;
	return None;
}

std::vector<uint8_t> GameClock::ocrImage(const uint8_t *rgb, int *w, int *h)
{
	// 흰 글씨 → 흰 바탕에 검은 글씨로 뒤집고, 2배 키우고, 여백을 줘서 인식이 잘 되게
	constexpr int pad = 24, scale = 2;
	const int tw = kTextX1 - kTextX0, th = kTextY1 - kTextY0;
	*w = tw * scale + pad * 2;
	*h = th * scale + pad * 2;
	std::vector<uint8_t> out(size_t(*w) * *h * 4, 255);
	for (int y = 0; y < th * scale; ++y)
		for (int x = 0; x < tw * scale; ++x) {
			const uint8_t *p = rgb + (size_t(kTextY0 + y / scale) * kW + (kTextX0 + x / scale)) * 3;
			const int lum = (p[0] * 3 + p[1] * 6 + p[2]) / 10;
			// 밝은 글씨일수록 검게 (밝기 110 이하 = 흰 바탕, 210 이상 = 검정)
			const uint8_t v = uint8_t(std::clamp(255 - (lum - 110) * 255 / 100, 0, 255));
			uint8_t *q = &out[(size_t(y + pad) * *w + (x + pad)) * 4];
			q[0] = q[1] = q[2] = v;
			q[3] = 255;
		}
	return out;
}

int GameClock::parseDay(const std::wstring &t)
{
	// "4일 차", "4일차", "4 일차" … '일' 바로 앞 숫자
	const size_t pos = t.find(L'일');
	if (pos == std::wstring::npos)
		return 0;
	size_t i = pos;
	while (i > 0 && t[i - 1] == L' ')
		--i;
	int value = 0, mul = 1, digits = 0;
	while (i > 0 && t[i - 1] >= L'0' && t[i - 1] <= L'9' && digits < 2) {
		value += (t[i - 1] - L'0') * mul;
		mul *= 10;
		--i;
		++digits;
	}
	return (digits > 0 && value >= 1 && value <= 20) ? value : 0;
}

int GameClock::readDay(const uint8_t *rgb, std::wstring *raw)
{
	int w = 0, h = 0;
	const std::vector<uint8_t> img = ocrImage(rgb, &w, &h);
	const std::wstring text = ocrReadBgra(img.data(), w, h);
	if (raw)
		*raw = text;
	return parseDay(text);
}

std::string GameClock::makeLabel(int day, Phase phase)
{
	const char *p = phase == Day ? "낮" : phase == Night ? "밤" : "";
	if (phase == None)
		return {};
	if (day <= 0)
		return p;
	return std::to_string(day) + "일차 " + p;
}

void GameClock::feed(const uint8_t *rgb, int64_t nowMs)
{
	const Phase p = phaseOf(rgb);
	bool wantOcr = false;
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		if (p == None) {
			if (m_absentSinceMs < 0)
				m_absentSinceMs = nowMs;
			if (nowMs - m_absentSinceMs >= kNewGameAbsenceMs)
				m_sawAbsence = true;
			if (m_sawAbsence) { // 게임 밖: 지난 판 정보는 버림
				m_day = 0;
				m_phase = None;
			}
			return;
		}
		if (m_sawAbsence && p == Day && m_phase == None)
			m_day = 1; // 게임 밖에 있다가 낮으로 시작 = 새 판 1일차
		m_sawAbsence = false;
		m_absentSinceMs = -1;
		if (m_phase == Night && p == Day && m_day > 0)
			++m_day; // 밤이 지나 다음 날
		m_phase = p;
		m_lastSeenMs = nowMs;
		if (nowMs - m_lastOcrMs >= kOcrEveryMs) {
			m_lastOcrMs = nowMs;
			wantOcr = true;
		}
	}
	if (wantOcr) {
		std::lock_guard<std::mutex> lock(m_mutex);
		m_job.assign(rgb, rgb + size_t(kW) * kH * 3);
		m_hasJob = true;
		m_cv.notify_one();
	}
}

void GameClock::workerLoop()
{
	std::vector<uint8_t> job;
	while (true) {
		{
			std::unique_lock<std::mutex> lock(m_mutex);
			m_cv.wait(lock, [this] { return m_stop || m_hasJob; });
			if (m_stop)
				return;
			job.swap(m_job);
			m_hasJob = false;
		}
		std::wstring raw;
		const int day = readDay(job.data(), &raw);
		std::lock_guard<std::mutex> lock(m_mutex);
		m_lastOcrText.clear();
		for (wchar_t c : raw) { // UTF-8 로 (로그용)
			const unsigned u = unsigned(c);
			if (u < 0x80) {
				m_lastOcrText += char(u);
			} else if (u < 0x800) {
				m_lastOcrText += char(0xC0 | (u >> 6));
				m_lastOcrText += char(0x80 | (u & 0x3F));
			} else {
				m_lastOcrText += char(0xE0 | (u >> 12));
				m_lastOcrText += char(0x80 | ((u >> 6) & 0x3F));
				m_lastOcrText += char(0x80 | (u & 0x3F));
			}
		}
		if (day > 0) {
			if (day == m_ocrCandidate)
				m_day = day; // 두 번 연속 같게 읽히면 믿음
			m_ocrCandidate = day;
		}
	}
}

std::string GameClock::label() const
{
	std::lock_guard<std::mutex> lock(m_mutex);
	return makeLabel(m_day, m_phase);
}

std::string GameClock::lastOcrText() const
{
	std::lock_guard<std::mutex> lock(m_mutex);
	return m_lastOcrText;
}
