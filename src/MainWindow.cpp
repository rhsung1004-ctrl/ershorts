#include "MainWindow.h"

#include "ClipInfoCache.h"
#include "Diagnostics.h"
#include "GameClock.h"
#include "HudKillWatcher.h"
#include "ShortsExporter.h"
#include "EditorWindow.h"
#include "GlobalHotkey.h"
#include "PreviewWidget.h"

#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QInputDialog>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMenu>
#include <QShortcut>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QItemSelectionModel>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QSettings>
#include <QSpinBox>
#include <QSplitter>
#include <QStandardPaths>
#include <QTimer>
#include <QStorageInfo>
#include <QProgressDialog>
#include <QHash>
#include <QCoreApplication>

#include <future>
#include <QPainter>
#include <QPixmap>
#include <QIcon>
#include <QProcess>
#include <QScreen>
#include <QGuiApplication>
#include <QUrl>
#include <QVBoxLayout>

#include <algorithm>

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

MainWindow::MainWindow(bool safeMode, QWidget *parent) : QMainWindow(parent), m_safeMode(safeMode)
{
	setWindowTitle("ERShorts — 이터널리턴 쇼츠 메이커");
	resize(1400, 820);

	m_core = new ObsCore(this);
	m_hotkey = new GlobalHotkey(this);

	buildUi();
	loadSettings();

	connect(m_core, &ObsCore::logMessage, this, &MainWindow::log);
	connect(m_core, &ObsCore::clipSaved, this, &MainWindow::onClipSaved);
	connect(m_core, &ObsCore::replayStateChanged, this, &MainWindow::onReplayStateChanged);
	connect(m_core, &ObsCore::hudKillDetected, this, &MainWindow::onHudKill);
	connect(m_hotkey, &GlobalHotkey::activated, this, &MainWindow::onSaveClip);
}

MainWindow::~MainWindow() = default;

bool MainWindow::initialize(QString *error)
{
	m_core->setVideoQuality(m_recQuality->currentData().toInt());
	connect(m_recQuality, &QComboBox::currentIndexChanged, this, [this] {
		saveSettings();
		log("녹화 화질: " + m_recQuality->currentText() + " → 프로그램을 다시 켜면 적용돼요");
	});
	if (!m_core->startup(error))
		return false;
	refreshWindowList(); // OBS 플러그인이 올라온 뒤에 창 목록을 채움
	if (!m_core->applySettings(currentSettings(), error))
		return false;

	// 캡처 방식/창을 바꾸면 바로 적용
	connect(m_captureMode, &QComboBox::currentIndexChanged, this, [this] {
		m_gameWindow->setEnabled(m_captureMode->currentData().toInt() != int(ObsCore::CaptureMode::Monitor));
		m_gameAudioOnly->setEnabled(m_captureMode->currentData().toInt() == int(ObsCore::CaptureMode::Window));
		onApplySettings();
	});
	connect(m_gameWindow, &QComboBox::activated, this, [this] { onApplySettings(); });
	connect(m_gameAudioOnly, &QCheckBox::toggled, this, [this] { onApplySettings(); });
	m_gameWindow->setEnabled(m_captureMode->currentData().toInt() != int(ObsCore::CaptureMode::Monitor));
	m_gameAudioOnly->setEnabled(m_captureMode->currentData().toInt() == int(ObsCore::CaptureMode::Window));

	// 게임을 프로그램보다 늦게 켜도 창 캡처가 자동으로 연결되도록 3초마다 확인
	auto *retarget = new QTimer(this);
	connect(retarget, &QTimer::timeout, this, [this] { m_core->retargetWindowIfNeeded(); });
	retarget->start(3000);

	if (m_hotkey->registerKey(VK_F9))
		log("단축키 F9: 최근 클립 저장");
	else
		log("⚠ F9 단축키 등록 실패 (다른 프로그램이 사용 중일 수 있음)");

	refreshClipList();

	applyKillWatch();
	m_core->setGameClock(m_nameByClock->isChecked());
	connect(m_nameByClock, &QCheckBox::toggled, this, [this](bool on) {
		m_core->setGameClock(on);
		saveSettings();
	});
	connect(m_killAuto, &QCheckBox::toggled, this, [this] {
		applyKillWatch();
		saveSettings();
		log(m_killAuto->isChecked() ? "킬 자동 저장 켬 (화면 오른쪽 위 TK·K·A 숫자를 지켜봐요)" : "킬 자동 저장 끔");
	});
	connect(m_killDelay, &QComboBox::currentIndexChanged, this, [this] { saveSettings(); });

	if (m_safeMode) {
		log("안전 모드: 자동 녹화와 실시간 미리보기를 껐습니다. '버퍼 시작'으로 직접 켤 수 있어요.");
	} else if (m_autoStart->isChecked()) {
		QString err;
		if (!m_core->startReplayBuffer(&err))
			log(err);
	}
	return true;
}

