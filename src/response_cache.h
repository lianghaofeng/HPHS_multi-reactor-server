#ifndef RESPONSE_CACHE_H
#define RESPONSE_CACHE_H

#include <string>
#include <unordered_map>
#include <fstream>
#include <sstream>
#include <dirent.h>
#include <sys/stat.h>
#include <memory>
#include "http_response.h"

// 缓存条目：预构建的完整 HTTP 响应
struct CacheEntry {
    std::string response;      // 完整的 HTTP 响应（headers + body）
    std::string content_type;
    size_t body_size;
};

// 静态文件响应缓存
// 启动时预加载所有静态文件到内存，避免每次请求都读磁盘
class ResponseCache {
public:
    // 预加载指定目录下的所有文件
    void preload(const std::string& www_root) {
        www_root_ = www_root;
        loadDirectory(www_root, "");
    }

    // 查找缓存，返回 nullptr 表示未命中
    const CacheEntry* find(const std::string& path) const {
        std::string key = path;
        if (key.empty() || key.back() == '/') {
            key += "index.html";
        }

        auto it = cache_.find(key);
        if (it != cache_.end()) {
            return &it->second;
        }
        return nullptr;
    }

    size_t size() const { return cache_.size(); }

private:
    void loadDirectory(const std::string& base_path, const std::string& rel_path) {
        std::string full_path = base_path + rel_path;
        DIR* dir = opendir(full_path.c_str());
        if (!dir) return;

        struct dirent* entry;
        while ((entry = readdir(dir)) != nullptr) {
            std::string name = entry->d_name;
            if (name == "." || name == "..") continue;

            std::string file_rel_path = rel_path + "/" + name;
            std::string file_full_path = base_path + file_rel_path;

            struct stat st;
            if (stat(file_full_path.c_str(), &st) < 0) continue;

            if (S_ISDIR(st.st_mode)) {
                loadDirectory(base_path, file_rel_path);
            } else if (S_ISREG(st.st_mode)) {
                loadFile(file_full_path, file_rel_path);
            }
        }
        closedir(dir);
    }

    // 文件大小限制：大文件用 sendfile 零拷贝更快
    static constexpr size_t MAX_CACHE_FILE_SIZE = 1 * 1024 * 1024;  // 1MB

    void loadFile(const std::string& full_path, const std::string& url_path) {
        struct stat st;
        if (stat(full_path.c_str(), &st) < 0) return;

        // 大文件不缓存，避免内存爆炸
        if (st.st_size < 0 || static_cast<size_t>(st.st_size) > MAX_CACHE_FILE_SIZE) return;

        std::ifstream file(full_path, std::ios::binary);
        if (!file) return;

        file.seekg(0, std::ios::end);
        size_t size = file.tellg();
        file.seekg(0, std::ios::beg);

        std::string body(size, '\0');
        file.read(&body[0], size);
        
        // 构建完整的 HTTP 响应
        std::string content_type = HttpResponse::getContentType(full_path);

        std::string resp;
        resp += "HTTP/1.1 200 OK\r\n";
        resp += "Server: HPHS/1.0\r\n";
        resp += "Content-Type: " + content_type + "\r\n";
        resp += "Content-Length: "+ std::to_string(body.size()) + "\r\n";
        resp += "Connection: keep-alive\r\n";
        resp += "\r\n";
        resp += body;

        CacheEntry entry;
        entry.response = std::move(resp);
        entry.content_type = content_type;
        entry.body_size = body.size();

        // 如果是 index.html，同时缓存目录路径
        // "/index.html" (12字符) -> "/" (1字符)
        if (url_path.size() >= 12 &&
            url_path.substr(url_path.size() - 12) == "/index.html") {
            std::string dir_path = url_path.substr(0, url_path.size() - 11);
            cache_[dir_path] = entry;  // 先存目录路径（拷贝）
        }

        cache_[url_path] = std::move(entry);  // 再 move 到文件路径
    }

    std::string www_root_;
    std::unordered_map<std::string, CacheEntry> cache_;
};

#endif
