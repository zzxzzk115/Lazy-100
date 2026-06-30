#include "lazy100/script/lua_runtime.hpp"

#include "lazy100/common/log.hpp"
#include "lazy100/script/lua_api.hpp"

namespace lazy100
{
    struct LuaRuntime::Impl
    {
        sol::state              lua;
        sol::protected_function init, update, update60, draw;
    };

    LuaRuntime::LuaRuntime()  = default;
    LuaRuntime::~LuaRuntime() = default;

    bool LuaRuntime::init(Console& console)
    {
        p_ = std::make_unique<Impl>();
        p_->lua.open_libraries(sol::lib::base, sol::lib::math, sol::lib::table, sol::lib::string);
        bind_api(p_->lua, console);
        return true;
    }

    bool LuaRuntime::load_cart(const char* path)
    {
        if (!p_)
            return false;
        auto result = p_->lua.safe_script_file(path, sol::script_pass_on_error);
        if (!result.valid())
        {
            sol::error err = result;
            LZ_ERROR("cart load failed (%s): %s", path, err.what());
            return false;
        }
        p_->init     = p_->lua["_init"];
        p_->update   = p_->lua["_update"];
        p_->update60 = p_->lua["_update60"];
        p_->draw     = p_->lua["_draw"];
        LZ_INFO("cart loaded: %s", path);
        return true;
    }

    namespace
    {
        void call_cb(sol::protected_function& f, const char* name)
        {
            if (!f.valid())
                return;
            sol::protected_function_result r = f();
            if (!r.valid())
            {
                sol::error err = r;
                LZ_ERROR("%s() error: %s", name, err.what());
            }
        }
    } // namespace

    void LuaRuntime::call_init()
    {
        if (p_)
            call_cb(p_->init, "_init");
    }
    void LuaRuntime::call_update()
    {
        if (!p_)
            return;
        // A cart picks its logic rate by which callback it defines; _update60 wins.
        if (p_->update60.valid())
            call_cb(p_->update60, "_update60");
        else
            call_cb(p_->update, "_update");
    }
    void LuaRuntime::call_draw()
    {
        if (p_)
            call_cb(p_->draw, "_draw");
    }

    bool LuaRuntime::has_update() const { return p_ && (p_->update.valid() || p_->update60.valid()); }
    bool LuaRuntime::has_draw() const { return p_ && p_->draw.valid(); }
    bool LuaRuntime::wants_60hz() const { return p_ && p_->update60.valid(); }
} // namespace lazy100
