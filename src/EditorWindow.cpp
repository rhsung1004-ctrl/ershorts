#include "EditorWindow.h"

#include "BandLayout.h"
#include "FontManager.h"
#include "BeatDetector.h"
#include "ShortsExporter.h"
#include "ThumbnailCache.h"
#include "TimelineWidget.h"

#include <QAudioOutput>
#include <QCheckBox>
#include <QCloseEvent>
#include <QColorDialog>
#include <QComboBox>
#include <QDesktopServices>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGraphicsRectItem>
#include <QGraphicsScene>
#include <QGraphicsSimpleTextItem>
#include <QGraphicsVideoItem>
#include <QGraphicsView>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMediaPlayer>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <QScrollArea>
#include <QShortcut>
#include <QSlider>
#include <QSpinBox>
#include <QStandardPaths>
#include <QStatusBar>
#include <QSplitter>
#include <QTabWidget>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

namespace {
constexpr double kCanvasW = 1080.0;
constexpr double kCanvasH = 1920.0;

const QColor kClipColors[] = {QColor("#3E63DD"), QColor("#2F9E6B"), QColor("#D6409F"),
			      QColor("#E5932E"), QColor("#12A594"), QColor("#8E4EC6"),
			      QColor("#E5484D"), QColor("#0090FF")};

QString fmt(double t)
{
	if (t < 0)
		return "--:--.-";
	const int m = int(t) / 60;
	return QString("%1:%2").arg(m).arg(t - m * 60, 4, 'f', 1, QChar('0'));
}

class FitView : public QGraphicsView {
public:
	using QGraphicsView::QGraphicsView;

protected:
	void resizeEvent(QResizeEvent *e) override
	{
		QGraphicsView::resizeEvent(e);
		fitInView(sceneRect(), Qt::KeepAspectRatio);
	}
	void showEvent(QShowEvent *e) override
	{
		QGraphicsView::showEvent(e);
		fitInView(sceneRect(), Qt::KeepAspectRatio);
	}
};

const QVector<QPair<QString, double>> kSpeeds = {
	{"0.25x (아주 느리게)", 0.25}, {"0.5x (슬로우)", 0.5}, {"0.75x", 0.75}, {"1x (원속도)", 1.0},
	{"1.25x", 1.25},           {"1.5x", 1.5},         {"2x (빠르게)", 2.0}, {"3x", 3.0},
};

QWidget *scrollable(QWidget *content)
{
	auto *sa = new QScrollArea;
	sa->setWidget(content);
	sa->setWidgetResizable(true);
	sa->setFrameShape(QFrame::NoFrame);
	return sa;
}

QLabel *helpLabel(const QString &text)
{
	auto *l = new QLabel(text);
	l->setWordWrap(true);
	l->setStyleSheet("color:#9a9aa5; font-size:11px;");
	return l;
}
} // namespace

// ═════════════════════════════════════════════════════════════
// 생성
// ═════════════════════════════════════════════════════════════
EditorWindow::EditorWindow(const QString &projectPath, const QStringList &newClips, const QString &clipsDir,
			   const QString &shortsDir, QWidget *parent)
	: QMainWindow(parent), m_clipsDir(clipsDir), m_shortsDir(shortsDir)
{
	setAttribute(Qt::WA_DeleteOnClose);
	resize(1360, 900);

	if (!m_project.load(projectPath))
		m_project.filePath = projectPath;
	for (const QString &c : newClips)
		m_project.addSource(c);
	setWindowTitle("매드무비 편집 — " + QFileInfo(projectPath).completeBaseName());

	m_exporter = new ShortsExporter(this);
	m_beats = new BeatDetector(this);
	m_thumbs = new ThumbnailCache(this);

	m_bgm = new QMediaPlayer(this);
	m_bgmAudio = new QAudioOutput(this);
	m_bgm->setAudioOutput(m_bgmAudio);

	m_tickTimer = new QTimer(this);
	m_tickTimer->setInterval(20);
	connect(m_tickTimer, &QTimer::timeout, this, &EditorWindow::tick);

	// 연속 입력(드래그, 글자 입력)은 0.4초 동안 묶어서 실행 취소 한 단계로 기록
	m_undoTimer = new QTimer(this);
	m_undoTimer->setSingleShot(true);
	m_undoTimer->setInterval(400);
	connect(m_undoTimer, &QTimer::timeout, this, &EditorWindow::commitUndoStep);

	m_saveTimer = new QTimer(this);
	m_saveTimer->setSingleShot(true);
	m_saveTimer->setInterval(800);
	connect(m_saveTimer, &QTimer::timeout, this, [this] { m_project.save(); });

	buildUi();
	setupShortcuts();

	for (int i = 0; i < m_project.sources.size(); ++i)
		createPlayerFor(i);
	if (!m_project.music.path.isEmpty())
		m_bgm->setSource(QUrl::fromLocalFile(m_project.music.path));

	connect(m_exporter, &ShortsExporter::logMessage, this, &EditorWindow::log);
	connect(m_exporter, &ShortsExporter::progress, m_progress, &QProgressBar::setValue);
	connect(m_exporter, &ShortsExporter::finished, this, [this](bool ok, const QString &path) {
		m_exportBtn->setText("쇼츠로 내보내기 (고화질 1080×1920)");
		m_previewExportBtn->setText("⚡ 빠른 미리보기 내보내기 (540×960)");
		m_exportBtn->setEnabled(true);
		m_previewExportBtn->setEnabled(true);
		if (ok)
			QDesktopServices::openUrl(QUrl::fromLocalFile(path));
		else
			m_progress->setValue(0);
	});
	connect(m_beats, &BeatDetector::finished, this,
		[this](bool ok, double bpm, double firstBeat, const QString &msg) {
			m_detectBtn->setEnabled(true);
			m_beatStatus->setText(msg);
			if (!ok)
				return;
			m_project.music.bpm = bpm;
			m_project.music.firstBeat = firstBeat;
			m_syncing = true;
			m_bpm->setValue(bpm);
			m_firstBeat->setValue(firstBeat);
			m_syncing = false;
			projectChanged();
		});

	loadUiFromProject();
	m_project.save();
	m_undoBaseline = m_project.toJson();
	updateUndoButtons();
	m_tickTimer->start();
}

EditorWindow::~EditorWindow() { m_project.save(); }

// ═════════════════════════════════════════════════════════════
// UI 구성
// ═════════════════════════════════════════════════════════════
void EditorWindow::buildUi()
{
	auto *central = new QWidget;
	auto *root = new QVBoxLayout(central);
	setCentralWidget(central);

	auto *split = new QSplitter;
	root->addWidget(split, 1);

	// ── 왼쪽: 9:16 미리보기 ───────────────
	auto *left = new QWidget;
	auto *leftLay = new QVBoxLayout(left);
	leftLay->setContentsMargins(0, 0, 0, 0);

	m_scene = new QGraphicsScene(this);
	m_scene->setSceneRect(0, 0, kCanvasW, kCanvasH);

	m_canvas = new QGraphicsRectItem(0, 0, kCanvasW, kCanvasH);
	m_canvas->setBrush(QColor("#111114"));
	m_canvas->setPen(Qt::NoPen);
	m_canvas->setFlag(QGraphicsItem::ItemClipsChildrenToShape);
	m_scene->addItem(m_canvas);

	// 영상은 이 사각형 안에서만 보임 (제목 띠 레이아웃에서 가운데 영역으로 잘라냄)
	m_videoClip = new QGraphicsRectItem(0, 0, kCanvasW, kCanvasH, m_canvas);
	m_videoClip->setPen(Qt::NoPen);
	m_videoClip->setBrush(Qt::NoBrush);
	m_videoClip->setFlag(QGraphicsItem::ItemClipsChildrenToShape);
	m_videoClip->setZValue(0);

	m_bandTop = new QGraphicsRectItem(m_canvas);
	m_bandBottom = new QGraphicsRectItem(m_canvas);
	for (QGraphicsRectItem *b : {m_bandTop, m_bandBottom}) {
		b->setPen(Qt::NoPen);
		b->setZValue(6);
		b->setVisible(false);
	}

	m_minimapHint = new QGraphicsRectItem(m_canvas);
	m_minimapHint->setPen(QPen(QColor(255, 255, 255, 200), 4, Qt::DashLine));
	m_minimapHint->setZValue(5);

	m_flashOverlay = new QGraphicsRectItem(0, 0, kCanvasW, kCanvasH, m_canvas);
	m_flashOverlay->setPen(Qt::NoPen);
	m_flashOverlay->setZValue(8);
	m_flashOverlay->setVisible(false);

	m_cardItem = new QGraphicsRectItem(0, 0, kCanvasW, kCanvasH, m_canvas);
	m_cardItem->setPen(Qt::NoPen);
	m_cardItem->setZValue(9);
	m_cardTitle = new QGraphicsSimpleTextItem(m_cardItem);
	m_cardSub = new QGraphicsSimpleTextItem(m_cardItem);
	m_cardItem->setVisible(false);

	m_effectBadge = new QGraphicsSimpleTextItem(m_canvas);
	QFont badgeFont("Malgun Gothic");
	badgeFont.setPixelSize(34);
	badgeFont.setBold(true);
	m_effectBadge->setFont(badgeFont);
	m_effectBadge->setBrush(QColor("#FFD84D"));
	m_effectBadge->setPen(QPen(Qt::black, 2));
	m_effectBadge->setPos(30, 30);
	m_effectBadge->setZValue(20);

	m_view = new FitView(m_scene);
	m_view->setRenderHints(QPainter::Antialiasing | QPainter::SmoothPixmapTransform);
	m_view->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
	m_view->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
	m_view->setFrameShape(QFrame::NoFrame);
	m_view->setBackgroundBrush(QColor("#0B0B0D"));
	m_view->setMinimumSize(300, 480);
	leftLay->addWidget(m_view, 1);

	auto *ctrl = new QHBoxLayout;
	auto *homeBtn = new QPushButton("⏮");
	auto *backBtn = new QPushButton("⏪ 1초");
	m_playBtn = new QPushButton("▶ 재생");
	m_playBtn->setMinimumWidth(100);
	auto *fwdBtn = new QPushButton("1초 ⏩");
	ctrl->addWidget(homeBtn);
	ctrl->addWidget(backBtn);
	ctrl->addWidget(m_playBtn);
	ctrl->addWidget(fwdBtn);
	ctrl->addStretch();
	m_undoBtn = new QPushButton("↶ 실행 취소");
	m_redoBtn = new QPushButton("↷ 다시 실행");
	m_undoBtn->setToolTip("Ctrl+Z");
	m_redoBtn->setToolTip("Ctrl+Y / Ctrl+Shift+Z");
	ctrl->addWidget(m_undoBtn);
	ctrl->addWidget(m_redoBtn);
	connect(m_undoBtn, &QPushButton::clicked, this, &EditorWindow::undo);
	connect(m_redoBtn, &QPushButton::clicked, this, &EditorWindow::redo);
	leftLay->addLayout(ctrl);
	m_timeLabel = new QLabel;
	leftLay->addWidget(m_timeLabel);
	connect(homeBtn, &QPushButton::clicked, this, [this] { seek(0); });
	connect(backBtn, &QPushButton::clicked, this, [this] { seek(position() - 1.0); });
	connect(fwdBtn, &QPushButton::clicked, this, [this] { seek(position() + 1.0); });
	connect(m_playBtn, &QPushButton::clicked, this, &EditorWindow::togglePlay);
	split->addWidget(left);

	// ── 오른쪽: 탭 ──────────────────────
	m_tabs = new QTabWidget;
	m_tabs->setMinimumWidth(400);
	m_tabs->addTab(scrollable(buildClipTab()), "클립");
	m_tabs->addTab(scrollable(buildSegmentTab()), "구간·효과");
	m_tabs->addTab(scrollable(buildMusicTab()), "음악");
	m_tabs->addTab(scrollable(buildSubtitleTab()), "자막");
	m_tabs->addTab(scrollable(buildLayoutTab()), "화면 구성");
	// 인트로/아웃트로는 화면에서 뺐음 (예전 프로젝트 호환을 위해 설정만 유지)
	QWidget *cards = buildCardTab();
	cards->setParent(this);
	cards->hide();
	m_tabs->addTab(scrollable(buildExportTab()), "내보내기");
	split->addWidget(m_tabs);
	split->setStretchFactor(0, 1);
	split->setSizes({600, 460});

	// ── 아래: 타임라인 ──────────────────
	m_timeline = new TimelineWidget;
	m_timeline->setProject(&m_project);
	m_timeline->setThumbnails(m_thumbs);
	root->addWidget(m_timeline);
	connect(m_thumbs, &ThumbnailCache::updated, m_timeline, qOverload<>(&QWidget::update));

	// 타임라인 확대/축소
	auto *zoomRow = new QHBoxLayout;
	auto *zoomOut = new QPushButton("－");
	auto *zoomIn = new QPushButton("＋");
	auto *zoomFit = new QPushButton("전체 보기");
	zoomOut->setFixedWidth(32);
	zoomIn->setFixedWidth(32);
	m_zoomSlider = new QSlider(Qt::Horizontal);
	m_zoomSlider->setRange(0, 60); // 배율 = 2^(값/10) → 1배 ~ 64배
	m_zoomSlider->setMaximumWidth(220);
	zoomRow->addWidget(new QLabel("타임라인 확대"));
	zoomRow->addWidget(zoomOut);
	zoomRow->addWidget(m_zoomSlider);
	zoomRow->addWidget(zoomIn);
	zoomRow->addWidget(zoomFit);
	zoomRow->addWidget(helpLabel("Ctrl+휠: 확대/축소 · 휠: 좌우 이동"));
	zoomRow->addStretch();
	root->addLayout(zoomRow);
	connect(m_zoomSlider, &QSlider::valueChanged, this, [this](int v) {
		if (!m_syncing)
			m_timeline->setZoom(std::pow(2.0, v / 10.0));
	});
	connect(m_timeline, &TimelineWidget::zoomChanged, this, [this](double z) {
		m_syncing = true;
		m_zoomSlider->setValue(int(std::lround(10.0 * std::log2(z))));
		m_syncing = false;
	});
	connect(zoomIn, &QPushButton::clicked, this, [this] { m_timeline->setZoom(m_timeline->zoom() * 1.5); });
	connect(zoomOut, &QPushButton::clicked, this, [this] { m_timeline->setZoom(m_timeline->zoom() / 1.5); });
	connect(zoomFit, &QPushButton::clicked, this, [this] { m_timeline->zoomToFit(); });

	connect(m_timeline, &TimelineWidget::seekRequested, this, &EditorWindow::seek);
	connect(m_timeline, &TimelineWidget::segmentSelected, this, [this](int i) {
		selectSegment(i);
		if (i >= 0)
			m_tabs->setCurrentIndex(1);
	});
	connect(m_timeline, &TimelineWidget::subtitleSelected, this, [this](int i) {
		selectSubtitle(i);
		m_tabs->setCurrentIndex(3);
	});
	connect(m_timeline, &TimelineWidget::cardClicked, this, [this](int) { m_tabs->setCurrentIndex(4); });
	connect(m_timeline, &TimelineWidget::segmentsEdited, this, [this] {
		selectSegment(m_timeline->selectedSegment());
		projectChanged();
	});

	root->addWidget(helpLabel(
		"Space 재생/정지 · S 자르기 · B 선택 구간을 비트마다 자르기 · Delete 구간 삭제 · Ctrl+D 복제 · "
		"T 자막 추가 · Ctrl+Z 실행 취소 · ←/→ 1초 · ,/. 0.1초 · 선택한 구간을 다시 잡고 끌면 순서 변경"));
}

