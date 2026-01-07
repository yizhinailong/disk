#!/bin/bash

build_message="echo '🔄 文件变化，重新构建...'"
preset_name="linux-debug-clang"
build_command="cmake --build --preset $preset_name"
run_command="./build/$preset_name/src/disk"

full_command="$build_message && $build_command && $run_command"

watchexec \
  --exts cpp,h,cxx,hpp,c,cmake \
  --debounce 500ms \
  --restart \
  --clear \
  --project-origin . \
  --ignore build/** \
  --ignore .git/** \
  --ignore "*.swp" \
  --signal SIGTERM \
  -- bash -c "$full_command"
