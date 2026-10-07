#pragma once

#include <QFont>
#include <QString>
#include <QVector>

// 사용자가 추가한 자막용 글꼴 관리
//  - 추가한 글꼴 파일은 %LOCALAPPDATA%\ERShorts\fonts 에 복사해 두고 프로그램 시작 때마다 등록
//  - 프로젝트에는 글꼴 "파일 경로"를 저장 (빈 문자열 = 기본 글꼴: 맑은 고딕 Bold)
//  - 미리보기(Qt)와 내보내기(ffmpeg drawtext)가 같은 파일을 쓰므로 모양이 같음
namespace FontManager {

struct Entry {
	QString path;   // 글꼴 파일 (fonts 폴더 안의 복사본)
	QString family; // 글꼴 이름
	QString label;  // 목록에 보여줄 이름
};

void init();                                  // 프로그램 시작 시 한 번
QString fontsDir();
QVector<Entry> fonts();                       // 추가된 글꼴 목록
QString addFontFile(const QString &srcPath, QString *error); // 성공 시 복사본 경로
QFont qfont(const QString &path, int pixelSize); // 미리보기용 (빈 경로 = 기본 글꼴)
QString renderFile(const QString &path);      // 내보내기용 글꼴 파일 (없으면 기본 글꼴 파일)
QString displayName(const QString &path);

} // namespace FontManager
