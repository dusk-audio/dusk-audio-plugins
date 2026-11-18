#!/usr/bin/env python3
# Copyright (C) 2026 Dusk Audio — GNU GPL v3.0 or later (see repository LICENSE).
"""Check the opt-in adapter generation, incremental lifecycle and SDK-drift guards."""
from pathlib import Path
import shutil
import subprocess
import tempfile


def main():
    root = Path(__file__).resolve().parents[2]
    sdk = root / "external/clap-juce-extensions/src/wrapper"
    with tempfile.TemporaryDirectory(prefix="dusk-clap-output-") as temporary:
        source = Path(temporary) / "source"
        build = Path(temporary) / "build"
        wrapper = source / "sdk/src/wrapper"
        wrapper.mkdir(parents=True)
        for name in ("clap-juce-wrapper.cpp", "clap-juce-mac.mm"):
            shutil.copy(sdk / name, wrapper / name)
        original = (wrapper / "clap-juce-wrapper.cpp").read_text()
        cmake = '''cmake_minimum_required(VERSION 3.21)
project(ClapOutputWrapperProbe LANGUAGES CXX)
file(APPEND "${CMAKE_BINARY_DIR}/configurations.txt" "configure\\n")
include("@HELPER@")
# BUILD_CLAP=OFF / an absent target must remain a no-op.
dusk_clap_enable_output_parameters(Absent)
add_library(clap_juce_sources INTERFACE)
set_property(TARGET clap_juce_sources PROPERTY CLAP_JUCE_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/sdk")
target_sources(clap_juce_sources INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/sdk/src/wrapper/clap-juce-wrapper.cpp")
add_library(Probe_CLAP MODULE)
target_link_libraries(Probe_CLAP PRIVATE clap_juce_sources)
add_library(Neighbour_CLAP MODULE)
target_link_libraries(Neighbour_CLAP PRIVATE clap_juce_sources)
dusk_clap_enable_output_parameters(Probe)
get_target_property(probe_links Probe_CLAP LINK_LIBRARIES)
get_target_property(neighbour_links Neighbour_CLAP LINK_LIBRARIES)
if("clap_juce_sources" IN_LIST probe_links OR NOT "clap_juce_sources" IN_LIST neighbour_links)
  message(FATAL_ERROR "Output wrapper opt-in leaked to another module")
endif()
add_custom_target(probe COMMAND "${CMAKE_COMMAND}" -E echo "probe")
'''.replace("@HELPER@", (root / "cmake/ClapDefaults.cmake").as_posix())
        (source / "CMakeLists.txt").write_text(cmake)
        configure = ["cmake", "-S", str(source), "-B", str(build)]
        subprocess.run(configure, check=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        for _ in range(2):
            subprocess.run(["cmake", "--build", str(build), "--target", "probe"], check=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        assert (build / "configurations.txt").read_text().splitlines() == ["configure"], "unchanged builds unexpectedly reconfigure"
        generated = build / "dusk-clap-output-wrapper/clap-juce-wrapper.cpp"
        assert "CLAP_PARAM_IS_READONLY" in generated.read_text()
        assert (wrapper / "clap-juce-wrapper.cpp").read_text() == original, "SDK input was modified"
        # Windows checkouts may have CRLF; that is not SDK API drift.
        windows_bytes = original.replace("\n", "\r\n").encode()
        (wrapper / "clap-juce-wrapper.cpp").write_bytes(windows_bytes)
        subprocess.run(configure, check=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        assert (wrapper / "clap-juce-wrapper.cpp").read_bytes() == windows_bytes
        # Changing the SDK must regenerate the copy; no stale wrapper may survive.
        changed = original + "\n// SDK regeneration control\n"
        (wrapper / "clap-juce-wrapper.cpp").write_text(changed)
        subprocess.run(["cmake", "--build", str(build), "--target", "probe"], check=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        assert "SDK regeneration control" in generated.read_text()
        anchor = "        if (paramVariant.processorParam->isAutomatable())\n            info->flags = info->flags | CLAP_PARAM_IS_AUTOMATABLE;"
        assert original.count(anchor) == 1
        for invalid in (original.replace(anchor, ""), original + "\n" + anchor):
            (wrapper / "clap-juce-wrapper.cpp").write_text(invalid)
            result = subprocess.run(configure, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
            assert result.returncode != 0 and "expected one parameter-flags anchor" in " ".join(result.stdout.split()), result.stdout
    print("CLAP output wrapper: opt-in isolated, SDK unchanged, incremental copy refreshed, drift rejected")


if __name__ == "__main__":
    main()
