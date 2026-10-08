#pragma once

#include <cstdint>
#include <vector>

// 이터널리턴 화면 오른쪽 위 "TK x  K x  A x" 숫자를 지켜보다가,
// TK(팀 킬)와 K(내 킬) 또는 A(어시스트)가 함께 바뀌면 "내가 관여한 킬"로 알려 줌
//  - 숫자는 흰색, 글자(TK/K/A)는 회색이라 밝은 부분만 보면 숫자만 남음
//  - 숫자를 읽지 않고 "모양이 바뀌었는지"만 봄 (1초 이상 그대로일 때만 인정)
//  - 마우스 커서·아이콘이 숫자를 가리면 위치가 어긋나므로 무시
//  - 순수 C++ (OBS 영상 스레드와 저장된 클립 테스트에서 함께 사용)
class HudKillWatcher {
public:
	// 게임 화면 높이 720 기준 영역: 오른쪽 끝에서 왼쪽으로 160, 위에서 4~36
	static constexpr int kW = 160;
	static constexpr int kH = 32;
	static constexpr double kRoiRight = 0.0;  // 오른쪽 여백 (720 기준 px)
	static constexpr double kRoiTop = 4.0;

	struct Event {
		double time = 0.0; // 숫자가 바뀐 시각 (넣은 시간 기준, 초)
		bool kill = false; // K 가 올라감 (내 킬)
		bool assist = false;
	};

	void reset();
	// gray: kW x kH 밝기(0~255, 전체 범위), t: 초
	void feed(const uint8_t *gray, double t, std::vector<Event> *events);

private:
	struct Group {
		int x0 = 0, x1 = 0, y0 = 0, y1 = 0;
		std::vector<uint8_t> bits; // (x1-x0) x (y1-y0)
		double cx() const { return 0.5 * (x0 + x1); }
		int w() const { return x1 - x0; }
		int h() const { return y1 - y0; }
	};
	using Digits = std::vector<Group>; // 왼쪽부터 TK, K, A

	static bool extract(const uint8_t *gray, Digits *out);
	static bool sameShape(const Group &a, const Group &b);
	static bool samePlace(const Digits &a, const Digits &b);
	static bool allSame(const Digits &a, const Digits &b);

	bool m_haveStable = false;
	Digits m_stable;
	Digits m_cand;      // 바뀐 모양 후보 (같은 자리)
	double m_candT = -1;
	Digits m_alt;       // 자리가 어긋난 모양 후보 (레이아웃 변화면 3초 뒤 새 기준으로)
	double m_altT = -1;

	double m_tkT = -1e9; // 최근 TK 변화 시각
	double m_kaT = -1e9; // 최근 K/A 변화 시각
	bool m_k = false, m_a = false;
};
