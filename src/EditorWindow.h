#pragma once

#include <QElapsedTimer>
#include <QJsonObject>
#include <QMainWindow>
#include <QVector>

#include <functional>

#include "EditProject.h"

class QAudioOutput;
class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QGraphicsRectItem;
class QGraphicsScene;
class QGraphicsSimpleTextItem;
class QGraphicsVideoItem;
class QGraphicsView;
class QLabel;
class QLineEdit;
class QListWidget;
class QMediaPlayer;
class QPlainTextEdit;
class QProgressBar;
class QPushButton;
class QSlider;
class QSpinBox;
class QTabWidget;
class QTimer;
class BeatDetector;
class ShortsExporter;
class TimelineWidget;
class ThumbnailCache;

// 매드무비 편집기
//  - 여러 클립을 한 타임라인에 이어 붙이고 자르기/순서 바꾸기
//  - 구간별 배속·효과, 컷 전환 효과
//  - BGM + BPM 자동 감지 + 컷을 비트에 맞추기
//  - 인트로/아웃트로, 자막, 9:16 미리보기, 내보내기
class EditorWindow : public QMainWindow {
	Q_OBJECT
public:
	// projectPath 가 존재하면 불러오고, 없으면 새 프로젝트로 만들어 newClips 를 추가
	EditorWindow(const QString &projectPath, const QStringList &newClips, const QString &clipsDir,
		     const QString &shortsDir, QWidget *parent = nullptr);
	~EditorWindow() override;

protected:
	void closeEvent(QCloseEvent *e) override;

private:
	// ── UI ──
	void buildUi();
	QWidget *buildClipTab();
	QWidget *buildSegmentTab();
	QWidget *buildMusicTab();
	QWidget *buildSubtitleTab();
	QWidget *buildCardTab();
	QWidget *buildExportTab();
	void setupShortcuts();
	void loadUiFromProject(bool keepPosition = false);

	// ── 실행 취소 / 다시 실행 (프로젝트 전체 스냅샷) ──
	void commitUndoStep();
	void undo();
	void redo();
	void restoreSnapshot(const QJsonObject &snapshot);
	void updateUndoButtons();

	// ── 클립 / 플레이어 ──
	void createPlayerFor(int sourceIndex);
	void onSourceInfo(int sourceIndex);
	void addClips();
	void refreshClipList();

	// ── 재생 (결과 시간 기준 시계) ──
	double position() const;
	void play();
	void pause();
	void togglePlay();
	void seek(double t);
	void tick();
	void syncPlayers(double t, bool playing);

	// ── 미리보기 캔버스 ──
	void applyLayoutToPreview();
	void rebuildSubtitleVisuals();
	void rebuildCardVisual();
	void updateOverlays(double t);

	// ── 구간 ──
	void splitAtPlayhead();
	void splitSelectedOnBeats();
	void deleteSelectedSegment();
	void duplicateSelectedSegment();
	void moveSelected(int delta);
	void selectSegment(int i);
	void onSegmentPropsChanged();

	// ── 음악 ──
	void chooseMusic();
	void clearMusic();
	void onMusicPropsChanged();
	void detectBeats();
	void snapToBeats();

	// ── 자막 ──
	void addSubtitle();
	void deleteSelectedSubtitle();
	void selectSubtitle(int i);
	void onSubtitlePropsChanged();
	void refreshSubtitleList();

	// ── 인트로/아웃트로 ──
	void onCardPropsChanged();

	// ── 기타 ──
	void onExport(bool previewQuality);
	void projectChanged();
	void updateTimeLabel(double t);
	void log(const QString &msg);
	QPushButton *makeColorButton(QColor *target, std::function<void()> onChange);
	static void paintColorButton(QPushButton *b, const QColor &c);

	EditProject m_project;
	QString m_clipsDir;
	QString m_shortsDir;
	bool m_syncing = false;

	// 재생 시계
	bool m_playing = false;
	double m_pos = 0.0;
	double m_playStart = 0.0;
	QElapsedTimer m_clock;
	int m_activeSource = -1;
	int m_activeSeg = -1;

	struct SourcePlayer {
		QMediaPlayer *player = nullptr;
		QAudioOutput *audio = nullptr;
		QGraphicsVideoItem *item = nullptr;
		QString path; // 현재 열려 있는 파일 (실행 취소로 클립 구성이 바뀔 때 비교용)
	};
	QVector<SourcePlayer> m_players;
	QMediaPlayer *m_bgm = nullptr;
	QAudioOutput *m_bgmAudio = nullptr;

