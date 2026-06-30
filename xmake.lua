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
option("lazy100_build_examples") -- build examples?
    set_default(true)
    set_showmenu(true)
    set_description("Enable lazy100 examples")
option_end()

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
    -- Static CRT (MT/MTd) to match the VRI package and its ecosystem; VRI links statically,
    -- so a dynamic CRT here fails with LNK2038 runtime-mismatch errors.
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

-- add repositories
add_repositories("my-xmake-repo https://github.com/zzxzzk115/xmake-repo.git backup")

-- include external libraries
includes("external")

-- include source
includes("source")

-- include tests
if has_config("lazy100_build_tests") then
    includes("tests")
end

-- if build examples, then include examples
if has_config("lazy100_build_examples") then
    includes("examples")
end