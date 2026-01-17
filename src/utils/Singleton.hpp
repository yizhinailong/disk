/**
 * @file Singleton.hpp
 * @author LiuFeng (liufeng.code@outlook.com)
 * @brief 单例模板类
 * @version 0.1
 * @date 2026-01-18
 *
 * @copyright Copyright (c) 2026
 *
 */
#pragma once

#include <memory>
#include <mutex>

namespace disk::utils {

    template <typename T>
    class Singleton {
    public:
        static std::shared_ptr<T> GetInstance() {
            static std::once_flag s_flag;
            std::call_once(s_flag, [&]() {
                m_instance = std::shared_ptr<T>(new T);
            });

            return m_instance;
        }

        ~Singleton() = default;

    protected:
        Singleton() = default;
        Singleton(const Singleton<T>&) = delete;
        Singleton& operator=(const Singleton<T>&) = delete;
        Singleton(Singleton<T>&&) = delete;
        Singleton& operator=(Singleton<T>&&) = delete;

        static std::shared_ptr<T> m_instance;
    };

    template <typename T>
    std::shared_ptr<T> Singleton<T>::m_instance = nullptr;

} // namespace disk::utils
