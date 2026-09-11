#pragma once

#include <map>
#include <string>
#include <string_view>

namespace hs {

// Returns false on malformed escapes or an embedded NUL, which would otherwise
// be a way to smuggle a truncated path past the static file checks.
bool percentDecode(std::string_view input, std::string& out, bool plusIsSpace);

// RFC 3986 remove_dot_segments. Applied before any filesystem work so that
// "/a/../../etc" collapses to "/etc" instead of escaping the document root.
std::string normalisePath(std::string_view path);

std::map<std::string, std::string> parseQueryString(std::string_view query);

}  // namespace hs
