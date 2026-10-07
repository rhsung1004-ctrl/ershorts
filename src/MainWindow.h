#pragma once

#include <QMainWindow>

#include "ObsCore.h"

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPlainTextEdit;
class QProgressBar;
class QPushButton;
class QSpinBox;
class EditorWindow;
class GlobalHotkey;
class PreviewWidget;

class MainWindow : public QMainWindow {
	Q_OBJECT
public:
	explicit MainWindow(bool safeMode = false, QWidget *parent = nullptr);
	~MainWindow() override;

	bool initialize(QString *error);

protected:
	void closeEvent(QCloseEvent *e) override;

private slots:
	void onApplySettings();
	void onToggleReplay();
	void onSaveClip();
	void onClipSaved(const QString &path);
	void onReplayStateChanged(bool active);
	void openEditor();
	void openProject();
	void refreshWindowList();
	void refreshClipList();
	void log(const QString &msg);

private:
	void buildUi();
	void showEditor(EditorWindow *editor);
	void loadSettings();
	void saveSettings();
	ObsCore::Settings currentSettings() const;
	QString clipDir() const;
	QString shortsDir() const;
	QString projectsDir() const;

	ObsCore *m_core = nullptr;
	GlobalHotkey *m_hotkey = nullptr;

	PreviewWidget *m_preview = nullptr;

	// 녹화
	QLabel *m_status = nullptr;
	QComboBox *m_captureMode = nullptr;
	QComboBox *m_gameWindow = nullptr; // 캡처할 창 (0번 = 자동)
	QSpinBox *m_bufferSec = nullptr;
	QCheckBox *m_mic = nullptr;
	QCheckBox *m_autoStart = nullptr;
	QCheckBox *m_gameAudioOnly = nullptr;
	QCheckBox *m_saveSound = nullptr;
	QPushButton *m_toggleBtn = nullptr;
	QPushButton *m_saveBtn = nullptr;

	// 클립
	QListWidget *m_clips = nullptr;

	QPlainTextEdit *m_log = nullptr;
	QString m_outputDir;
	bool m_safeMode = false;
};
