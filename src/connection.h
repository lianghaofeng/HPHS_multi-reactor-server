#ifndef CONNECTION_H
#define CONNECTION_H

#include <string>
#include <chrono>
#include <unistd.h>
#include <memory>

enum class ConnectionState {READING, WRITING, CLOSING};

class Connection {
public:
    explicit Connection(int fd) : fd_(fd) {}

    int fd() const {return fd_;}

    void setFileFd(int fd) {file_fd_ = fd;}
    int fileFd() const { return file_fd_;}
    void closeFileFd() {if (file_fd_ >= 0) {close(file_fd_); file_fd_ = -1;}}

    // 读缓冲区
    void appendRead(const char* data, size_t len) {
        //offset太大的时候，进行整理
        if(read_offset_>4096 && read_offset_ > read_buffer_.size() / 2){
            read_buffer_.erase(0, read_offset_);
            read_offset_=0;
        }
        read_buffer_.append(data, len);
    }
    std::string& readBuffer(){
        return read_buffer_;
    }
    void clearReadBuffer(){
        read_buffer_.clear();
    }

    // 写缓冲区
    void setWriteBuffer(std::string&& data){
        write_buffer_ = std::move(data);
        write_offset_ = 0;
    }
    void setWriteBuffer(const std::string& data){
        write_buffer_ = data;
        write_offset_ = 0;
    }
    const char* writeData() const {
        return write_buffer_.data() + write_offset_;
    }
    size_t writeRemaining() const {
        return write_buffer_.size() - write_offset_;
    }
    void advanceWrite(size_t len){
        write_offset_ += len;
    }
    bool writeComplete() const {
        return write_offset_ >= write_buffer_.size();
    }

    // Sendfile
    void setSendfile(const std::string& path, off_t size){
        sendfile_path_ = path;
        sendfile_size_ = size;
        sendfile_offset_ = 0;
    }
    bool hasSendfile() const {
        return !sendfile_path_.empty();
    }
    const std::string& sendfilePath() const {
        return sendfile_path_;
    }
    off_t sendfileSize() const {
        return sendfile_size_;
    }
    off_t& sendfileOffset() {
        return sendfile_offset_;
    }
    bool sendfileComplete() const {
        return sendfile_offset_ >= sendfile_size_;
    }

    // Keep-Alive
    void setKeepAlive(bool keep){
        keep_alive_ = keep;
    }
    bool keepAlive() const {
        return keep_alive_;
    }

    // 状态
    ConnectionState state() const{
        return state_;
    }
    void setState(ConnectionState s){
        state_ = s;
    }

    //Activeness
    void updateActivity(const std::chrono::steady_clock::time_point& now) {
        last_active_ = now;
    }
    bool isIdle(int timeout_ms, const std::chrono::steady_clock::time_point& now) const {
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            now - last_active_
        ).count();
        return elapsed > timeout_ms;
    }

    // 重置连接状态（用于对象池复用）
    void reset(int fd) {
        fd_ = fd;
        closeFileFd();
        state_ = ConnectionState::READING;
        has_epollout_ = false;
        read_buffer_.clear();
        write_buffer_.clear();
        write_offset_ = 0;
        read_offset_ = 0; 
        sendfile_path_.clear();
        sendfile_size_ = 0;
        sendfile_offset_ = 0;
        keep_alive_ = false;
        pool_index_ = SIZE_MAX;
        cached_response_ = nullptr;  // 清理缓存响应
        cached_offset_ = 0;
        updateActivity(std::chrono::steady_clock::now());
    }

    void setPoolIndex(size_t idx) { pool_index_ = idx; }
    size_t poolIndex() const { return pool_index_; }

    void setCachedResponse(const std::string *resp){
        cached_response_ = resp;
        cached_offset_ = 0;
    }

    bool hasCachedResponse() const {
        return cached_response_ != nullptr;
    }

    const char* cachedData() const {
        return cached_response_->data() + cached_offset_;
    }

    size_t cachedRemaining() const {
        return cached_response_->size() - cached_offset_;
    }

    void advanceCached(size_t len){
        cached_offset_ += len;
    }

    void clearCachedResponse(){
        cached_response_ = nullptr;
        cached_offset_ = 0;
    }

    void consumeReadBuffer(size_t len){
        read_offset_ += len;
        //如果读完了，可以直接清空
        if(read_offset_ == read_buffer_.size()){
            read_buffer_.clear();
            read_offset_ = 0;
        }
    }

    std::string_view readBuffer() const{
        return std::string_view(read_buffer_.data() + read_offset_, 
                                    read_buffer_.size() - read_offset_);
    }

    bool hasEpollout() const { return has_epollout_; }
    void setHasEpollout(bool v) { has_epollout_ = v; }

private:
    int fd_;
    int file_fd_ = -1;
    size_t pool_index_ = SIZE_MAX;  // 在 active_conns_ 中的索引

    const std::string* cached_response_ = nullptr;
    size_t cached_offset_ = 0;
    
private:
    ConnectionState state_ = ConnectionState::READING;
    bool has_epollout_ = false;
    std::string read_buffer_;
    std::string write_buffer_;
    size_t read_offset_ = 0;
    size_t write_offset_ = 0;
    std::string sendfile_path_;
    off_t sendfile_size_ = 0;
    off_t sendfile_offset_ = 0;
    bool keep_alive_ = false;

    std::chrono::steady_clock::time_point last_active_ = std::chrono::steady_clock::now();

};

#endif