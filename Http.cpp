#include "Http.h"

#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

#include <curl/curl.h>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <winreg.h>
#else
#include <unistd.h>
#endif

namespace sek {

namespace {

// Appends received bytes to the output string. Signature is required by curl.
size_t writeCallback(char* data, size_t size, size_t count, void* userdata) {
    std::string* output = static_cast<std::string*>(userdata);
    output->append(data, size * count);
    return size * count;
}

// Directory that contains the running executable ("" if it cannot be resolved).
std::string executableDirectory() {
#ifdef _WIN32
    char path[MAX_PATH];
    const DWORD size = GetModuleFileNameA(nullptr, path, MAX_PATH);
    if (size == 0 || size >= MAX_PATH) {
        return "";
    }

    const std::string full(path, size);
    const std::string::size_type slash = full.find_last_of("\\/");
    return slash == std::string::npos ? "" : full.substr(0, slash);
#else
    char path[4096];
    const ssize_t count = readlink("/proc/self/exe", path, sizeof(path) - 1);
    if (count <= 0) {
        return "";
    }

    path[count] = '\0';
    const std::string full(path);
    const std::string::size_type slash = full.find_last_of("\\/");
    return slash == std::string::npos ? "" : full.substr(0, slash);
#endif
}

// Finds a CA certificate bundle to verify HTTPS connections. Returns an empty
// string when no bundle is available, letting curl use its own defaults.
std::string findCaBundle() {
    const std::string exeDir = executableDirectory();
    if (!exeDir.empty()) {
        const std::string candidate = exeDir + "/curl-ca-bundle.crt";
        std::ifstream check(candidate.c_str());
        if (check) {
            return candidate;
        }
    }

    std::ifstream check("curl-ca-bundle.crt");
    if (check) {
        return "curl-ca-bundle.crt";
    }

    return "";
}

// One-time curl global initialization (curl_global_init is not thread safe,
// so it must run before any other curl call; calling it again is harmless).
void ensureCurlInitialized() {
    static const bool initialized = [] {
        curl_global_init(CURL_GLOBAL_DEFAULT);
        return true;
    }();
    (void)initialized;
}

#ifdef _WIN32
// Reads a REG_SZ value from the current user's registry.
bool readRegStringW(HKEY root, const wchar_t* subkey, const wchar_t* name,
                    std::wstring& out) {
    DWORD size = 0;
    if (RegGetValueW(root, subkey, name, RRF_RT_REG_SZ, nullptr, nullptr, &size) !=
            ERROR_SUCCESS ||
        size == 0) {
        return false;
    }

    std::vector<wchar_t> buffer(size / sizeof(wchar_t));
    if (RegGetValueW(root, subkey, name, RRF_RT_REG_SZ, nullptr, buffer.data(),
                     &size) != ERROR_SUCCESS) {
        return false;
    }

    out = buffer.data();
    return true;
}

// Reads a REG_DWORD value from the current user's registry.
bool readRegDwordW(HKEY root, const wchar_t* subkey, const wchar_t* name,
                   DWORD& out) {
    DWORD size = sizeof(out);
    return RegGetValueW(root, subkey, name, RRF_RT_REG_DWORD, nullptr, &out,
                        &size) == ERROR_SUCCESS;
}

// Picks the proxy address for the request from a Windows ProxyServer string.
// The value can be a plain "host:port" (all protocols) or a ';'-separated
// list like "http=host:port;https=host:port". Returns "" when nothing fits.
std::string pickProxyAddr(const std::wstring& server, bool https) {
    std::wstring httpAddr, httpsAddr, plainAddr;
    size_t start = 0;
    while (start <= server.size()) {
        size_t end = server.find(L';', start);
        if (end == std::wstring::npos) {
            end = server.size();
        }

        std::wstring token = server.substr(start, end - start);
        const size_t first = token.find_first_not_of(L" \t");
        const size_t last = token.find_last_not_of(L" \t");
        if (first != std::wstring::npos) {
            token = token.substr(first, last - first + 1);

            const size_t eq = token.find(L'=');
            if (eq == std::wstring::npos) {
                plainAddr = token;
            } else {
                const std::wstring scheme = token.substr(0, eq);
                const std::wstring addr = token.substr(eq + 1);
                if (scheme == L"http") {
                    httpAddr = addr;
                } else if (scheme == L"https") {
                    httpsAddr = addr;
                }
            }
        }

        if (end == server.size()) {
            break;
        }
        start = end + 1;
    }

    std::wstring chosen;
    if (https) {
        if (!httpsAddr.empty()) {
            chosen = httpsAddr;
        } else if (!plainAddr.empty()) {
            chosen = plainAddr;
        } else {
            chosen = httpAddr;
        }
    } else {
        if (!httpAddr.empty()) {
            chosen = httpAddr;
        } else if (!plainAddr.empty()) {
            chosen = plainAddr;
        } else {
            chosen = httpsAddr;
        }
    }

    if (chosen.empty()) {
        return "";
    }

    std::string narrow(chosen.begin(), chosen.end());
    if (narrow.compare(0, 7, "http://") != 0 &&
        narrow.compare(0, 8, "https://") != 0 &&
        narrow.compare(0, 6, "socks") != 0) {
        narrow = "http://" + narrow;
    }
    return narrow;
}
#endif  // _WIN32

// Returns the system HTTP proxy to use, or "" to connect directly. On Windows
// the per-user Internet Settings proxy (the same one browsers use) is read
// from the registry; curl then reaches sites like api.telegram.org that may
// be unreachable directly. On other platforms curl's own env-var handling
// (http_proxy/https_proxy) applies, so nothing is done here.
std::string systemProxyFor(bool https) {
#ifdef _WIN32
    static const std::string cached = []() -> std::string {
        DWORD enabled = 0;
        if (!readRegDwordW(HKEY_CURRENT_USER,
                           L"Software\\Microsoft\\Windows\\CurrentVersion\\"
                           L"Internet Settings",
                           L"ProxyEnable", enabled) ||
            enabled == 0) {
            return "";
        }

        std::wstring server;
        if (!readRegStringW(HKEY_CURRENT_USER,
                            L"Software\\Microsoft\\Windows\\CurrentVersion\\"
                            L"Internet Settings",
                            L"ProxyServer", server) ||
            server.empty()) {
            return "";
        }

        return pickProxyAddr(server, true);
    }();
    return cached;
#else
    (void)https;
    return "";
#endif
}

// Returns the proxy bypass (no_proxy) list for curl, or "".
// Only used on Windows; elsewhere curl handles NO_PROXY itself.
std::string systemProxyBypass() {
#ifdef _WIN32
    static const std::string cached = []() -> std::string {
        std::wstring overrideList;
        if (!readRegStringW(HKEY_CURRENT_USER,
                            L"Software\\Microsoft\\Windows\\CurrentVersion\\"
                            L"Internet Settings",
                            L"ProxyOverride", overrideList) ||
            overrideList.empty()) {
            return "";
        }

        std::string narrow;
        for (std::wstring::const_iterator it = overrideList.begin();
             it != overrideList.end(); ++it) {
            narrow += (*it == L';') ? ',' : static_cast<char>(*it);
        }
        return narrow;
    }();
    return cached;
#else
    return "";
#endif
}

// Result carried internally between the curl call and the public wrappers.
struct RawResponse {
    long statusCode = 0;  // 0 = transport-level failure
    std::string body;
};

// Shared request driver used by httpGet/httpPost/httpPostWithStatus.
// Returns the response body and HTTP status; the body is kept even for
// status >= 400 so callers can inspect error payloads.
RawResponse performRawRequest(const std::string& url,
                              bool post,
                              const std::string& body,
                              const std::string& contentType,
                              const std::string& actionName) {
    ensureCurlInitialized();

    CURL* handle = curl_easy_init();
    if (handle == nullptr) {
        std::fprintf(stderr, "[gum] %s: failed to initialize curl\n", actionName.c_str());
        return RawResponse{0, ""};
    }

    std::string response;
    curl_slist* headers = nullptr;
    CURLcode result = CURLE_OK;

    curl_easy_setopt(handle, CURLOPT_URL, url.c_str());
    curl_easy_setopt(handle, CURLOPT_TIMEOUT, 30L);              // 30 second timeout
    curl_easy_setopt(handle, CURLOPT_USERAGENT, "gum/1.0");      // identify ourselves
    curl_easy_setopt(handle, CURLOPT_FOLLOWLOCATION, 1L);        // follow redirects
    curl_easy_setopt(handle, CURLOPT_WRITEFUNCTION, writeCallback);
    curl_easy_setopt(handle, CURLOPT_WRITEDATA, &response);

    const bool https = url.compare(0, 6, "https:") == 0;
    const std::string proxy = systemProxyFor(https);
    if (!proxy.empty()) {
        curl_easy_setopt(handle, CURLOPT_PROXY, proxy.c_str());
    }
    const std::string noProxy = systemProxyBypass();
    if (!noProxy.empty()) {
        curl_easy_setopt(handle, CURLOPT_NOPROXY, noProxy.c_str());
    }

    const std::string caBundle = findCaBundle();
    if (!caBundle.empty()) {
        curl_easy_setopt(handle, CURLOPT_CAINFO, caBundle.c_str());
    }

    if (post) {
        curl_easy_setopt(handle, CURLOPT_POST, 1L);
        curl_easy_setopt(handle, CURLOPT_POSTFIELDS, body.c_str());
        curl_easy_setopt(handle, CURLOPT_POSTFIELDSIZE, static_cast<long>(body.size()));
        if (!contentType.empty()) {
            headers = curl_slist_append(headers, ("Content-Type: " + contentType).c_str());
            if (headers != nullptr) {
                curl_easy_setopt(handle, CURLOPT_HTTPHEADER, headers);
            }
        }
    }

    result = curl_easy_perform(handle);

    long statusCode = 0;
    curl_easy_getinfo(handle, CURLINFO_RESPONSE_CODE, &statusCode);

    if (result != CURLE_OK) {
        std::fprintf(stderr, "[gum] %s failed for '%s': %s\n",
                     actionName.c_str(), url.c_str(), curl_easy_strerror(result));
        response.clear();
        statusCode = 0;
    }

    if (headers != nullptr) {
        curl_slist_free_all(headers);
    }
    curl_easy_cleanup(handle);
    return RawResponse{statusCode, std::move(response)};
}

}  // namespace

std::string httpGet(const std::string& url) {
    const RawResponse result = performRawRequest(url, false, "", "", "http_get");
    if (result.statusCode == 0 || result.statusCode >= 400) {
        if (result.statusCode >= 400) {
            std::fprintf(stderr, "[gum] http_get for '%s' returned HTTP status %ld\n",
                         url.c_str(), result.statusCode);
        }
        return "";
    }

    return result.body;
}

std::string httpPost(const std::string& url,
                     const std::string& body,
                     const std::string& contentType) {
    const RawResponse result =
        performRawRequest(url, true, body, contentType, "http_post");
    if (result.statusCode == 0 || result.statusCode >= 400) {
        if (result.statusCode >= 400) {
            std::fprintf(stderr, "[gum] http_post for '%s' returned HTTP status %ld\n",
                         url.c_str(), result.statusCode);
        }
        return "";
    }

    return result.body;
}

HttpResponse httpPostWithStatus(const std::string& url,
                                const std::string& body,
                                const std::string& contentType) {
    const RawResponse result =
        performRawRequest(url, true, body, contentType, "http_post");
    return HttpResponse{result.statusCode, result.body};
}

}  // namespace sek
