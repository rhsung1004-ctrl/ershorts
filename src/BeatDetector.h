#pragma once

#include <QObject>
#include <QProcess>
#include <vector>

// BGM 파일의 BPM과 첫 박 위치를 자동으로 찾음
//  1) ffmpeg로 앞부분 90초를 모노 PCM으로 디코딩
//  2) 에너지 변화량(온셋) 곡선 계산
//  3) 자기상관으로 템포 후보 → 콤 필터 탐색으로 박 간격과 위상 정밀화
class BeatDetector : public QObject {
	Q_OBJECT
public:
	explicit BeatDetector(QObject *parent = nullptr);
	~BeatDetector() override;

	void start(const QString &audioPath);
	bool isRunning() const { return m_proc.state() != QProcess::NotRunning; }

	struct Result {
		bool ok = false;
		double bpm = 0.0;
		double firstBeat = 0.0; // 초
		double confidence = 0.0;
	};
	static Result analyze(const std::vector<float> &mono, int sampleRate);

signals:
	void finished(bool ok, double bpm, double firstBeat, const QString &message);

private:
	QProcess m_proc;
	QByteArray m_pcm;
};
