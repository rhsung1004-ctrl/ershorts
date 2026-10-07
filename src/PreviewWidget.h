#pragma once

#include <QWidget>
#include <obs.h>

// libobs가 직접 D3D11로 그리는 미리보기 창 (+ 9:16 크롭 가이드)
// OBS Studio의 OBSQTDisplay와 같은 방식으로 만듦:
//  - 창이 실제로 화면에 표시된(exposed) 뒤에만 D3D 디스플레이 생성
//  - 부모 위젯까지 네이티브 창으로 바뀌지 않게 해서 창 핸들이 다시 만들어지는 일을 막음
class PreviewWidget : public QWidget {
	Q_OBJECT
public:
	explicit PreviewWidget(QWidget *parent = nullptr);
	~PreviewWidget() override;

	void destroyDisplay();
	void setDisplayEnabled(bool on) { m_enabled = on; }
	void setCropGuideVisible(bool v) { m_showGuide = v; }

	QPaintEngine *paintEngine() const override { return nullptr; }

protected:
	void showEvent(QShowEvent *e) override;
	void resizeEvent(QResizeEvent *e) override;
	void paintEvent(QPaintEvent *e) override;

private:
	void createDisplay();
	void resizeDisplay();
	void hookWindowSignals();
	QSize pixelSize() const;
	static void draw(void *data, uint32_t cx, uint32_t cy);

	obs_display_t *m_display = nullptr;
	bool m_showGuide = true;
	bool m_enabled = true;
	bool m_hooked = false;
	bool m_destroying = false;
};
