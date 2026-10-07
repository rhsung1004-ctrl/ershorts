#include "MainWindow.h"

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
#include <QUrl>
#include <QVBoxLayout>

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent)
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
	if (!m_core->applySettings(currentSettings(), error))
		return false;

	if (m_hotkey->registerKey(VK_F9))
		log("단축키 F9: 최근 클립 저장");
	else
		log("⚠ F9 단축키 등록 실패 (다른 프로그램이 사용 중일 수 있음)");

	refreshClipList();

	if (m_autoStart->isChecked()) {
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
	m_captureMode->addItems({"게임 캡처 (권장)", "모니터 캡처"});
	m_gameWindow = new QLineEdit;
	m_gameWindow->setPlaceholderText("비우면 전체화면 게임 자동 감지");
	m_bufferSec = new QSpinBox;
	m_bufferSec->setRange(10, 300);
	m_bufferSec->setSuffix(" 초");
	m_mic = new QCheckBox("마이크 녹음");
	m_autoStart = new QCheckBox("실행 시 자동 시작");
	auto *applyBtn = new QPushButton("설정 적용");
	m_toggleBtn = new QPushButton("버퍼 시작");
	m_saveBtn = new QPushButton("클립 저장 (F9)");
	m_saveBtn->setMinimumHeight(40);

	recForm->addRow("상태", m_status);
	recForm->addRow("캡처 방식", m_captureMode);
	recForm->addRow("게임 창", m_gameWindow);
	recForm->addRow("버퍼 길이", m_bufferSec);
	recForm->addRow(m_mic);
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
	clipBtns->addWidget(playBtn);
	clipBtns->addWidget(refreshBtn);
	clipBtns->addWidget(openDirBtn);
	clipBtns->addWidget(openShortsBtn);
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
	m_captureMode->setCurrentIndex(s.value("captureMode", 0).toInt());
	m_gameWindow->setText(s.value("gameWindow").toString());
	m_bufferSec->setValue(s.value("bufferSec", 45).toInt());
	m_mic->setChecked(s.value("mic", false).toBool());
	m_autoStart->setChecked(s.value("autoStart", true).toBool());
	m_outputDir = s.value("outputDir",
			      QStandardPaths::writableLocation(QStandardPaths::MoviesLocation) + "/ERShorts")
			      .toString();
}

void MainWindow::saveSettings()
{
	QSettings s("ERShorts", "ERShorts");
	s.setValue("captureMode", m_captureMode->currentIndex());
	s.setValue("gameWindow", m_gameWindow->text());
	s.setValue("bufferSec", m_bufferSec->value());
	s.setValue("mic", m_mic->isChecked());
	s.setValue("autoStart", m_autoStart->isChecked());
	s.setValue("outputDir", m_outputDir);
}

ObsCore::Settings MainWindow::currentSettings() const
{
	ObsCore::Settings st;
	st.captureMode = m_captureMode->currentIndex() == 0 ? ObsCore::CaptureMode::Game
							      : ObsCore::CaptureMode::Monitor;
	st.gameWindow = m_gameWindow->text();
	st.bufferSeconds = m_bufferSec->value();
	st.captureMic = m_mic->isChecked();
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
		MessageBeep(MB_ICONHAND);
	}
}

void MainWindow::onClipSaved(const QString &path)
{
	MessageBeep(MB_OK); // 게임 중에도 저장됐는지 소리로 확인
	log("클립 저장됨: " + QFileInfo(path).fileName());
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
	auto *editor = new EditorWindow(path, clips, clipDir(), shortsDir(), this);
	editor->show();
}

void MainWindow::openProject()
{
	QDir().mkpath(projectsDir());
	const QString path = QFileDialog::getOpenFileName(this, "프로젝트 열기", projectsDir(), "ERShorts 프로젝트 (*.json)");
	if (path.isEmpty())
		return;
	auto *editor = new EditorWindow(path, {}, clipDir(), shortsDir(), this);
	editor->show();
}

void MainWindow::log(const QString &msg)
{
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
