#ifndef HTTP_REQUEST_H
#define HTTP_REQUEST_H

#include <string>
#include <unordered_map>

/**
 * 
 * -请求行: GET / path HTTP/1.1
 * -请求头: Host: example.com
 * -请求体: POST
 * 
 */

class HttpRequest{
public:
    enum Method{
        INVALID,
        GET,
        POST,
        HEAD,
        PUT,
        DELETE
    };

    HttpRequest() : method_(INVALID), version_("HTTP/1.1") {}

    /**
     * 解析HTTP请求
     * @param buffer 原始HTTP请求字符串
     * @return 解析是否成功
     */
    bool parse(std::string_view buffer);

    Method method() const {return method_; }
    const std::string& path() const {return path_; }
    const std::string& version() const {return version_; }
    const std::string& body() const {return body_; }   
    
    /**
     * @param key 请求头名称
     * @return 请求头的值
     */
    std::string getHeader(const std::string& key) const;

    /**
     * 是否为Keep-Alive
     */
    bool keepAlive() const;

    /**
     * 方法枚举转换成字符串
     */
    std::string methodString() const;

    /**
     * 返回解析消耗的字节数
     */
    size_t parseLength() const {return parsed_length_;}


private:
    /**
     * 解析请求行
     */
    bool parseRequestLine(std::string_view line);

    /**
     * 解析请求头
     */
    void parseHeaders(std::string_view header_part);

    /**
     * 字符串转小写
     */
    static std::string toLower(const std::string& str);


private:
    Method method_;
    std::string path_;
    std::string version_;
    std::unordered_map<std::string, std::string> headers_;
    std::string body_;
    size_t parsed_length_ = 0;


};

#endif