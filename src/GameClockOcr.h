#pragma once

#include <cstdint>
#include <string>

// Windows 기본 글자 인식(Windows.Media.Ocr, 한국어)으로 그림 속 글씨 읽기
//  - bgra: w x h BGRA 픽셀. 실패하면 빈 문자열
//  - 오래 걸릴 수 있으니(수십~수백 ms) UI 스레드가 아닌 곳에서 호출
std::wstring ocrReadBgra(const uint8_t *bgra, int w, int h);
