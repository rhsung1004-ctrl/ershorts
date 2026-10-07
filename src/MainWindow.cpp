#include "MainWindow.h"

#include "Diagnostics.h"
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
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QSettings>
#include <QSpinBox>
#include <QSplitter>
#include <QStandardPaths>
#include <QTimer>
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
	connect(m_hotkey, &GlobalHotkey::activated, this, &MainWindow::onSaveClip);
}

MainWindow::~MainWindow() = default;

bool MainWindow::initialize(QString *error)
{
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
	auto *applyBtn = new QPushButton("설정 적용");
	m_toggleBtn = new QPushButton("버퍼 시작");
	m_saveBtn = new QPushButton("클립 저장 (F9)");
	m_saveBtn->setMinimumHeight(40);

	recForm->addRow("상태", m_status);
	recForm->addRow("캡처 방식", m_captureMode);
	recForm->addRow("게임 창", winRow);
	recForm->addRow("버퍼 길이", m_bufferSec);
	recForm->addRow(m_mic);
	recForm->addRow(m_gameAudioOnly);
	recForm->addRow(m_saveSound);
	recForm->addRow(m_autoStart);
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
	auto *clipBox = new QGroupBox("저장된 클립 (Ctrl/Shift로 여러 개 선택)");
	auto *clipLay = new QVBoxLayout(clipBox);
	m_clips = new QListWidget;
	m_clips->setSelectionMode(QAbstractItemView::ExtendedSelection);
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
	m_outputDir = s.value("outputDir",
			      QStandardPaths::writableLocation(QStandardPaths::MoviesLocation) + "/ERShorts")
			      .toString();
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

void MainWindow::onSaveClip()
{
	if (!m_core->saveReplay()) {
		log("버퍼가 꺼져 있어 저장할 수 없습니다");
		if (m_saveSound->isChecked())
			MessageBeep(MB_ICONHAND);
	}
}

void MainWindow::onClipSaved(const QString &path)
{
	if (m_saveSound->isChecked())
		MessageBeep(MB_OK); // (선택) 게임 중에도 저장됐는지 소리로 확인
	log("클립 저장됨: " + QFileInfo(path).fileName());
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
	m_clips->clear();
	QDir dir(clipDir());
	const QFileInfoList files =
		dir.entryInfoList({"*.mp4", "*.mkv"}, QDir::Files, QDir::Time); // 최신순
	for (const QFileInfo &fi : files) {
		auto *item = new QListWidgetItem(
			QString("%1   (%2 MB)").arg(fi.fileName()).arg(fi.size() / (1024.0 * 1024.0), 0, 'f', 1));
		item->setData(Qt::UserRole, fi.absoluteFilePath());
		m_clips->addItem(item);
	}
}

void MainWindow::openEditor()
{
	// 목록은 최신순이므로, 영상에는 녹화된 순서(오래된 것 먼저)대로 넣음
	QStringList clips;
	for (int row = m_clips->count() - 1; row >= 0; --row) {
		QListWidgetItem *it = m_clips->item(row);
		if (it->isSelected())
			clips << it->data(Qt::UserRole).toString();
	}
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

void MainWindow::closeEvent(QCloseEvent *e)
{
	saveSettings();
	m_hotkey->unregisterKey();
	m_preview->destroyDisplay(); // obs_shutdown 전에 디스플레이부터 해제
	m_core->shutdown();
	e->accept();
}
