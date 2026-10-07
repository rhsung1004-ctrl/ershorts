#include "PreviewWidget.h"

#include <QResizeEvent>
#include <QScreen>
#include <QShowEvent>
#include <QWindow>

#include <algorithm>

#include <graphics/graphics.h>
#include <graphics/matrix4.h>

PreviewWidget::PreviewWidget(QWidget *parent) : QWidget(parent)
{
	// OBSQTDisplay 와 같은 속성 (libobs가 이 위젯의 창 핸들에 직접 그림)
	setAttribute(Qt::WA_PaintOnScreen);
	setAttribute(Qt::WA_StaticContents);
	setAttribute(Qt::WA_NoSystemBackground);
	setAttribute(Qt::WA_OpaquePaintEvent);
	setAttribute(Qt::WA_DontCreateNativeAncestors);
	setAttribute(Qt::WA_NativeWindow);
	setMinimumSize(480, 270);
}

PreviewWidget::~PreviewWidget()
{
	m_destroying = true;
	destroyDisplay();
}

void PreviewWidget::destroyDisplay()
{
	if (m_display) {
		obs_display_remove_draw_callback(m_display, &PreviewWidget::draw, this);
		obs_display_destroy(m_display);
		m_display = nullptr;
	}
}

QSize PreviewWidget::pixelSize() const
{
	const qreal dpr = devicePixelRatioF();
	return QSize(int(width() * dpr), int(height() * dpr));
}

void PreviewWidget::hookWindowSignals()
{
	QWindow *w = windowHandle();
	if (m_hooked || !w)
		return;
	m_hooked = true;
	connect(w, &QWindow::visibleChanged, this, [this](bool visible) {
		if (!visible)
			return;
		if (!m_display)
			createDisplay();
		else
			resizeDisplay();
	});
	connect(w, &QWindow::screenChanged, this, [this](QScreen *) {
		createDisplay();
		resizeDisplay();
	});
}

void PreviewWidget::createDisplay()
{
	if (m_display || m_destroying || !m_enabled)
		return;
	QWindow *w = windowHandle();
	if (!w || !w->isExposed()) // 화면에 실제로 보이기 전에는 만들지 않음
		return;
	const QSize size = pixelSize();
	if (size.width() <= 0 || size.height() <= 0)
		return;

	gs_init_data info = {};
	info.cx = uint32_t(size.width());
	info.cy = uint32_t(size.height());
	info.format = GS_BGRA;
	info.zsformat = GS_ZS_NONE;
	info.window.hwnd = reinterpret_cast<void *>(w->winId());

	m_display = obs_display_create(&info, 0xFF1E1E24);
	if (m_display)
		obs_display_add_draw_callback(m_display, &PreviewWidget::draw, this);
}

void PreviewWidget::resizeDisplay()
{
	if (!m_display || !isVisible())
		return;
	const QSize size = pixelSize();
	if (size.width() > 0 && size.height() > 0)
		obs_display_resize(m_display, uint32_t(size.width()), uint32_t(size.height()));
}

void PreviewWidget::showEvent(QShowEvent *e)
{
	QWidget::showEvent(e);
	hookWindowSignals();
	createDisplay();
}

void PreviewWidget::paintEvent(QPaintEvent *e)
{
	hookWindowSignals();
	createDisplay();
	QWidget::paintEvent(e);
}

void PreviewWidget::resizeEvent(QResizeEvent *e)
{
	QWidget::resizeEvent(e);
	createDisplay();
	resizeDisplay();
}

// 단색 사각형 (OBS 멀티뷰와 같은 방식: solid 효과 + 스프라이트)
static void drawBox(float x, float y, float w, float h, uint32_t argb)
{
	gs_effect_t *solid = obs_get_base_effect(OBS_EFFECT_SOLID);
	gs_eparam_t *color = gs_effect_get_param_by_name(solid, "color");
	gs_effect_set_color(color, argb);

	gs_matrix_push();
	gs_matrix_translate3f(x, y, 0.0f);
	while (gs_effect_loop(solid, "Solid"))
		gs_draw_sprite(nullptr, 0, uint32_t(std::max(1.0f, w)), uint32_t(std::max(1.0f, h)));
	gs_matrix_pop();
}

void PreviewWidget::draw(void *data, uint32_t cx, uint32_t cy)
{
	auto *self = static_cast<PreviewWidget *>(data);

	obs_video_info ovi;
	if (!obs_get_video_info(&ovi) || cx == 0 || cy == 0)
		return;

	const float baseW = float(ovi.base_width);
	const float baseH = float(ovi.base_height);

	// 비율 유지하며 위젯 안에 맞추기
	const float scale = std::min(float(cx) / baseW, float(cy) / baseH);
	const int vw = std::max(1, int(baseW * scale));
	const int vh = std::max(1, int(baseH * scale));
	const int vx = (int(cx) - vw) / 2;
	const int vy = (int(cy) - vh) / 2;

	gs_viewport_push();
	gs_projection_push();
	gs_ortho(0.0f, baseW, 0.0f, baseH, -100.0f, 100.0f);
	gs_set_viewport(vx, vy, vw, vh);

	obs_render_main_texture();

	if (self->m_showGuide) {
		// 쇼츠로 잘릴 9:16 영역 (가운데) 테두리
		const float guideW = baseH * 9.0f / 16.0f;
		const float gx = (baseW - guideW) / 2.0f;
		const float t = std::max(2.0f, baseH / 270.0f); // 화면에서 약 2px 두께
		const uint32_t c = 0xFFFFD400;                   // ARGB 노란색
		drawBox(gx, 0, t, baseH, c);
		drawBox(gx + guideW - t, 0, t, baseH, c);
		drawBox(gx, 0, guideW, t, c);
		drawBox(gx, baseH - t, guideW, t, c);
	}

	gs_projection_pop();
	gs_viewport_pop();
}
