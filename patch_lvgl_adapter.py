Import("env")
from pathlib import Path

proj = Path(env["PROJECT_DIR"])
cmake = proj / "components" / "espressif__esp_lvgl_adapter" / "CMakeLists.txt"

def patch():
    if not cmake.exists():
        return

    print(">>> PATCH.....")
    s = cmake.read_text(encoding="utf-8")

    # Replace the split -include form with the sticky form (prevents argument splitting)
    s2 = s.replace(
        'target_compile_options(${COMPONENT_LIB} PUBLIC -include "${CMAKE_CURRENT_SOURCE_DIR}/src/display/ports/lvgl_port_alignment.h")',
        'target_compile_options(${COMPONENT_LIB} PUBLIC "-include${CMAKE_CURRENT_SOURCE_DIR}/src/display/ports/lvgl_port_alignment.h")',
    ).replace(
        'target_compile_options(${lvgl_comp_lib} PUBLIC -include "${CMAKE_CURRENT_SOURCE_DIR}/src/display/ports/lvgl_port_alignment.h")',
        'target_compile_options(${lvgl_comp_lib} PUBLIC "-include${CMAKE_CURRENT_SOURCE_DIR}/src/display/ports/lvgl_port_alignment.h")',
    )
dependencies:
  waveshare/esp32_s3_touch_amoled_1_75: "^2.0.6"

    if s2 != s:
        cmake.write_text(s2, encoding="utf-8")
        print("Patched esp_lvgl_adapter CMakeLists.txt (-include sticky form)")

patch()
