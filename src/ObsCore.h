#pragma once

#include <QObject>
#include <QString>
#include <QVector>
#include <obs.h>

#include <atomic>
#include <memory>
#include <mutex>

class GameClock;
class HudKillWatcher;
class QTimer;

// libobs를 앱 안에서 초기화하고, 캡처 소스 + 리플레이 버퍼를 관리하는 클래스
class ObsCore : public QObject {
	Q_OBJECT
public:
	// Window: Windows Graphics Capture로 게임 창을 캡처 (게임에 끼어들지 않아 안티치트/창 모드에 강함)
	enum class CaptureMode { Window, Game, Monitor };

	struct WindowInfo {
		QString label; // 목록에 보여줄 이름 "[실행파일]: 창 제목"
		QString value; // OBS가 쓰는 "제목:클래스:실행파일" 문자열
	};
	static QVector<WindowInfo> listWindows();
	static QString findEternalReturnWindow(); // 못 찾으면 빈 문자열

	struct Settings {
		CaptureMode captureMode = CaptureMode::Window;
		QString gameWindow;        // 비어 있으면 자동 (창 캡처: 이터널리턴 창 찾기 / 게임 캡처: 전체화면 앱)
		int bufferSeconds = 45;    // 리플레이 버퍼 길이
		int maxBufferMB = 1500;
		QString outputDir;         // 클립 저장 폴더
		bool captureMic = false;
		bool gameAudioOnly = true; // 창 캡처일 때 게임 프로그램 소리만 녹음 (알림음·디스코드 등 제외)
	};

	explicit ObsCore(QObject *parent = nullptr);
	~ObsCore() override;

	bool startup(QString *error);
	void shutdown();

	bool applySettings(const Settings &s, QString *error);

	bool startReplayBuffer(QString *error);
	void stopReplayBuffer();
	bool isReplayActive() const;
	bool saveReplay();          // 비동기, 완료 시 clipSaved 시그널

	// 창 캡처 자동 모드에서 아직 게임 창을 못 찾았으면 다시 찾아 연결 (주기적으로 호출)
	void retargetWindowIfNeeded();
	QString currentWindowTarget() const { return m_windowTarget; }

	// 화면 오른쪽 위 TK/K/A 숫자를 지켜보다가 TK 와 K 또는 A 가 함께 오르면 hudKillDetected 신호
	void setHudKillWatch(bool on);
	// 화면 가운데 위 "N일 차"와 해/달을 읽어 지금 게임 시간 ("4일차 낮") 기억
	void setGameClock(bool on);
	QString gameTimeLabel() const; // 모르면 빈 문자열
	QString lastOcrText() const;

	QString videoEncoderName() const { return m_videoEncoderId; }
	uint32_t baseWidth() const { return m_baseW; }
	uint32_t baseHeight() const { return m_baseH; }

signals:
	void clipSaved(const QString &path);
	void replayStateChanged(bool active);
	void logMessage(const QString &msg);
	void hudKillDetected(bool kill, bool assist);

private:
	bool resetVideo(QString *error);
	bool resetAudio();
	void createScene();
	void rebuildCaptureSource();
	void rebuildAudioSources();
	bool createEncodersAndOutput(QString *error);
	void releaseOutput();
	QString pickVideoEncoder() const;

	static void onRawVideo(void *param, struct video_data *frame);
	void updateCaptureSize();
	void updateRawCallback();

	static void onReplaySaved(void *data, calldata_t *cd);
	static void onReplayStopped(void *data, calldata_t *cd);

	Settings m_settings;
	bool m_started = false;

	uint32_t m_baseW = 1920, m_baseH = 1080;

	obs_scene_t *m_scene = nullptr;
	obs_source_t *m_capture = nullptr;
	obs_sceneitem_t *m_captureItem = nullptr;
	QString m_windowTarget; // 창 캡처가 지금 붙어 있는 창
	obs_source_t *m_desktopAudio = nullptr;
	obs_source_t *m_micAudio = nullptr;

	obs_encoder_t *m_venc = nullptr;
	obs_encoder_t *m_aenc = nullptr;
	obs_output_t *m_replay = nullptr;
	QString m_videoEncoderId;

	// 킬 숫자 감시 (OBS 영상 스레드에서 1초에 5번)
	std::unique_ptr<HudKillWatcher> m_hud;
	std::mutex m_hudMutex;
	bool m_rawCbOn = false;
	std::atomic<bool> m_hudOn{false};
	std::atomic<bool> m_clockOn{false};
	std::unique_ptr<GameClock> m_clock;
	int m_rawFrame = 0; // 영상 스레드 전용
	std::atomic<int> m_capW{0}, m_capH{0}; // 게임 화면(캡처 소스) 크기
	QTimer *m_capSizeTimer = nullptr;
};
