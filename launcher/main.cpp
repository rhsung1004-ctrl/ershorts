// ERShorts 실행기
// libobs는 실행 파일이 bin\64bit 안에 있어야 data\, obs-plugins\ 를 찾을 수 있어서,
// 압축을 푼 폴더 맨 위에 이 작은 실행 파일을 두고 진짜 프로그램을 대신 실행합니다.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <string>

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int)
{
	wchar_t self[32768];
	const DWORD n = GetModuleFileNameW(nullptr, self, static_cast<DWORD>(std::size(self)));
	if (n == 0 || n >= std::size(self))
		return 1;

	std::wstring dir(self, n);
	dir = dir.substr(0, dir.find_last_of(L"\\/"));
	const std::wstring binDir = dir + L"\\bin\\64bit";
	const std::wstring exe = binDir + L"\\ERShorts.exe";
	std::wstring cmd = L"\"" + exe + L"\"";

	STARTUPINFOW si = {};
	si.cb = sizeof(si);
	PROCESS_INFORMATION pi = {};
	if (!CreateProcessW(exe.c_str(), cmd.data(), nullptr, nullptr, FALSE, 0, nullptr, binDir.c_str(), &si,
			    &pi)) {
		MessageBoxW(nullptr,
			    L"bin\\64bit\\ERShorts.exe 를 찾을 수 없습니다.\n"
			    L"압축을 풀 때 폴더 구조를 그대로 유지했는지 확인하세요.",
			    L"ERShorts", MB_ICONERROR);
		return 1;
	}
	CloseHandle(pi.hThread);
	CloseHandle(pi.hProcess);
	return 0;
}