void MainWindow::buildUi()
{
	auto *splitter = new QSplitter(this);
	setCentralWidget(splitter);

	// ── 왼쪽: 미리보기 + 로그 ─────────────────────────
	auto *left = new QWidget;
	auto *leftLay = new QVBoxLayout(left);
	m_preview = new PreviewWidget;
	m_preview->setDisplayEnabled(!m_safeMode);
	auto *guide = new QCheckBox("9:16 크롭 영역 표시");
	guide->setChecked(true);
	connect(guide, &QCheckBox::toggled, this, [this](bool v) { m_preview->setCropGuideVisible(v); });
	m_log = new QPlainTextEdit;
	m_log->setReadOnly(true);
	m_log->setMaximumHeight(150);
	leftLay->addWidget(m_preview, 1);
	leftLay->addWidget(guide);
	auto *perfNote = new QLabel("게임 화면으로 넘어가면 이 미리보기는 잠시 멈춰요 (녹화는 계속됩니다)");
	perfNote->setStyleSheet("color:#9a9aa5; font-size:11px;");
	leftLay->addWidget(perfNote);
	leftLay->addWidget(m_log);
	splitter->addWidget(left);

	// ── 오른쪽: 컨트롤 패널 ──────────────────────────
	auto *right = new QWidget;
	right->setMinimumWidth(360);
	right->setMaximumWidth(440);
	auto *rightLay = new QVBoxLayout(right);

	// 녹화
	auto *recBox = new QGroupBox("녹화 (리플레이 버퍼)");
	auto *recForm = new QFormLayout(recBox);
	m_status = new QLabel;
	m_captureMode = new QComboBox;
	m_captureMode->addItem("창 캡처 (권장)", int(ObsCore::CaptureMode::Window));
	m_captureMode->addItem("게임 캡처 (훅)", int(ObsCore::CaptureMode::Game));
	m_captureMode->addItem("모니터 전체 캡처", int(ObsCore::CaptureMode::Monitor));
	m_captureMode->setToolTip("창 캡처: 게임에 끼어들지 않아 안정적 (창/테두리 없는 창 모드 모두 가능)\n"
				  "게임 캡처: 전체화면 전용, 안티치트가 막으면 검은 화면\n"
				  "모니터 캡처: 화면 전체를 그대로 녹화");
	m_gameWindow = new QComboBox;
	m_gameWindow->setMinimumContentsLength(18);
	m_gameWindow->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
	auto *refreshWinBtn = new QPushButton("↻");
	refreshWinBtn->setFixedWidth(30);
	refreshWinBtn->setToolTip("창 목록 새로고침");
	auto *winRow = new QHBoxLayout;
	winRow->addWidget(m_gameWindow, 1);
	winRow->addWidget(refreshWinBtn);
	connect(refreshWinBtn, &QPushButton::clicked, this, &MainWindow::refreshWindowList);
	m_bufferSec = new QSpinBox;
	m_bufferSec->setRange(10, 300);
	m_bufferSec->setSuffix(" 초");
	m_mic = new QCheckBox("마이크 녹음");
	m_gameAudioOnly = new QCheckBox("게임 소리만 녹음 (알림음·디스코드 제외)");
	m_gameAudioOnly->setToolTip("창 캡처일 때 이터널리턴 프로그램의 소리만 녹음합니다.\n"
				    "끄면 컴퓨터에서 나는 모든 소리를 녹음합니다.");
	m_saveSound = new QCheckBox("클립 저장 시 알림음");
	m_saveSound->setToolTip("'게임 소리만 녹음'을 끈 상태에서 켜면 알림음이 다음 클립에 녹음될 수 있어요");
	m_autoStart = new QCheckBox("실행 시 자동 시작");

	// 킬 자동 저장 (화면의 TK/K/A 숫자 인식)
	m_killAuto = new QCheckBox("내가 킬·어시스트하면 자동 저장");
	m_killAuto->setToolTip("화면 오른쪽 위 'TK  K  A' 숫자를 지켜보다가, TK와 함께 K(킬) 또는 A(어시스트)가\n"
			       "올라가면 정해 둔 시간 뒤에 클립을 저장합니다. TK만 오르면(팀원끼리 킬) 저장하지 않아요.\n"
			       "연속 킬은 한 클립으로 묶어 저장해요.");
	m_killDelay = new QComboBox;
	for (int sec : {5, 10, 15})
		m_killDelay->addItem(QString("%1초 뒤 저장").arg(sec), sec);
	m_killDelay->setToolTip("킬 뒤 장면을 얼마나 더 담을지");
	m_killSaveTimer = new QTimer(this);
	m_killSaveTimer->setSingleShot(true);
	connect(m_killSaveTimer, &QTimer::timeout, this, [this] {
		if (!m_core->isReplayActive())
			return;
		log("킬 장면 자동 저장");
		saveClipWith(m_killLabel.isEmpty() ? m_core->gameTimeLabel() : m_killLabel, m_killWhat);
	});
	auto *applyBtn = new QPushButton("설정 적용");
	m_toggleBtn = new QPushButton("버퍼 시작");
	m_saveBtn = new QPushButton("클립 저장 (F9)");
	m_saveBtn->setMinimumHeight(40);

	recForm->addRow("상태", m_status);
	recForm->addRow("캡처 방식", m_captureMode);
	recForm->addRow("게임 창", winRow);
	recForm->addRow("버퍼 길이", m_bufferSec);
	m_recQuality = new QComboBox;
	m_recQuality->addItem("원본 해상도 · 60fps (기본, 가장 선명)", 0);
	m_recQuality->addItem("원본 해상도 · 30fps", 1);
	m_recQuality->addItem("1080p · 60fps (가벼움)", 2);
	m_recQuality->addItem("1080p · 30fps (가장 가벼움)", 3);
	m_recQuality->setToolTip("게임이 버벅이면 가벼운 설정을 고르세요. 다음 실행부터 적용됩니다.\n"
				 "쇼츠는 1080x1920이라 1080p로도 충분하지만, 가운데를 크게 확대하는 영상은 원본이 더 선명해요.");
	recForm->addRow("녹화 화질", m_recQuality);
	// 저장 폴더 (클립·쇼츠·프로젝트가 이 아래 clips / shorts / projects 폴더에 저장됨)
	m_outDirEdit = new QLineEdit;
	m_outDirEdit->setReadOnly(true);
	auto *outDirBtn = new QPushButton("변경…");
	outDirBtn->setToolTip("클립·쇼츠·프로젝트를 저장할 폴더 (D드라이브 등 다른 드라이브도 가능)");
	auto *outRow = new QHBoxLayout;
	outRow->addWidget(m_outDirEdit, 1);
	outRow->addWidget(outDirBtn);
	recForm->addRow("저장 폴더", outRow);
	connect(outDirBtn, &QPushButton::clicked, this, &MainWindow::chooseOutputDir);
	recForm->addRow(m_mic);
	recForm->addRow(m_gameAudioOnly);
	recForm->addRow(m_saveSound);
	recForm->addRow(m_autoStart);
	m_nameByClock = new QCheckBox("클립 이름을 게임 시간으로 (예: 4일차 낮)");
	m_nameByClock->setToolTip("화면 가운데 위 'N일 차'와 해/달 아이콘을 읽어서, 저장할 때의 게임 시간을 클립 이름으로 씁니다.\n"
				  "며칠째인지는 Windows 글자 인식으로 읽어요. 못 읽으면 '낮'/'밤'만 붙여요.");
	recForm->addRow(m_nameByClock);
	auto *killRow = new QHBoxLayout;
	killRow->addWidget(m_killAuto, 1);
	killRow->addWidget(m_killDelay);
	recForm->addRow(killRow);
	auto *btnRow = new QHBoxLayout;
	btnRow->addWidget(applyBtn);
	btnRow->addWidget(m_toggleBtn);
	recForm->addRow(btnRow);
	recForm->addRow(m_saveBtn);
	rightLay->addWidget(recBox);

	connect(applyBtn, &QPushButton::clicked, this, &MainWindow::onApplySettings);
	connect(m_toggleBtn, &QPushButton::clicked, this, &MainWindow::onToggleReplay);
	connect(m_saveBtn, &QPushButton::clicked, this, &MainWindow::onSaveClip);

	// 클립 목록
	auto *clipBox = new QGroupBox("저장된 클립 (Ctrl/Shift로 여러 개 선택, 우클릭 메뉴)");
	auto *clipLay = new QVBoxLayout(clipBox);
	m_clipInfo = new ClipInfoCache(this);
	m_clips = new QListWidget;
	m_clips->setSelectionMode(QAbstractItemView::ExtendedSelection);
	m_clips->setIconSize(QSize(128, 72));
	m_clips->setSpacing(2);
	m_clips->setContextMenuPolicy(Qt::CustomContextMenu);
	connect(m_clips, &QListWidget::customContextMenuRequested, this, &MainWindow::showClipMenu);
	connect(m_clipInfo, &ClipInfoCache::ready, this, [this](const QString &path) {
		for (int i = 0; i < m_clips->count(); ++i)
			if (m_clips->item(i)->data(Qt::UserRole).toString() == path)
				updateClipItem(m_clips->item(i));
	});
	auto *renameKey = new QShortcut(QKeySequence(Qt::Key_F2), m_clips);
	renameKey->setContext(Qt::WidgetShortcut);
	connect(renameKey, &QShortcut::activated, this, &MainWindow::renameClip);
	auto *deleteKey = new QShortcut(QKeySequence(QKeySequence::Delete), m_clips);
	deleteKey->setContext(Qt::WidgetShortcut);
	connect(deleteKey, &QShortcut::activated, this, &MainWindow::deleteClips);

	auto *manageRow = new QHBoxLayout;
	auto *favBtn = new QPushButton("★ 즐겨찾기");
	favBtn->setToolTip("선택한 클립을 즐겨찾기에 넣거나 뺍니다");
	auto *renameBtn = new QPushButton("이름 바꾸기");
	renameBtn->setToolTip("F2");
	auto *deleteBtn = new QPushButton("삭제");
	deleteBtn->setToolTip("휴지통으로 이동 (Delete)");
	m_favOnly = new QCheckBox("★만 보기");
	manageRow->addWidget(favBtn);
	manageRow->addWidget(renameBtn);
	manageRow->addWidget(deleteBtn);
	manageRow->addStretch();
	manageRow->addWidget(m_favOnly);
	connect(favBtn, &QPushButton::clicked, this, &MainWindow::toggleFavorite);
	connect(renameBtn, &QPushButton::clicked, this, &MainWindow::renameClip);
	connect(deleteBtn, &QPushButton::clicked, this, &MainWindow::deleteClips);
	connect(m_favOnly, &QCheckBox::toggled, this, &MainWindow::refreshClipList);

	auto *editBtn = new QPushButton("🎬 선택한 클립으로 영상 만들기");
	editBtn->setMinimumHeight(44);
	auto *openProjBtn = new QPushButton("📂 저장된 프로젝트 열기");
	auto *clipBtns = new QHBoxLayout;
	auto *playBtn = new QPushButton("재생");
	auto *refreshBtn = new QPushButton("새로고침");
	auto *openDirBtn = new QPushButton("클립 폴더");
	auto *openShortsBtn = new QPushButton("쇼츠 폴더");
	auto *openLogsBtn = new QPushButton("로그 폴더");
	openLogsBtn->setToolTip("문제가 생겼을 때 이 폴더의 최신 로그 파일을 보내주세요");
	connect(openLogsBtn, &QPushButton::clicked, this,
		[] { QDesktopServices::openUrl(QUrl::fromLocalFile(Diagnostics::logDir())); });
	clipBtns->addWidget(playBtn);
	clipBtns->addWidget(refreshBtn);
	clipBtns->addWidget(openDirBtn);
	clipBtns->addWidget(openShortsBtn);
	clipBtns->addWidget(openLogsBtn);
	auto *aboutBtn = new QPushButton("정보");
	clipBtns->addWidget(aboutBtn);
	connect(aboutBtn, &QPushButton::clicked, this, [this] {
		QMessageBox::about(
			this, "ERShorts 정보",
			"<h3>ERShorts — 이터널리턴 쇼츠 메이커</h3>"
			"<p>Copyright © 2026 rhsung1004-ctrl<br>"
			"이 프로그램은 <b>GNU GPL v3</b>(또는 이후 버전)로 배포되는 자유 소프트웨어입니다. "
			"누구나 사용·수정·재배포할 수 있으며, 어떠한 보증도 제공하지 않습니다.</p>"
			"<p>소스 코드: <a href='https://github.com/rhsung1004-ctrl/ershorts'>"
			"github.com/rhsung1004-ctrl/ershorts</a></p>"
			"<p>포함된 구성요소: OBS Studio/libobs (GPL-2.0+), Qt 6 (LGPL-3.0), FFmpeg (GPL-3.0), "
			"Visual C++ 런타임 — 자세한 내용은 프로그램 폴더의 THIRD_PARTY_NOTICES.txt 참고</p>"
			"<p><small>ERShorts는 이터널리턴(Eternal Return), 님블뉴런(Nimble Neuron), OBS Project와 관련 없는 "
			"비공식 팬 제작 도구입니다. 화면만 캡처하며 게임 파일이나 메모리는 건드리지 않고, "
			"어떤 정보도 외부로 보내지 않습니다.</small></p>");
	});
	clipLay->addWidget(m_clips);
	clipLay->addLayout(manageRow);
	clipLay->addWidget(editBtn);
	clipLay->addWidget(openProjBtn);
	clipLay->addLayout(clipBtns);
	rightLay->addWidget(clipBox, 1);

	connect(editBtn, &QPushButton::clicked, this, &MainWindow::openEditor);
	connect(m_clips, &QListWidget::itemDoubleClicked, this, &MainWindow::openEditor);
	connect(openProjBtn, &QPushButton::clicked, this, &MainWindow::openProject);
	connect(playBtn, &QPushButton::clicked, this, [this] {
		if (QListWidgetItem *it = m_clips->currentItem())
			QDesktopServices::openUrl(QUrl::fromLocalFile(it->data(Qt::UserRole).toString()));
	});
	connect(refreshBtn, &QPushButton::clicked, this, &MainWindow::refreshClipList);
	connect(openDirBtn, &QPushButton::clicked, this,
		[this] { QDesktopServices::openUrl(QUrl::fromLocalFile(clipDir())); });
	connect(openShortsBtn, &QPushButton::clicked, this, [this] {
		QDir().mkpath(shortsDir());
		QDesktopServices::openUrl(QUrl::fromLocalFile(shortsDir()));
	});

	splitter->addWidget(right);
	splitter->setStretchFactor(0, 1);

	onReplayStateChanged(false);
}

