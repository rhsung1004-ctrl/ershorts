#include "ProcessUtil.h"

#include <QProcess>

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

void setLowPriority(QProcess &proc)
{
	proc.setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments *args) {
		args->flags |= BELOW_NORMAL_PRIORITY_CLASS | CREATE_NO_WINDOW;
	});
}
