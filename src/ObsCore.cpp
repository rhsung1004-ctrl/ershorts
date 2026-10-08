#include "ObsCore.h"

#include "GameClock.h"
#include "HudKillWatcher.h"

#include <algorithm>
#include <cmath>

#include <QTimer>

#include <QCoreApplication>
#include <QDir>
#include <QGuiApplication>
#include <QScreen>
#include <QStandardPaths>
#include <QStringList>
#include <QThread>

#include <graphics/vec2.h>

#include <vector>

namespace {
// OBS 기본 채널 배치와 맞춤: 0 = 씬, 1 = 데스크톱 오디오, 3 = 마이크
constexpr uint32_t kChannelScene = 0;
constexpr uint32_t kChannelDesktop = 1;
constexpr uint32_t kChannelMic = 3;

QByteArray u8(const QString &s) { return s.toUtf8(); }
} // namespace

ObsCore::ObsCore(QObject *parent)
	: QObject(parent), m_hud(std::make_unique<HudKillWatcher>()), m_clock(std::make_unique<GameClock>())
{
	// 게임 창 크기는 영상 스레드에서 직접 묻지 않고 1초마다 여기서 기억해 둠
	m_capSizeTimer = new QTimer(this);
	connect(m_capSizeTimer, &QTimer::timeout, this, &ObsCore::updateCaptureSize);
	m_capSizeTimer->start(1000);
}

ObsCore::~ObsCore() { shutdown(); }

bool ObsCore::startup(QString *error)
{
	if (m_started)
		return true;

	// libobs는 data/, obs-plugins/ 를 현재 작업 폴더 기준 상대경로(../../)로 찾으므로
	// 실행 파일 폴더(bin/64bit)를 작업 폴더로 맞춰 둡니다.
	QDir::setCurrent(QCoreApplication::applicationDirPath());

	const QString cfg = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation) +
			    "/obs-module-config";
	QDir().mkpath(cfg);

	if (!obs_startup("ko-KR", u8(cfg).constData(), nullptr)) {
		*error = "obs_startup 실패";
		return false;
	}
	m_started = true;

	if (!resetAudio()) {
		*error = "오디오 초기화 실패";
		return false;
	}
	if (!resetVideo(error))
		return false;

	// 플러그인 로드 (win-capture, win-wasapi, obs-x264, obs-nvenc, obs-ffmpeg, obs-outputs ...)
	obs_load_all_modules();
	obs_post_load_modules();
	obs_log_loaded_modules();

	createScene();
	emit logMessage(QString("OBS 초기화 완료 (%1x%2)").arg(m_baseW).arg(m_baseH));
	return true;
}

bool ObsCore::resetAudio()
{
	obs_audio_info ai = {};
	ai.samples_per_sec = 48000;
	ai.speakers = SPEAKERS_STEREO;
	return obs_reset_audio(&ai);
}

bool ObsCore::resetVideo(QString *error)
{
	QScreen *screen = QGuiApplication::primaryScreen();
	const qreal dpr = screen ? screen->devicePixelRatio() : 1.0;
	if (screen) {
		m_baseW = uint32_t(screen->size().width() * dpr) & ~1u;
		m_baseH = uint32_t(screen->size().height() * dpr) & ~1u;
	}

	obs_video_info vi = {};
	vi.graphics_module = "libobs-d3d11";
	vi.fps_num = 60;
	vi.fps_den = 1;
	vi.base_width = m_baseW;
	vi.base_height = m_baseH;
	// 세로 크롭 시 화질 손실을 줄이려고 출력 해상도 = 원본 해상도로 녹화
	vi.output_width = m_baseW;
	vi.output_height = m_baseH;
	vi.output_format = VIDEO_FORMAT_NV12;
	vi.colorspace = VIDEO_CS_709;
	vi.range = VIDEO_RANGE_PARTIAL;
	vi.adapter = 0;
	vi.gpu_conversion = true;
	vi.scale_type = OBS_SCALE_BICUBIC;

	const int r = obs_reset_video(&vi);
	if (r != OBS_VIDEO_SUCCESS) {
		*error = QString("obs_reset_video 실패 (코드 %1). D3D11 지원 GPU 드라이버를 확인하세요.").arg(r);
		return false;
	}
	return true;
}

void ObsCore::createScene()
{
	m_scene = obs_scene_create("ERShorts Scene");
	obs_set_output_source(kChannelScene, obs_scene_get_source(m_scene));
}

