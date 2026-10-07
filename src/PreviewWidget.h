#pragma once

#include <QWidget>
#include <obs.h>

// libobs가 직접 D3D11로 그리는 미리보기 창 (+ 9:16 크롭 가이드)
class PreviewWidget : public QWidget {
	Q_OBJECT
public:
	explicit PreviewWidget(QWidget *parent = nullptr);
	~PreviewWidget() override;

	void destroyDisplay();
	void setCropGuideVisible(bool v) { m_showGuide = v; }

	QPaintEngine *paintEngine() const override { return nullptr; }

protected:
	void showEvent(QShowEvent *e) override;
	void resizeEvent(QResizeEvent *e) override;
	void paintEvent(QPaintEvent *) override {}

private:
	void createDisplay();
	static void draw(void *data, uint32_t cx, uint32_t cy);

	obs_display_t *m_display = nullptr;
	bool m_showGuide = true;
};
