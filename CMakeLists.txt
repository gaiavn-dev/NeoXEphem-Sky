cmake_minimum_required(VERSION 3.20)
project(NeoXEphemWin C)

# XEphem's libastro and test, vendored unmodified (see third_party/xephem).
set(XEPHEM_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/xephem" CACHE PATH "Vendored XEphem sources")
set(ASTRO_DIR "${XEPHEM_DIR}/libastro")
set(DATA_DIR "${CMAKE_CURRENT_SOURCE_DIR}/data")

if(MSVC)
  add_compile_definitions(_CRT_SECURE_NO_WARNINGS _CRT_NONSTDC_NO_DEPRECATE _USE_MATH_DEFINES)
  add_compile_options(/W3 /wd4244 /wd4305 /wd4267 /wd4996 /utf-8)
endif()

# ---- libastro: XEphem's astronomy engine, compiled natively ----
file(GLOB ASTRO_SRC "${ASTRO_DIR}/*.c")
add_library(astro STATIC ${ASTRO_SRC})
target_include_directories(astro PUBLIC "${ASTRO_DIR}")
if(MSVC)
  target_include_directories(astro PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/compat")
endif()

# ---- upstream regression test (tests/test.c vs output.expected) ----
add_executable(astrotest "${XEPHEM_DIR}/tests/test.c")
target_link_libraries(astrotest astro)

# ---- shared sky core (catalogs, ephemeris, projection) ----
add_library(skycore STATIC src/skycore.c)
target_link_libraries(skycore PUBLIC astro)
target_include_directories(skycore PUBLIC src)

# ---- command-line: sky positions at any date/place ----
add_executable(neox-sky src/skycli.c)
target_link_libraries(neox-sky skycore)

# ---- native Win32 GUI sky map ----
add_executable(NeoXEphemSky WIN32 src/skywin.c src/skywin.rc)
target_link_libraries(NeoXEphemSky skycore comctl32 gdi32 user32 comdlg32)

# copy data files next to the executables
foreach(t neox-sky NeoXEphemSky)
  add_custom_command(TARGET ${t} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:${t}>/data"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
      "${DATA_DIR}/SKY2k65.edb"
      "${DATA_DIR}/Messier.edb"
      "${DATA_DIR}/xephem_sites"
      "${DATA_DIR}/classicfigs.csf"
      "$<TARGET_FILE_DIR:${t}>/data/")
endforeach()
