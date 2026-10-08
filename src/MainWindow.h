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
class QTimer;
class EditorWindow;
class GlobalHotkey;
class PreviewWidget;
class ClipInfoCache;
class QListWidgetItem;

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

	// 클립 목록
	QStringList selectedClipPaths() const; // 녹화된 순서(오래된 것 먼저)
	void updateClipItem(QListWidgetItem *item);
	void toggleFavorite();
	void renameClip();
	void deleteClips();
	void showClipMenu(const QPoint &pos);
	bool isFavorite(const QString &path) const;
	void setFavorite(const QString &path, bool on);
	void updateProjectsForRename(const QString &oldPath, const QString &newPath);

	// 킬 사운드 자동 저장
	double killThreshold() const;
	void applyKillDetection();
	void onKillSound(double score, int kind);
	void findKillSoundsInClip(const QString &path); // 저장된 클립으로 감지 테스트

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
	QCheckBox *m_killAuto = nullptr;
	QComboBox *m_killSens = nullptr;
	QTimer *m_killSaveTimer = nullptr;
	qint64 m_killFirstMs = 0; // 이번 연속 킬에서 첫 감지 시각
	QPushButton *m_toggleBtn = nullptr;
	QPushButton *m_saveBtn = nullptr;

	// 클립
	QListWidget *m_clips = nullptr;
	QCheckBox *m_favOnly = nullptr;
	ClipInfoCache *m_clipInfo = nullptr;
	QStringList m_favorites; // 즐겨찾기한 클립 파일 이름

	QPlainTextEdit *m_log = nullptr;
	QString m_outputDir;
	bool m_safeMode = false;
};