void ObsCore::rebuildCaptureSource()
{
	if (m_captureItem) {
		obs_sceneitem_remove(m_captureItem);
		m_captureItem = nullptr;
	}
	if (m_capture) {
		obs_source_release(m_capture);
		m_capture = nullptr;
	}

	obs_data_t *s = obs_data_create();
	const char *id = nullptr;

	m_windowTarget.clear();
	if (m_settings.captureMode == CaptureMode::Window) {
		id = "window_capture";
		QString target = m_settings.gameWindow.trimmed();
		if (target.isEmpty())
			target = findEternalReturnWindow();
		m_windowTarget = target;
		obs_data_set_string(s, "window", u8(target).constData());
		obs_data_set_int(s, "method", 2);   // Windows Graphics Capture
		obs_data_set_int(s, "priority", 2); // 실행 파일 이름으로 창을 다시 찾음 (창 제목이 바뀌어도 OK)
		obs_data_set_bool(s, "cursor", false);
		obs_data_set_bool(s, "client_area", true);
		// 게임 창의 소리만 따로 녹음 (Windows 10 2004 이상). 이때는 데스크톱 소리를 녹음하지 않음
		obs_data_set_bool(s, "capture_audio", m_settings.gameAudioOnly);
		if (target.isEmpty())
			emit logMessage("이터널리턴 창을 아직 찾지 못했어요. 게임을 켜면 자동으로 연결됩니다.");
		else
			emit logMessage("캡처할 창: " + target);
	} else if (m_settings.captureMode == CaptureMode::Game) {
		id = "game_capture";
		if (m_settings.gameWindow.trimmed().isEmpty()) {
			obs_data_set_string(s, "capture_mode", "any_fullscreen");
		} else {
			// 형식: "창제목:창클래스:실행파일.exe" (OBS 게임 캡처의 window 문자열과 동일)
			obs_data_set_string(s, "capture_mode", "window");
			obs_data_set_string(s, "window", u8(m_settings.gameWindow).constData());
			obs_data_set_int(s, "priority", 2); // 실행파일 이름 우선 매칭
		}
		obs_data_set_bool(s, "capture_cursor", false);
		obs_data_set_bool(s, "allow_transparency", false);
	} else {
		id = "monitor_capture"; // 게임 캡처가 막힐 때의 대안 (화면 전체 캡처)
		obs_data_set_bool(s, "capture_cursor", false);
	}

	m_capture = obs_source_create(id, "Game", s, nullptr);
	obs_data_release(s);

	if (!m_capture) {
		emit logMessage(QString("캡처 소스 생성 실패: %1 (win-capture 플러그인 확인)").arg(id));
		return;
	}

	m_captureItem = obs_scene_add(m_scene, m_capture);

	// 해상도가 달라도 화면에 꽉 맞게
	vec2 bounds;
	vec2_set(&bounds, float(m_baseW), float(m_baseH));
	obs_sceneitem_set_bounds_type(m_captureItem, OBS_BOUNDS_SCALE_INNER);
	obs_sceneitem_set_bounds_alignment(m_captureItem, OBS_ALIGN_CENTER);
	obs_sceneitem_set_bounds(m_captureItem, &bounds);

	emit logMessage(m_settings.captureMode == CaptureMode::Window ? "창 캡처 소스 준비됨"
			: m_settings.captureMode == CaptureMode::Game ? "게임 캡처 소스 준비됨"
								      : "모니터 캡처 소스 준비됨");
}

QVector<ObsCore::WindowInfo> ObsCore::listWindows()
{
	QVector<WindowInfo> out;
	// OBS 창 캡처가 쓰는 창 목록을 그대로 가져옴
	obs_properties_t *props = obs_get_source_properties("window_capture");
	if (!props)
		return out;
	obs_property_t *p = obs_properties_get(props, "window");
	const size_t n = p ? obs_property_list_item_count(p) : 0;
	for (size_t i = 0; i < n; ++i) {
		const char *name = obs_property_list_item_name(p, i);
		const char *val = obs_property_list_item_string(p, i);
		if (!val || !*val)
			continue;
		out.push_back({QString::fromUtf8(name ? name : val), QString::fromUtf8(val)});
	}
	obs_properties_destroy(props);
	return out;
}

