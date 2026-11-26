-- Nebel Multi-Language i18n Manager (EN / DE / PL) (MIT License)
-- Add or edit translations in scripts/locales/*.lua without recompiling C.

local M = {
    order = { "en", "de", "pl" },
    tables = {},
}

local function load_locale(code)
    if M.tables[code] then return M.tables[code] end
    local ok, tbl = pcall(require, "locales." .. code)
    if ok and type(tbl) == "table" then
        M.tables[code] = tbl
        return tbl
    end
    return nil
end

function M.lang()
    local cur = nebel.platform.lang()
    if cur == "de" or cur == "pl" or cur == "en" then
        return cur
    end
    return "en"
end

function M.set_lang(code)
    nebel.platform.set_lang(code)
end

function M.cycle()
    local cur = M.lang()
    local next_code = "en"
    for i, c in ipairs(M.order) do
        if c == cur then
            next_code = M.order[(i % #M.order) + 1]
            break
        end
    end
    M.set_lang(next_code)
    return next_code
end

function M.t(key)
    local cur = M.lang()
    local tbl = load_locale(cur)
    if tbl and tbl[key] then return tbl[key] end
    local en = load_locale("en")
    if en and en[key] then return en[key] end
    return key
end

function M.get_table(code)
    return load_locale(code)
end

return M
