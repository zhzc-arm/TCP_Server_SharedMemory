#ifndef FTP_GET_FILE_HPP
#define FTP_GET_FILE_HPP

#include <string>

class FtpGetFile {

public:
    bool ftp_get_file(const std::string& url, 
                        const std::string& output_file,
                        const std::string& username = std::string(),
                        const std::string& password = std::string());

private:

};
 
#endif