QString ObsCore::findEternalReturnWindow()
{
	// 실행 파일/창 제목에 Eternal Return(이터널리턴)이 들어간 창
	for (const WindowInfo &w : listWindows()) {
		const QString all = (w.label + " " + w.value).toLower();
		if (all.contains("eternal") || all.contains(QString::fromUtf8("이터널")))
			return w.value;
	}
	return {};
}

void ObsCore::retargetWindowIfNeeded()
{
	if (!m_capture || m_settings.captureMode != CaptureMode::Window || !m_windowTarget.isEmpty() ||
	    !m_settings.gameWindow.trimmed().isEmpty())
		return;
	const QString found = findEternalReturnWindow();
	if (found.isEmpty())
		return;
	m_windowTarget = found;
	obs_data_t *s = obs_source_get_settings(m_capture);
	obs_data_set_string(s, "window", u8(found).constData());
	obs_source_update(m_capture, s);
	obs_data_release(s);
	emit logMessage("이터널리턴 창을 찾아서 연결했어요");
}

void ObsCore::rebuildAudioSources()
{
	obs_set_output_source(kChannelDesktop, nullptr);
	obs_set_output_source(kChannelMic, nullptr);
	if (m_desktopAudio) {
		obs_source_release(m_desktopAudio);
		m_desktopAudio = nullptr;
	}
	if (m_micAudio) {
		obs_source_release(m_micAudio);
		m_micAudio = nullptr;
	}

	obs_data_t *s = obs_data_create();
	obs_data_set_string(s, "device_id", "default");

	const bool appAudio = m_settings.gameAudioOnly && m_settings.captureMode == CaptureMode::Window;
	if (appAudio) {
		emit logMessage("소리: 게임 소리만 녹음 (Windows 알림음, 디스코드 등은 녹음 안 됨)");
	} else {
		m_desktopAudio = obs_source_create("wasapi_output_capture", "Desktop Audio", s, nullptr);
		if (m_desktopAudio)
			obs_set_output_source(kChannelDesktop, m_desktopAudio);
		emit logMessage("소리: 컴퓨터 전체 소리 녹음");
	}

	if (m_settings.captureMic) {
		m_micAudio = obs_source_create("wasapi_input_capture", "Mic", s, nullptr);
		if (m_micAudio)
			obs_set_output_source(kChannelMic, m_micAudio);
	}
	obs_data_release(s);
}

QString ObsCore::pickVideoEncoder() const
{
	// 게임 성능에 영향이 적은 하드웨어 인코더 우선
	static const char *preferred[] = {
		"obs_nvenc_h264_tex", // NVIDIA (OBS 31+)
		"jim_nvenc",          // NVIDIA (OBS 30 이하)
		"h264_texture_amf",   // AMD
		"obs_qsv11_v2",       // Intel
		"obs_x264",           // CPU (최후 수단)
	};

	QStringList available;
	const char *id = nullptr;
	for (size_t i = 0; obs_enum_encoder_types(i, &id); ++i)
		available << QString::fromUtf8(id);

	for (const char *p : preferred)
		if (available.contains(p))
			return p;
	return "obs_x264";
}

