#include "http_response.h"
#include <sstream>

void HttpResponse::setStatusCode(int code){
    status_code_ = code;
    status_message_ = getStatusMessage(code);
}

std::string HttpResponse::build(){
    std::ostringstream oss;

    // 状态行： HTTP/1.1 200 OK
    oss << version_ << " " << status_code_ << " " << status_message_ << "\r\n";

    if(useSendfile()) {
        headers_["Content-Length"] =  std::to_string(sendfile_size_);
    } else if(!body_.empty()){
        headers_["Content-Length"] = std::to_string(body_.size());
    }

    // 响应头
    for(const auto& header : headers_){
        oss << header.first << ": " << header.second << "\r\n";
    }

    oss << "\r\n";

    if(!useSendfile()){
        oss << body_;
    }
    
    return oss.str();

}

std::string HttpResponse::getStatusMessage(int code){
    switch(code){
        case 200: return "OK";
        case 201: return "Created";
        case 204: return "No Content";
        case 301: return "Moved Permanently";
        case 302: return "Found";
        case 304: return "Not Modified";
        case 400: return "Bad Request";
        case 401: return "Unauthorized";
        case 403: return "Forbidden";
        case 404: return "Not Found";
        case 405: return "Method Not Allowed";
        case 500: return "Internal Server Error";
        case 501: return "Not Implemented";
        case 502: return "Bad Gateway";
        case 503: return "Service Unavailable";
        default: return "Unknown";

    }
}

std::string HttpResponse::getContentType(const std::string& path){
    size_t dot_pos = path.find_last_of('.');
    if(dot_pos == std::string::npos){
        return "application/octet-stream"; //二进制类型
    }

    std::string ext = path.substr(dot_pos + 1);

    if(ext == "html" || ext == "htm") return "text/html; charset=utf-8";
    if(ext == "css") return "text/css";
    if(ext == "js") return "application/javascript";
    if(ext == "xml") return "application/xml";
    if(ext == "txt") return "text/plain; charset=utf-8";

    if(ext == "jpg" || ext == "jpeg") return "image/jpeg";
    if(ext == "png") return "image/png";
    if(ext == "gif") return "image/gif";
    if(ext == "svg") return "image/svg+xml";
    if(ext == "ico") return "image/x-icon";

    if(ext == "pdf") return "application/pdf";
    if(ext == "png") return "image/png";
    if(ext == "gif") return "image/gif";
    if(ext == "svg") return "image/svg+xml";
    if(ext == "ico") return "image/x-icon";

    if(ext == "pdf") return "application/pdf";
    if(ext == "zip") return "application/zip";
    if(ext == "mp4") return "video/mp4";
    if(ext == "mp3") return "audio/mpeg";

    return "application/octet-stream";

}