void MainWindow::loadSettings()
{
	QSettings s("ERShorts", "ERShorts");
	// 캡처 방식 (이전 버전 설정 키와 겹치지 않게 새 키 사용, 기본 = 창 캡처)
	m_captureMode->setCurrentIndex(std::max(0, m_captureMode->findData(s.value("captureMode2", 0).toInt())));
	m_gameWindow->setProperty("saved", s.value("gameWindow2").toString());
	m_bufferSec->setValue(s.value("bufferSec", 45).toInt());
	m_mic->setChecked(s.value("mic", false).toBool());
	m_gameAudioOnly->setChecked(s.value("gameAudioOnly", true).toBool());
	m_saveSound->setChecked(s.value("saveSound", false).toBool());
	m_autoStart->setChecked(s.value("autoStart", true).toBool());
	m_killAuto->setChecked(s.value("killAutoHud", false).toBool());
	m_recQuality->setCurrentIndex(std::max(0, m_recQuality->findData(s.value("recQuality", 0).toInt())));
	m_nameByClock->setChecked(s.value("nameByClock", true).toBool());
	m_killDelay->setCurrentIndex(std::max(0, m_killDelay->findData(s.value("killDelay", 10).toInt())));
	m_favorites = s.value("favoriteClips").toStringList();
	m_favOnly->blockSignals(true);
	m_favOnly->setChecked(s.value("favoriteOnly", false).toBool());
	m_favOnly->blockSignals(false);
	m_outputDir = s.value("outputDir",
			      QStandardPaths::writableLocation(QStandardPaths::MoviesLocation) + "/ERShorts")
			      .toString();
	updateOutputDirLabel();
}

