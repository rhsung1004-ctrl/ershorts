#pragma once

#include <QObject>
#include <QString>
#include <QVector>
#include <obs.h>

#include <atomic>
#include <memory>
#include <mutex>

class KillSoundDetector;

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

	// 게임 소리에서 킬(빈사) 효과음을 들으면 killSoundDetected 신호 (threshold: KillSoundDetector 기준값)
	void setKillDetection(bool on, double threshold);

	QString videoEncoderName() const { return m_videoEncoderId; }
	uint32_t baseWidth() const { return m_baseW; }
	uint32_t baseHeight() const { return m_baseH; }

signals:
	void clipSaved(const QString &path);
	void replayStateChanged(bool active);
	void logMessage(const QString &msg);
	void killSoundDetected(double score, int kind);

private:
	bool resetVideo(QString *error);
	bool resetAudio();
	void createScene();
	void rebuildCaptureSource();
	void rebuildAudioSources();
	bool createEncodersAndOutput(QString *error);
	void releaseOutput();
	QString pickVideoEncoder() const;

	void attachKillListener();
	void detachKillListener();
	static void onAudioCapture(void *param, obs_source_t *source, const struct audio_data *audio, bool muted);

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

	// 킬 사운드 감지 (오디오 스레드에서 실행)
	std::unique_ptr<KillSoundDetector> m_kill;
	std::mutex m_killMutex;
	std::atomic<bool> m_killOn{false};
	obs_source_t *m_listenSrc = nullptr;
};
