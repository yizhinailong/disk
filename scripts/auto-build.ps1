$env:JWT_SECRET = "dev-only-jwt-secret-key-change-in-production-2024"

$buildMessage = "Write-Host '🔄 文件变化，重新构建...' -ForegroundColor Yellow;"
$presetName = "windows-debug-clang-cl"
$buildCommand = "cmake --build --preset $presetName;"
$runCommand = "./build/$presetName/src/disk.exe"

$fullCommand = $buildMessage + $buildCommand + $runCommand

cmake --build --preset $presetName
watchexec `
  --watch config.json `
  --watch src `
  --exts cpp,h,cxx,hpp,c,cmake `
  --debounce 500ms `
  --restart `
  --clear `
  --project-origin . `
  --ignore "build/**" `
  --ignore ".git/**" `
  --ignore "*.swp" `
  -- pwsh -NoProfile -Command $fullCommand
