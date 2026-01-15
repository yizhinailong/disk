#!/usr/bin/env bash

# 用法：  ./rename.sh  /path/to/your/project
#       或   ./rename.sh .          （当前目录）

target_dir="${1:-.}"

# 改 .h → .hpp
find "$target_dir" -type f -name "*.h" -print0 | while IFS= read -r -d '' file; do
    newname="${file%.h}.hpp"
    if [[ "$file" != "$newname" ]]; then
        echo "mv \"$file\" → \"$newname\""
        mv -i "$file" "$newname"
    fi
done

# 改 .cc → .cpp
find "$target_dir" -type f -name "*.cc" -print0 | while IFS= read -r -d '' file; do
    newname="${file%.cc}.cpp"
    if [[ "$file" != "$newname" ]]; then
        echo "mv \"$file\" → \"$newname\""
        mv -i "$file" "$newname"
    fi
done

echo "完成"
