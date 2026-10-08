#pragma once

#include <QMainWindow>

#include "ObsCore.h"

#include <QDateTime>
#include <QList>

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
	void applyKillWatch();
	void onHudKill(bool kill, bool assist);
	void findKillsInClip(const QString &path); // 저장된 클립으로 인식 테스트

	// 클립 이름 = 게임 시간 ("4일차 낮 …")
	void saveClipWith(const QString &label, const QString &tag);
	QString renameToGameTime(const QString &path, const QString &label, const QString &tag, const QDateTime &when);
	void renameClipsByGameTime();

	// 저장 폴더 바꾸기 (다른 드라이브 가능)
	void chooseOutputDir();
	void updateOutputDirLabel();

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
	QComboBox *m_killDelay = nullptr;
	QTimer *m_killSaveTimer = nullptr;
	qint64 m_killFirstMs = 0; // 이번 연속 킬에서 첫 감지 시각
	QString m_killWhat;       // 이번 자동 저장의 종류 (킬/어시스트)
	QString m_killLabel;      // 첫 킬 때의 게임 시간
	QCheckBox *m_nameByClock = nullptr;
	QLineEdit *m_outDirEdit = nullptr;
	struct PendingName {
		QString label;
		QString tag;
		QDateTime when;
	};
	QList<PendingName> m_pendingNames; // 저장 요청 순서대로 (저장 완료 때 이름 붙임)
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
