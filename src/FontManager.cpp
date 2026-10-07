#include "FontManager.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFontDatabase>
#include <QHash>
#include <QRawFont>
#include <QStandardPaths>

#include <algorithm>

namespace {
const QString kDefaultFile = QStringLiteral("C:/Windows/Fonts/malgunbd.ttf");
const QString kDefaultFamily = QStringLiteral("Malgun Gothic");

struct Registered {
	QString family;
	QFont::Weight weight = QFont::Normal;
	QFont::Style style = QFont::StyleNormal;
};
QHash<QString, Registered> g_registered; // 경로 → 등록 정보
QVector<FontManager::Entry> g_fonts;

bool registerFile(const QString &path)
{
	if (g_registered.contains(path))
		return true;
	const int id = QFontDatabase::addApplicationFont(path);
	if (id < 0)
		return false;
	const QStringList families = QFontDatabase::applicationFontFamilies(id);
	if (families.isEmpty())
		return false;

	Registered r;
	r.family = families.first();
	// 같은 이름의 다른 굵기와 섞이지 않도록 파일 자체의 굵기/기울임을 기억
	const QRawFont raw(path, 32);
	if (raw.isValid()) {
		r.weight = raw.weight();
		r.style = raw.style();
	}
	g_registered.insert(path, r);

	FontManager::Entry e;
	e.path = path;
	e.family = r.family;
	e.label = r.family + "  (" + QFileInfo(path).fileName() + ")";
	g_fonts.push_back(e);
	return true;
}
} // namespace

namespace FontManager {

QString fontsDir()
{
	return QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + "/ERShorts/fonts";
}

void init()
{
	QDir().mkpath(fontsDir());
	const QFileInfoList files =
		QDir(fontsDir()).entryInfoList({"*.ttf", "*.otf", "*.ttc", "*.TTF", "*.OTF", "*.TTC"}, QDir::Files,
					       QDir::Name);
	for (const QFileInfo &fi : files)
		registerFile(fi.absoluteFilePath());
}

QVector<Entry> fonts() { return g_fonts; }

QString addFontFile(const QString &srcPath, QString *error)
{
	const QFileInfo src(srcPath);
	if (!src.exists()) {
		*error = "파일을 찾을 수 없습니다";
		return {};
	}
	QString dest = fontsDir() + "/" + src.fileName();
	if (QFileInfo(dest).absoluteFilePath() == src.absoluteFilePath() || g_registered.contains(dest))
		return dest; // 이미 추가된 글꼴
	for (int n = 2; QFile::exists(dest); ++n)
		dest = fontsDir() + "/" + src.completeBaseName() + QString("_%1.").arg(n) + src.suffix();
	if (!QFile::copy(srcPath, dest)) {
		*error = "글꼴 파일을 복사하지 못했습니다";
		return {};
	}
	if (!registerFile(dest)) {
		QFile::remove(dest);
		*error = "글꼴 파일을 읽을 수 없습니다 (TTF/OTF 글꼴인지 확인하세요)";
		return {};
	}
	return dest;
}

QFont qfont(const QString &path, int pixelSize)
{
	QFont f;
	const auto it = g_registered.constFind(path);
	if (path.isEmpty() || it == g_registered.constEnd()) {
		f.setFamily(kDefaultFamily);
		f.setBold(true); // 기본 글꼴 = 맑은 고딕 Bold (내보내기와 같게)
	} else {
		f.setFamily(it->family);
		f.setWeight(it->weight);
		f.setStyle(it->style);
	}
	f.setPixelSize(std::max(1, pixelSize));
	return f;
}

QString renderFile(const QString &path)
{
	if (!path.isEmpty() && QFile::exists(path))
		return path;
	return kDefaultFile;
}

QString displayName(const QString &path)
{
	if (path.isEmpty())
		return "맑은 고딕 Bold (기본)";
	const auto it = g_registered.constFind(path);
	return it != g_registered.constEnd() ? it->family : QFileInfo(path).completeBaseName();
}

} // namespace FontManager
