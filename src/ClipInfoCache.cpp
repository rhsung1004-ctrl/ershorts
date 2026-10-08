#include "ClipInfoCache.h"

#include "ProcessUtil.h"
#include "ShortsExporter.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QStandardPaths>

ClipInfoCache::ClipInfoCache(QObject *parent) : QObject(parent)
{
	m_proc.setProcessChannelMode(QProcess::MergedChannels);
	setLowPriority(m_proc);
	connect(&m_proc, &QProcess::readyRead, this, [this] { m_output += m_proc.readAll(); });
	connect(&m_proc, &QProcess::finished, this, &ClipInfoCache::onFinished);
}

ClipInfoCache::~ClipInfoCache() { cancelAll(); }

void ClipInfoCache::cancelAll()
{
	m_queue.clear();
	m_current.clear(); // 먼저 비워 둬야 강제 종료된 결과가 캐시에 저장되지 않음
	if (m_proc.state() != QProcess::NotRunning) {
		m_proc.kill();
		m_proc.waitForFinished(1000);
	}
}

QString ClipInfoCache::cacheDir() const
{
	return QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + "/clipinfo";
}

QString ClipInfoCache::keyFor(const QString &path) const
{
	const QFileInfo fi(path);
	const QByteArray key =
		(QString::number(fi.size()) + "|" + QString::number(fi.lastModified().toMSecsSinceEpoch())).toUtf8();
	return QCryptographicHash::hash(key, QCryptographicHash::Md5).toHex().left(16);
}

bool ClipInfoCache::get(const QString &path, Info *out)
{
	const QString key = keyFor(path);
	if (auto it = m_mem.constFind(key); it != m_mem.constEnd()) {
		*out = *it;
		return true;
	}
	const QString base = cacheDir() + "/" + key;
	QFile meta(base + ".txt");
	if (meta.open(QIODevice::ReadOnly)) {
		Info info;
		info.duration = QString::fromUtf8(meta.readAll()).trimmed().toDouble();
		info.thumb = QImage(base + ".jpg"); // 너무 짧은 클립이면 없을 수 있음
		m_mem.insert(key, info);
		*out = info;
		return true;
	}
	if (!m_queue.contains(path) && m_current != path) {
		m_queue << path;
		startNext();
	}
	return false;
}

void ClipInfoCache::startNext()
{
	if (m_proc.state() != QProcess::NotRunning || m_queue.isEmpty())
		return;
	m_current = m_queue.takeFirst();
	m_output.clear();
	QDir().mkpath(cacheDir());
	const QString base = cacheDir() + "/" + keyFor(m_current);
	QFile::remove(base + ".jpg");
	// 1초 지점 장면 1장 + 로그의 Duration 줄에서 길이 읽기
	m_proc.start(ShortsExporter::ffmpegPath(),
		     {"-hide_banner", "-y", "-ss", "1", "-i", m_current, "-an", "-frames:v", "1", "-vf",
		      "scale=192:-2", "-q:v", "5", base + ".jpg"});
}

void ClipInfoCache::onFinished(int, QProcess::ExitStatus)
{
	if (m_current.isEmpty()) {
		startNext();
		return;
	}
	const QString path = m_current;
	m_current.clear();
	m_output += m_proc.readAll();

	Info info;
	const QRegularExpression re("Duration:\\s*(\\d+):(\\d+):(\\d+(?:\\.\\d+)?)");
	const auto m = re.match(QString::fromUtf8(m_output));
	if (m.hasMatch())
		info.duration = m.captured(1).toInt() * 3600 + m.captured(2).toInt() * 60 + m.captured(3).toDouble();

	if (QFileInfo::exists(path)) { // 처리하는 동안 지워졌으면 저장하지 않음
		const QString key = keyFor(path);
		const QString base = cacheDir() + "/" + key;
		info.thumb = QImage(base + ".jpg");
		QFile meta(base + ".txt");
		if (meta.open(QIODevice::WriteOnly | QIODevice::Truncate))
			meta.write(QByteArray::number(info.duration, 'f', 3));
		m_mem.insert(key, info);
		emit ready(path);
	}
	startNext();
}
