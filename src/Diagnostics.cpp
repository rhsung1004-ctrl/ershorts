#include "Diagnostics.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QStandardPaths>
#include <QSysInfo>

#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <string>
#include <exception>
#include <mutex>

#include <util/base.h>

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <DbgHelp.h>

namespace {
std::mutex g_mutex;
FILE *g_log = nullptr;
QString g_logDir;
QString g_logPath;
std::wstring g_dumpPathW;
std::wstring g_logPathW;
bool g_previousCrashed = false;
bool g_noDialogs = false; // 자동 테스트(빌드 서버)에서는 안내 창을 띄우지 않음

QString markerPath() { return g_logDir + "/.running"; }

void writeStackTrace(EXCEPTION_POINTERS *ep);
std::string currentThreadName();

void writeRaw(const char *text)
{
	std::lock_guard<std::mutex> lock(g_mutex);
	if (!g_log)
		return;
	SYSTEMTIME st;
	GetLocalTime(&st);
	std::fprintf(g_log, "%02d:%02d:%02d.%03d %s\n", st.wHour, st.wMinute, st.wSecond, st.wMilliseconds, text);
	std::fflush(g_log); // 갑자기 죽어도 마지막 줄까지 남도록 매번 기록
}

// ── Qt 메시지 ──
void qtMessageHandler(QtMsgType type, const QMessageLogContext &, const QString &msg)
{
	const char *lvl = type == QtDebugMsg ? "debug" : type == QtInfoMsg ? "info"
		: type == QtWarningMsg ? "warning" : type == QtCriticalMsg ? "critical" : "fatal";
	writeRaw(QString("[Qt %1] %2").arg(lvl, msg).toUtf8().constData());
}

// ── libobs 로그 ──
void obsLogHandler(int lvl, const char *fmt, va_list args, void *)
{
	char buf[8192];
	std::vsnprintf(buf, sizeof(buf), fmt, args);
	const char *tag = lvl <= LOG_ERROR ? "obs error" : lvl <= LOG_WARNING ? "obs warning" : "obs";
	char line[8300];
	std::snprintf(line, sizeof(line), "[%s] %s", tag, buf);
	writeRaw(line);
}

// libobs가 복구할 수 없는 오류를 만났을 때 (이후 libobs가 프로그램을 끝냄)
void obsCrashHandler(const char *fmt, va_list args, void *)
{
	char buf[4096];
	std::vsnprintf(buf, sizeof(buf), fmt, args);
	char line[4200];
	std::snprintf(line, sizeof(line), "[libobs 치명적 오류] %s", buf);
	writeRaw(line);

	wchar_t wmsg[4096];
	MultiByteToWideChar(CP_UTF8, 0, buf, -1, wmsg, 4096);
	std::wstring text = L"녹화 엔진(libobs)에서 치명적인 오류가 발생해 종료합니다.\n\n";
	text += wmsg;
	text += L"\n\n로그 파일:\n" + g_logPathW;
	if (!g_noDialogs)
		MessageBoxW(nullptr, text.c_str(), L"ERShorts 오류", MB_ICONERROR | MB_TOPMOST);
	ExitProcess(1);
}

// ── 처리되지 않은 예외 (메모리 접근 오류 등) ──
LONG WINAPI unhandledException(EXCEPTION_POINTERS *ep)
{
	const DWORD code = ep->ExceptionRecord->ExceptionCode;
	void *addr = ep->ExceptionRecord->ExceptionAddress;

	// 어느 DLL/EXE 안에서 죽었는지
	wchar_t moduleW[MAX_PATH] = L"알 수 없음";
	uintptr_t offset = 0;
	HMODULE mod = nullptr;
	if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
			       static_cast<LPCWSTR>(addr), &mod)) {
		GetModuleFileNameW(mod, moduleW, MAX_PATH);
		offset = reinterpret_cast<uintptr_t>(addr) - reinterpret_cast<uintptr_t>(mod);
	}
	char module[MAX_PATH * 3];
	WideCharToMultiByte(CP_UTF8, 0, moduleW, -1, module, sizeof(module), nullptr, nullptr);
	char line[1024];
	std::snprintf(line, sizeof(line), "[충돌] 예외 코드 0x%08lX, 위치 %s + 0x%llX (스레드 %lu '%s')",
		      static_cast<unsigned long>(code), module, static_cast<unsigned long long>(offset),
		      GetCurrentThreadId(), currentThreadName().c_str());
	writeRaw(line);
	writeStackTrace(ep);

	// 미니덤프 저장 (개발자가 정확한 원인을 볼 수 있음)
	HANDLE f = CreateFileW(g_dumpPathW.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL,
			       nullptr);
	if (f != INVALID_HANDLE_VALUE) {
		MINIDUMP_EXCEPTION_INFORMATION mei = {};
		mei.ThreadId = GetCurrentThreadId();
		mei.ExceptionPointers = ep;
		mei.ClientPointers = FALSE;
		MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), f,
				  MINIDUMP_TYPE(MiniDumpWithIndirectlyReferencedMemory | MiniDumpScanMemory), &mei,
				  nullptr, nullptr);
		CloseHandle(f);
		writeRaw("[충돌] 덤프 파일 저장됨");
	}

	wchar_t wline[1024];
	MultiByteToWideChar(CP_UTF8, 0, line, -1, wline, 1024);
	std::wstring text = L"ERShorts가 예기치 않게 종료되었습니다.\n\n";
	text += wline;
	text += L"\n\n아래 폴더의 최신 로그(.txt)와 덤프(.dmp)를 보내주시면 원인을 찾을 수 있어요.\n";
	text += g_logPathW;
	if (!g_noDialogs)
		MessageBoxW(nullptr, text.c_str(), L"ERShorts 충돌", MB_ICONERROR | MB_TOPMOST);
	return EXCEPTION_EXECUTE_HANDLER;
}

