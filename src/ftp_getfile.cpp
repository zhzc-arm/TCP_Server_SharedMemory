#include "ftp_getfile.hpp"
#include <iostream>
#include <fstream>
#include <string>
#include <curl/curl.h>
#include <iomanip>


struct FtpFile {
    const char* filename;
    FILE* stream;
};
 
// 写入回调函数：libcurl 每收到一块数据就会调用此函数
static size_t write_data(void* buffer, size_t size, size_t nmemb, void* userp) {
    FtpFile* out = (FtpFile*)userp;
    if (!out->stream) {
        out->stream = fopen(out->filename, "wb");
        if (!out->stream) {
            std::cerr << "[错误] 无法创建本地文件: " << out->filename << std::endl;
            return 0;
        }
    }
    size_t real_size = size * nmemb;
    size_t written = fwrite(buffer, 1, real_size, out->stream);
    return written;
}

// 进度回调函数（可选），显示下载进度
static int progress_callback(void* clientp, curl_off_t dltotal, curl_off_t dlnow,
                             curl_off_t ultotal, curl_off_t ulnow) {
    if (dltotal > 0) {
        double percent = (double)dlnow / dltotal * 100;
        std::cout << "\r下载进度: " << dlnow << " / " << dltotal
                  << " bytes (" << std::fixed << std::setprecision(2)
                  << percent << "%)" << std::flush;
    } else {
        std::cout << "\r已下载: " << dlnow << " bytes" << std::flush;
    }
    return 0;
}

// 核心下载函数
bool FtpGetFile::ftp_get_file(const std::string& url, 
                              const std::string& output_file,
                              const std::string& username,
                              const std::string& password)
{
    CURL* curl;
    CURLcode res;
    FtpFile ftpfile = { output_file.c_str(), nullptr };

    curl_global_init(CURL_GLOBAL_DEFAULT);
    curl = curl_easy_init();
    if (!curl) {
        std::cerr << "[错误] 初始化 libcurl 失败" << std::endl;
        curl_global_cleanup();
        return false;
    }

    // 设置 URL
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());

    // 设置用户名和密码（如果提供）
    if (!username.empty()) {
        std::string userpwd = username + ":" + password;
        curl_easy_setopt(curl, CURLOPT_USERPWD, userpwd.c_str());
    }

    // 开启被动模式（兼容旧版 libcurl：使用 EPSV 禁用，强制使用 PASV）
    // 如果新版 libcurl 支持 CURLOPT_FTP_USE_PASV，可以改用那一行，但下面这行更通用
    curl_easy_setopt(curl, CURLOPT_FTP_USE_EPSV, 0L);  // 禁用 EPSV，从而使用 PASV

    // 设置写入回调
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_data);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &ftpfile);

    // 设置进度回调
    curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, progress_callback);
    curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);

    // 启用详细输出（便于调试，可以注释掉）
    curl_easy_setopt(curl, CURLOPT_VERBOSE, 1L);

    std::cout << "开始从 " << url << " 下载文件到 " << output_file << " ..." << std::endl;

    // 执行下载
    res = curl_easy_perform(curl);
    std::cout << std::endl;

    bool success = false;
    if (res != CURLE_OK) {
        std::cerr << "[错误] 下载失败: " << curl_easy_strerror(res) << std::endl;
    } else {
        long response_code = 0;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response_code);
        if (response_code >= 400) {
            std::cerr << "[错误] 服务器返回错误码: " << response_code << std::endl;
        } else {
            std::cout << "[成功] 文件已保存为: " << output_file << std::endl;
            success = true;
        }
    }

    if (ftpfile.stream) {
        fclose(ftpfile.stream);
    }
    curl_easy_cleanup(curl);
    curl_global_cleanup();

    return success;
}