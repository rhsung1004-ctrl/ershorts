#include "GlobalHotkey.h"

#include <QCoreApplication>

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

GlobalHotkey::GlobalHotkey(QObject *parent) : QObject(parent)
{
	QCoreApplication::instance()->installNativeEventFilter(this);
}

GlobalHotkey::~GlobalHotkey()
{
	unregisterKey();
	if (QCoreApplication::instance())
		QCoreApplication::instance()->removeNativeEventFilter(this);
}

bool GlobalHotkey::registerKey(unsigned vk, unsigned mods)
{
	unregisterKey();
	// hWnd = nullptr: 이 스레드의 메시지 큐로 WM_HOTKEY가 들어옴
	m_registered = RegisterHotKey(nullptr, m_id, mods | MOD_NOREPEAT, vk) != 0;
	return m_registered;
}

void GlobalHotkey::unregisterKey()
{
	if (m_registered) {
		UnregisterHotKey(nullptr, m_id);
		m_registered = false;
	}
}

bool GlobalHotkey::nativeEventFilter(const QByteArray &eventType, void *message, qintptr *)
{
	if (eventType != "windows_generic_MSG")
		return false;
	const MSG *msg = static_cast<const MSG *>(message);
	if (msg->message == WM_HOTKEY && int(msg->wParam) == m_id) {
		emit activated();
		return true;
	}
	return false;
}
