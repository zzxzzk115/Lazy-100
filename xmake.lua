-- set project name
set_project("lazy100")

-- set project version
set_version("0.1.0")

-- set language version: C++ 23
set_languages("cxx23")

-- root ?
local is_root = (os.projectdir() == os.scriptdir())
set_config("root", is_root)
set_config("project_dir", os.scriptdir())

-- global options
option("lazy100_build_tests") -- build tests?
    set_default(true)
    set_showmenu(true)
    set_description("Enable lazy100 tests")
option_end()

-- if build on windows
if is_plat("windows") then
    add_cxxflags("/Zc:__cplusplus", {tools = {"msvc", "cl"}}) -- fix __cplusplus == 199711L error
    add_cxxflags("/utf-8", {tools = {"msvc", "cl"}}) -- source is UTF-8 (CJK comments/strings)
    add_cxxflags("/bigobj") -- avoid big obj
    add_cxxflags("-D_SILENCE_STDEXT_ARR_ITERS_DEPRECATION_WARNING")
    add_cxxflags("/EHsc")
    set_runtimes(is_mode("debug") and "MTd" or "MT")
else
    add_cxxflags("-fexceptions")
end

-- add rules
rule("clangd.config")
    on_config(function (target)
        if is_host("windows") then
            os.cp(".clangd.win", ".clangd")
        else
            os.cp(".clangd.nowin", ".clangd")
        end
    end)
rule_end()

add_rules("mode.debug", "mode.release")
add_rules("plugin.vsxmake.autoupdate")
add_rules("plugin.compile_commands.autoupdate", {outputdir = ".vscode", lsp = "clangd"})
add_rules("clangd.config")

-- 32-bit ARM: xmake injects a bare -march=armv7-a, which resets gcc's FPU selection and
-- then clashes with the hard-float ABI every armhf distro uses ("selected architecture
-- lacks an FPU"). Re-add the FPU (+fp = VFPv3-D16, the Debian/Ubuntu armhf baseline) for
-- the project and for every package build.
if is_plat("linux") and is_arch("armv7") then
    local armfpu = "-march=armv7-a+fp"
    add_cflags(armfpu)
    add_cxxflags(armfpu)
    add_asflags(armfpu)
    add_requireconfs("**", {configs = {cflags = armfpu, cxflags = armfpu, asflags = armfpu}})
end

-- add repositories
add_repositories("my-xmake-repo https://github.com/zzxzzk115/xmake-repo.git backup")

-- include external libraries
includes("external")

-- include source
includes("source")

-- host tools (cartshot preview generator)
includes("tools")

-- include tests
if has_config("lazy100_build_tests") then
    includes("tests")
end