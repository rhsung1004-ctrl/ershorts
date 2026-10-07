#include "PreviewWidget.h"

#include <QResizeEvent>
#include <QShowEvent>
#include <QWindow>

#include <algorithm>

#include <graphics/graphics.h>
#include <graphics/vec4.h>

PreviewWidget::PreviewWidget(QWidget *parent) : QWidget(parent)
{
	// libobs가 이 위젯의 HWND에 직접 그리도록 네이티브 창으로 설정
	setAttribute(Qt::WA_PaintOnScreen);
	setAttribute(Qt::WA_NativeWindow);
	setAttribute(Qt::WA_NoSystemBackground);
	setAttribute(Qt::WA_OpaquePaintEvent);
	setMinimumSize(480, 270);
}

PreviewWidget::~PreviewWidget() { destroyDisplay(); }

void PreviewWidget::destroyDisplay()
{
	if (m_display) {
		obs_display_remove_draw_callback(m_display, &PreviewWidget::draw, this);
		obs_display_destroy(m_display);
		m_display = nullptr;
	}
}

void PreviewWidget::createDisplay()
{
	if (m_display || !isVisible() || !m_enabled)
		return;

	const qreal dpr = devicePixelRatioF();
	gs_init_data info = {};
	info.cx = uint32_t(width() * dpr);
	info.cy = uint32_t(height() * dpr);
	info.format = GS_BGRA;
	info.zsformat = GS_ZS_NONE;
	info.window.hwnd = reinterpret_cast<void *>(winId());

	m_display = obs_display_create(&info, 0xFF1E1E24);
	if (m_display)
		obs_display_add_draw_callback(m_display, &PreviewWidget::draw, this);
}

void PreviewWidget::showEvent(QShowEvent *e)
{
	QWidget::showEvent(e);
	createDisplay();
}

void PreviewWidget::resizeEvent(QResizeEvent *e)
{
	QWidget::resizeEvent(e);
	if (m_display) {
		const qreal dpr = devicePixelRatioF();
		obs_display_resize(m_display, uint32_t(width() * dpr), uint32_t(height() * dpr));
	}
}

static void drawRect(float x, float y, float w, float h, uint32_t argb)
{
	gs_effect_t *solid = obs_get_base_effect(OBS_EFFECT_SOLID);
	gs_eparam_t *color = gs_effect_get_param_by_name(solid, "color");
	gs_technique_t *tech = gs_effect_get_technique(solid, "Solid");

	vec4 c;
	vec4_from_rgba(&c, argb); // 0xAABBGGRR
	gs_effect_set_vec4(color, &c);

	gs_technique_begin(tech);
	gs_technique_begin_pass(tech, 0);

	gs_render_start(true);
	gs_vertex2f(x, y);
	gs_vertex2f(x + w, y);
	gs_vertex2f(x + w, y + h);
	gs_vertex2f(x, y + h);
	gs_vertex2f(x, y);
	gs_render_stop(GS_LINESTRIP);

	gs_technique_end_pass(tech);
	gs_technique_end(tech);
}

void PreviewWidget::draw(void *data, uint32_t cx, uint32_t cy)
{
	auto *self = static_cast<PreviewWidget *>(data);

	obs_video_info ovi;
	if (!obs_get_video_info(&ovi))
		return;

	const float baseW = float(ovi.base_width);
	const float baseH = float(ovi.base_height);

	// 비율 유지하며 위젯 안에 맞추기
	const float scale = std::min(float(cx) / baseW, float(cy) / baseH);
	const int vw = int(baseW * scale);
	const int vh = int(baseH * scale);
	const int vx = (int(cx) - vw) / 2;
	const int vy = (int(cy) - vh) / 2;

	gs_viewport_push();
	gs_projection_push();
	gs_ortho(0.0f, baseW, 0.0f, baseH, -100.0f, 100.0f);
	gs_set_viewport(vx, vy, vw, vh);

	obs_render_main_texture();

	if (self->m_showGuide) {
		// 쇼츠로 잘릴 9:16 영역 (가운데)
		const float guideW = baseH * 9.0f / 16.0f;
		drawRect((baseW - guideW) / 2.0f, 1.0f, guideW, baseH - 2.0f, 0xFF00D4FF);
	}

	gs_projection_pop();
	gs_viewport_pop();
}