void MainWindow::saveSettings()
{
	QSettings s("ERShorts", "ERShorts");
	s.setValue("captureMode2", m_captureMode->currentData().toInt());
	s.setValue("gameWindow2", m_gameWindow->currentData().toString());
	s.setValue("bufferSec", m_bufferSec->value());
	s.setValue("mic", m_mic->isChecked());
	s.setValue("gameAudioOnly", m_gameAudioOnly->isChecked());
	s.setValue("saveSound", m_saveSound->isChecked());
	s.setValue("autoStart", m_autoStart->isChecked());
	s.setValue("killAutoHud", m_killAuto->isChecked());
	s.setValue("recQuality", m_recQuality->currentData().toInt());
	s.setValue("nameByClock", m_nameByClock->isChecked());
	s.setValue("killDelay", m_killDelay->currentData().toInt());
	s.setValue("favoriteClips", m_favorites);
	s.setValue("favoriteOnly", m_favOnly->isChecked());
	s.setValue("outputDir", m_outputDir);
}

ObsCore::Settings MainWindow::currentSettings() const
{
	ObsCore::Settings st;
	st.captureMode = ObsCore::CaptureMode(m_captureMode->currentData().toInt());
	st.gameWindow = m_gameWindow->currentData().toString();
	st.bufferSeconds = m_bufferSec->value();
	st.captureMic = m_mic->isChecked();
	st.gameAudioOnly = m_gameAudioOnly->isChecked();
	st.outputDir = clipDir();
	return st;
}

QString MainWindow::clipDir() const { return m_outputDir + "/clips"; }
QString MainWindow::shortsDir() const { return m_outputDir + "/shorts"; }
QString MainWindow::projectsDir() const { return m_outputDir + "/projects"; }

void MainWindow::onApplySettings()
{
	QString err;
	if (!m_core->applySettings(currentSettings(), &err))
		log("설정 적용 실패: " + err);
	else
		log("설정 적용됨");
	saveSettings();
}

void MainWindow::onToggleReplay()
{
	if (m_core->isReplayActive()) {
		m_core->stopReplayBuffer();
	} else {
		QString err;
		if (!m_core->startReplayBuffer(&err))
			log(err);
	}
}

void MainWindow::onSaveClip() { saveClipWith(m_core->gameTimeLabel(), QString()); }

void MainWindow::saveClipWith(const QString &label, const QString &tag)
{
	if (!m_core->saveReplay()) {
		log("버퍼가 꺼져 있어 저장할 수 없습니다");
		if (m_saveSound->isChecked())
			MessageBeep(MB_ICONHAND);
		return;
	}
	// 저장 버튼을 누른 순간의 게임 시간을 기억해 뒀다가, 파일이 다 저장되면 이름을 바꿈
	m_pendingNames.push_back({m_nameByClock->isChecked() ? label : QString(), tag, QDateTime::currentDateTime()});
}

QString MainWindow::renameToGameTime(const QString &path, const QString &label, const QString &tag,
				     const QDateTime &when)
{
	if (label.isEmpty())
		return path;
	const QFileInfo fi(path);
	QString base = label;
	if (!tag.isEmpty())
		base += " " + tag;
	base += " " + when.toString("MM.dd hh시mm분");
	static const QString bad = "\\/:*?\"<>|";
	for (QChar c : bad)
		base.remove(c);
	QString target = fi.absolutePath() + "/" + base + "." + fi.suffix();
	for (int n = 2; QFileInfo::exists(target); ++n)
		target = fi.absolutePath() + "/" + base + QString(" (%1).").arg(n) + fi.suffix();
	if (!QFile::rename(path, target))
		return path;
	if (isFavorite(path)) {
		setFavorite(path, false);
		setFavorite(target, true);
	}
	updateProjectsForRename(path, target);
	return target;
}

void MainWindow::onClipSaved(const QString &path)
{
	if (m_saveSound->isChecked())
		MessageBeep(MB_OK); // (선택) 게임 중에도 저장됐는지 소리로 확인
	QString saved = path;
	if (!m_pendingNames.isEmpty()) {
		const PendingName pn = m_pendingNames.takeFirst();
		saved = renameToGameTime(path, pn.label, pn.tag, pn.when);
		if (m_nameByClock->isChecked() && pn.label.isEmpty())
			log("게임 시간을 읽지 못해 기본 이름으로 저장했어요 (게임 화면이 아니었거나 가운데 위 표시가 가려짐)");
	}
	log("클립 저장됨: " + QFileInfo(saved).fileName());
	// 소리 대신 화면으로 알림: 상태 표시를 3초간 '저장됨'으로
	m_status->setText("<b style='color:#30a46c'>✔ 클립 저장됨</b>");
	QTimer::singleShot(3000, this, [this] { onReplayStateChanged(m_core->isReplayActive()); });
	refreshClipList();
	if (m_clips->count() > 0)
		m_clips->setCurrentRow(0);
}

void MainWindow::onReplayStateChanged(bool active)
{
	m_status->setText(active ? "<b style='color:#e5484d'>● 녹화 중</b>" : "○ 대기");
	m_toggleBtn->setText(active ? "버퍼 중지" : "버퍼 시작");
	m_saveBtn->setEnabled(active);
}

void MainWindow::refreshClipList()
{
	QStringList keepSel;
	for (QListWidgetItem *it : m_clips->selectedItems())
		keepSel << it->data(Qt::UserRole).toString();

	m_clips->clear();
	QDir dir(clipDir());
	const QFileInfoList files =
		dir.entryInfoList({"*.mp4", "*.mkv"}, QDir::Files, QDir::Time); // 최신순
	for (const QFileInfo &fi : files) {
		const QString path = fi.absoluteFilePath();
		if (m_favOnly->isChecked() && !isFavorite(path))
			continue;
		auto *item = new QListWidgetItem;
		item->setData(Qt::UserRole, path);
		item->setData(Qt::UserRole + 1, fi.lastModified().toMSecsSinceEpoch());
		m_clips->addItem(item);
		updateClipItem(item);
		if (keepSel.contains(path))
			item->setSelected(true);
	}
}

