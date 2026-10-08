#pragma once

#include <QMap>
#include <QObject>
#include <QProcess>
#include <QString>
#include <QStringList>
#include <QTemporaryDir>
#include <QVector>

#include <memory>

#include "EditProject.h"

// 매드무비 프로젝트(여러 클립 구간, 배속, 효과, 전환, 인트로/아웃트로, BGM, 자막)를
// ffmpeg 한 번으로 9:16 영상으로 렌더링 (번들 ffmpeg.exe 사용)
class ShortsExporter : public QObject {
	Q_OBJECT
public:
	explicit ShortsExporter(QObject *parent = nullptr);
	~ShortsExporter() override;

	bool isRunning() const { return m_proc.state() != QProcess::NotRunning; }
	// previewQuality: 540x960 / 30fps / 빠른 인코딩 (결과 확인용)
	void start(const EditProject &project, const QString &output, bool previewQuality = false);
	void cancel();

	static QString ffmpegPath();

signals:
	void progress(int percent);
	void finished(bool ok, const QString &outputPath);
	void logMessage(const QString &msg);

private:
	QString buildFilter();
	QString cardFilter(const TitleCard &card, const QString &tag, int frames);
	QString writeTextFile(const QString &name, const QString &text);
	// 인코더: 0 = NVIDIA, 1 = AMD, 2 = Intel, 3 = CPU(x264)
	QStringList buildArgs(int encoder);
	void run(int encoder);
	void onStdErr();
	void onFinished(int code, QProcess::ExitStatus status);

	QProcess m_proc;
	EditProject m_project;
	QString m_output;
	QStringList m_inputs;    // ffmpeg -i 목록
	QMap<int, int> m_inputOf; // 소스 클립 번호 → 입력 번호
	int m_bgmInput = -1;
	QVector<int> m_sfxInput; // 효과음별 입력 번호 (-1 = 파일 없음)
	QVector<int> m_imgInput; // 이미지별 입력 번호
	QString m_filter;
	int m_encoder = 0;
	static int s_lastGoodEncoder;
	bool m_previewQuality = false;
	bool m_cancelled = false;
	QByteArray m_errBuf;
	std::unique_ptr<QTemporaryDir> m_tmp;
};
