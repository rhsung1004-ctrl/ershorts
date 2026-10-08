#include "HudKillWatcher.h"

#include <algorithm>
#include <cmath>

namespace {
constexpr int kBright = 190;   // 흰 숫자 (회색 글씨 TK/K/A 는 이보다 어두움)
constexpr int kBandTop = 6;    // 숫자가 있는 줄 범위 (아래쪽 장식·이펙트 제외)
constexpr int kBandBottom = 24;
constexpr double kStableSec = 1.0;   // 이만큼 그대로여야 숫자가 바뀐 것으로 인정
constexpr double kRebaseSec = 3.0;   // 자리가 바뀐 모양이 이만큼 계속되면 새 기준
constexpr double kPairSec = 3.0;     // TK 와 K/A 변화가 이 안에 함께 있어야 함
} // namespace

void HudKillWatcher::reset()
{
	m_haveStable = false;
	m_stable.clear();
	m_cand.clear();
	m_alt.clear();
	m_candT = m_altT = -1;
	m_tkT = m_kaT = -1e9;
	m_k = m_a = false;
}

bool HudKillWatcher::extract(const uint8_t *g, Digits *out)
{
	out->clear();
	bool col[kW];
	for (int x = 0; x < kW; ++x) {
		col[x] = false;
		for (int y = kBandTop; y < kBandBottom && !col[x]; ++y)
			col[x] = g[y * kW + x] > kBright;
	}
	// 밝은 열 묶음 → 3칸 이하 틈은 한 숫자(두 자리 포함)로 붙임
	std::vector<std::pair<int, int>> runs;
	for (int x = 0; x < kW;) {
		if (!col[x]) {
			++x;
			continue;
		}
		int e = x;
		while (e < kW && col[e])
			++e;
		if (!runs.empty() && x - runs.back().second <= 3)
			runs.back().second = e;
		else
			runs.push_back({x, e});
		x = e;
	}
	Digits all;
	for (const auto &r : runs) {
		Group gr;
		gr.x0 = r.first;
		gr.x1 = r.second;
		int y0 = kH, y1 = -1, count = 0;
		for (int y = kBandTop; y < kBandBottom; ++y)
			for (int x = gr.x0; x < gr.x1; ++x)
				if (g[y * kW + x] > kBright) {
					y0 = std::min(y0, y);
					y1 = std::max(y1, y);
					++count;
				}
		if (y1 < 0)
			continue;
		gr.y0 = y0;
		gr.y1 = y1 + 1;
		// 숫자다운 크기만 (720 기준 숫자 높이 약 8px)
		if (gr.h() < 5 || gr.h() > 14 || gr.w() > int(gr.h() * 2.8) || count < 6)
			continue;
		gr.bits.resize(size_t(gr.w() * gr.h()));
		for (int y = gr.y0; y < gr.y1; ++y)
			for (int x = gr.x0; x < gr.x1; ++x)
				gr.bits[size_t((y - gr.y0) * gr.w() + (x - gr.x0))] = g[y * kW + x] > kBright;
		all.push_back(gr);
	}
	if (all.size() < 3)
		return false;
	out->assign(all.end() - 3, all.end()); // 오른쪽 세 개 = TK, K, A 숫자
	return true;
}

bool HudKillWatcher::sameShape(const Group &a, const Group &b)
{
	if (std::abs(a.w() - b.w()) > 1 || std::abs(a.h() - b.h()) > 1)
		return false;
	const int w = std::max(a.w(), b.w()), h = std::max(a.h(), b.h());
	int diff = 0, any = 0;
	for (int y = 0; y < h; ++y)
		for (int x = 0; x < w; ++x) {
			const bool pa = (x < a.w() && y < a.h()) && a.bits[size_t(y * a.w() + x)];
			const bool pb = (x < b.w() && y < b.h()) && b.bits[size_t(y * b.w() + x)];
			diff += pa != pb;
			any += pa || pb;
		}
	return diff <= std::max(1, int(0.12 * any));
}

bool HudKillWatcher::samePlace(const Digits &a, const Digits &b)
{
	for (int i = 0; i < 3; ++i) {
		const double tol = std::max(4.0, 0.7 * a[i].h());
		if (std::abs(a[i].cx() - b[i].cx()) > tol || std::abs(a[i].y0 - b[i].y0) > 2)
			return false;
	}
	return true;
}

bool HudKillWatcher::allSame(const Digits &a, const Digits &b)
{
	for (int i = 0; i < 3; ++i)
		if (!sameShape(a[i], b[i]))
			return false;
	return true;
}

void HudKillWatcher::feed(const uint8_t *gray, double t, std::vector<Event> *events)
{
	Digits d;
	if (!extract(gray, &d))
		return; // 숫자가 안 보임 (지도, 사망 화면, 가려짐 등)

	if (!m_haveStable) {
		if (m_cand.empty() || !samePlace(m_cand, d) || !allSame(m_cand, d)) {
			m_cand = d;
			m_candT = t;
		} else if (t - m_candT >= kStableSec) {
			m_stable = d;
			m_haveStable = true;
			m_cand.clear();
		}
		return;
	}

	if (!samePlace(m_stable, d)) {
		// 커서·아이콘이 끼었거나 숫자 자리가 바뀜 → 오래 그대로면 새 기준으로만 삼음 (알림 없음)
		if (m_alt.empty() || !samePlace(m_alt, d) || !allSame(m_alt, d)) {
			m_alt = d;
			m_altT = t;
		} else if (t - m_altT >= kRebaseSec) {
			m_stable = d;
			m_alt.clear();
			m_cand.clear();
		}
		return;
	}
	m_alt.clear();

	if (allSame(m_stable, d)) {
		m_cand.clear();
		return;
	}
	if (m_cand.empty() || !allSame(m_cand, d)) {
		m_cand = d;
		m_candT = t;
		return;
	}
	if (t - m_candT < kStableSec)
		return;

	// 숫자가 바뀜 확정
	bool changed[3];
	for (int i = 0; i < 3; ++i)
		changed[i] = !sameShape(m_cand[i], m_stable[i]);
	const bool reset = changed[0] && changed[1] && changed[2] && sameShape(m_cand[0], m_cand[1]) &&
			   sameShape(m_cand[1], m_cand[2]); // 새 판 시작 (0 0 0)
	const double when = m_candT;
	m_stable = m_cand;
	m_cand.clear();
	if (reset) {
		m_tkT = m_kaT = -1e9;
		return;
	}

	if (changed[0])
		m_tkT = when;
	if (changed[1] || changed[2]) {
		if (when - m_kaT > kPairSec)
			m_k = m_a = false;
		m_kaT = when;
		m_k = m_k || changed[1];
		m_a = m_a || changed[2];
	}
	// TK 와 K/A 가 함께(3초 안에) 바뀌었으면 내가 관여한 킬
	if (m_tkT > -1e8 && m_kaT > -1e8 && std::abs(m_tkT - m_kaT) <= kPairSec) {
		Event e;
		e.time = std::min(m_tkT, m_kaT);
		e.kill = m_k;
		e.assist = m_a;
		if (events)
			events->push_back(e);
		m_tkT = m_kaT = -1e9;
		m_k = m_a = false;
	}
}
