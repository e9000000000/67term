#pragma once

#include <string>

int RunSelfTest(const std::wstring& logPath);
int RunBench(int lines, const std::wstring& logPath);
int RunStartupBench(const std::wstring& logPath);
