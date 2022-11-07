#include "precomp.h";
#include <string>
#include <winsock2.h>
#include <cstring>

using namespace std;

int parseCmd(HTTP_COOKED_URL url) {
	if (wcslen(url.pQueryString) <= 1 || url.pQueryString[0] != L'?') {
		return -1;
	}
	int offset = 1;
	wstring queryString = wstring(url.pQueryString + offset);

	int idx = wstring(url.pQueryString + offset).find_first_of(L"&");
	wstring subString;
	int equalIdx;
	while (1) {
		subString = wstring(url.pQueryString + offset);
		equalIdx = subString.find_first_of(L"=");
		if (equalIdx != -1) {
			wstring key = subString.substr(0, equalIdx);
			wstring value = subString.substr(equalIdx + 1, idx - equalIdx - 1);
			// CONNECT DISCONNECT FORWARD READ ; 1/2/3/4
			if (key == L"cmd") {
				if (value == L"connect") {
					return 1;
				}
				else if (value == L"disconnect") {
					return 2;
				}
				else if (value == L"forward") {
					return 3;
				}
				else if (value == L"read") {
					return 4;
				}
				else {
					return -1;
				}
			}
		}
		offset += idx + 1;

		idx = wstring(url.pQueryString + offset).find_first_of(L"&");
		if (idx == -1) {
			if (wstring(url.pQueryString + offset).find_first_of(L"=") != -1) {
				idx = wcslen(url.pQueryString + offset);
			}
			else {
				return -1;
			}
		}
	}
	return -1;
}

wstring parseTarget(HTTP_COOKED_URL url) {
	if (wcslen(url.pQueryString) <= 1 || url.pQueryString[0] != L'?') {
		return L"";
	}
	int offset = 1;
	wstring queryString = wstring(url.pQueryString + offset);

	int idx = wstring(url.pQueryString + offset).find_first_of(L"&");
	while (1) {
		wstring subString = wstring(url.pQueryString + offset);
		int equalIdx = subString.find_first_of(L"=");
		if (equalIdx != -1) {
			wstring key = subString.substr(0, equalIdx);
			wstring value = subString.substr(equalIdx + 1, idx - equalIdx - 1);
			// CONNECT DISCONNECT FORWARD READ ; 1/2/3/4
			if (key == L"target") {
				return value;
			}
		}
		offset += idx + 1;
		idx = wstring(url.pQueryString + offset).find_first_of(L"&");
		if (idx == -1) {
			if (wstring(url.pQueryString + offset).find_first_of(L"=") != -1) {
				idx = wcslen(url.pQueryString + offset);
			}
			else {
				return L"";
			}
		}
	}
	return L"";
}

wstring parsePort(HTTP_COOKED_URL url) {
	if (wcslen(url.pQueryString) <= 1 || url.pQueryString[0] != L'?') {
		return L"";
	}
	int offset = 1;
	wstring queryString = wstring(url.pQueryString + offset);
	int idx = wstring(url.pQueryString + offset).find_first_of(L"&");
	while (1) {
		wstring subString = wstring(url.pQueryString + offset);
		int equalIdx = subString.find_first_of(L"=");
		if (equalIdx != -1) {
			wstring key = subString.substr(0, equalIdx);
			wstring value = subString.substr(equalIdx + 1, idx - equalIdx - 1);
			// CONNECT DISCONNECT FORWARD READ ; 1/2/3/4
			if (key == L"port") {
				return value;
			}
		}
		offset += idx + 1;
		idx = wstring(url.pQueryString + offset).find_first_of(L"&");
		if (idx == -1) {
			if (wstring(url.pQueryString + offset).find_first_of(L"=") != -1) {
				idx = wcslen(url.pQueryString + offset);
			}
			else {
				return L"";
			}
		}
	}
	return L"";
}


/*test main.cpp*/
//#include <iostream>
////#include "urlparse.cpp"
//void main() {
//	HTTP_COOKED_URL url;
//	url.pQueryString = L"?cmd=connect&target=192.168.6.1&port=1234";
//	cout << parseCmd(url) << endl;
//	wprintf(L"target: %s; port: %s", parseTarget(url), parsePort(url));
//}