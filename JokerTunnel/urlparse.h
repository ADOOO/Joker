#pragma once
#include "precomp.h";
#include <string>
#include <winsock2.h>

int parseCmd(HTTP_COOKED_URL url);
std::wstring parsePort(HTTP_COOKED_URL url);
std::wstring parseTarget(HTTP_COOKED_URL url);
