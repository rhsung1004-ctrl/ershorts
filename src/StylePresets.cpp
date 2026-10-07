#include "StylePresets.h"

#include "EditProject.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QSettings>
#include <QStandardPaths>

#include <algorithm>

namespace StylePresets {

QString dir()
{
	return QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + "/ERShorts/styles";
}

static QString fileFor(const QString &name) { return dir() + "/" + sanitizeName(name) + ".json"; }

QString sanitizeName(const QString &name)
{
	QString n = name.trimmed();
	static const QString bad = "\\/:*?\"<>|";
	for (QChar c : bad)
		n.remove(c);
	return n.left(60);
}

QStringList names()
{
	QStringList out;
	const QFileInfoList files = QDir(dir()).entryInfoList({"*.json"}, QDir::Files, QDir::Name);
	for (const QFileInfo &fi : files)
		out << fi.completeBaseName();
	return out;
}

QJsonObject capture(const EditProject &p, bool includeText)
{
	return QJsonObject{
		{"version", 1},
		{"layout", int(p.layout)},
		{"bands", p.bands.toJson(includeText)},
		{"subStyle", p.subStyle.styleToJson()},
		{"beatFx", QJsonObject{{"zoom", p.beatFx.zoom},
				       {"shake", p.beatFx.shake},
				       {"strength", p.beatFx.strength},
				       {"every", p.beatFx.every}}},
		{"audio", QJsonObject{{"musicVolume", p.music.volume},
				      {"gameVolume", p.gameVolume},
				      {"fadeOut", p.music.fadeOut},
				      {"duck", p.music.duck},
				      {"duckStrength", p.music.duckStrength}}},
	};
}

void apply(const QJsonObject &o, EditProject *p)
{
	if (o.contains("layout"))
		p->layout = ShortsLayout(std::clamp(o.value("layout").toInt(), 0, 3));

	if (o.contains("bands")) {
		const QJsonObject b = o.value("bands").toObject();
		// 글씨 내용이 저장된 템플릿만 글씨를 바꾸고, 아니면 지금 쓴 제목을 그대로 둠
		p->bands.fromJson(b, b.contains("title"));
	}

	if (o.contains("subStyle")) {
		p->subStyle.styleFromJson(o.value("subStyle").toObject());
		for (Subtitle &s : p->subtitles) // 이미 넣은 자막도 같은 모양으로
			s.copyStyleFrom(p->subStyle);
	}

	if (o.contains("beatFx")) {
		const QJsonObject fx = o.value("beatFx").toObject();
		p->beatFx.zoom = fx.value("zoom").toBool();
		p->beatFx.shake = fx.value("shake").toBool();
		p->beatFx.strength = std::clamp(fx.value("strength").toInt(1), 0, 2);
		p->beatFx.every = std::clamp(fx.value("every").toInt(1), 1, 8);
	}

	if (o.contains("audio")) {
		const QJsonObject a = o.value("audio").toObject();
		p->music.volume = std::clamp(a.value("musicVolume").toDouble(0.9), 0.0, 2.0);
		p->gameVolume = std::clamp(a.value("gameVolume").toDouble(1.0), 0.0, 2.0);
		p->music.fadeOut = a.value("fadeOut").toBool(true);
		p->music.duck = a.value("duck").toBool(false);
		p->music.duckStrength = std::clamp(a.value("duckStrength").toInt(1), 0, 2);
	}
}

bool save(const QString &name, const QJsonObject &preset, QString *error)
{
	if (sanitizeName(name).isEmpty()) {
		*error = "이름을 입력하세요";
		return false;
	}
	QDir().mkpath(dir());
	QFile f(fileFor(name));
	if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
		*error = "파일을 저장하지 못했습니다";
		return false;
	}
	f.write(QJsonDocument(preset).toJson(QJsonDocument::Indented));
	return true;
}

QJsonObject load(const QString &name)
{
	QFile f(fileFor(name));
	if (!f.open(QIODevice::ReadOnly))
		return {};
	return QJsonDocument::fromJson(f.readAll()).object();
}

bool exists(const QString &name) { return QFileInfo::exists(fileFor(name)); }

bool remove(const QString &name)
{
	if (defaultName() == name)
		setDefaultName({});
	return QFile::remove(fileFor(name));
}

QString defaultName()
{
	const QString n = QSettings("ERShorts", "ERShorts").value("defaultStyle").toString();
	return (!n.isEmpty() && exists(n)) ? n : QString();
}

void setDefaultName(const QString &name) { QSettings("ERShorts", "ERShorts").setValue("defaultStyle", name); }

} // namespace StylePresets