	QTimer *m_tickTimer = nullptr;
	QTimer *m_undoTimer = nullptr;
	QVector<QJsonObject> m_undoStack;
	QVector<QJsonObject> m_redoStack;
	QJsonObject m_undoBaseline; // 마지막으로 기록된 상태
	bool m_restoring = false;
	QPushButton *m_undoBtn = nullptr;
	QPushButton *m_redoBtn = nullptr;
	QTimer *m_saveTimer = nullptr;
	ShortsExporter *m_exporter = nullptr;
	BeatDetector *m_beats = nullptr;

	// 미리보기
	QGraphicsView *m_view = nullptr;
	QGraphicsScene *m_scene = nullptr;
	QGraphicsRectItem *m_canvas = nullptr;
	QGraphicsRectItem *m_minimapHint = nullptr;
	QGraphicsRectItem *m_flashOverlay = nullptr;
	QGraphicsRectItem *m_cardItem = nullptr;
	QGraphicsSimpleTextItem *m_cardTitle = nullptr;
	QGraphicsSimpleTextItem *m_cardSub = nullptr;
	QGraphicsSimpleTextItem *m_effectBadge = nullptr;
	struct SubVisual {
		QGraphicsRectItem *box = nullptr;
		QGraphicsSimpleTextItem *text = nullptr;
	};
	QVector<SubVisual> m_subVisuals;

	QTabWidget *m_tabs = nullptr;
	QPushButton *m_playBtn = nullptr;
	QLabel *m_timeLabel = nullptr;
	TimelineWidget *m_timeline = nullptr;
	ThumbnailCache *m_thumbs = nullptr;
	QSlider *m_zoomSlider = nullptr;

	// 클립 탭
	QListWidget *m_clipList = nullptr;

	// 구간 탭
	QWidget *m_segProps = nullptr;
	QLabel *m_segInfo = nullptr;
	QComboBox *m_segSpeed = nullptr;
	QComboBox *m_segTrans = nullptr;
	QCheckBox *m_fxZoom = nullptr, *m_fxShake = nullptr, *m_fxGray = nullptr, *m_fxVivid = nullptr,
		  *m_fxVignette = nullptr;
	QCheckBox *m_rampIn = nullptr, *m_rampOut = nullptr;
	QDoubleSpinBox *m_rampLen = nullptr;

	// 음악 탭
	QLabel *m_musicFile = nullptr;
	QDoubleSpinBox *m_musicOffset = nullptr;
	QSlider *m_musicVol = nullptr;
	QSlider *m_gameVol = nullptr;
	QDoubleSpinBox *m_bpm = nullptr;
	QDoubleSpinBox *m_firstBeat = nullptr;
	QComboBox *m_beatEvery = nullptr;
	QCheckBox *m_snap = nullptr;
	QCheckBox *m_musicFade = nullptr;
	QLabel *m_beatStatus = nullptr;
	QCheckBox *m_fxBeatZoom = nullptr;
	QCheckBox *m_fxBeatShake = nullptr;
	QComboBox *m_fxBeatStrength = nullptr;
	QComboBox *m_fxBeatEvery = nullptr;
	QPushButton *m_detectBtn = nullptr;

	// 자막 탭
	QListWidget *m_subList = nullptr;
	QWidget *m_subProps = nullptr;
	QPlainTextEdit *m_subText = nullptr;
	QDoubleSpinBox *m_subStart = nullptr;
	QDoubleSpinBox *m_subEnd = nullptr;
	QSpinBox *m_subSize = nullptr;
	QPushButton *m_subColor = nullptr;
	QSlider *m_subY = nullptr;
	QCheckBox *m_subBox = nullptr;
	QColor m_subColorValue = Qt::white;

	// 인트로/아웃트로 탭
	struct CardUi {
		QCheckBox *enabled = nullptr;
		QDoubleSpinBox *duration = nullptr;
		QLineEdit *title = nullptr;
		QLineEdit *subtitle = nullptr;
		QPushButton *bg = nullptr;
		QPushButton *color = nullptr;
	};
	CardUi m_introUi, m_outroUi;

	// 내보내기 탭
	QComboBox *m_layout = nullptr;
	QLineEdit *m_outName = nullptr;
	QPushButton *m_exportBtn = nullptr;
	QPushButton *m_previewExportBtn = nullptr;
	QProgressBar *m_progress = nullptr;
	QPlainTextEdit *m_log = nullptr;
};
