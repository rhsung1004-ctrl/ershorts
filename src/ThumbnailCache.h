#pragma once

#include <QHash>
#include <QImage>
#include <QObject>
#include <QProcess>
#include <QStringList>
#include <QVector>

// 클립마다 0.5초 간격의 작은 장면 이미지를 ffmpeg로 뽑아 두고 타임라인에 그릴 때 꺼내 씀
//  - 한 번에 하나씩 백그라운드로 생성
//  - 캐시 폴더(%LOCALAPPDATA%/ERShorts/cache/thumbs/…)에 저장해서 다음에 열 때는 바로 읽음
class ThumbnailCache : public QObject {
	Q_OBJECT
public:
	static constexpr double kInterval = 0.5; // 초
	static constexpr int kHeight = 90;       // 픽셀

	explicit ThumbnailCache(QObject *parent = nullptr);
	~ThumbnailCache() override;

	void request(const QString &videoPath);
	// 원본 시간 t 에 가장 가까운 장면 (아직 없으면 null 이미지)
	QImage frameAt(const QString &videoPath, double t) const;

signals:
	void updated();

private:
	QString cacheDirFor(const QString &videoPath) const;
	bool loadFromDir(const QString &videoPath, const QString &dir);
	void startNext();
	void onFinished(int code, QProcess::ExitStatus status);

	QHash<QString, QVector<QImage>> m_frames;
	QStringList m_queue;
	QProcess m_proc;
	QString m_current;
	QString m_currentDir;
};
