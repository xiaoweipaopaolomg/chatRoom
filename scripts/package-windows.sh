#!/usr/bin/env bash
set -euo pipefail

root_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
image="${CHATROOM_WINDOWS_BUILDER_IMAGE:-stateoftheartio/qt6:6.2-mingw-aqt}"
output_dir="$root_dir/out/windows"
build_dir="$output_dir/build"
package_name="ChatRoom-Windows-x64"
package_dir="$output_dir/$package_name"
archive="$output_dir/$package_name.zip"

mkdir -p "$output_dir"
cmake -E remove_directory "$build_dir"
cmake -E remove_directory "$package_dir"
cmake -E rm -f "$archive"

docker run --rm \
  --volume "$root_dir:/home/user/project:ro" \
  --volume "$output_dir:/home/user/output:rw" \
  "$image" sh -lc '
    set -eu
    qt-cmake /home/user/project -G Ninja -B /home/user/output/build \
      -DCMAKE_BUILD_TYPE=Release \
      -DBUILD_QT_CLIENT_ONLY=ON \
      -DBUILD_SERVER=OFF \
      -DBUILD_TESTS=OFF \
      -DBUILD_LOAD_TEST=OFF \
      -DBUILD_MYSQL_ADAPTER=OFF \
      -DBUILD_QT_CLIENT=ON
    cmake --build /home/user/output/build --target chatroom_qt_client
    mkdir -p /home/user/output/ChatRoom-Windows-x64
    cp /home/user/output/build/src/qt_client/chatroom_qt_client.exe \
      /home/user/output/ChatRoom-Windows-x64/ChatRoom.exe
    cp /home/user/project/packaging/windows/README.txt \
      /home/user/output/ChatRoom-Windows-x64/README.txt
    windeployqt --release --no-translations --compiler-runtime \
      --dir /home/user/output/ChatRoom-Windows-x64 \
      /home/user/output/ChatRoom-Windows-x64/ChatRoom.exe
  '

(
  cd "$output_dir"
  cmake -E tar cf "$archive" --format=zip "$package_name"
)

echo "Windows package: $archive"
