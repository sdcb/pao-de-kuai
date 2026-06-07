#pragma once

#include <utility>

namespace pdk::graphics {

template <typename T>
class ComPtr {
public:
    ComPtr() = default;
    explicit ComPtr(T* ptr) : ptr_(ptr) {}
    ~ComPtr() { Reset(); }

    ComPtr(const ComPtr& other) : ptr_(other.ptr_) {
        if (ptr_) {
            ptr_->AddRef();
        }
    }

    ComPtr& operator=(const ComPtr& other) {
        if (this != &other) {
            Reset();
            ptr_ = other.ptr_;
            if (ptr_) {
                ptr_->AddRef();
            }
        }
        return *this;
    }

    ComPtr(ComPtr&& other) noexcept : ptr_(std::exchange(other.ptr_, nullptr)) {}

    ComPtr& operator=(ComPtr&& other) noexcept {
        if (this != &other) {
            Reset();
            ptr_ = std::exchange(other.ptr_, nullptr);
        }
        return *this;
    }

    T* Get() const { return ptr_; }
    T** GetAddressOf() { return &ptr_; }
    T** ReleaseAndGetAddressOf() {
        Reset();
        return &ptr_;
    }
    T* operator->() const { return ptr_; }
    explicit operator bool() const { return ptr_ != nullptr; }

    void Reset() {
        if (ptr_) {
            ptr_->Release();
            ptr_ = nullptr;
        }
    }

    T* Detach() {
        return std::exchange(ptr_, nullptr);
    }

    void Attach(T* ptr) {
        Reset();
        ptr_ = ptr;
    }

private:
    T* ptr_{nullptr};
};

} // namespace pdk::graphics