// 충돌한 스레드의 호출 스택 (모듈!함수+오프셋). PDB가 없어도 DLL이 내보낸 함수 이름은 나옴
void writeStackTrace(EXCEPTION_POINTERS *ep)
{
	HANDLE proc = GetCurrentProcess();
	SymSetOptions(SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS | SYMOPT_FAIL_CRITICAL_ERRORS);
	SymInitialize(proc, nullptr, TRUE);

	CONTEXT ctx = *ep->ContextRecord;
	STACKFRAME64 sf = {};
	sf.AddrPC.Offset = ctx.Rip;
	sf.AddrPC.Mode = AddrModeFlat;
	sf.AddrFrame.Offset = ctx.Rbp;
	sf.AddrFrame.Mode = AddrModeFlat;
	sf.AddrStack.Offset = ctx.Rsp;
	sf.AddrStack.Mode = AddrModeFlat;

	alignas(SYMBOL_INFO) char symBuf[sizeof(SYMBOL_INFO) + 256];
	for (int i = 0; i < 40; ++i) {
		if (!StackWalk64(IMAGE_FILE_MACHINE_AMD64, proc, GetCurrentThread(), &sf, &ctx, nullptr,
				 SymFunctionTableAccess64, SymGetModuleBase64, nullptr))
			break;
		const DWORD64 pc = sf.AddrPC.Offset;
		if (pc == 0)
			break;

		wchar_t modW[MAX_PATH] = L"?";
		HMODULE mod = nullptr;
		DWORD64 modOff = 0;
		if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
				       reinterpret_cast<LPCWSTR>(pc), &mod)) {
			GetModuleFileNameW(mod, modW, MAX_PATH);
			modOff = pc - reinterpret_cast<DWORD64>(mod);
		}
		const wchar_t *base = wcsrchr(modW, L'\\');
		char modName[MAX_PATH * 3];
		WideCharToMultiByte(CP_UTF8, 0, base ? base + 1 : modW, -1, modName, sizeof(modName), nullptr, nullptr);

		auto *sym = reinterpret_cast<SYMBOL_INFO *>(symBuf);
		std::memset(symBuf, 0, sizeof(symBuf));
		sym->SizeOfStruct = sizeof(SYMBOL_INFO);
		sym->MaxNameLen = 255;
		DWORD64 symOff = 0;
		char line[700];
		if (SymFromAddr(proc, pc, &symOff, sym))
			std::snprintf(line, sizeof(line), "  #%02d %s!%s+0x%llX", i, modName, sym->Name,
				      static_cast<unsigned long long>(symOff));
		else
			std::snprintf(line, sizeof(line), "  #%02d %s+0x%llX", i, modName,
				      static_cast<unsigned long long>(modOff));
		writeRaw(line);
	}
}

