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

    /**
     * @brief 单例基类模板
     *
     * 使用方式：
     * @code
     * class MyClass : public Singleton<MyClass> {
     *     friend class Singleton<MyClass>;
     * private:
     *     MyClass() = default;
     * };
     * @endcode
     *
     * 线程安全保证：
     * - 使用 std::call_once 确保单例只初始化一次
     * - GetInstance() 方法是线程安全的
     *
     * 注意事项：
     * - 确保构造函数是 private 且默认或显式实现
     * - 通过 friend class Singleton<T> 授予基类访问权限
     */
    template <typename T>
    class Singleton {
    public:
        static std::shared_ptr<T> GetInstance() {
            static std::once_flag s_flag;
            std::call_once(s_flag, [&]() {
                m_instance = std::make_shared<T>();
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