QWidget *EditorWindow::buildClipTab()
{
	auto *w = new QWidget;
	auto *lay = new QVBoxLayout(w);
	lay->addWidget(helpLabel("이 영상에 쓰는 클립 목록입니다. 추가한 클립은 타임라인 끝에 통째로 붙고, "
				 "필요 없는 부분은 '구간·효과' 탭에서 잘라내면 됩니다."));

	m_clipList = new QListWidget;
	m_clipList->setMinimumHeight(220);
	lay->addWidget(m_clipList);

	auto *row = new QHBoxLayout;
	auto *addBtn = new QPushButton("＋ 클립 추가");
	auto *appendBtn = new QPushButton("타임라인에 다시 넣기");
	auto *removeBtn = new QPushButton("타임라인에서 빼기");
	row->addWidget(addBtn);
	row->addWidget(appendBtn);
	row->addWidget(removeBtn);
	lay->addLayout(row);
	lay->addStretch();

	connect(addBtn, &QPushButton::clicked, this, &EditorWindow::addClips);
	connect(appendBtn, &QPushButton::clicked, this, [this] {
		const int i = m_clipList->currentRow();
		if (i < 0 || i >= m_project.sources.size())
			return;
		Segment s;
		s.source = i;
		s.out = m_project.sources[i].duration > 0 ? m_project.sources[i].duration : -1.0;
		s.transIn = m_project.segments.isEmpty() ? Transition::None : Transition::Flash;
		m_project.segments.push_back(s);
		projectChanged();
	});
	connect(removeBtn, &QPushButton::clicked, this, [this] {
		const int i = m_clipList->currentRow();
		if (i < 0)
			return;
		m_project.segments.erase(std::remove_if(m_project.segments.begin(), m_project.segments.end(),
							[i](const Segment &s) { return s.source == i; }),
					 m_project.segments.end());
		selectSegment(-1);
		projectChanged();
	});
	connect(m_clipList, &QListWidget::itemDoubleClicked, this, [this] {
		// 해당 클립이 처음 나오는 위치로 이동
		const int i = m_clipList->currentRow();
		for (int k = 0; k < m_project.segments.size(); ++k)
			if (m_project.segments[k].source == i) {
				seek(m_project.segmentStart(k));
				selectSegment(k);
				return;
			}
	});
	return w;
}

QWidget *EditorWindow::buildSegmentTab()
{
	auto *w = new QWidget;
	auto *lay = new QVBoxLayout(w);
	lay->addWidget(helpLabel("타임라인에서 구간을 클릭해 선택하세요. 양 끝을 드래그하면 길이 조절, "
				 "선택된 구간을 다시 잡고 끌면 순서를 바꿀 수 있습니다."));

	auto *grid = new QGridLayout;
	auto *splitBtn = new QPushButton("✂ 여기서 자르기 (S)");
	auto *beatSplitBtn = new QPushButton("♪ 비트마다 자르기 (B)");
	auto *delBtn = new QPushButton("🗑 구간 삭제 (Del)");
	auto *dupBtn = new QPushButton("⧉ 복제 (Ctrl+D)");
	auto *leftBtn = new QPushButton("◀ 앞으로");
	auto *rightBtn = new QPushButton("뒤로 ▶");
	grid->addWidget(splitBtn, 0, 0);
	grid->addWidget(beatSplitBtn, 0, 1);
	grid->addWidget(delBtn, 1, 0);
	grid->addWidget(dupBtn, 1, 1);
	grid->addWidget(leftBtn, 2, 0);
	grid->addWidget(rightBtn, 2, 1);
	lay->addLayout(grid);
	connect(splitBtn, &QPushButton::clicked, this, &EditorWindow::splitAtPlayhead);
	connect(beatSplitBtn, &QPushButton::clicked, this, &EditorWindow::splitSelectedOnBeats);
	connect(delBtn, &QPushButton::clicked, this, &EditorWindow::deleteSelectedSegment);
	connect(dupBtn, &QPushButton::clicked, this, &EditorWindow::duplicateSelectedSegment);
	connect(leftBtn, &QPushButton::clicked, this, [this] { moveSelected(-1); });
	connect(rightBtn, &QPushButton::clicked, this, [this] { moveSelected(+1); });

	auto *box = new QGroupBox("선택한 구간");
	m_segProps = box;
	auto *form = new QFormLayout(box);
	m_segInfo = new QLabel("-");
	m_segInfo->setWordWrap(true);
	m_segSpeed = new QComboBox;
	for (const auto &sp : kSpeeds)
		m_segSpeed->addItem(sp.first, sp.second);
	m_segTrans = new QComboBox;
	for (int t = 0; t < kTransitionCount; ++t)
		m_segTrans->addItem(transitionName(Transition(t)), t);
	form->addRow("구간", m_segInfo);
	form->addRow("배속", m_segSpeed);

	// 속도 램프
	auto *rampRow = new QHBoxLayout;
	m_rampIn = new QCheckBox("들어갈 때 ↘");
	m_rampOut = new QCheckBox("나올 때 ↗");
	m_rampLen = new QDoubleSpinBox;
	m_rampLen->setRange(0.1, 3.0);
	m_rampLen->setSingleStep(0.1);
	m_rampLen->setDecimals(1);
	m_rampLen->setSuffix(" 초");
	m_rampLen->setToolTip("속도가 바뀌는 데 걸리는 길이 (원본 영상 기준)");
	rampRow->addWidget(m_rampIn);
	rampRow->addWidget(m_rampOut);
	rampRow->addWidget(m_rampLen);
	form->addRow("속도 램프", rampRow);
	form->addRow(helpLabel("램프를 켜면 1x에서 위 배속으로 부드럽게 바뀌고, 나올 때 다시 1x로 돌아옵니다. "
			       "예: 0.5x + 들어갈 때·나올 때 → 킬 장면에서 서서히 느려졌다가 원래 속도로."));
	connect(m_rampIn, &QCheckBox::toggled, this, &EditorWindow::onSegmentPropsChanged);
	connect(m_rampOut, &QCheckBox::toggled, this, &EditorWindow::onSegmentPropsChanged);
	connect(m_rampLen, &QDoubleSpinBox::valueChanged, this, &EditorWindow::onSegmentPropsChanged);
	form->addRow("들어올 때 전환", m_segTrans);

	auto *fxGrid = new QGridLayout;
	m_fxZoom = new QCheckBox("줌인 (1.3배)");
	m_fxShake = new QCheckBox("화면 흔들림");
	m_fxGray = new QCheckBox("흑백");
	m_fxVivid = new QCheckBox("색감 강조 + 선명");
	m_fxVignette = new QCheckBox("비네팅");
	const QList<QCheckBox *> fx = {m_fxZoom, m_fxShake, m_fxGray, m_fxVivid, m_fxVignette};
	for (int i = 0; i < fx.size(); ++i) {
		fxGrid->addWidget(fx[i], i / 2, i % 2);
		connect(fx[i], &QCheckBox::toggled, this, &EditorWindow::onSegmentPropsChanged);
	}
	form->addRow("효과", fxGrid);
	connect(m_segSpeed, &QComboBox::currentIndexChanged, this, &EditorWindow::onSegmentPropsChanged);
	connect(m_segTrans, &QComboBox::currentIndexChanged, this, &EditorWindow::onSegmentPropsChanged);

	auto *allTrans = new QPushButton("이 전환을 모든 컷에 적용");
	connect(allTrans, &QPushButton::clicked, this, [this] {
		const Transition t = Transition(m_segTrans->currentData().toInt());
		for (int i = 1; i < m_project.segments.size(); ++i)
			m_project.segments[i].transIn = t;
		projectChanged();
		log("모든 컷의 전환: " + transitionName(t));
	});
	form->addRow(allTrans);
	form->addRow(helpLabel("배속, 속도 램프, 줌인, 전환은 미리보기에 바로 보이고, 흑백/선명/비네팅/흔들림은 내보낸 영상에서 적용됩니다."));

	lay->addWidget(box);
	lay->addStretch();
	return w;
}