bool ObsCore::createEncodersAndOutput(QString *error)
{
	releaseOutput();

	m_videoEncoderId = pickVideoEncoder();
	const bool isX264 = (m_videoEncoderId == "obs_x264");

	obs_data_t *vs = obs_data_create();
	obs_data_set_int(vs, "keyint_sec", 1); // 키프레임 1초: 클립 자르기가 정확해짐
	if (isX264) {
		obs_data_set_string(vs, "rate_control", "CRF");
		obs_data_set_int(vs, "crf", 20);
		obs_data_set_string(vs, "preset", "veryfast");
	} else {
		obs_data_set_string(vs, "rate_control", "CQP");
		obs_data_set_int(vs, "cqp", 20);
		obs_data_set_string(vs, "preset2", "p5");
	}
	m_venc = obs_video_encoder_create(u8(m_videoEncoderId).constData(), "Replay Video", vs, nullptr);
	obs_data_release(vs);
	if (!m_venc) {
		*error = "비디오 인코더 생성 실패: " + m_videoEncoderId;
		return false;
	}
	obs_encoder_set_video(m_venc, obs_get_video());

	obs_data_t *as = obs_data_create();
	obs_data_set_int(as, "bitrate", 192);
	m_aenc = obs_audio_encoder_create("ffmpeg_aac", "Replay Audio", as, 0, nullptr);
	obs_data_release(as);
	if (!m_aenc) {
		*error = "오디오 인코더(ffmpeg_aac) 생성 실패";
		return false;
	}
	obs_encoder_set_audio(m_aenc, obs_get_audio());

	QDir().mkpath(m_settings.outputDir);
	obs_data_t *os = obs_data_create();
	obs_data_set_string(os, "directory", u8(m_settings.outputDir).constData());
	obs_data_set_string(os, "format", "ER_%CCYY-%MM-%DD_%hh-%mm-%ss");
	obs_data_set_string(os, "extension", "mp4");
	obs_data_set_int(os, "max_time_sec", m_settings.bufferSeconds);
	obs_data_set_int(os, "max_size_mb", m_settings.maxBufferMB);
	m_replay = obs_output_create("replay_buffer", "Replay Buffer", os, nullptr);
	obs_data_release(os);
	if (!m_replay) {
		*error = "replay_buffer 출력 생성 실패 (obs-ffmpeg 플러그인 확인)";
		return false;
	}

	obs_output_set_video_encoder(m_replay, m_venc);
	obs_output_set_audio_encoder(m_replay, m_aenc, 0);

	signal_handler_t *sh = obs_output_get_signal_handler(m_replay);
	signal_handler_connect(sh, "saved", &ObsCore::onReplaySaved, this);
	signal_handler_connect(sh, "stop", &ObsCore::onReplayStopped, this);

	emit logMessage("비디오 인코더: " + m_videoEncoderId);
	return true;
}

bool ObsCore::applySettings(const Settings &s, QString *error)
{
	const bool wasActive = isReplayActive();
	if (wasActive)
		stopReplayBuffer();

	m_settings = s;
	if (m_settings.outputDir.isEmpty())
		m_settings.outputDir =
			QStandardPaths::writableLocation(QStandardPaths::MoviesLocation) + "/ERShorts";

	rebuildCaptureSource();
	rebuildAudioSources();
	if (!createEncodersAndOutput(error))
		return false;

	if (wasActive)
		return startReplayBuffer(error);
	return true;
}

bool ObsCore::startReplayBuffer(QString *error)
{
	if (!m_replay) {
		*error = "리플레이 버퍼가 준비되지 않았습니다";
		return false;
	}
	if (obs_output_active(m_replay))
		return true;

	if (!obs_output_start(m_replay)) {
		const char *e = obs_output_get_last_error(m_replay);
		*error = QString("리플레이 버퍼 시작 실패: %1").arg(e ? QString::fromUtf8(e) : "알 수 없음");
		return false;
	}
	emit replayStateChanged(true);
	emit logMessage(QString("리플레이 버퍼 시작 (최근 %1초 유지)").arg(m_settings.bufferSeconds));
	return true;
}

void ObsCore::stopReplayBuffer()
{
	if (!m_replay || !obs_output_active(m_replay))
		return;
	obs_output_stop(m_replay);
	for (int i = 0; i < 60 && obs_output_active(m_replay); ++i)
		QThread::msleep(50);
	if (obs_output_active(m_replay))
		obs_output_force_stop(m_replay);
}

bool ObsCore::isReplayActive() const
{
	return m_replay && obs_output_active(m_replay);
}

bool ObsCore::saveReplay()
{
	if (!isReplayActive())
		return false;
	calldata_t cd = {};
	proc_handler_t *ph = obs_output_get_proc_handler(m_replay);
	proc_handler_call(ph, "save", &cd);
	calldata_free(&cd);
	emit logMessage("클립 저장 중...");
	return true;
}

// libobs 내부 스레드에서 호출되므로 Qt 메인 스레드로 넘겨서 처리
void ObsCore::onReplaySaved(void *data, calldata_t *)
{
	auto *self = static_cast<ObsCore *>(data);

	calldata_t cd = {};
	proc_handler_t *ph = obs_output_get_proc_handler(self->m_replay);
	proc_handler_call(ph, "get_last_replay", &cd);
	const char *p = calldata_string(&cd, "path");
	const QString path = p ? QString::fromUtf8(p) : QString();
	calldata_free(&cd);

	QMetaObject::invokeMethod(self, [self, path] { emit self->clipSaved(path); }, Qt::QueuedConnection);
}

