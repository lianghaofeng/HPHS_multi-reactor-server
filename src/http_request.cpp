#include "http_request.h"
#include <sstream>
#include <algorithm>

bool HttpRequest::parse(std::string_view buffer){
    /*
    HTTP请求格式
    请求行\r\n
    头部1\r\n
    头部2\r\n
    \r\n
    请求体
    */

    size_t header_end = buffer.find("\r\n\r\n");
    if(header_end == std::string_view::npos){
        return false;
    }

    // parsed_length_ = header_end + 4;

    std::string_view header_part = buffer.substr(0, header_end);
    body_ = buffer.substr(header_end + 4);

    // 解析请求行
    size_t line_end = header_part.find("\r\n");
    if (line_end == std::string::npos) return false;
    
    std::string_view request_line = header_part.substr(0, line_end);
    if (!parseRequestLine(request_line)) return false;

    // 解析请求头
    std::string_view headers = header_part.substr(line_end + 2);
    parseHeaders(headers);

    size_t body_len = 0;
    if(headers_.count("content-length")){
        body_len = std::stoi(headers_["content-length"]);
    }

    if(buffer.size() < header_end + 4 + body_len){
        return false;
    }

    parsed_length_ = header_end + 4 + body_len;

    if(body_len > 0){
        body_ = buffer.substr(header_end + 4, body_len);
    }

    return true;
}

bool HttpRequest::parseRequestLine(std::string_view line){
    // METHOD PATH VERSION
    // GET /index.html HTTP/1.1

    size_t method_end = line.find(' ');
    if (method_end == std::string_view::npos) return false;

    std::string_view method_str = line.substr(0, method_end);

    if (method_str == "GET"){
        method_ = GET;
    } else if (method_str == "POST"){
        method_ = POST;
    } else if (method_str == "HEAD"){
        method_ = HEAD;
    } else if (method_str == "PUT"){
        method_ = PUT;
    } else if (method_str == "DELETE"){
        method_ = DELETE;
    } else {
        method_ = INVALID;
        return false;
    }

    line.remove_prefix(method_end + 1);
    size_t path_end = line.find(' ');
    if (path_end == std::string_view::npos) return false;
    path_ = line.substr(0, path_end);

    line.remove_prefix(path_end + 1);
    version_ = line;    

    if (path_.empty()){
        path_ = "/";
    }

    return true;
}


void HttpRequest::parseHeaders(std::string_view header_part){
    size_t start = 0;
    size_t pos = 0;

    while ((pos = header_part.find("\r\n", start)) != std::string_view::npos){
        std::string_view line = header_part.substr(start, pos - start);

        start = pos + 2;

        if (line.empty()) continue;


        size_t colon = line.find(':');
        if (colon == std::string::npos) continue;

        std::string_view key = line.substr(0, colon);
        std::string_view value = line.substr(colon + 1);

        size_t non_space = value.find_first_not_of(" \t");
        if(non_space != std::string::npos){
            value.remove_prefix(non_space);
        } else {
            value = {};
        }
        headers_[toLower(std::string(key))] = std::string(value);
    }
}

std::string HttpRequest::getHeader(const std::string& key) const {
    std::string lower_key = toLower(key);
    auto it = headers_.find(lower_key);
    if(it != headers_.end()){
        return it->second;
    }
    return "";
}

bool HttpRequest::keepAlive() const {
    std::string connection = getHeader("Connection");

    if(version_ == "HTTP/1.1"){
        if (connection == "close") return false;
        return true;
    } else {
        if (connection != "keep-alive") return false;
        return true; 
    }
}

std::string HttpRequest::methodString() const {
    switch (method_){
        case GET: return "GET";
        case POST: return "POST";
        case HEAD: return "HEAD";
        case PUT: return "PUT";
        case DELETE: return "DELETE";
        default: return "INVALID";
    }
}

std::string HttpRequest::toLower(const std::string& str) {
    std::string result = str;
    std::transform(result.begin(), result.end(), result.begin(), ::tolower);
    return result;
}