QWidget *EditorWindow::buildMusicTab()
{
	auto *w = new QWidget;
	auto *lay = new QVBoxLayout(w);

	auto *fileBox = new QGroupBox("배경음악 (BGM)");
	auto *ff = new QFormLayout(fileBox);
	m_musicFile = new QLabel("없음");
	m_musicFile->setWordWrap(true);
	auto *fileRow = new QHBoxLayout;
	auto *pickBtn = new QPushButton("음악 파일 선택…");
	auto *clearBtn = new QPushButton("제거");
	fileRow->addWidget(pickBtn);
	fileRow->addWidget(clearBtn);
	ff->addRow("파일", m_musicFile);
	ff->addRow(fileRow);

	m_musicOffset = new QDoubleSpinBox;
	m_musicOffset->setRange(0, 3600);
	m_musicOffset->setDecimals(2);
	m_musicOffset->setSingleStep(0.1);
	m_musicOffset->setSuffix(" 초부터");
	ff->addRow("음악 시작 위치", m_musicOffset);

	m_musicVol = new QSlider(Qt::Horizontal);
	m_musicVol->setRange(0, 150);
	m_gameVol = new QSlider(Qt::Horizontal);
	m_gameVol->setRange(0, 150);
	ff->addRow("음악 볼륨", m_musicVol);
	ff->addRow("게임 소리 볼륨", m_gameVol);
	m_musicFade = new QCheckBox("끝날 때 음악 페이드 아웃");
	ff->addRow(m_musicFade);
	lay->addWidget(fileBox);

	auto *beatBox = new QGroupBox("비트");
	auto *bf = new QFormLayout(beatBox);
	m_detectBtn = new QPushButton("BPM 자동 감지");
	m_beatStatus = new QLabel;
	m_beatStatus->setWordWrap(true);
	bf->addRow(m_detectBtn, m_beatStatus);
	m_bpm = new QDoubleSpinBox;
	m_bpm->setRange(0, 260);
	m_bpm->setDecimals(1);
	m_bpm->setSpecialValueText("없음");
	m_firstBeat = new QDoubleSpinBox;
	m_firstBeat->setRange(0, 60);
	m_firstBeat->setDecimals(3);
	m_firstBeat->setSingleStep(0.01);
	m_firstBeat->setSuffix(" 초");
	bf->addRow("BPM", m_bpm);
	bf->addRow("첫 박 위치 (음악 파일 기준)", m_firstBeat);
	m_beatEvery = new QComboBox;
	m_beatEvery->addItem("매 박", 1);
	m_beatEvery->addItem("2박마다", 2);
	m_beatEvery->addItem("4박마다 (1마디)", 4);
	m_beatEvery->addItem("8박마다 (2마디)", 8);
	bf->addRow("컷 간격", m_beatEvery);
	m_snap = new QCheckBox("구간 끝을 끌 때 비트에 자석처럼 붙기");
	m_snap->setChecked(true);
	bf->addRow(m_snap);
	auto *snapBtn = new QPushButton("모든 컷을 비트에 맞추기");
	snapBtn->setMinimumHeight(38);
	bf->addRow(snapBtn);
	bf->addRow(helpLabel("타임라인의 노란 세로선이 비트 위치입니다. 자동 감지가 반 박 어긋나면 "
			     "'첫 박 위치'를 조금씩 조절하세요."));
	lay->addWidget(beatBox);

	auto *fxBox = new QGroupBox("비트 효과 (영상 구간에만 적용)");
	auto *xf = new QFormLayout(fxBox);
	auto *fxRow = new QHBoxLayout;
	m_fxBeatZoom = new QCheckBox("줌 펄스");
	m_fxBeatShake = new QCheckBox("흔들림");
	fxRow->addWidget(m_fxBeatZoom);
	fxRow->addWidget(m_fxBeatShake);
	fxRow->addStretch();
	xf->addRow("효과", fxRow);
	m_fxBeatStrength = new QComboBox;
	m_fxBeatStrength->addItems({"약하게", "보통", "강하게"});
	xf->addRow("세기", m_fxBeatStrength);
	m_fxBeatEvery = new QComboBox;
	m_fxBeatEvery->addItem("매 박", 1);
	m_fxBeatEvery->addItem("2박마다", 2);
	m_fxBeatEvery->addItem("4박마다 (1마디)", 4);
	xf->addRow("간격", m_fxBeatEvery);
	xf->addRow(helpLabel("비트에 맞춰 화면이 순간적으로 확대되거나 흔들렸다가 0.1초 안에 돌아옵니다. "
			     "BPM이 설정되어 있어야 하고, 자막은 흔들리지 않아요."));
	lay->addWidget(fxBox);
	connect(m_fxBeatZoom, &QCheckBox::toggled, this, &EditorWindow::onMusicPropsChanged);
	connect(m_fxBeatShake, &QCheckBox::toggled, this, &EditorWindow::onMusicPropsChanged);
	connect(m_fxBeatStrength, &QComboBox::currentIndexChanged, this, &EditorWindow::onMusicPropsChanged);
	connect(m_fxBeatEvery, &QComboBox::currentIndexChanged, this, &EditorWindow::onMusicPropsChanged);
	lay->addStretch();

	connect(pickBtn, &QPushButton::clicked, this, &EditorWindow::chooseMusic);
	connect(clearBtn, &QPushButton::clicked, this, &EditorWindow::clearMusic);
	connect(m_detectBtn, &QPushButton::clicked, this, &EditorWindow::detectBeats);
	connect(snapBtn, &QPushButton::clicked, this, &EditorWindow::snapToBeats);
	connect(m_snap, &QCheckBox::toggled, this, [this](bool on) { m_timeline->setSnapToBeats(on); });
	connect(m_musicOffset, &QDoubleSpinBox::valueChanged, this, &EditorWindow::onMusicPropsChanged);
	connect(m_musicVol, &QSlider::valueChanged, this, &EditorWindow::onMusicPropsChanged);
	connect(m_gameVol, &QSlider::valueChanged, this, &EditorWindow::onMusicPropsChanged);
	connect(m_musicFade, &QCheckBox::toggled, this, &EditorWindow::onMusicPropsChanged);
	connect(m_bpm, &QDoubleSpinBox::valueChanged, this, &EditorWindow::onMusicPropsChanged);
	connect(m_firstBeat, &QDoubleSpinBox::valueChanged, this, &EditorWindow::onMusicPropsChanged);
	connect(m_beatEvery, &QComboBox::currentIndexChanged, this, &EditorWindow::onMusicPropsChanged);
	return w;
}

QWidget *EditorWindow::buildSubtitleTab()
{
	auto *w = new QWidget;
	auto *lay = new QVBoxLayout(w);

	m_subList = new QListWidget;
	m_subList->setMaximumHeight(170);
	lay->addWidget(m_subList);

	auto *btns = new QHBoxLayout;
	auto *addBtn = new QPushButton("＋ 현재 위치에 자막 추가 (T)");
	auto *delBtn = new QPushButton("삭제");
	btns->addWidget(addBtn, 1);
	btns->addWidget(delBtn);
	lay->addLayout(btns);
	connect(addBtn, &QPushButton::clicked, this, &EditorWindow::addSubtitle);
	connect(delBtn, &QPushButton::clicked, this, &EditorWindow::deleteSelectedSubtitle);
	connect(m_subList, &QListWidget::currentRowChanged, this, [this](int row) {
		if (!m_syncing)
			selectSubtitle(row);
	});

	auto *box = new QGroupBox("선택한 자막");
	m_subProps = box;
	auto *form = new QFormLayout(box);

	m_subText = new QPlainTextEdit;
	m_subText->setMaximumHeight(72);
	m_subText->setPlaceholderText("자막 내용 (Enter로 줄바꿈)");
	form->addRow("내용", m_subText);

	auto makeTimeRow = [this](QDoubleSpinBox *&spin) {
		auto *row = new QHBoxLayout;
		spin = new QDoubleSpinBox;
		spin->setDecimals(2);
		spin->setSingleStep(0.1);
		spin->setSuffix(" 초");
		spin->setRange(0, 3600);
		auto *nowBtn = new QPushButton("현재 위치");
		row->addWidget(spin, 1);
		row->addWidget(nowBtn);
		QDoubleSpinBox *target = spin;
		connect(nowBtn, &QPushButton::clicked, this, [this, target] { target->setValue(position()); });
		return row;
	};
	form->addRow("시작", makeTimeRow(m_subStart));
	form->addRow("끝", makeTimeRow(m_subEnd));

	m_subSize = new QSpinBox;
	m_subSize->setRange(28, 200);
	m_subSize->setSuffix(" px");
	form->addRow("글자 크기", m_subSize);
	m_subFont = makeFontCombo();
	connect(m_subFont, &QComboBox::currentIndexChanged, this, [this] {
		if (!handleFontComboAdd(m_subFont))
			onSubtitlePropsChanged();
	});
	form->addRow("글꼴", m_subFont);

	auto *colorRow = new QHBoxLayout;
	m_subColor = makeColorButton(&m_subColorValue, [this] { onSubtitlePropsChanged(); });
	colorRow->addWidget(m_subColor);
	for (const char *c : {"#FFFFFF", "#FFE14D", "#FF5A5A", "#5AD1FF", "#7CFF7A"}) {
		auto *b = new QPushButton;
		b->setFixedSize(26, 26);
		paintColorButton(b, QColor(c));
		const QColor col(c);
		connect(b, &QPushButton::clicked, this, [this, col] {
			m_subColorValue = col;
			paintColorButton(m_subColor, col);
			onSubtitlePropsChanged();
		});
		colorRow->addWidget(b);
	}
	colorRow->addStretch();
	form->addRow("색상", colorRow);

	m_subY = new QSlider(Qt::Horizontal);
	m_subY->setRange(5, 95);
	form->addRow("세로 위치", m_subY);
	m_subBox = new QCheckBox("반투명 배경 박스");
	form->addRow(m_subBox);

	connect(m_subText, &QPlainTextEdit::textChanged, this, &EditorWindow::onSubtitlePropsChanged);
	connect(m_subStart, &QDoubleSpinBox::valueChanged, this, &EditorWindow::onSubtitlePropsChanged);
	connect(m_subEnd, &QDoubleSpinBox::valueChanged, this, &EditorWindow::onSubtitlePropsChanged);
	connect(m_subSize, &QSpinBox::valueChanged, this, &EditorWindow::onSubtitlePropsChanged);
	connect(m_subY, &QSlider::valueChanged, this, &EditorWindow::onSubtitlePropsChanged);
	connect(m_subBox, &QCheckBox::toggled, this, &EditorWindow::onSubtitlePropsChanged);

	lay->addWidget(box);
	lay->addWidget(helpLabel("자막 시간은 결과 영상 기준입니다. 구간을 많이 바꾼 뒤에는 위치를 한 번 확인하세요."));
	lay->addStretch();
	m_subProps->setEnabled(false);
	return w;
}

