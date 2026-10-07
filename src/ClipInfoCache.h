#pragma once

#include <QHash>
#include <QImage>
#include <QObject>
#include <QProcess>
#include <QStringList>

// 저장된 클립 목록용: 클립마다 대표 장면 1장과 길이(초)를 ffmpeg로 한 번 뽑아 캐시
//  - 캐시 키는 파일 크기+수정 시각 → 이름을 바꿔도 다시 만들 필요 없음
class ClipInfoCache : public QObject {
	Q_OBJECT
public:
	struct Info {
		QImage thumb;
		double duration = 0.0; // 0 = 모름
	};

	explicit ClipInfoCache(QObject *parent = nullptr);
	~ClipInfoCache() override;

	// 캐시에 있으면 true 와 함께 바로 채워 주고, 없으면 백그라운드로 만들고 ready 신호
	bool get(const QString &path, Info *out);
	void cancelAll();

signals:
	void ready(const QString &path);

private:
	QString keyFor(const QString &path) const;
	QString cacheDir() const;
	void startNext();
	void onFinished(int code, QProcess::ExitStatus status);

	QHash<QString, Info> m_mem; // 키 → 정보
	QStringList m_queue;
	QProcess m_proc;
	QString m_current;
	QByteArray m_output;
};
