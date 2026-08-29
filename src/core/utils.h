#pragma once

#include <string>

std::string lower(std::string s);
std::string fwd(std::string s);
const char* rel(const char* file);
const char* toUtf8(const char* ansi);
bool hasExt(const std::string& k, const char* e);
std::string ctlPath(const char* name);