void ObsCore::onReplayStopped(void *data, calldata_t *cd)
{
	auto *self = static_cast<ObsCore *>(data);
	const int code = int(calldata_int(cd, "code"));
	QMetaObject::invokeMethod(
		self,
		[self, code] {
			emit self->replayStateChanged(false);
			if (code != OBS_OUTPUT_SUCCESS)
				emit self->logMessage(QString("리플레이 버퍼가 비정상 종료됨 (코드 %1)").arg(code));
			else
				emit self->logMessage("리플레이 버퍼 중지");
		},
		Qt::QueuedConnection);
}

void ObsCore::releaseOutput()
{
	if (m_replay) {
		stopReplayBuffer();
		signal_handler_t *sh = obs_output_get_signal_handler(m_replay);
		signal_handler_disconnect(sh, "saved", &ObsCore::onReplaySaved, this);
		signal_handler_disconnect(sh, "stop", &ObsCore::onReplayStopped, this);
		obs_output_release(m_replay);
		m_replay = nullptr;
	}
	if (m_venc) {
		obs_encoder_release(m_venc);
		m_venc = nullptr;
	}
	if (m_aenc) {
		obs_encoder_release(m_aenc);
		m_aenc = nullptr;
	}
}

void ObsCore::shutdown()
{
	if (!m_started)
		return;

	releaseOutput();
	m_hudOn = false;
	m_clockOn = false;
	updateRawCallback();

	for (uint32_t ch = 0; ch < MAX_CHANNELS; ++ch)
		obs_set_output_source(ch, nullptr);

	if (m_captureItem) {
		obs_sceneitem_remove(m_captureItem);
		m_captureItem = nullptr;
	}
	if (m_capture) {
		obs_source_release(m_capture);
		m_capture = nullptr;
	}
	if (m_desktopAudio) {
		obs_source_release(m_desktopAudio);
		m_desktopAudio = nullptr;
	}
	if (m_micAudio) {
		obs_source_release(m_micAudio);
		m_micAudio = nullptr;
	}
	if (m_scene) {
		obs_scene_release(m_scene);
		m_scene = nullptr;
	}

	obs_shutdown();
	m_started = false;
}

// ─── 킬 숫자 감시 ─────────────────────────────────────────
void ObsCore::updateCaptureSize()
{
	if (!m_capture) {
		m_capW = m_capH = 0;
		return;
	}
	m_capW = int(obs_source_get_width(m_capture));
	m_capH = int(obs_source_get_height(m_capture));
}

void ObsCore::setHudKillWatch(bool on)
{
	if (on && !m_hudOn) {
		std::lock_guard<std::mutex> lock(m_hudMutex);
		m_hud->reset();
	}
	m_hudOn = on;
	updateRawCallback();
}

void ObsCore::setGameClock(bool on)
{
	m_clockOn = on;
	updateRawCallback();
}

QString ObsCore::gameTimeLabel() const { return m_clockOn ? QString::fromStdString(m_clock->label()) : QString(); }

QString ObsCore::lastOcrText() const { return QString::fromStdString(m_clock->lastOcrText()); }

void ObsCore::updateRawCallback()
{
	const bool want = m_started && (m_hudOn || m_clockOn);
	if (want == m_rawCbOn)
		return;
	if (want) {
		updateCaptureSize();
		// 전체 범위 NV12, 60fps 중 12장마다 1장 = 초당 5장
		video_scale_info conv = {};
		conv.format = VIDEO_FORMAT_NV12;
		conv.width = m_baseW;
		conv.height = m_baseH;
		conv.range = VIDEO_RANGE_FULL;
		conv.colorspace = VIDEO_CS_709;
		obs_add_raw_video_callback2(&conv, 12, &ObsCore::onRawVideo, this);
	} else {
		obs_remove_raw_video_callback(&ObsCore::onRawVideo, this);
	}
	m_rawCbOn = want;
}

