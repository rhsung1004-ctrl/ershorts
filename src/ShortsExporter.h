#pragma once

#include <QMap>
#include <QObject>
#include <QProcess>
#include <QString>
#include <QStringList>
#include <QTemporaryDir>

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
	QStringList buildArgs(bool useHw);
	void run(bool useHw);
	void onStdErr();
	void onFinished(int code, QProcess::ExitStatus status);

	QProcess m_proc;
	EditProject m_project;
	QString m_output;
	QStringList m_inputs;    // ffmpeg -i 목록
	QMap<int, int> m_inputOf; // 소스 클립 번호 → 입력 번호
	int m_bgmInput = -1;
	QString m_filter;
	bool m_usingHw = true;
	bool m_previewQuality = false;
	bool m_cancelled = false;
	QByteArray m_errBuf;
	std::unique_ptr<QTemporaryDir> m_tmp;
};
