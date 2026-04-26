#!/bin/bash

export JWT_SECRET="dev-only-jwt-secret-key-change-in-production-2024"

build_message="echo '🔄 文件变化，重新构建...'"
preset_name="linux-debug-clang"
build_command="cmake --build --preset $preset_name"
run_command="./build/$preset_name/src/disk"

full_command="$build_message && $build_command && $run_command"

cmake --preset $preset_name
watchexec \
  --watch src/ \
  --watch config.json \
  --exts cpp,h,cxx,hpp,c,cmake \
  --debounce 500ms \
  --restart \
  --clear \
  --project-origin . \
  -- bash -c "$full_command"