void ObsCore::onRawVideo(void *param, struct video_data *frame)
{
	auto *self = static_cast<ObsCore *>(param);
	const int sw = self->m_capW.load(), sh = self->m_capH.load();
	if (!frame || !frame->data[0] || sw <= 0 || sh <= 0)
		return;

	// 게임 화면이 캔버스 안에 들어간 자리 (가운데 맞춤, 비율 유지)
	const double W = self->m_baseW, H = self->m_baseH;
	const double s = std::min(W / sw, H / sh);
	const double gw = sw * s, gh = sh * s;
	const double gx = (W - gw) / 2, gy = (H - gh) / 2;
	const double u = gh / 720.0; // 게임 화면 높이 720 기준 1px
	const double x0 = gx + gw - (HudKillWatcher::kW + HudKillWatcher::kRoiRight) * u;
	const double y0 = gy + HudKillWatcher::kRoiTop * u;

	// ── 게임 시간 (1초에 한 번): 가운데 위 영역을 RGB 로 ──
	if (self->m_clockOn && (self->m_rawFrame++ % 5) == 0 && frame->data[1]) {
		thread_local std::vector<uint8_t> rgb(size_t(GameClock::kW) * GameClock::kH * 3);
		const uint8_t *Yp = frame->data[0];
		const uint8_t *UV = frame->data[1];
		const int lsY = int(frame->linesize[0]), lsUV = int(frame->linesize[1]);
		const double step = u / 4.0; // 720 기준 1px 를 4칸으로
		const double cx0 = gx + gw / 2 + GameClock::kLeft * u;
		const double cy0 = gy + GameClock::kTop * u;
		for (int j = 0; j < GameClock::kH; ++j) {
			const double fy = std::clamp(cy0 + (j + 0.5) * step - 0.5, 0.0, H - 2);
			const int y = int(fy);
			const double ty = fy - y;
			for (int i = 0; i < GameClock::kW; ++i) {
				const double fx = std::clamp(cx0 + (i + 0.5) * step - 0.5, 0.0, W - 2);
				const int x = int(fx);
				const double tx = fx - x;
				const double Yv = (1 - ty) * ((1 - tx) * Yp[size_t(y) * lsY + x] + tx * Yp[size_t(y) * lsY + x + 1]) +
						  ty * ((1 - tx) * Yp[size_t(y + 1) * lsY + x] + tx * Yp[size_t(y + 1) * lsY + x + 1]);
				const uint8_t *c = UV + size_t(y / 2) * lsUV + (x / 2) * 2;
				const double Cb = c[0] - 128.0, Cr = c[1] - 128.0;
				uint8_t *o = &rgb[(size_t(j) * GameClock::kW + i) * 3];
				o[0] = uint8_t(std::clamp(Yv + 1.5748 * Cr, 0.0, 255.0));
				o[1] = uint8_t(std::clamp(Yv - 0.1873 * Cb - 0.4681 * Cr, 0.0, 255.0));
				o[2] = uint8_t(std::clamp(Yv + 1.8556 * Cb, 0.0, 255.0));
			}
		}
		self->m_clock->feed(rgb.data(), int64_t(frame->timestamp / 1000000));
	}
	if (!self->m_hudOn)
		return;

	// ── 킬 숫자: 오른쪽 위 영역을 160x32 로 줄여 밝기만 ──
	thread_local std::vector<uint8_t> roi(HudKillWatcher::kW * HudKillWatcher::kH);
	const uint8_t *Y = frame->data[0];
	const int ls = int(frame->linesize[0]);
	for (int j = 0; j < HudKillWatcher::kH; ++j) {
		int ya = int(std::floor(y0 + j * u)), yb = int(std::floor(y0 + (j + 1) * u));
		ya = std::clamp(ya, 0, int(H) - 1);
		yb = std::clamp(std::max(yb, ya + 1), 1, int(H));
		for (int i = 0; i < HudKillWatcher::kW; ++i) {
			int xa = int(std::floor(x0 + i * u)), xb = int(std::floor(x0 + (i + 1) * u));
			xa = std::clamp(xa, 0, int(W) - 1);
			xb = std::clamp(std::max(xb, xa + 1), 1, int(W));
			unsigned sum = 0, n = 0;
			for (int y = ya; y < yb; ++y)
				for (int x = xa; x < xb; ++x) {
					sum += Y[size_t(y) * ls + x];
					++n;
				}
			roi[size_t(j * HudKillWatcher::kW + i)] = uint8_t(n ? sum / n : 0);
		}
	}

	thread_local std::vector<HudKillWatcher::Event> events;
	events.clear();
	{
		std::lock_guard<std::mutex> lock(self->m_hudMutex);
		self->m_hud->feed(roi.data(), double(frame->timestamp) / 1e9, &events);
	}
	for (const HudKillWatcher::Event &e : events) {
		const bool k = e.kill, a = e.assist;
		QMetaObject::invokeMethod(self, [self, k, a] { emit self->hudKillDetected(k, a); }, Qt::QueuedConnection);
	}
}