QWidget *EditorWindow::buildCardTab()
{
	auto *w = new QWidget;
	auto *lay = new QVBoxLayout(w);

	auto build = [this, lay](CardUi &ui, TitleCard *card, const QString &title, const QString &placeholder) {
		auto *box = new QGroupBox(title);
		auto *form = new QFormLayout(box);
		ui.enabled = new QCheckBox("사용");
		ui.duration = new QDoubleSpinBox;
		ui.duration->setRange(0.5, 10);
		ui.duration->setSingleStep(0.5);
		ui.duration->setSuffix(" 초");
		ui.title = new QLineEdit;
		ui.title->setPlaceholderText(placeholder);
		ui.subtitle = new QLineEdit;
		ui.subtitle->setPlaceholderText("작은 글씨 (예: 닉네임, 시즌)");
		ui.bg = makeColorButton(&card->background, [this] { onCardPropsChanged(); });
		ui.color = makeColorButton(&card->color, [this] { onCardPropsChanged(); });
		form->addRow(ui.enabled);
		form->addRow("길이", ui.duration);
		form->addRow("제목", ui.title);
		form->addRow("부제", ui.subtitle);
		form->addRow("배경색", ui.bg);
		form->addRow("글자색", ui.color);
		lay->addWidget(box);

		connect(ui.enabled, &QCheckBox::toggled, this, &EditorWindow::onCardPropsChanged);
		connect(ui.duration, &QDoubleSpinBox::valueChanged, this, &EditorWindow::onCardPropsChanged);
		connect(ui.title, &QLineEdit::textChanged, this, &EditorWindow::onCardPropsChanged);
		connect(ui.subtitle, &QLineEdit::textChanged, this, &EditorWindow::onCardPropsChanged);
	};
	build(m_introUi, &m_project.intro, "인트로", "예: 이터널리턴 매드무비");
	build(m_outroUi, &m_project.outro, "아웃트로", "예: 구독과 좋아요");
	lay->addWidget(helpLabel("인트로/아웃트로 동안에도 BGM은 계속 나옵니다. 첫 구간의 전환을 "
				 "'화이트 플래시'로 두면 인트로에서 넘어갈 때 임팩트가 커요."));
	lay->addStretch();
	return w;
}

QWidget *EditorWindow::buildLayoutTab()
{
	auto *w = new QWidget;
	auto *lay = new QVBoxLayout(w);

	auto *top = new QFormLayout;
	m_layout = new QComboBox;
	m_layout->addItem("제목 띠 (위/아래에 글씨)", int(ShortsLayout::TitleBands));
	m_layout->addItem("가운데 크롭 (9:16 꽉 채움)", int(ShortsLayout::CenterCrop));
	m_layout->addItem("가운데 크롭 + 미니맵", int(ShortsLayout::CropWithMinimap));
	m_layout->addItem("원본 + 흐린 배경", int(ShortsLayout::BlurBackground));
	connect(m_layout, &QComboBox::currentIndexChanged, this, [this] {
		if (m_syncing)
			return;
		m_project.layout = ShortsLayout(m_layout->currentData().toInt());
		m_bandBox->setEnabled(m_project.layout == ShortsLayout::TitleBands);
		applyLayoutToPreview();
		projectChanged();
	});
	top->addRow("레이아웃", m_layout);
	lay->addLayout(top);

	auto *box = new QGroupBox("제목 띠");
	m_bandBox = box;
	auto *f = new QFormLayout(box);

	auto sizeSpin = [](int lo, int hi) {
		auto *sp = new QSpinBox;
		sp->setRange(lo, hi);
		sp->setSuffix(" px");
		return sp;
	};
	auto styleRow = [](QSpinBox *sp, QPushButton *color, QComboBox *font) {
		auto *row = new QHBoxLayout;
		row->addWidget(font, 1);
		row->addSpacing(6);
		row->addWidget(new QLabel("크기"));
		row->addWidget(sp);
		row->addSpacing(8);
		row->addWidget(new QLabel("색"));
		row->addWidget(color);
		row->addStretch();
		return row;
	};

	m_bandTitle = new QPlainTextEdit;
	m_bandTitle->setMaximumHeight(60);
	m_bandTitle->setPlaceholderText("위 띠 제목 (예: 1대3 역관광)");
	m_bandTitleSize = sizeSpin(20, 220);
	m_bandTitleColor = makeColorButton(&m_project.bands.titleColor, [this] { onBandPropsChanged(); });
	f->addRow("제목", m_bandTitle);
	m_bandTitleFont = makeFontCombo();
	f->addRow("", styleRow(m_bandTitleSize, m_bandTitleColor, m_bandTitleFont));

	m_bandSubtitle = new QLineEdit;
	m_bandSubtitle->setPlaceholderText("제목 아래 작은 글씨 (예: 아델라 장인의 하루)");
	m_bandSubSize = sizeSpin(20, 160);
	m_bandSubColor = makeColorButton(&m_project.bands.subtitleColor, [this] { onBandPropsChanged(); });
	f->addRow("부제", m_bandSubtitle);
	m_bandSubFont = makeFontCombo();
	f->addRow("", styleRow(m_bandSubSize, m_bandSubColor, m_bandSubFont));

	m_bandBottomText = new QPlainTextEdit;
	m_bandBottomText->setMaximumHeight(60);
	m_bandBottomText->setPlaceholderText("아래 띠 문구 (예: 끝까지 보세요)");
	m_bandBottomSize = sizeSpin(20, 160);
	m_bandBottomColor = makeColorButton(&m_project.bands.bottomColor, [this] { onBandPropsChanged(); });
	f->addRow("아래 문구", m_bandBottomText);
	m_bandBottomFont = makeFontCombo();
	f->addRow("", styleRow(m_bandBottomSize, m_bandBottomColor, m_bandBottomFont));

	auto slider = [](int lo, int hi, int step) {
		auto *s = new QSlider(Qt::Horizontal);
		s->setRange(lo, hi);
		s->setSingleStep(step);
		s->setPageStep(step * 5);
		return s;
	};
	m_bandTopH = slider(0, 700, 10);
	m_bandBottomH = slider(0, 700, 10);
	m_bandBg = makeColorButton(&m_project.bands.background, [this] { onBandPropsChanged(); });
	m_bandZoom = slider(100, 200, 5);
	m_bandOffset = slider(-100, 100, 5);
	f->addRow("위 띠 높이", m_bandTopH);
	f->addRow("아래 띠 높이", m_bandBottomH);
	f->addRow("띠 색", m_bandBg);
	f->addRow("영상 확대", m_bandZoom);
	f->addRow("영상 세로 위치", m_bandOffset);
	f->addRow(helpLabel("가운데 게임 화면은 9:16보다 넓게 잘라서 더 많이 보입니다. 긴 글씨는 화면 폭에 맞게 "
			    "자동으로 작아져요. 영상 확대/세로 위치로 캐릭터가 잘 보이게 맞추세요."));
	lay->addWidget(box);
	lay->addStretch();

	connect(m_bandTitle, &QPlainTextEdit::textChanged, this, &EditorWindow::onBandPropsChanged);
	connect(m_bandSubtitle, &QLineEdit::textChanged, this, &EditorWindow::onBandPropsChanged);
	connect(m_bandBottomText, &QPlainTextEdit::textChanged, this, &EditorWindow::onBandPropsChanged);
	for (QSpinBox *sp : {m_bandTitleSize, m_bandSubSize, m_bandBottomSize})
		connect(sp, &QSpinBox::valueChanged, this, &EditorWindow::onBandPropsChanged);
	for (QComboBox *fc : {m_bandTitleFont, m_bandSubFont, m_bandBottomFont})
		connect(fc, &QComboBox::currentIndexChanged, this, [this, fc] {
			if (!handleFontComboAdd(fc))
				onBandPropsChanged();
		});
	for (QSlider *sl : {m_bandTopH, m_bandBottomH, m_bandZoom, m_bandOffset})
		connect(sl, &QSlider::valueChanged, this, &EditorWindow::onBandPropsChanged);
	return w;
}

void EditorWindow::onBandPropsChanged()
{
	if (m_syncing)
		return;
	TitleBands &b = m_project.bands;
	b.title = m_bandTitle->toPlainText();
	b.subtitle = m_bandSubtitle->text();
	b.bottomText = m_bandBottomText->toPlainText();
	b.titleSize = m_bandTitleSize->value();
	b.subtitleSize = m_bandSubSize->value();
	b.bottomSize = m_bandBottomSize->value();
	b.topHeight = m_bandTopH->value() & ~1;
	b.bottomHeight = m_bandBottomH->value() & ~1;
	b.zoom = m_bandZoom->value() / 100.0;
	b.offsetY = m_bandOffset->value() / 100.0;
	b.titleFont = m_bandTitleFont->currentData().toString();
	b.subtitleFont = m_bandSubFont->currentData().toString();
	b.bottomFont = m_bandBottomFont->currentData().toString();
	applyLayoutToPreview();
	projectChanged();
}

// ═════════════════════════════════════════════════════════════
// 글꼴 선택 목록 (자막 / 제목 띠 공통)
// ═════════════════════════════════════════════════════════════
static const QString kAddFontItem = QStringLiteral("__add_font__");

QComboBox *EditorWindow::makeFontCombo()
{
	auto *c = new QComboBox;
	c->setMinimumContentsLength(12);
	c->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
	c->setToolTip("목록 맨 아래 '＋ 글꼴 추가…'로 원하는 글꼴 파일(TTF/OTF)을 추가할 수 있어요");
	m_fontCombos.push_back(c);
	refreshFontCombos();
	return c;
}

void EditorWindow::refreshFontCombos()
{
	const bool wasSyncing = m_syncing;
	m_syncing = true;
	for (QComboBox *c : m_fontCombos) {
		const QString keep = c->currentData().toString();
		c->blockSignals(true);
		c->clear();
		c->addItem(FontManager::displayName(QString()), QString());
		c->setItemData(0, FontManager::qfont(QString(), 15), Qt::FontRole);
		for (const FontManager::Entry &e : FontManager::fonts()) {
			c->addItem(e.family, e.path);
			c->setItemData(c->count() - 1, FontManager::qfont(e.path, 15), Qt::FontRole); // 목록에서 모양 미리보기
			c->setItemData(c->count() - 1, e.label, Qt::ToolTipRole);
		}
		c->addItem("＋ 글꼴 추가…", kAddFontItem);
		const int idx = c->findData(keep);
		c->setCurrentIndex(idx >= 0 && keep != kAddFontItem ? idx : 0);
		c->blockSignals(false);
	}
	m_syncing = wasSyncing;
}

void EditorWindow::setFontComboValue(QComboBox *combo, const QString &path)
{
	combo->blockSignals(true);
	int idx = combo->findData(path);
	if (idx < 0 && !path.isEmpty()) {
		// 프로젝트에 저장된 글꼴 파일이 지금 목록에 없으면 (다른 PC 등) 이름만이라도 표시
		combo->insertItem(combo->count() - 1, FontManager::displayName(path) + " (파일 없음 → 기본 글꼴)", path);
		idx = combo->findData(path);
	}
	combo->setCurrentIndex(std::max(0, idx));
	combo->blockSignals(false);
}

