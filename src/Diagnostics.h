#pragma once

#include <QString>

// 문제 진단용 기록
//  - Qt / libobs 로그를 %LOCALAPPDATA%\ERShorts\logs\ershorts_날짜_시간.txt 에 저장
//  - 프로그램이 예외로 죽으면 원인 모듈/주소와 덤프(.dmp)를 남기고 안내 창 표시
//  - 지난 실행이 비정상 종료였는지 알려줌 (안전 모드 제안용)
namespace Diagnostics {

void init();                 // QApplication 생성 직후 가장 먼저 호출
bool previousRunCrashed();   // init() 이전 실행이 정상 종료되지 않았으면 true
void markCleanExit();        // 정상 종료 직전에 호출
void write(const QString &line);
QString logDir();
QString currentLogPath();

} // namespace Diagnostics
