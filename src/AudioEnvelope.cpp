#include "AudioEnvelope.h"

#include "ProcessUtil.h"
#include "ShortsExporter.h"

#include <algorithm>
#include <cmath>
#include <cstring>

AudioEnvelope::AudioEnvelope(QObject *parent) : QObject(parent)
{
	setLowPriority(m_proc);
	connect(&m_proc, &QProcess::readyReadStandardOutput, this, [this] { m_pcm += m_proc.readAllStandardOutput(); });
	connect(&m_proc, &QProcess::readyReadStandardError, this, [this] { m_proc.readAllStandardError(); });
	connect(&m_proc, &QProcess::finished, this, &AudioEnvelope::onFinished);
}

AudioEnvelope::~AudioEnvelope()
{
	m_queue.clear();
	m_current.clear();
	if (m_proc.state() != QProcess::NotRunning) {
		m_proc.kill();
		m_proc.waitForFinished(1000);
	}
}

void AudioEnvelope::request(const QString &path)
{
	if (path.isEmpty() || m_levels.contains(path) || m_queue.contains(path) || m_current == path)
		return;
	m_queue << path;
	startNext();
}

void AudioEnvelope::startNext()
{
	if (m_proc.state() != QProcess::NotRunning || m_queue.isEmpty())
		return;
	m_current = m_queue.takeFirst();
	m_pcm.clear();
	m_proc.start(ShortsExporter::ffmpegPath(), {"-hide_banner", "-loglevel", "error", "-i", m_current, "-vn",
						    "-ac", "1", "-ar", QString::number(kRate), "-f", "f32le", "-"});
}

void AudioEnvelope::onFinished(int, QProcess::ExitStatus)
{
	if (m_current.isEmpty()) {
		startNext();
		return;
	}
	m_pcm += m_proc.readAllStandardOutput();
	const int n = int(m_pcm.size() / int(sizeof(float)));
	const int win = int(kRate * kStep);
	QVector<float> levels;
	levels.reserve(n / win + 1);
	const char *raw = m_pcm.constData();
	for (int start = 0; start + win <= n; start += win) {
		double e = 0.0;
		for (int k = 0; k < win; ++k) {
			float v;
			std::memcpy(&v, raw + size_t(start + k) * sizeof(float), sizeof(float));
			e += double(v) * v;
		}
		levels.push_back(float(std::sqrt(e / win)));
	}
	m_levels.insert(m_current, levels); // 소리 없는 클립이면 빈 목록 = 조용함
	m_current.clear();
	m_pcm.clear();
	startNext();
}

double AudioEnvelope::levelAt(const QString &path, double t) const
{
	const auto it = m_levels.constFind(path);
	if (it == m_levels.constEnd())
		return -1.0;
	if (it->isEmpty())
		return 0.0;
	const int i = std::clamp(int(t / kStep), 0, int(it->size()) - 1);
	return it->at(i);
}