void MainWindow::updateClipItem(QListWidgetItem *item)
{
	const QString path = item->data(Qt::UserRole).toString();
	const QFileInfo fi(path);
	ClipInfoCache::Info info;
	const bool known = m_clipInfo->get(path, &info);

	QString len = "…";
	if (known && info.duration > 0) {
		const int sec = int(info.duration + 0.5);
		len = QString("%1:%2").arg(sec / 60).arg(sec % 60, 2, 10, QChar('0'));
	} else if (known) {
		len = "?";
	}
	const bool fav = isFavorite(path);
	item->setText(QString("%1%2\n%3  ·  %4 MB  ·  %5")
			      .arg(fav ? "★ " : "")
			      .arg(fi.completeBaseName())
			      .arg(len)
			      .arg(fi.size() / (1024.0 * 1024.0), 0, 'f', 1)
			      .arg(fi.lastModified().toString("MM/dd hh:mm")));
	item->setToolTip(fi.fileName());
	if (fav)
		item->setForeground(QColor("#f5b400"));
	else
		item->setData(Qt::ForegroundRole, QVariant());

	QPixmap pm(128, 72);
	pm.fill(QColor(40, 40, 40));
	if (!info.thumb.isNull()) {
		QPainter p(&pm);
		const QImage img = info.thumb.scaled(pm.size(), Qt::KeepAspectRatio, Qt::SmoothTransformation);
		p.drawImage((pm.width() - img.width()) / 2, (pm.height() - img.height()) / 2, img);
	}
	item->setIcon(QIcon(pm));
}

bool MainWindow::isFavorite(const QString &path) const { return m_favorites.contains(QFileInfo(path).fileName()); }

void MainWindow::setFavorite(const QString &path, bool on)
{
	const QString name = QFileInfo(path).fileName();
	m_favorites.removeAll(name);
	if (on)
		m_favorites << name;
	saveSettings();
}

QStringList MainWindow::selectedClipPaths() const
{
	// 녹화된 순서(오래된 것 먼저)대로
	QList<QListWidgetItem *> items = m_clips->selectedItems();
	std::sort(items.begin(), items.end(), [](QListWidgetItem *a, QListWidgetItem *b) {
		return a->data(Qt::UserRole + 1).toLongLong() < b->data(Qt::UserRole + 1).toLongLong();
	});
	QStringList out;
	for (QListWidgetItem *it : items)
		out << it->data(Qt::UserRole).toString();
	return out;
}

void MainWindow::toggleFavorite()
{
	const QList<QListWidgetItem *> items = m_clips->selectedItems();
	if (items.isEmpty())
		return;
	// 하나라도 즐겨찾기가 아니면 전부 켜고, 전부 켜져 있으면 전부 끔
	bool allFav = true;
	for (QListWidgetItem *it : items)
		allFav = allFav && isFavorite(it->data(Qt::UserRole).toString());
	for (QListWidgetItem *it : items)
		setFavorite(it->data(Qt::UserRole).toString(), !allFav);
	if (m_favOnly->isChecked())
		refreshClipList();
	else
		for (QListWidgetItem *it : items)
			updateClipItem(it);
}

void MainWindow::renameClip()
{
	QListWidgetItem *item = m_clips->currentItem();
	if (!item || !item->isSelected())
		return;
	const QString oldPath = item->data(Qt::UserRole).toString();
	const QFileInfo fi(oldPath);
	bool ok = false;
	QString name = QInputDialog::getText(this, "클립 이름 바꾸기", "새 이름:", QLineEdit::Normal,
					     fi.completeBaseName(), &ok)
			       .trimmed();
	if (!ok || name.isEmpty() || name == fi.completeBaseName())
		return;
	static const QString bad = "\\/:*?\"<>|";
	for (QChar c : bad)
		name.remove(c);
	if (name.isEmpty())
		return;
	const QString newPath = fi.absolutePath() + "/" + name + "." + fi.suffix();
	if (QFileInfo::exists(newPath)) {
		QMessageBox::warning(this, "이름 바꾸기", "같은 이름의 클립이 이미 있어요.");
		return;
	}
	const bool fav = isFavorite(oldPath);
	m_clipInfo->cancelAll(); // 목록용 장면 추출이 파일을 붙잡고 있지 않게
	if (!QFile::rename(oldPath, newPath)) {
		QMessageBox::warning(this, "이름 바꾸기",
				     "이름을 바꾸지 못했어요. 다른 프로그램(플레이어, 편집 창)에서 열려 있는지 확인하세요.");
		return;
	}
	if (fav) {
		setFavorite(oldPath, false);
		setFavorite(newPath, true);
	}
	updateProjectsForRename(oldPath, newPath); // 저장된 프로젝트가 계속 이 클립을 찾을 수 있게
	log("클립 이름 변경: " + fi.fileName() + " → " + QFileInfo(newPath).fileName());
	m_clips->clearSelection();
	refreshClipList();
	for (int i = 0; i < m_clips->count(); ++i)
		if (m_clips->item(i)->data(Qt::UserRole).toString() == QFileInfo(newPath).absoluteFilePath()) {
			m_clips->setCurrentRow(i);
			break;
		}
}

void MainWindow::updateProjectsForRename(const QString &oldPath, const QString &newPath)
{
	const QString oldAbs = QDir::cleanPath(QFileInfo(oldPath).absoluteFilePath());
	const QString newAbs = QDir::cleanPath(QFileInfo(newPath).absoluteFilePath());
	const QFileInfoList projects = QDir(projectsDir()).entryInfoList({"*.json"}, QDir::Files);
	for (const QFileInfo &pf : projects) {
		QFile f(pf.absoluteFilePath());
		if (!f.open(QIODevice::ReadOnly))
			continue;
		QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
		f.close();
		if (!doc.isObject())
			continue;
		QJsonObject root = doc.object();
		QJsonArray sources = root.value("sources").toArray();
		bool changed = false;
		for (int i = 0; i < sources.size(); ++i) {
			QJsonObject src = sources[i].toObject();
			if (QDir::cleanPath(src.value("path").toString()).compare(oldAbs, Qt::CaseInsensitive) == 0) {
				src["path"] = newAbs;
				sources[i] = src;
				changed = true;
			}
		}
		if (!changed)
			continue;
		root["sources"] = sources;
		if (f.open(QIODevice::WriteOnly | QIODevice::Truncate))
			f.write(QJsonDocument(root).toJson());
	}
}