bool EditorWindow::handleFontComboAdd(QComboBox *combo)
{
	if (combo->currentData().toString() != kAddFontItem)
		return false;
	const QStringList files = QFileDialog::getOpenFileNames(
		this, "글꼴 파일 추가",
		QStandardPaths::writableLocation(QStandardPaths::DownloadLocation),
		"글꼴 파일 (*.ttf *.otf *.ttc);;모든 파일 (*)");
	QString added;
	for (const QString &f : files) {
		QString err;
		const QString path = FontManager::addFontFile(f, &err);
		if (path.isEmpty()) {
			log(QFileInfo(f).fileName() + ": " + err);
			continue;
		}
		log("글꼴 추가: " + FontManager::displayName(path));
		added = path;
	}
	// 모든 글꼴 목록을 새로 채우고, 각 목록은 프로젝트에 저장된 글꼴로 되돌림 (취소했을 때 포함)
	refreshFontCombos();
	const TitleBands &tb = m_project.bands;
	setFontComboValue(m_bandTitleFont, tb.titleFont);
	setFontComboValue(m_bandSubFont, tb.subtitleFont);
	setFontComboValue(m_bandBottomFont, tb.bottomFont);
	const int si = m_subList->currentRow();
	setFontComboValue(m_subFont, (si >= 0 && si < m_project.subtitles.size()) ? m_project.subtitles[si].font : QString());

	if (!added.isEmpty()) {
		setFontComboValue(combo, added); // 방금 추가한 글꼴을 바로 선택해서 반영
		if (combo == m_subFont)
			onSubtitlePropsChanged();
		else
			onBandPropsChanged();
	}
	return true;
}

QWidget *EditorWindow::buildExportTab()
{
	auto *w = new QWidget;
	auto *form = new QFormLayout(w);

	m_outName = new QLineEdit(QFileInfo(m_project.filePath).completeBaseName());
	form->addRow("파일 이름", m_outName);

	m_previewExportBtn = new QPushButton("⚡ 빠른 미리보기 내보내기 (540×960)");
	m_previewExportBtn->setMinimumHeight(36);
	m_previewExportBtn->setToolTip("저화질·30fps로 빠르게 렌더링해서 컷, 자막, 비트 효과를 확인하는 용도");
	connect(m_previewExportBtn, &QPushButton::clicked, this, [this] { onExport(true); });
	form->addRow(m_previewExportBtn);

	m_exportBtn = new QPushButton("쇼츠로 내보내기 (고화질 1080×1920)");
	m_exportBtn->setMinimumHeight(44);
	connect(m_exportBtn, &QPushButton::clicked, this, [this] { onExport(false); });
	form->addRow(m_exportBtn);
	form->addRow(helpLabel("미리보기는 shorts/preview 폴더에 매번 같은 이름으로 덮어써집니다. "
			       "확인이 끝나면 고화질로 내보내서 업로드하세요."));

	m_progress = new QProgressBar;
	m_progress->setRange(0, 100);
	m_progress->setValue(0);
	form->addRow(m_progress);

	auto *openBtn = new QPushButton("결과 폴더 열기");
	connect(openBtn, &QPushButton::clicked, this, [this] {
		QDir().mkpath(m_shortsDir);
		QDesktopServices::openUrl(QUrl::fromLocalFile(m_shortsDir));
	});
	form->addRow(openBtn);

	m_log = new QPlainTextEdit;
	m_log->setReadOnly(true);
	m_log->setMinimumHeight(160);
	form->addRow(m_log);
	return w;
}

void EditorWindow::setupShortcuts()
{
	auto add = [this](const QKeySequence &k, auto fn) {
		auto *s = new QShortcut(k, this);
		connect(s, &QShortcut::activated, this, fn);
	};
	add(QKeySequence(Qt::Key_Space), [this] { togglePlay(); });
	add(QKeySequence(Qt::Key_S), [this] { splitAtPlayhead(); });
	add(QKeySequence(Qt::Key_B), [this] { splitSelectedOnBeats(); });
	add(QKeySequence(Qt::Key_Delete), [this] { deleteSelectedSegment(); });
	add(QKeySequence(Qt::CTRL | Qt::Key_D), [this] { duplicateSelectedSegment(); });
	add(QKeySequence(Qt::Key_T), [this] { addSubtitle(); });
	add(QKeySequence(Qt::Key_Left), [this] { seek(position() - 1.0); });
	add(QKeySequence(Qt::Key_Right), [this] { seek(position() + 1.0); });
	add(QKeySequence(Qt::Key_Comma), [this] { seek(position() - 0.1); });
	add(QKeySequence(Qt::Key_Period), [this] { seek(position() + 0.1); });
	add(QKeySequence(Qt::Key_Home), [this] { seek(0); });
	add(QKeySequence(Qt::CTRL | Qt::Key_Z), [this] { undo(); });
	add(QKeySequence(Qt::CTRL | Qt::Key_Y), [this] { redo(); });
	add(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Z), [this] { redo(); });
}

QPushButton *EditorWindow::makeColorButton(QColor *target, std::function<void()> onChange)
{
	auto *b = new QPushButton;
	b->setFixedSize(60, 26);
	paintColorButton(b, *target);
	connect(b, &QPushButton::clicked, this, [this, b, target, onChange] {
		const QColor c = QColorDialog::getColor(*target, this, "색상 선택");
		if (!c.isValid())
			return;
		*target = c;
		paintColorButton(b, c);
		onChange();
	});
	return b;
}

void EditorWindow::paintColorButton(QPushButton *b, const QColor &c)
{
	b->setStyleSheet(QString("background:%1; border:1px solid #555; border-radius:4px;").arg(c.name()));
}

void EditorWindow::loadUiFromProject(bool keepPosition)
{
	m_syncing = true;
	const MusicTrack &m = m_project.music;
	m_musicFile->setText(m.path.isEmpty() ? "없음" : QFileInfo(m.path).fileName());
	m_musicOffset->setValue(m.fileOffset);
	m_musicVol->setValue(int(std::lround(m.volume * 100)));
	m_gameVol->setValue(int(std::lround(m_project.gameVolume * 100)));
	m_musicFade->setChecked(m.fadeOut);
	m_bpm->setValue(m.bpm);
	m_firstBeat->setValue(m.firstBeat);
	m_beatEvery->setCurrentIndex(std::max(0, m_beatEvery->findData(m.beatEvery)));
	m_fxBeatZoom->setChecked(m_project.beatFx.zoom);
	m_fxBeatShake->setChecked(m_project.beatFx.shake);
	m_fxBeatStrength->setCurrentIndex(m_project.beatFx.strength);
	m_fxBeatEvery->setCurrentIndex(std::max(0, m_fxBeatEvery->findData(m_project.beatFx.every)));

	auto loadCard = [](CardUi &ui, const TitleCard &c) {
		ui.enabled->setChecked(c.enabled);
		ui.duration->setValue(c.duration);
		ui.title->setText(c.title);
		ui.subtitle->setText(c.subtitle);
		paintColorButton(ui.bg, c.background);
		paintColorButton(ui.color, c.color);
	};
	loadCard(m_introUi, m_project.intro);
	loadCard(m_outroUi, m_project.outro);

	m_layout->setCurrentIndex(std::max(0, m_layout->findData(int(m_project.layout))));
	m_bandBox->setEnabled(m_project.layout == ShortsLayout::TitleBands);
	{
		const TitleBands &b = m_project.bands;
		m_bandTitle->setPlainText(b.title);
		m_bandSubtitle->setText(b.subtitle);
		m_bandBottomText->setPlainText(b.bottomText);
		m_bandTitleSize->setValue(b.titleSize);
		m_bandSubSize->setValue(b.subtitleSize);
		m_bandBottomSize->setValue(b.bottomSize);
		paintColorButton(m_bandTitleColor, b.titleColor);
		paintColorButton(m_bandSubColor, b.subtitleColor);
		paintColorButton(m_bandBottomColor, b.bottomColor);
		paintColorButton(m_bandBg, b.background);
		m_bandTopH->setValue(b.topHeight);
		m_bandBottomH->setValue(b.bottomHeight);
		m_bandZoom->setValue(int(std::lround(b.zoom * 100)));
		m_bandOffset->setValue(int(std::lround(b.offsetY * 100)));
		setFontComboValue(m_bandTitleFont, b.titleFont);
		setFontComboValue(m_bandSubFont, b.subtitleFont);
		setFontComboValue(m_bandBottomFont, b.bottomFont);
	}
	m_syncing = false;

	refreshClipList();
	refreshSubtitleList();
	applyLayoutToPreview();
	rebuildSubtitleVisuals();
	if (keepPosition) {
		const double pos = position(); // selectSubtitle 이 위치를 옮길 수 있어서 먼저 기억
		selectSegment(std::min(m_timeline->selectedSegment(), int(m_project.segments.size()) - 1));
		selectSubtitle(std::min(m_subList->currentRow(), int(m_project.subtitles.size()) - 1));
		m_activeSeg = -1;
		seek(pos);
	} else {
		selectSegment(-1);
		seek(0);
	}
}

// ═════════════════════════════════════════════════════════════
// 클립 / 플레이어
// ═════════════════════════════════════════════════════════════
void EditorWindow::createPlayerFor(int i)
{
	// 실행 취소로 클립이 빠졌다가 다시 추가되면 같은 번호의 플레이어를 재사용
	m_thumbs->request(m_project.sources[i].path);
	if (i < m_players.size()) {
		SourcePlayer &sp = m_players[i];
		sp.path = m_project.sources[i].path;
		sp.player->setSource(QUrl::fromLocalFile(sp.path));
		sp.player->pause();
		return;
	}

	SourcePlayer sp;
	sp.path = m_project.sources[i].path;
	sp.player = new QMediaPlayer(this);
	sp.audio = new QAudioOutput(this);
	sp.player->setAudioOutput(sp.audio);
	sp.item = new QGraphicsVideoItem(m_videoClip);
	sp.item->setAspectRatioMode(Qt::IgnoreAspectRatio);
	sp.item->setVisible(false);
	sp.player->setVideoOutput(sp.item);
	m_players.push_back(sp);

	connect(sp.player, &QMediaPlayer::durationChanged, this, [this, i] { onSourceInfo(i); });
	connect(sp.player, &QMediaPlayer::hasAudioChanged, this, [this, i] { onSourceInfo(i); });
	connect(sp.player, &QMediaPlayer::mediaStatusChanged, this, [this, i](QMediaPlayer::MediaStatus s) {
		if (s == QMediaPlayer::LoadedMedia)
			onSourceInfo(i);
		else if (s == QMediaPlayer::InvalidMedia)
			log("클립을 열 수 없습니다: " + m_project.sources.value(i).path);
	});
	connect(sp.item, &QGraphicsVideoItem::nativeSizeChanged, this, [this, i] { onSourceInfo(i); });

	sp.player->setSource(QUrl::fromLocalFile(m_project.sources[i].path));
	sp.player->pause(); // 첫 프레임 디코딩
}

void EditorWindow::onSourceInfo(int i)
{
	if (i < 0 || i >= m_players.size())
		return;
	const SourcePlayer &sp = m_players[i];
	const double dur = sp.player->duration() / 1000.0;
	const QSize size = sp.item->nativeSize().toSize();
	m_project.setSourceInfo(i, dur, size, sp.player->hasAudio());
	// 클립 길이/해상도를 알아낸 것은 사용자 편집이 아니므로 실행 취소 기록에 넣지 않음
	if (!m_undoTimer->isActive())
		m_undoBaseline = m_project.toJson();
	refreshClipList();
	applyLayoutToPreview();
	projectChanged();
	syncPlayers(position(), m_playing);
}

