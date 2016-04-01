/* 
  * Compile with: 
  * g++ main.cpp -o fcp -Ofast -march=native -std=c++1z -lstdc++fs
*/
#include <iostream>
#include <string>
#include <experimental/filesystem>
#include <fstream>
#include <sys/mman.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
using namespace std;
namespace fs = std::experimental::filesystem;

void die(const string& s) {
    cerr << s << endl;
    exit(1);
}

void usage() {
    static string usage_str = "Usage:\n cpf <source> <dest>";
    die(usage_str);
}

bool create_file(const string& p) {
    ofstream f(p);
    return f.is_open();
}

void fast_copy(const string& source_p, const string& dest_p) {
    auto source_fd = ::open64(source_p.c_str(), O_RDONLY);
    auto old_umask = umask(S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH);
    auto dest_fd = ::open64(source_p.c_str(), O_RDWR, O_CREAT | O_TRUNC);
    if(source_fd == -1)
        throw std::runtime_error("Could not open " + source_p + " " + strerror(errno));
    if(dest_fd == -1) {
        ::close(source_fd);
        throw std::runtime_error("Could not create " + dest_p + " " + strerror(errno));
    }
    umask(old_umask);

    auto close_fds = [source_fd, dest_fd]() {
        ::close(source_fd);
        ::close(dest_fd);
    };

    auto size = fs::file_size(source_p);
    truncate:
    auto ret = ftruncate(dest_fd, size);
    if(ret == -1) {
        if(errno == EINTR)
            goto truncate;
        close_fds();
        throw std::runtime_error("Could not truncate " + dest_p + " " + strerror(errno));
    }

    char* source = static_cast<char*>(::mmap64(NULL, size, PROT_READ, MAP_SHARED, source_fd, 0));
    char* dest = static_cast<char*>(::mmap64(NULL, size, PROT_WRITE, MAP_SHARED, dest_fd, 0));
    if(source == MAP_FAILED) {
        close_fds();
        throw std::runtime_error("Could not map " + source_p + " " + strerror(errno));
    }
    if(dest == MAP_FAILED) {
        munmap(source, size);
        close_fds();
        throw std::runtime_error("Could not map " + dest_p + " " + strerror(errno));
    }

    madvise(source, size, MADV_SEQUENTIAL);
    madvise(dest, size, MADV_SEQUENTIAL);
    std::copy(source, source + size, dest);

    munmap(source, size);
    munmap(dest, size);

    close_fds();
}

int main(int argc, char** argv)
{
    try {
        if(argc < 3)
            usage();
        string source = argv[1];
        string dest = argv[2];
        if(!fs::exists(source))
            die(source + " does not exist");
        fast_copy(source, dest);
    }
    catch(const exception& ex) {
        die(ex.what());
    }

    return 0;
}