void MainWindow::deleteClips()
{
	const QStringList paths = selectedClipPaths();
	if (paths.isEmpty())
		return;
	const QString what = paths.size() == 1 ? QFileInfo(paths.first()).fileName()
					       : QString("클립 %1개").arg(paths.size());
	if (QMessageBox::question(this, "클립 삭제",
				  what + "를 휴지통으로 옮길까요?\n(휴지통에서 되살릴 수 있어요. 이 클립을 쓰는 "
					 "프로젝트는 해당 구간이 빠지게 됩니다.)") != QMessageBox::Yes)
		return;
	m_clipInfo->cancelAll(); // 목록용 장면 추출이 파일을 붙잡고 있지 않게
	int done = 0;
	for (const QString &p : paths) {
		if (QFile::moveToTrash(p)) {
			setFavorite(p, false);
			++done;
		} else {
			log("삭제 실패 (다른 프로그램에서 열려 있을 수 있음): " + QFileInfo(p).fileName());
		}
	}
	if (done > 0)
		log(QString("클립 %1개를 휴지통으로 옮겼습니다").arg(done));
	m_clips->clearSelection();
	refreshClipList();
}

void MainWindow::showClipMenu(const QPoint &pos)
{
	QListWidgetItem *item = m_clips->itemAt(pos);
	if (!item)
		return;
	if (!item->isSelected()) {
		m_clips->clearSelection();
		item->setSelected(true);
	}
	m_clips->setCurrentItem(item, QItemSelectionModel::NoUpdate);
	const QString path = item->data(Qt::UserRole).toString();
	const bool multi = m_clips->selectedItems().size() > 1;

	QMenu menu(this);
	menu.addAction("🎬 영상 만들기", this, &MainWindow::openEditor);
	if (!multi)
		menu.addAction("재생", this, [path] { QDesktopServices::openUrl(QUrl::fromLocalFile(path)); });
	menu.addSeparator();
	menu.addAction(isFavorite(path) ? "☆ 즐겨찾기 해제" : "★ 즐겨찾기", this, &MainWindow::toggleFavorite);
	if (!multi)
		menu.addAction("이름 바꾸기 (F2)", this, &MainWindow::renameClip);
	if (!multi)
		menu.addAction("폴더에서 보기", this, [path] {
			QProcess::startDetached("explorer.exe", {"/select,", QDir::toNativeSeparators(path)});
		});
	menu.addAction("🕒 게임 시간으로 이름 바꾸기", this, &MainWindow::renameClipsByGameTime);
	if (!multi)
		menu.addAction("🔎 이 클립에서 킬 찾기 (테스트)", this, [this, path] { findKillsInClip(path); });
	menu.addSeparator();
	menu.addAction("휴지통으로 삭제 (Delete)", this, &MainWindow::deleteClips);
	menu.exec(m_clips->viewport()->mapToGlobal(pos));
}

void MainWindow::openEditor()
{
	const QStringList clips = selectedClipPaths();
	if (clips.isEmpty()) {
		log("영상에 넣을 클립을 먼저 선택하세요 (Ctrl/Shift로 여러 개)");
		return;
	}
	QDir().mkpath(projectsDir());
	const QString path =
		projectsDir() + "/영상_" + QDateTime::currentDateTime().toString("yyyyMMdd_hhmmss") + ".json";
	showEditor(new EditorWindow(path, clips, clipDir(), shortsDir(), this));
}

void MainWindow::openProject()
{
	QDir().mkpath(projectsDir());
	const QString path = QFileDialog::getOpenFileName(this, "프로젝트 열기", projectsDir(), "ERShorts 프로젝트 (*.json)");
	if (path.isEmpty())
		return;
	showEditor(new EditorWindow(path, {}, clipDir(), shortsDir(), this));
}

void MainWindow::refreshWindowList()
{
	const QString keep = m_gameWindow->count() > 0 ? m_gameWindow->currentData().toString()
						       : m_gameWindow->property("saved").toString();
	m_gameWindow->blockSignals(true);
	m_gameWindow->clear();
	m_gameWindow->addItem("자동 (이터널리턴 창 찾기)", QString());
	bool found = keep.isEmpty();
	for (const ObsCore::WindowInfo &w : ObsCore::listWindows()) {
		m_gameWindow->addItem(w.label, w.value);
		if (w.value == keep)
			found = true;
	}
	if (!found) // 저장해 둔 창이 지금은 안 떠 있어도 선택은 유지 (게임을 켜면 다시 연결됨)
		m_gameWindow->addItem("(저장된 창) " + keep.section(':', 0, 0).replace("#3A", ":"), keep);
	m_gameWindow->setCurrentIndex(std::max(0, m_gameWindow->findData(keep)));
	m_gameWindow->blockSignals(false);
}

void MainWindow::showEditor(EditorWindow *editor)
{
	// 메인 창이 있는 모니터 가운데에, 화면 안에 들어오는 크기로 띄움
	QScreen *scr = screen() ? screen() : QGuiApplication::primaryScreen();
	const QRect avail = scr->availableGeometry();
	const QSize size(std::min(1360, int(avail.width() * 0.92)), std::min(900, int(avail.height() * 0.9)));
	editor->resize(size);
	editor->move(avail.left() + (avail.width() - size.width()) / 2,
		     avail.top() + std::max(0, (avail.height() - size.height()) / 2 - 20));
	editor->show();
	editor->raise();
	editor->activateWindow();
}

void MainWindow::log(const QString &msg)
{
	Diagnostics::write("[앱] " + msg);
	m_log->appendPlainText(QDateTime::currentDateTime().toString("[hh:mm:ss] ") + msg);
}

void MainWindow::changeEvent(QEvent *e)
{
	QMainWindow::changeEvent(e);
	if (e->type() == QEvent::ActivationChange || e->type() == QEvent::WindowStateChange) {
		// 게임을 하는 동안(이 창이 뒤에 있거나 최소화) 미리보기를 그리지 않아 GPU 부담을 줄임. 녹화는 계속됨
		const bool show = isActiveWindow() && !isMinimized();
		if (m_preview)
			m_preview->setRendering(show);
	}
}

void MainWindow::closeEvent(QCloseEvent *e)
{
	saveSettings();
	m_hotkey->unregisterKey();
	m_preview->destroyDisplay(); // obs_shutdown 전에 디스플레이부터 해제
	m_core->shutdown();
	e->accept();
}

// ─── 킬 자동 저장 (화면 숫자) ─────────────────────────────
void MainWindow::applyKillWatch() { m_core->setHudKillWatch(m_killAuto->isChecked()); }

