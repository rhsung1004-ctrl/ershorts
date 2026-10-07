#pragma once

#include <QJsonObject>
#include <QString>
#include <QStringList>

struct EditProject;

// 스타일 템플릿: 화면 구성(레이아웃·제목 띠), 자막 모양, 비트 효과, 소리 설정을 이름 붙여 저장
//  저장 위치: %LOCALAPPDATA%/ERShorts/styles/<이름>.json
namespace StylePresets {
QString dir();
QStringList names();

// includeText = true 면 제목 띠의 글씨 내용(채널 이름 같은 고정 문구)도 함께 저장
QJsonObject capture(const EditProject &p, bool includeText);
// 템플릿을 프로젝트에 적용 (영상 구간·자막 내용·음악 파일은 그대로)
void apply(const QJsonObject &preset, EditProject *p);

bool save(const QString &name, const QJsonObject &preset, QString *error);
QJsonObject load(const QString &name);
bool remove(const QString &name);
bool exists(const QString &name);
QString sanitizeName(const QString &name);

// 새 영상을 만들 때 자동으로 적용할 템플릿 (빈 문자열 = 없음)
QString defaultName();
void setDefaultName(const QString &name);
} // namespace StylePresets
