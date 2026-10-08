#include "ThumbnailCache.h"

#include "ProcessUtil.h"
#include "ShortsExporter.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>

#include <algorithm>
#include <cmath>

ThumbnailCache::ThumbnailCache(QObject *parent) : QObject(parent)
{
	m_proc.setProcessChannelMode(QProcess::MergedChannels);
	setLowPriority(m_proc);
	connect(&m_proc, &QProcess::readyRead, this, [this] { m_proc.readAll(); }); // 출력은 버림
	connect(&m_proc, &QProcess::finished, this, &ThumbnailCache::onFinished);
}

ThumbnailCache::~ThumbnailCache()
{
	if (m_proc.state() != QProcess::NotRunning) {
		m_proc.kill();
		m_proc.waitForFinished(1000);
	}
}

QString ThumbnailCache::cacheDirFor(const QString &videoPath) const
{
	// 같은 파일이라도 내용이 바뀌면(크기/수정시각) 새로 만들도록 키에 포함
	const QFileInfo fi(videoPath);
	const QByteArray key = (fi.absoluteFilePath() + "|" + QString::number(fi.size()) + "|" +
				QString::number(fi.lastModified().toMSecsSinceEpoch()))
				       .toUtf8();
	const QString hash = QCryptographicHash::hash(key, QCryptographicHash::Md5).toHex().left(16);
	return QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + "/thumbs/" + hash;
}

bool ThumbnailCache::loadFromDir(const QString &videoPath, const QString &dir)
{
	if (!QFile::exists(dir + "/.done"))
		return false;
	QVector<QImage> frames;
	const QStringList files = QDir(dir).entryList({"*.jpg"}, QDir::Files, QDir::Name);
	frames.reserve(files.size());
	for (const QString &f : files)
		frames.push_back(QImage(dir + "/" + f));
	if (frames.isEmpty())
		return false;
	m_frames.insert(videoPath, frames);
	return true;
}

void ThumbnailCache::request(const QString &videoPath)
{
	if (videoPath.isEmpty() || m_frames.contains(videoPath) || m_queue.contains(videoPath) ||
	    m_current == videoPath)
		return;
	if (loadFromDir(videoPath, cacheDirFor(videoPath))) {
		emit updated();
		return;
	}
	m_queue << videoPath;
	startNext();
}

void ThumbnailCache::startNext()
{
	if (m_proc.state() != QProcess::NotRunning || m_queue.isEmpty())
		return;
	m_current = m_queue.takeFirst();
	m_currentDir = cacheDirFor(m_current);
	QDir(m_currentDir).removeRecursively();
	QDir().mkpath(m_currentDir);

	m_proc.start(ShortsExporter::ffmpegPath(),
		     {"-hide_banner", "-loglevel", "error", "-y", "-hwaccel", "auto", "-i", m_current, "-an",
		      "-vf", QString("fps=%1,scale=-2:%2").arg(1.0 / kInterval).arg(kHeight), "-q:v", "6",
		      m_currentDir + "/%05d.jpg"});
}

void ThumbnailCache::onFinished(int code, QProcess::ExitStatus status)
{
	if (status == QProcess::NormalExit && code == 0) {
		QFile done(m_currentDir + "/.done");
		if (done.open(QIODevice::WriteOnly))
			done.close();
		if (loadFromDir(m_current, m_currentDir))
			emit updated();
	}
	m_current.clear();
	startNext();
}

QImage ThumbnailCache::frameAt(const QString &videoPath, double t) const
{
	const auto it = m_frames.constFind(videoPath);
	if (it == m_frames.constEnd() || it->isEmpty())
		return {};
	// fps 필터는 0.5초 칸마다 그 칸 가운데쯤(0.25초, 0.75초 …)의 장면을 저장함 → 칸 번호 = floor(t / 0.5)
	const int idx = std::clamp(int(std::floor(t / kInterval)), 0, int(it->size()) - 1);
	return it->at(idx);
}
