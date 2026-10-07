#pragma once

#include <QHash>
#include <QObject>
#include <QProcess>
#include <QStringList>
#include <QVector>

// 클립 소리 크기(RMS)를 0.05초 간격으로 미리 계산 → 편집기 미리보기에서 BGM 덕킹 흉내낼 때 사용
class AudioEnvelope : public QObject {
	Q_OBJECT
public:
	static constexpr double kStep = 0.05; // 초
	static constexpr int kRate = 8000;

	explicit AudioEnvelope(QObject *parent = nullptr);
	~AudioEnvelope() override;

	void request(const QString &path);
	// 원본 시간 t 의 소리 크기 (0~1, 아직 모르면 -1)
	double levelAt(const QString &path, double t) const;

private:
	void startNext();
	void onFinished(int code, QProcess::ExitStatus status);

	QHash<QString, QVector<float>> m_levels;
	QStringList m_queue;
	QProcess m_proc;
	QString m_current;
	QByteArray m_pcm;
};
