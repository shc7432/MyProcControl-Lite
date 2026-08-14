#include "util.h"
#include "../MyProcControlLite/targetver.h"
using namespace std;

string ws2s(const wstring wstr) {
	string result;
	size_t len = WideCharToMultiByte(CP_ACP, 0, wstr.c_str(),
		(int)(wstr.size()), NULL, 0, NULL, NULL);
	if (len < 0) return result;
	char* buffer = new char[len + 1];
	if (buffer == NULL) return result;
	WideCharToMultiByte(CP_ACP, 0, wstr.c_str(),
		(int)(wstr.size()), buffer, (int)len, NULL, NULL);
	buffer[len] = '\0';
	result.append(buffer);
	delete[] buffer;
	return result;
}
wstring s2ws(const string str) {
	wstring result;
	size_t len = MultiByteToWideChar(CP_ACP, 0, str.c_str(),
		(int)(str.size()), NULL, 0);
	if (len < 0) return result;
	wchar_t* buffer = new wchar_t[len + 1];
	if (buffer == NULL) return result;
	MultiByteToWideChar(CP_ACP, 0, str.c_str(), (int)(str.size()),
		buffer, (int)len);
	buffer[len] = '\0';
	result.append(buffer);
	delete[] buffer;
	return result;
}


