#pragma once

class QProcess;

// 썸네일·길이 추출처럼 급하지 않은 ffmpeg 작업을 "낮은 우선순위"로 실행
// → 게임 중에 클립이 저장돼도 게임 프레임에 영향을 덜 줌
void setLowPriority(QProcess &proc);
