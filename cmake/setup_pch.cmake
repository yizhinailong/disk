# cmake/setup_pch.cmake
# 预编译头文件配置

function(setup_precompile_headers target)
    target_precompile_headers(${target}
        PRIVATE
        # Drogon 框架头文件
        <drogon/drogon.h>
        <drogon/HttpAppFramework.h>
        <drogon/HttpController.h>
        <drogon/HttpRequest.h>
        <drogon/HttpResponse.h>
        <drogon/HttpClient.h>

        # C++ 标准库常用头文件
        <iostream>
        <string>
        <vector>
        <map>
        <unordered_map>
        <memory>
        <functional>
        <algorithm>
        <chrono>
        <thread>
        <mutex>
        <fstream>
        <sstream>
        <exception>
        <stdexcept>

        # 其他常用头文件
        <cstdint>
        <cstring>
        <cassert>
    )
endfunction()