void MainWindow::onHudKill(bool kill, bool assist)
{
	const QString what = kill && assist ? "킬+어시스트" : kill ? "킬" : "어시스트";
	log("킬 감지: " + what + " (TK와 함께 올라감)");
	if (!m_killAuto->isChecked() || !m_core->isReplayActive())
		return;
	// 마지막 킬에서 정해 둔 시간 뒤 저장. 그 사이 또 킬하면 미뤄서 연속 킬을 한 클립에 (첫 킬 후 최대 +20초)
	const int delayMs = m_killDelay->currentData().toInt() * 1000;
	const qint64 now = QDateTime::currentMSecsSinceEpoch();
	if (!m_killSaveTimer->isActive()) {
		m_killFirstMs = now;
		m_killLabel = m_core->gameTimeLabel(); // 첫 킬 때의 게임 시간으로 이름 붙임
		m_killWhat = what;
	} else if (what.contains("킬") || m_killWhat.contains("킬")) {
		m_killWhat = "킬"; // 연속으로 묶일 때 킬이 하나라도 있으면 '킬'
	}
	const qint64 wait = std::clamp<qint64>(m_killFirstMs + delayMs + 20000 - now, 0, delayMs);
	m_killSaveTimer->start(int(wait));
	m_status->setText(QString("<b style='color:#f5b400'>⚔ %1 — %2초 뒤 저장</b>").arg(what).arg((wait + 500) / 1000));
	if (m_bufferSec->value() < m_killDelay->currentData().toInt() + 15)
		log("⚠ 버퍼 길이가 짧아서 킬 전 장면이 조금만 담길 수 있어요 (버퍼 30초 이상 권장)");
}

void MainWindow::findKillsInClip(const QString &path)
{
	log("킬 찾는 중 (화면 오른쪽 위 숫자): " + QFileInfo(path).fileName());
	auto *proc = new QProcess(this);
	auto *raw = new QByteArray;
	connect(proc, &QProcess::readyReadStandardOutput, this, [proc, raw] { *raw += proc->readAllStandardOutput(); });
	connect(proc, &QProcess::readyReadStandardError, this, [proc] { proc->readAllStandardError(); });
	connect(proc, &QProcess::finished, this, [this, proc, raw](int code, QProcess::ExitStatus) {
		*raw += proc->readAllStandardOutput();
		proc->deleteLater();
		const QByteArray data = *raw;
		delete raw;
		const int fsz = HudKillWatcher::kW * HudKillWatcher::kH;
		if (code != 0 || data.size() < fsz) {
			log("클립의 화면을 읽지 못했어요");
			return;
		}
		HudKillWatcher w;
		std::vector<HudKillWatcher::Event> events;
		const int frames = int(data.size() / fsz);
		for (int i = 0; i < frames; ++i)
			w.feed(reinterpret_cast<const uint8_t *>(data.constData()) + size_t(i) * fsz, i / 5.0, &events);
		if (events.empty()) {
			log("이 클립에서는 내가 관여한 킬을 찾지 못했어요. 킬이 있는 클립인데 못 찾으면 그 클립을 보내 주세요 "
			    "(게임 화면이 클립을 꽉 채우고 있어야 해요).");
			return;
		}
		QStringList parts;
		for (const auto &e : events) {
			const int m = int(e.time) / 60;
			parts << QString("%1:%2 %3")
					 .arg(m)
					 .arg(e.time - m * 60, 4, 'f', 1, QChar('0'))
					 .arg(e.kill && e.assist ? "킬+어시" : e.kill ? "킬" : "어시스트");
		}
		log(QString("킬 %1번 찾음: %2").arg(events.size()).arg(parts.join(", ")));
	});
	// 게임 화면 높이 720 기준 오른쪽 위 160x32 영역을 초당 5장
	const QString vf = QString("fps=5,crop=w='ih/720*%1':h='ih/720*%2':x='iw-ih/720*%1':y='ih/720*%3',"
				   "scale=%1:%2:flags=area,format=gray")
				   .arg(HudKillWatcher::kW)
				   .arg(HudKillWatcher::kH)
				   .arg(HudKillWatcher::kRoiTop);
	proc->start(ShortsExporter::ffmpegPath(),
		    {"-hide_banner", "-loglevel", "error", "-i", path, "-an", "-vf", vf, "-f", "rawvideo", "-"});
}

void MainWindow::renameClipsByGameTime()
{
	const QStringList paths = selectedClipPaths();
	if (paths.isEmpty())
		return;
	m_clipInfo->cancelAll(); // 목록용 장면 추출이 파일을 붙잡고 있지 않게
	int done = 0;
	for (const QString &path : paths) {
		// 클립 끝부분(저장한 순간)의 가운데 위 영역을 한 장 뽑아 판단 (안 보이면 조금 앞에서 다시)
		GameClock::Phase phase = GameClock::None;
		int day = 0;
		std::wstring raw;
		for (const char *from : {"-1", "-3", "-6"}) {
			QProcess ff;
			const QString vf =
				QString("crop=w='ih/720*%1':h='ih/720*%2':x='iw/2+ih/720*(%3)':y='ih/720*%4',scale=%5:%6:flags=bicubic")
					.arg(GameClock::kUnitW)
					.arg(GameClock::kUnitH)
					.arg(GameClock::kLeft)
					.arg(GameClock::kTop)
					.arg(GameClock::kW)
					.arg(GameClock::kH);
			ff.start(ShortsExporter::ffmpegPath(), {"-hide_banner", "-loglevel", "error", "-sseof", from, "-i", path,
								"-frames:v", "1", "-vf", vf, "-f", "rawvideo", "-pix_fmt",
								"rgb24", "-"});
			if (!ff.waitForFinished(15000))
				continue;
			const QByteArray rgb = ff.readAllStandardOutput();
			if (rgb.size() < GameClock::kW * GameClock::kH * 3)
				continue;
			const auto *px = reinterpret_cast<const uint8_t *>(rgb.constData());
			phase = GameClock::phaseOf(px);
			if (phase == GameClock::None)
				continue;
			// 글자 인식은 UI 스레드에서 기다리면 안 되므로 다른 스레드에서
			const std::vector<uint8_t> copy(px, px + rgb.size());
			day = std::async(std::launch::async, [copy, &raw] { return GameClock::readDay(copy.data(), &raw); }).get();
			break;
		}
		const QString label = QString::fromStdString(GameClock::makeLabel(day, phase));
		if (label.isEmpty()) {
			log("게임 시간을 찾지 못함: " + QFileInfo(path).fileName());
			continue;
		}
		const QString now = renameToGameTime(path, label, QString(), QFileInfo(path).lastModified());
		if (now != path) {
			++done;
			log(QString("이름 변경: %1 → %2 (읽은 글자: %3)")
				    .arg(QFileInfo(path).fileName(), QFileInfo(now).fileName(), QString::fromStdWString(raw)));
		}
	}
	if (done > 0)
		refreshClipList();
}