void EditorWindow::addClips()
{
	const QStringList files = QFileDialog::getOpenFileNames(this, "클립 추가", m_clipsDir,
								"영상 (*.mp4 *.mkv *.mov)");
	for (const QString &f : files) {
		const int idx = m_project.addSource(f);
		createPlayerFor(idx);
	}
	if (!files.isEmpty()) {
		refreshClipList();
		projectChanged();
	}
}

void EditorWindow::refreshClipList()
{
	const int keep = m_clipList->currentRow();
	m_clipList->clear();
	for (int i = 0; i < m_project.sources.size(); ++i) {
		const SourceClip &c = m_project.sources[i];
		int uses = 0;
		for (const Segment &s : m_project.segments)
			uses += (s.source == i);
		auto *item = new QListWidgetItem(QString("#%1  %2   (%3초 · 타임라인 %4개)")
							 .arg(i + 1)
							 .arg(c.name())
							 .arg(c.duration, 0, 'f', 1)
							 .arg(uses));
		item->setForeground(kClipColors[i % 8].lighter(130));
		m_clipList->addItem(item);
	}
	if (keep >= 0 && keep < m_clipList->count())
		m_clipList->setCurrentRow(keep);
}

// ═════════════════════════════════════════════════════════════
// 재생: 결과 시간 시계를 기준으로 클립 플레이어와 BGM을 맞춰 줌
// ═════════════════════════════════════════════════════════════
double EditorWindow::position() const
{
	return m_playing ? m_playStart + m_clock.elapsed() / 1000.0 : m_pos;
}

void EditorWindow::play()
{
	const double total = m_project.totalDuration();
	if (total <= 0)
		return;
	if (m_pos >= total - 0.05)
		m_pos = 0;
	m_playStart = m_pos;
	m_clock.start();
	m_playing = true;
	m_activeSeg = -1;
	m_timeline->setFollowPlayhead(true);
	m_playBtn->setText("⏸ 일시정지");
	syncPlayers(m_pos, true);
}

void EditorWindow::pause()
{
	m_pos = position();
	m_playing = false;
	for (const SourcePlayer &sp : m_players)
		sp.player->pause();
	m_bgm->pause();
	m_timeline->setFollowPlayhead(false);
	m_playBtn->setText("▶ 재생");
	syncPlayers(m_pos, false);
}

void EditorWindow::togglePlay() { m_playing ? pause() : play(); }

void EditorWindow::seek(double t)
{
	t = std::clamp(t, 0.0, std::max(0.0, m_project.totalDuration() - 0.01));
	m_pos = t;
	if (m_playing) {
		m_playStart = t;
		m_clock.restart();
		m_activeSeg = -1;
	}
	syncPlayers(t, m_playing);
	m_timeline->setPosition(t);
	updateOverlays(t);
	updateTimeLabel(t);
}

void EditorWindow::tick()
{
	double t = position();
	const double total = m_project.totalDuration();
	if (m_playing && t >= total) {
		m_pos = std::max(0.0, total - 0.01);
		pause();
		t = m_pos;
	}
	if (m_playing)
		syncPlayers(t, true);
	m_timeline->setPosition(t);
	updateOverlays(t);
	updateTimeLabel(t);
}

void EditorWindow::syncPlayers(double t, bool playing)
{
	const auto loc = m_project.locate(t);

	// ── 영상 ──
	int wantSource = -1;
	if (loc.kind == EditProject::Locate::Segment)
		wantSource = m_project.segments[loc.seg].source;

	if (wantSource != m_activeSource) {
		if (m_activeSource >= 0 && m_activeSource < m_players.size()) {
			m_players[m_activeSource].player->pause();
			m_players[m_activeSource].item->setVisible(false);
		}
		m_activeSource = wantSource;
		m_activeSeg = -1;
	}

	if (wantSource >= 0 && wantSource < m_players.size()) {
		const Segment &seg = m_project.segments[loc.seg];
		SourcePlayer &sp = m_players[wantSource];
		sp.item->setVisible(true);
		sp.audio->setVolume(float(std::min(1.0, m_project.gameVolume)));
		const qint64 expected = qint64(loc.srcTime * 1000.0);
		const qint64 drift = std::llabs(sp.player->position() - expected);

		if (playing) {
			// 속도 램프 중에는 재생 속도가 계속 바뀜
			const double rate = seg.speedAt(loc.srcTime - seg.in);
			if (std::abs(sp.player->playbackRate() - rate) > 0.02)
				sp.player->setPlaybackRate(rate);
			if (sp.player->playbackState() != QMediaPlayer::PlayingState) {
				sp.player->setPosition(expected);
				sp.player->play();
			} else {
				// 같은 클립에서 바로 이어지는 컷이면 다시 탐색하지 않음 (끊김 방지)
				bool contiguous = false;
				if (m_activeSeg >= 0 && m_activeSeg == loc.seg - 1) {
					const Segment &prev = m_project.segments[m_activeSeg];
					contiguous = prev.source == seg.source && std::abs(prev.out - seg.in) < 0.02;
				}
				if ((m_activeSeg != loc.seg && !contiguous) || drift > 150)
					sp.player->setPosition(expected);
			}
		} else {
			if (sp.player->playbackState() == QMediaPlayer::PlayingState)
				sp.player->pause();
			if (drift > 15)
				sp.player->setPosition(expected);
		}
		m_activeSeg = loc.seg;
	} else {
		m_activeSeg = -1;
	}

	// ── BGM ──
	if (!m_project.music.path.isEmpty() && m_bgm->duration() > 0) {
		const double total = m_project.totalDuration();
		double vol = std::min(1.0, m_project.music.volume);
		if (m_project.music.fadeOut && total > 3.0 && t > total - 1.5)
			vol *= std::clamp((total - t) / 1.5, 0.0, 1.0);
		m_bgmAudio->setVolume(float(vol));

		const qint64 expected = qint64((m_project.music.fileOffset + t) * 1000.0);
		const bool inRange = expected < m_bgm->duration();
		const qint64 drift = std::llabs(m_bgm->position() - expected);
		if (playing && inRange) {
			if (m_bgm->playbackState() != QMediaPlayer::PlayingState) {
				m_bgm->setPosition(expected);
				m_bgm->play();
			} else if (drift > 120) {
				m_bgm->setPosition(expected);
			}
		} else {
			if (m_bgm->playbackState() == QMediaPlayer::PlayingState)
				m_bgm->pause();
			if (inRange && drift > 50)
				m_bgm->setPosition(expected);
		}
	}
}

void EditorWindow::updateTimeLabel(double t)
{
	QString beatInfo;
	if (m_project.beatInterval() > 0) {
		const double b = m_project.nearestBeat(t);
		if (b >= 0)
			beatInfo = QString("   ·   가장 가까운 비트 %1").arg(fmt(b));
	}
	m_timeLabel->setText(QString("%1 / %2%3").arg(fmt(t), fmt(m_project.totalDuration()), beatInfo));
}

// ═════════════════════════════════════════════════════════════
// 미리보기 캔버스
// ═════════════════════════════════════════════════════════════
void EditorWindow::applyLayoutToPreview()
{
	const ShortsLayout layout = m_project.layout;
	const bool blur = (layout == ShortsLayout::BlurBackground);
	const bool bands = (layout == ShortsLayout::TitleBands);
	m_canvas->setBrush(blur ? QColor("#22222A") : QColor("#111114"));

	const TitleBands &tb = m_project.bands;
	const double midTop = bands ? tb.topHeight : 0.0;
	const double midH = bands ? tb.middleHeight() : kCanvasH;
	m_videoClip->setRect(0, midTop, kCanvasW, midH);

	for (int i = 0; i < m_players.size() && i < m_project.sources.size(); ++i) {
		const QSize s = m_project.sources[i].size;
		const double sw = std::max(1, s.width());
		const double sh = std::max(1, s.height());
		QGraphicsVideoItem *item = m_players[i].item;
		QPointF pos;
		if (bands) {
			// 원본에서 잘라낼 영역이 가운데 칸(1080 x midH)에 꽉 차도록 배치
			const QRect c = m_project.bandCropRect(s);
			const double k = kCanvasW / std::max(1, c.width());
			item->setSize(QSizeF(sw * k, sh * k));
			pos = QPointF(-c.x() * k, midTop - c.y() * k);
		} else if (blur) {
			const double h = kCanvasW * sh / sw;
			item->setSize(QSizeF(kCanvasW, h));
			pos = QPointF(0, (kCanvasH - h) / 2);
		} else {
			const double w = kCanvasH * sw / sh;
			item->setSize(QSizeF(w, kCanvasH));
			pos = QPointF((kCanvasW - w) / 2, 0);
		}
		item->setPos(pos);
		item->setData(0, pos); // 글리치 흔들림 기준 위치
		item->setTransformOriginPoint(item->size().width() / 2, item->size().height() / 2);
	}

	const bool mm = (layout == ShortsLayout::CropWithMinimap);
	m_minimapHint->setVisible(mm);
	if (mm) {
		const QSize s = m_project.sources.isEmpty() ? QSize(1920, 1080) : m_project.sources.first().size;
		const QRectF r = m_project.minimapRect;
		const double mw = 380.0;
		const double mh = mw * (r.height() * s.height()) / (r.width() * std::max(1, s.width()));
		m_minimapHint->setRect(kCanvasW - 28 - mw, 150, mw, mh);
	}
	rebuildBandVisuals();
}

void EditorWindow::rebuildBandVisuals()
{
	qDeleteAll(m_bandTexts);
	m_bandTexts.clear();

	const bool bands = (m_project.layout == ShortsLayout::TitleBands);
	const TitleBands &tb = m_project.bands;
	m_bandTop->setVisible(bands && tb.topHeight > 0);
	m_bandBottom->setVisible(bands && tb.bottomHeight > 0);
	if (!bands)
		return;
	m_bandTop->setRect(0, 0, kCanvasW, tb.topHeight);
	m_bandBottom->setRect(0, kCanvasH - tb.bottomHeight, kCanvasW, tb.bottomHeight);
	m_bandTop->setBrush(tb.background);
	m_bandBottom->setBrush(tb.background);

	for (const BandLine &l : BandLayout::lines(m_project)) {
		auto *t = new QGraphicsSimpleTextItem(l.text, m_canvas);
		t->setFont(FontManager::qfont(l.fontPath, l.size));
		t->setBrush(l.color);
		t->setZValue(7);
		const QRectF br = t->boundingRect();
		t->setPos((kCanvasW - br.width()) / 2, l.slotTop + (l.slotHeight - br.height()) / 2);
		m_bandTexts.push_back(t);
	}
}


void EditorWindow::rebuildSubtitleVisuals()
{
	for (SubVisual &v : m_subVisuals) {
		delete v.text;
		delete v.box;
	}
	m_subVisuals.clear();

	for (const Subtitle &s : m_project.subtitles) {
		SubVisual v;
		v.box = new QGraphicsRectItem(m_canvas);
		v.box->setBrush(QColor(0, 0, 0, 102));
		v.box->setPen(Qt::NoPen);
		v.box->setZValue(10);

		v.text = new QGraphicsSimpleTextItem(s.text.trimmed(), m_canvas);
		v.text->setFont(FontManager::qfont(s.font, s.fontSize));
		v.text->setBrush(s.color);
		v.text->setPen(QPen(Qt::black, 3));
		v.text->setZValue(11);

		const QRectF br = v.text->boundingRect();
		const QPointF pos((kCanvasW - br.width()) / 2, kCanvasH * s.y - br.height() / 2);
		v.text->setPos(pos);
		v.box->setRect(QRectF(pos, br.size()).adjusted(-22, -22, 22, 22));
		v.box->setVisible(false);
		v.text->setVisible(false);
		m_subVisuals.push_back(v);
	}
	updateOverlays(position());
}

