#ifndef MEM_MAPPING_H
#define MEM_MAPPING_H

#include <string>
#include <sys/mman.h>
#include <sys/fcntl.h>
#include <unistd.h>
#include <experimental/filesystem>

namespace fs = std::experimental::filesystem;

template<typename T>
class mem_mapping
{
    int fd;
    void* mem;
    std::size_t len;

    void map_in_mem() {
        mem = mmap(NULL, len, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    }

public:
    typedef T type;

    mem_mapping() : fd(-1), mem(nullptr), len(0) {}
    mem_mapping(int fd, std::size_t len) : fd(fd), mem(nullptr), len(len) {
        map_in_mem();
    }
    mem_mapping(const std::string& path, std::size_t len = 0) : mem_mapping() {
        this->fd = ::open(path.c_str(), O_RDWR, O_CREAT);
        this-> len = !len ? fs::file_size(path) : len;
        map_in_mem();
    }
    mem_mapping(const mem_mapping&) = delete;
    mem_mapping& operator=(const mem_mapping&) = delete;
    mem_mapping(mem_mapping&& other) {
        (*this) = std::move(other);
    }
    mem_mapping& operator=(mem_mapping&& other) {
        if(this != &other) {
            fd = other.fd;
            other.fd = -1;

            mem = other.mem;
            other.mem = nullptr;

            len = other.len;
            other.len = 0;
        }
    }

    void unmap_close()
    {
        if(this->good())
            munmap(mem, len);
        if(fd != -1)
            ::close(fd);
    }

    virtual ~mem_mapping() {
        unmap_close();
    }

    void map(unsigned int prot_flags, unsigned int other_flags, std::size_t len) noexcept {
        unmap_close();
        this->fd = fd;
        this->len = len;
        mem = mmap(NULL, len, prot_flags, other_flags | MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
    }

    void map(unsigned int prot_flags, unsigned int other_flags, int fd, std::size_t len) noexcept {
        unmap_close();
        this->fd = fd;
        this->len = len;
        mem = mmap(NULL, len, prot_flags, other_flags, fd, 0);
    }

    void map(unsigned int prot_flags, unsigned int other_flags, const std::string& path, std::size_t len = 0) noexcept {
        unmap_close();
        unsigned int flags = 0;
        if((prot_flags & PROT_READ) && (prot_flags & PROT_WRITE))
            flags = O_RDWR;
        else if(prot_flags & PROT_READ)
            flags = O_RDONLY;
        else if(prot_flags & PROT_WRITE)
            flags = O_WRONLY;
        this->fd = ::open(path.c_str(), flags, O_CREAT);
        this->len = !len ? fs::file_size(path) : len;
        mem = mmap(NULL, len, prot_flags, other_flags, fd, 0);
    }

    void sync() const noexcept {
        if(fd != -1)
            fsync(fd);
    }

    bool good() const noexcept {
        return !(mem == MAP_FAILED || mem == nullptr);
    }

    std::size_t length() const noexcept {
        return len;
    }

    std::size_t dimension() const noexcept {
        return len / sizeof(type);
    }

    inline type& operator[](std::size_t idx) noexcept {
        return *(static_cast<type*>(mem) + idx * sizeof(type));
    }

    inline type& at(std::size_t idx) {
        if(idx < len * sizeof(type)) {
            return (*this)[idx];
        }
        else throw std::out_of_range{""};
    }



    class iterator {
        iterator() = default;
        iterator
    };
};

#endif // MEM_MAPPING_H