// 충돌한 스레드 이름 (libobs는 "libobs: graphics thread" 같은 이름을 붙여 둠)
std::string currentThreadName()
{
	using GetDescFn = HRESULT(WINAPI *)(HANDLE, PWSTR *);
	auto fn = reinterpret_cast<GetDescFn>(
		reinterpret_cast<void *>(GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "GetThreadDescription")));
	if (!fn)
		return {};
	PWSTR desc = nullptr;
	std::string out;
	if (SUCCEEDED(fn(GetCurrentThread(), &desc)) && desc) {
		char buf[512];
		WideCharToMultiByte(CP_UTF8, 0, desc, -1, buf, sizeof(buf), nullptr, nullptr);
		out = buf;
		LocalFree(desc);
	}
	return out;
}

void terminateHandler()
{
	writeRaw("[충돌] std::terminate 호출 (처리되지 않은 C++ 예외)");
	std::abort();
}

void removeOldLogs()
{
	QDir d(g_logDir);
	const QFileInfoList files =
		d.entryInfoList({"ershorts_*.txt", "ershorts_*.dmp"}, QDir::Files, QDir::Time); // 최신순
	for (int i = 20; i < files.size(); ++i)
		QFile::remove(files[i].absoluteFilePath());
}
} // namespace

namespace Diagnostics {

void init()
{
	g_noDialogs = qEnvironmentVariableIsSet("ERSHORTS_NO_DIALOGS");
	g_logDir = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + "/ERShorts/logs";
	QDir().mkpath(g_logDir);
	removeOldLogs();

	g_previousCrashed = QFile::exists(markerPath());
	QFile marker(markerPath());
	if (marker.open(QIODevice::WriteOnly))
		marker.close();

	const QString stamp = QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss");
	g_logPath = g_logDir + "/ershorts_" + stamp + ".txt";
	g_logPathW = QDir::toNativeSeparators(g_logPath).toStdWString();
	g_dumpPathW = QDir::toNativeSeparators(g_logDir + "/ershorts_" + stamp + ".dmp").toStdWString();
	g_log = _wfopen(g_logPathW.c_str(), L"w");

	SetUnhandledExceptionFilter(unhandledException);
	std::set_terminate(terminateHandler);
	qInstallMessageHandler(qtMessageHandler);
	base_set_log_handler(obsLogHandler, nullptr);
	base_set_crash_handler(obsCrashHandler, nullptr);

	write(QString("ERShorts 시작 — %1 (%2), Qt %3")
		      .arg(QSysInfo::prettyProductName(), QSysInfo::currentCpuArchitecture(), qVersion()));
	if (g_previousCrashed)
		write("지난 실행이 정상적으로 종료되지 않았습니다");
}

bool previousRunCrashed() { return g_previousCrashed; }

void markCleanExit()
{
	write("정상 종료");
	QFile::remove(markerPath());
}

void write(const QString &line) { writeRaw(line.toUtf8().constData()); }

QString logDir() { return g_logDir; }
QString currentLogPath() { return g_logPath; }

} // namespace Diagnostics