void EditorWindow::rebuildCardVisual()
{
	// 현재 위치가 인트로인지 아웃트로인지에 따라 내용 채움 (updateOverlays에서 호출)
	const auto loc = m_project.locate(position());
	const TitleCard &c = (loc.kind == EditProject::Locate::Outro) ? m_project.outro : m_project.intro;

	m_cardItem->setBrush(c.background);
	QFont tf("Malgun Gothic");
	tf.setBold(true);
	tf.setPixelSize(110);
	m_cardTitle->setFont(tf);
	m_cardTitle->setText(c.title.trimmed());
	m_cardTitle->setBrush(c.color);
	QFont sf("Malgun Gothic");
	sf.setBold(true);
	sf.setPixelSize(54);
	m_cardSub->setFont(sf);
	m_cardSub->setText(c.subtitle.trimmed());
	QColor sc = c.color;
	sc.setAlphaF(0.85);
	m_cardSub->setBrush(sc);

	const bool hasSub = !c.subtitle.trimmed().isEmpty();
	const QRectF tb = m_cardTitle->boundingRect();
	m_cardTitle->setPos((kCanvasW - tb.width()) / 2,
			    hasSub ? kCanvasH / 2 - tb.height() - 20 : (kCanvasH - tb.height()) / 2);
	const QRectF sb = m_cardSub->boundingRect();
	m_cardSub->setPos((kCanvasW - sb.width()) / 2, kCanvasH / 2 + 30);
}

void EditorWindow::updateOverlays(double t)
{
	const auto loc = m_project.locate(t);

	// 자막
	for (int i = 0; i < m_subVisuals.size() && i < m_project.subtitles.size(); ++i) {
		const Subtitle &s = m_project.subtitles[i];
		const bool on = t >= s.start && t < s.end && !s.text.trimmed().isEmpty();
		m_subVisuals[i].text->setVisible(on);
		m_subVisuals[i].box->setVisible(on && s.box);
	}

	// 인트로/아웃트로 카드
	const bool card = (loc.kind == EditProject::Locate::Intro || loc.kind == EditProject::Locate::Outro);
	m_cardItem->setVisible(card);
	if (card) {
		rebuildCardVisual();
		const double d = (loc.kind == EditProject::Locate::Intro) ? m_project.introDuration()
									    : m_project.outroDuration();
		const double fd = std::min(0.3, d / 3);
		m_cardTitle->setOpacity(std::clamp(std::min(loc.local / fd, (d - loc.local) / fd), 0.0, 1.0));
		m_cardSub->setOpacity(m_cardTitle->opacity());
	}

	// 구간 효과 / 전환
	m_flashOverlay->setVisible(false);
	m_effectBadge->setVisible(false);
	if (loc.kind != EditProject::Locate::Segment)
		return;

	const Segment &seg = m_project.segments[loc.seg];
	const double local = loc.local;
	const double dur = seg.outDuration();
	double scale = seg.zoom ? 1.3 : 1.0;
	QPointF jitter;

	switch (seg.transIn) {
	case Transition::Flash:
		if (local < 0.25) {
			m_flashOverlay->setBrush(Qt::white);
			m_flashOverlay->setOpacity(1.0 - local / 0.25);
			m_flashOverlay->setVisible(true);
		}
		break;
	case Transition::BlackDip:
		if (local < 0.15) {
			m_flashOverlay->setBrush(Qt::black);
			m_flashOverlay->setOpacity(1.0 - local / 0.15);
			m_flashOverlay->setVisible(true);
		}
		break;
	case Transition::ZoomPunch:
		scale *= std::max(1.0, 1.35 - 0.35 * local / 0.3);
		break;
	case Transition::Glitch:
		if (local < 0.2)
			jitter = QPointF(QRandomGenerator::global()->bounded(-24, 25),
					 QRandomGenerator::global()->bounded(-12, 13));
		break;
	case Transition::None:
		break;
	}
	if (loc.seg + 1 < m_project.segments.size() &&
	    m_project.segments[loc.seg + 1].transIn == Transition::BlackDip && dur - local < 0.15) {
		m_flashOverlay->setBrush(Qt::black);
		m_flashOverlay->setOpacity(1.0 - (dur - local) / 0.15);
		m_flashOverlay->setVisible(true);
	}

	// 비트 효과 (미리보기)
	double phase = 0.0;
	const double pulse = m_project.beatPulseAt(t, &phase);
	if (pulse > 0.001) {
		if (m_project.beatFx.zoom)
			scale *= 1.0 + m_project.beatFx.zoomAmount() * pulse;
		if (m_project.beatFx.shake) {
			const double a = m_project.beatFx.shakeAmount() * pulse;
			jitter += QPointF(a * kCanvasW * std::sin(phase * 70), a * kCanvasH * 0.5 * std::cos(phase * 55));
		}
	}

	if (seg.source >= 0 && seg.source < m_players.size()) {
		QGraphicsVideoItem *item = m_players[seg.source].item;
		item->setScale(scale);
		if (item->data(0).isValid())
			item->setPos(item->data(0).toPointF() + jitter);
	}

	QStringList info;
	if (std::abs(seg.speed - 1.0) > 0.01)
		info << QString("%1x").arg(seg.speedAt(loc.srcTime - seg.in), 0, 'f', 2);
	for (const QString &n : seg.effectNames())
		if (n != "줌인")
			info << n;
	if (!info.isEmpty()) {
		m_effectBadge->setText(info.join(" · "));
		m_effectBadge->setVisible(true);
	}
}

// ═════════════════════════════════════════════════════════════
// 구간
// ═════════════════════════════════════════════════════════════
void EditorWindow::splitAtPlayhead()
{
	const double t = position();
	if (!m_project.splitAt(t)) {
		log("여기서는 자를 수 없습니다 (구간 끝에 너무 가깝거나 인트로/아웃트로)");
		return;
	}
	selectSegment(m_project.locate(t).seg);
	projectChanged();
}

void EditorWindow::splitSelectedOnBeats()
{
	const int i = m_timeline->selectedSegment();
	if (i < 0 || i >= m_project.segments.size()) {
		log("비트마다 자를 구간을 먼저 선택하세요");
		return;
	}
	if (m_project.beatInterval() <= 0) {
		log("음악 탭에서 BGM과 BPM을 먼저 설정하세요");
		return;
	}
	const double start = m_project.segmentStart(i);
	const double end = start + m_project.segments[i].outDuration();
	int n = 0;
	for (double b : m_project.beatTimes()) {
		if (b <= start + 0.15)
			continue;
		if (b >= end - 0.15)
			break;
		n += m_project.splitAt(b) ? 1 : 0;
	}
	log(QString("비트 위치에서 %1번 잘랐습니다").arg(n));
	selectSegment(i);
	projectChanged();
}

void EditorWindow::deleteSelectedSegment()
{
	const int i = m_timeline->selectedSegment();
	if (i < 0 || i >= m_project.segments.size()) {
		log("삭제할 구간을 타임라인에서 먼저 선택하세요");
		return;
	}
	m_project.segments.removeAt(i);
	selectSegment(std::min(i, int(m_project.segments.size()) - 1));
	projectChanged();
	seek(position());
}

void EditorWindow::duplicateSelectedSegment()
{
	const int i = m_timeline->selectedSegment();
	if (i < 0 || i >= m_project.segments.size())
		return;
	Segment copy = m_project.segments[i];
	copy.transIn = Transition::Flash;
	m_project.segments.insert(i + 1, copy);
	selectSegment(i + 1);
	projectChanged();
}

void EditorWindow::moveSelected(int delta)
{
	const int i = m_timeline->selectedSegment();
	const int j = i + delta;
	if (i < 0 || j < 0 || j >= m_project.segments.size())
		return;
	m_project.moveSegment(i, j);
	selectSegment(j);
	projectChanged();
	seek(m_project.segmentStart(j));
}

void EditorWindow::selectSegment(int i)
{
	if (i >= m_project.segments.size())
		i = -1;
	m_timeline->setSelectedSegment(i);
	m_segProps->setEnabled(i >= 0);
	if (i < 0) {
		m_segInfo->setText("타임라인에서 구간을 클릭하세요");
		return;
	}

	const Segment &s = m_project.segments[i];
	m_syncing = true;
	m_segInfo->setText(QString("#%1 %2\n원본 %3 ~ %4 → 결과 %5초")
				   .arg(s.source + 1)
				   .arg(m_project.sources.value(s.source).name())
				   .arg(fmt(s.in), fmt(s.out))
				   .arg(s.outDuration(), 0, 'f', 2));
	int speedIdx = 3;
	for (int k = 0; k < kSpeeds.size(); ++k)
		if (std::abs(kSpeeds[k].second - s.speed) < 0.01)
			speedIdx = k;
	m_segSpeed->setCurrentIndex(speedIdx);
	m_segTrans->setCurrentIndex(int(s.transIn));
	m_fxZoom->setChecked(s.zoom);
	m_fxShake->setChecked(s.shake);
	m_fxGray->setChecked(s.gray);
	m_fxVivid->setChecked(s.vivid);
	m_fxVignette->setChecked(s.vignette);
	m_rampIn->setChecked(s.rampIn);
	m_rampOut->setChecked(s.rampOut);
	m_rampLen->setValue(s.rampLen);
	const bool canRamp = std::abs(s.speed - 1.0) > 0.01;
	m_rampIn->setEnabled(canRamp);
	m_rampOut->setEnabled(canRamp);
	m_rampLen->setEnabled(canRamp);
	m_syncing = false;
}

void EditorWindow::onSegmentPropsChanged()
{
	const int i = m_timeline->selectedSegment();
	if (m_syncing || i < 0 || i >= m_project.segments.size())
		return;
	Segment &s = m_project.segments[i];
	s.speed = m_segSpeed->currentData().toDouble();
	s.transIn = Transition(m_segTrans->currentData().toInt());
	s.zoom = m_fxZoom->isChecked();
	s.shake = m_fxShake->isChecked();
	s.gray = m_fxGray->isChecked();
	s.vivid = m_fxVivid->isChecked();
	s.vignette = m_fxVignette->isChecked();
	s.rampIn = m_rampIn->isChecked();
	s.rampOut = m_rampOut->isChecked();
	s.rampLen = m_rampLen->value();
	selectSegment(i);
	projectChanged();
	seek(position());
}

// ═════════════════════════════════════════════════════════════
// 음악
// ═════════════════════════════════════════════════════════════
void EditorWindow::chooseMusic()
{
	const QString f = QFileDialog::getOpenFileName(this, "배경음악 선택", QString(),
						       "오디오 (*.mp3 *.wav *.m4a *.aac *.ogg *.flac)");
	if (f.isEmpty())
		return;
	m_project.music.path = f;
	m_project.music.bpm = 0;
	m_project.music.firstBeat = 0;
	m_bgm->setSource(QUrl::fromLocalFile(f));
	loadUiFromProject();
	projectChanged();
	detectBeats();
}

void EditorWindow::clearMusic()
{
	m_project.music = MusicTrack{};
	m_bgm->stop();
	m_bgm->setSource(QUrl());
	m_beatStatus->clear();
	loadUiFromProject();
	projectChanged();
}