// ─── 저장 폴더 ────────────────────────────────────────────
void MainWindow::updateOutputDirLabel()
{
	m_outDirEdit->setText(QDir::toNativeSeparators(m_outputDir));
	m_outDirEdit->setToolTip(QString("클립: %1\n쇼츠: %2\n프로젝트: %3")
					 .arg(QDir::toNativeSeparators(clipDir()), QDir::toNativeSeparators(shortsDir()),
					      QDir::toNativeSeparators(projectsDir())));
}

static bool moveFileAnyDrive(const QString &from, const QString &to)
{
	if (QFile::rename(from, to)) // 같은 드라이브면 바로 이동
		return true;
	if (!QFile::copy(from, to)) // 다른 드라이브: 복사 후 원본 삭제
		return false;
	if (!QFile::remove(from)) { // 원본이 사용 중이면 복사본을 지우고 원래대로 둠
		QFile::remove(to);
		return false;
	}
	return true;
}

void MainWindow::chooseOutputDir()
{
	if (!findChildren<EditorWindow *>().isEmpty()) {
		QMessageBox::information(this, "저장 폴더", "열려 있는 편집 창을 먼저 닫아 주세요.");
		return;
	}
	const QString picked = QFileDialog::getExistingDirectory(this, "저장 폴더 선택 (이 안에 clips / shorts / projects 폴더가 생겨요)",
								m_outputDir);
	if (picked.isEmpty())
		return;
	const QString newDir = QDir::cleanPath(picked);
	if (QDir::cleanPath(m_outputDir).compare(newDir, Qt::CaseInsensitive) == 0)
		return;

	// 쓸 수 있는 폴더인지 확인
	if (!QDir().mkpath(newDir + "/clips")) {
		QMessageBox::warning(this, "저장 폴더", "이 폴더에는 저장할 수 없어요. 다른 폴더를 골라 주세요.");
		return;
	}
	{
		QFile probe(newDir + "/clips/.ershorts_write_test");
		if (!probe.open(QIODevice::WriteOnly)) {
			QMessageBox::warning(this, "저장 폴더", "이 폴더에 쓸 권한이 없어요. 다른 폴더를 골라 주세요.");
			return;
		}
		probe.close();
		probe.remove();
	}
	const QStorageInfo storage(newDir);
	if (storage.isValid() && storage.bytesAvailable() < qint64(5) * 1024 * 1024 * 1024)
		log(QString("⚠ 새 저장 위치의 남은 공간이 %1 GB 뿐이에요").arg(storage.bytesAvailable() / double(1 << 30), 0, 'f', 1));

	// 기존 클립·프로젝트를 옮길지 물어봄 (쇼츠 결과물은 그대로 둠)
	const QStringList oldClips = QDir(clipDir()).entryList({"*.mp4", "*.mkv"}, QDir::Files);
	const QStringList oldProjects = QDir(projectsDir()).entryList({"*.json"}, QDir::Files);
	bool move = false;
	if (!oldClips.isEmpty() || !oldProjects.isEmpty()) {
		QMessageBox box(this);
		box.setWindowTitle("저장 폴더 변경");
		box.setText(QString("기존 클립 %1개와 프로젝트 %2개도 새 폴더로 옮길까요?\n"
				    "(다른 드라이브면 시간이 조금 걸려요. 만든 쇼츠 영상은 원래 폴더에 그대로 남아요.)")
				    .arg(oldClips.size())
				    .arg(oldProjects.size()));
		auto *moveBtn = box.addButton("옮기기", QMessageBox::AcceptRole);
		box.addButton("옮기지 않고 새 클립만 저장", QMessageBox::RejectRole);
		box.exec();
		move = (box.clickedButton() == moveBtn);
	}

	const QString oldClipDir = clipDir(), oldProjectDir = projectsDir();
	if (move) {
		m_clipInfo->cancelAll(); // 목록용 장면 추출이 파일을 붙잡고 있지 않게
		QDir().mkpath(newDir + "/projects");
		QProgressDialog progress("옮기는 중…", QString(), 0, int(oldClips.size() + oldProjects.size()), this);
		progress.setWindowModality(Qt::WindowModal);
		progress.setMinimumDuration(300);
		QHash<QString, QString> moved; // 옛 경로 → 새 경로 (프로젝트 안의 클립 경로 고치기용)
		int done = 0, failed = 0, projectsMoved = 0;
		for (const QString &name : oldClips) {
			progress.setLabelText("클립 옮기는 중: " + name);
			QCoreApplication::processEvents();
			QString to = newDir + "/clips/" + name;
			for (int n = 2; QFileInfo::exists(to); ++n)
				to = newDir + "/clips/" + QFileInfo(name).completeBaseName() + QString(" (%1).").arg(n) +
				     QFileInfo(name).suffix();
			const QString from = oldClipDir + "/" + name;
			if (moveFileAnyDrive(from, to))
				moved.insert(QDir::cleanPath(QFileInfo(from).absoluteFilePath()).toLower(),
					     QDir::cleanPath(QFileInfo(to).absoluteFilePath()));
			else
				++failed;
			progress.setValue(++done);
		}
		for (const QString &name : oldProjects) {
			QString to = newDir + "/projects/" + name;
			for (int n = 2; QFileInfo::exists(to); ++n)
				to = newDir + "/projects/" + QFileInfo(name).completeBaseName() + QString(" (%1).json").arg(n);
			if (!moveFileAnyDrive(oldProjectDir + "/" + name, to)) {
				++failed;
			} else {
				++projectsMoved;
				// 프로젝트 안의 클립 경로를 새 위치로
				QFile f(to);
				if (f.open(QIODevice::ReadOnly)) {
					QJsonObject root = QJsonDocument::fromJson(f.readAll()).object();
					f.close();
					QJsonArray sources = root.value("sources").toArray();
					bool changed = false;
					for (int i = 0; i < sources.size(); ++i) {
						QJsonObject src = sources[i].toObject();
						const QString key = QDir::cleanPath(src.value("path").toString()).toLower();
						if (moved.contains(key)) {
							src["path"] = moved.value(key);
							sources[i] = src;
							changed = true;
						}
					}
					if (changed && f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
						root["sources"] = sources;
						f.write(QJsonDocument(root).toJson());
					}
				}
			}
			progress.setValue(++done);
		}
		log(QString("클립 %1개, 프로젝트 %2개를 옮겼어요").arg(moved.size()).arg(projectsMoved));
		if (failed > 0)
			log(QString("⚠ %1개는 사용 중이라 옮기지 못했어요 (원래 폴더에 그대로 있어요)").arg(failed));
	}

	m_outputDir = newDir;
	saveSettings();
	updateOutputDirLabel();
	onApplySettings(); // 녹화 저장 위치 적용 (버퍼가 켜져 있었으면 다시 시작)
	refreshClipList();
	log("저장 폴더: " + QDir::toNativeSeparators(newDir));
}
