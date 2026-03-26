git clone https://github.com/quickjs-ng/quickjs.git

pushd quickjs
mkdir build
pushd build
set PATH=%PATH%;C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin
cmake .. -DCMAKE_POLICY_DEFAULT_CMP0091=NEW -DCMAKE_MSVC_RUNTIME_LIBRARY="MultiThreaded$<$<CONFIG:Debug>:Debug>"
cmake --build . --config Release
cmake --build . --config Debug
popd
popd

