#pragma once

#include <QAbstractNativeEventFilter>
#include <QObject>

// 게임이 포커스를 가진 상태에서도 동작하는 전역 단축키 (Win32 RegisterHotKey)
class GlobalHotkey : public QObject, public QAbstractNativeEventFilter {
	Q_OBJECT
public:
	explicit GlobalHotkey(QObject *parent = nullptr);
	~GlobalHotkey() override;

	// vk: 가상 키 코드 (예: VK_F9 = 0x78), mods: MOD_CONTROL / MOD_ALT / MOD_SHIFT 조합
	bool registerKey(unsigned vk, unsigned mods = 0);
	void unregisterKey();

	bool nativeEventFilter(const QByteArray &eventType, void *message, qintptr *result) override;

signals:
	void activated();

private:
	int m_id = 0xE121;
	bool m_registered = false;
};
