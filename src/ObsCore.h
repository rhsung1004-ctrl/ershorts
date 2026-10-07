#pragma once

#include <QObject>
#include <QString>
#include <obs.h>

// libobs를 앱 안에서 초기화하고, 캡처 소스 + 리플레이 버퍼를 관리하는 클래스
class ObsCore : public QObject {
	Q_OBJECT
public:
	enum class CaptureMode { Game, Monitor };

	struct Settings {
		CaptureMode captureMode = CaptureMode::Game;
		QString gameWindow;        // 비어 있으면 "전체화면 앱 자동 캡처"
		int bufferSeconds = 45;    // 리플레이 버퍼 길이
		int maxBufferMB = 1500;
		QString outputDir;         // 클립 저장 폴더
		bool captureMic = false;
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

	QString videoEncoderName() const { return m_videoEncoderId; }
	uint32_t baseWidth() const { return m_baseW; }
	uint32_t baseHeight() const { return m_baseH; }

signals:
	void clipSaved(const QString &path);
	void replayStateChanged(bool active);
	void logMessage(const QString &msg);

private:
	bool resetVideo(QString *error);
	bool resetAudio();
	void createScene();
	void rebuildCaptureSource();
	void rebuildAudioSources();
	bool createEncodersAndOutput(QString *error);
	void releaseOutput();
	QString pickVideoEncoder() const;

	static void onReplaySaved(void *data, calldata_t *cd);
	static void onReplayStopped(void *data, calldata_t *cd);

	Settings m_settings;
	bool m_started = false;

	uint32_t m_baseW = 1920, m_baseH = 1080;

	obs_scene_t *m_scene = nullptr;
	obs_source_t *m_capture = nullptr;
	obs_sceneitem_t *m_captureItem = nullptr;
	obs_source_t *m_desktopAudio = nullptr;
	obs_source_t *m_micAudio = nullptr;

	obs_encoder_t *m_venc = nullptr;
	obs_encoder_t *m_aenc = nullptr;
	obs_output_t *m_replay = nullptr;
	QString m_videoEncoderId;
};