void EditorWindow::onMusicPropsChanged()
{
	if (m_syncing)
		return;
	MusicTrack &m = m_project.music;
	m.fileOffset = m_musicOffset->value();
	m.volume = m_musicVol->value() / 100.0;
	m_project.gameVolume = m_gameVol->value() / 100.0;
	m.fadeOut = m_musicFade->isChecked();
	m.bpm = m_bpm->value();
	m.firstBeat = m_firstBeat->value();
	m.beatEvery = m_beatEvery->currentData().toInt();
	m_project.beatFx.zoom = m_fxBeatZoom->isChecked();
	m_project.beatFx.shake = m_fxBeatShake->isChecked();
	m_project.beatFx.strength = m_fxBeatStrength->currentIndex();
	m_project.beatFx.every = m_fxBeatEvery->currentData().toInt();
	if (m_project.beatFx.enabled() && m.bpm <= 0)
		log("비트 효과를 쓰려면 BPM이 필요합니다 (음악 선택 → 자동 감지)");
	projectChanged();
	syncPlayers(position(), m_playing);
}

void EditorWindow::detectBeats()
{
	if (m_project.music.path.isEmpty()) {
		log("먼저 음악 파일을 선택하세요");
		return;
	}
	m_detectBtn->setEnabled(false);
	m_beatStatus->setText("분석 중...");
	m_beats->start(m_project.music.path);
}

void EditorWindow::snapToBeats()
{
	if (m_project.beatInterval() <= 0) {
		log("BPM이 설정되지 않았습니다 (음악 탭에서 자동 감지 또는 직접 입력)");
		return;
	}
	const int n = m_project.snapCutsToBeats();
	log(QString("컷 %1개를 비트에 맞췄습니다").arg(n));
	selectSegment(m_timeline->selectedSegment());
	projectChanged();
	seek(position());
}

// ═════════════════════════════════════════════════════════════
// 자막
// ═════════════════════════════════════════════════════════════
void EditorWindow::addSubtitle()
{
	const double total = m_project.totalDuration();
	if (total <= 0)
		return;
	const double t = position();
	Subtitle s;
	if (!m_project.subtitles.isEmpty()) {
		const Subtitle &prev = m_project.subtitles.last();
		s.fontSize = prev.fontSize;
		s.color = prev.color;
		s.y = prev.y;
		s.box = prev.box;
		s.font = prev.font;
	}
	s.start = t;
	s.end = std::min(t + 2.0, total);
	if (s.end - s.start < 0.3)
		s.start = std::max(0.0, s.end - 2.0);
	s.text = "자막 입력";
	m_project.subtitles.push_back(s);

	refreshSubtitleList();
	rebuildSubtitleVisuals();
	selectSubtitle(int(m_project.subtitles.size()) - 1);
	m_tabs->setCurrentIndex(3);
	m_subText->setFocus();
	m_subText->selectAll();
	projectChanged();
}

void EditorWindow::deleteSelectedSubtitle()
{
	const int i = m_subList->currentRow();
	if (i < 0 || i >= m_project.subtitles.size())
		return;
	m_project.subtitles.removeAt(i);
	refreshSubtitleList();
	rebuildSubtitleVisuals();
	selectSubtitle(std::min(i, int(m_project.subtitles.size()) - 1));
	projectChanged();
}

void EditorWindow::selectSubtitle(int i)
{
	if (i >= m_project.subtitles.size())
		i = -1;
	m_timeline->setSelectedSubtitle(i);
	m_subProps->setEnabled(i >= 0);

	m_syncing = true;
	m_subList->setCurrentRow(i);
	if (i >= 0) {
		const Subtitle &s = m_project.subtitles[i];
		m_subText->setPlainText(s.text);
		m_subStart->setValue(s.start);
		m_subEnd->setValue(s.end);
		m_subSize->setValue(s.fontSize);
		m_subColorValue = s.color;
		paintColorButton(m_subColor, s.color);
		m_subY->setValue(int(std::lround(s.y * 100)));
		m_subBox->setChecked(s.box);
		setFontComboValue(m_subFont, s.font);
		if (!m_playing && (position() < s.start || position() >= s.end))
			seek(s.start);
	}
	m_syncing = false;
}

void EditorWindow::onSubtitlePropsChanged()
{
	const int i = m_subList->currentRow();
	if (m_syncing || i < 0 || i >= m_project.subtitles.size())
		return;
	Subtitle &s = m_project.subtitles[i];
	s.text = m_subText->toPlainText();
	s.start = m_subStart->value();
	s.end = std::max(m_subEnd->value(), s.start + 0.1);
	s.fontSize = m_subSize->value();
	s.color = m_subColorValue;
	s.y = m_subY->value() / 100.0;
	s.box = m_subBox->isChecked();
	s.font = m_subFont->currentData().toString();

	m_syncing = true;
	if (QListWidgetItem *it = m_subList->item(i))
		it->setText(QString("%1 – %2   %3").arg(fmt(s.start), fmt(s.end), s.text.simplified()));
	m_syncing = false;

	rebuildSubtitleVisuals();
	projectChanged();
}

void EditorWindow::refreshSubtitleList()
{
	m_syncing = true;
	const int keep = m_subList->currentRow();
	m_subList->clear();
	for (const Subtitle &s : m_project.subtitles)
		m_subList->addItem(QString("%1 – %2   %3").arg(fmt(s.start), fmt(s.end), s.text.simplified()));
	if (keep >= 0 && keep < m_subList->count())
		m_subList->setCurrentRow(keep);
	m_syncing = false;
}

// ═════════════════════════════════════════════════════════════
// 인트로 / 아웃트로
// ═════════════════════════════════════════════════════════════
void EditorWindow::onCardPropsChanged()
{
	if (m_syncing)
		return;
	auto read = [](const CardUi &ui, TitleCard &c) {
		c.enabled = ui.enabled->isChecked();
		c.duration = ui.duration->value();
		c.title = ui.title->text();
		c.subtitle = ui.subtitle->text();
	};
	read(m_introUi, m_project.intro);
	read(m_outroUi, m_project.outro);
	projectChanged();
	seek(position());
}

// ═════════════════════════════════════════════════════════════
// 내보내기 / 기타
// ═════════════════════════════════════════════════════════════
void EditorWindow::onExport(bool previewQuality)
{
	if (m_exporter->isRunning()) {
		m_exporter->cancel();
		return;
	}
	QString base = m_outName->text().trimmed();
	base.replace(QRegularExpression(R"([\\/:*?"<>|])"), "_");
	if (base.isEmpty())
		base = "madmovie";

	QString out;
	if (previewQuality) {
		// 미리보기는 같은 파일을 덮어씀 (재생 중이라 지울 수 없으면 번호를 붙임)
		const QString dir = m_shortsDir + "/preview";
		QDir().mkpath(dir);
		out = dir + "/" + base + "_preview.mp4";
		for (int n = 2; QFileInfo::exists(out) && !QFile::remove(out); ++n)
			out = dir + QString("/%1_preview_%2.mp4").arg(base).arg(n);
	} else {
		QDir().mkpath(m_shortsDir);
		out = m_shortsDir + "/" + base + ".mp4";
		for (int n = 2; QFileInfo::exists(out); ++n)
			out = m_shortsDir + QString("/%1_%2.mp4").arg(base).arg(n);
	}

	if (m_playing)
		pause();
	m_project.save();
	(previewQuality ? m_previewExportBtn : m_exportBtn)->setText("취소");
	(previewQuality ? m_exportBtn : m_previewExportBtn)->setEnabled(false);
	m_tabs->setCurrentIndex(5);
	m_exporter->start(m_project, out, previewQuality);
}

void EditorWindow::projectChanged()
{
	if (!m_restoring) {
		m_undoTimer->start();
		updateUndoButtons();
	}
	m_timeline->update();
	refreshClipList();
	updateTimeLabel(position());
	m_saveTimer->start();
}

// ═════════════════════════════════════════════════════════════
// 실행 취소 / 다시 실행
// 편집할 때마다 프로젝트 전체를 JSON 스냅샷으로 기록 (프로젝트가 작아서 가볍고 확실함)
// ═════════════════════════════════════════════════════════════
void EditorWindow::commitUndoStep()
{
	m_undoTimer->stop();
	const QJsonObject cur = m_project.toJson();
	if (cur == m_undoBaseline)
		return;
	m_undoStack.push_back(m_undoBaseline);
	if (m_undoStack.size() > 200)
		m_undoStack.removeFirst();
	m_redoStack.clear();
	m_undoBaseline = cur;
	updateUndoButtons();
}

void EditorWindow::undo()
{
	commitUndoStep(); // 아직 기록 안 된 마지막 편집부터 확정
	if (m_undoStack.isEmpty()) {
		log("더 이상 되돌릴 작업이 없습니다");
		return;
	}
	m_redoStack.push_back(m_undoBaseline);
	restoreSnapshot(m_undoStack.takeLast());
	log("실행 취소");
}

void EditorWindow::redo()
{
	commitUndoStep();
	if (m_redoStack.isEmpty()) {
		log("다시 실행할 작업이 없습니다");
		return;
	}
	m_undoStack.push_back(m_undoBaseline);
	restoreSnapshot(m_redoStack.takeLast());
	log("다시 실행");
}

void EditorWindow::restoreSnapshot(const QJsonObject &snapshot)
{
	m_restoring = true;
	const QVector<SourceClip> before = m_project.sources;
	const QString musicBefore = m_project.music.path;

	m_project.fromJson(snapshot);

	// 같은 파일이면 실제로 읽어 둔 길이/해상도 정보를 유지
	for (int i = 0; i < m_project.sources.size() && i < before.size(); ++i) {
		SourceClip &c = m_project.sources[i];
		if (c.path == before[i].path && before[i].duration > 0) {
			c.duration = before[i].duration;
			c.size = before[i].size;
			c.hasAudio = before[i].hasAudio;
		}
	}
	// 클립 구성이 바뀌었으면 플레이어도 맞춰 줌
	for (int i = 0; i < m_project.sources.size(); ++i) {
		if (i >= m_players.size())
			createPlayerFor(i);
		else if (m_players[i].path != m_project.sources[i].path)
			createPlayerFor(i); // 같은 번호 재사용
	}
	if (m_project.music.path != musicBefore) {
		m_bgm->stop();
		m_bgm->setSource(m_project.music.path.isEmpty() ? QUrl() : QUrl::fromLocalFile(m_project.music.path));
	}

	m_undoBaseline = m_project.toJson();
	loadUiFromProject(true);
	m_timeline->update();
	m_restoring = false;
	m_saveTimer->start();
	updateUndoButtons();
}

void EditorWindow::updateUndoButtons()
{
	if (!m_undoBtn)
		return;
	// 기록 대기 중인 편집이 있으면 되돌릴 수 있음
	m_undoBtn->setEnabled(!m_undoStack.isEmpty() || m_undoTimer->isActive());
	m_redoBtn->setEnabled(!m_redoStack.isEmpty());
}

void EditorWindow::log(const QString &msg)
{
	m_log->appendPlainText(msg);
	statusBar()->showMessage(msg.section('\n', 0, 0), 5000);
}

void EditorWindow::closeEvent(QCloseEvent *e)
{
	if (m_exporter->isRunning()) {
		if (QMessageBox::question(this, "내보내기 중", "내보내기를 취소하고 닫을까요?") != QMessageBox::Yes) {
			e->ignore();
			return;
		}
		m_exporter->cancel();
	}
	m_tickTimer->stop();
	for (const SourcePlayer &sp : m_players)
		sp.player->stop();
	m_bgm->stop();
	m_project.save();
	e->accept();
}
