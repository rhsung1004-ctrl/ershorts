// 이 파일만 C++/WinRT 사용 (Qt 헤더와 섞지 않음)
#include "GameClockOcr.h"

#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Globalization.h>
#include <winrt/Windows.Graphics.Imaging.h>
#include <winrt/Windows.Media.Ocr.h>
#include <winrt/Windows.Storage.Streams.h>

#include <cstring>

using namespace winrt;
using namespace winrt::Windows::Graphics::Imaging;
using namespace winrt::Windows::Media::Ocr;

std::wstring ocrReadBgra(const uint8_t *bgra, int w, int h)
{
	try {
		thread_local bool apartment = false;
		if (!apartment) {
			try {
				init_apartment(apartment_type::multi_threaded);
			} catch (...) {
				// 이미 초기화된 스레드면 그대로 사용
			}
			apartment = true;
		}

		thread_local OcrEngine engine{nullptr};
		if (!engine) {
			engine = OcrEngine::TryCreateFromLanguage(winrt::Windows::Globalization::Language(L"ko"));
			if (!engine)
				engine = OcrEngine::TryCreateFromUserProfileLanguages();
			if (!engine)
				return {};
		}

		const uint32_t size = uint32_t(w) * uint32_t(h) * 4u;
		winrt::Windows::Storage::Streams::Buffer buffer(size);
		std::memcpy(buffer.data(), bgra, size);
		buffer.Length(size);
		SoftwareBitmap bmp = SoftwareBitmap::CreateCopyFromBuffer(buffer, BitmapPixelFormat::Bgra8, w, h,
									  BitmapAlphaMode::Premultiplied);
		OcrResult result = engine.RecognizeAsync(bmp).get();
		return std::wstring(result.Text());
	} catch (...) {
		return {};
	}
}
