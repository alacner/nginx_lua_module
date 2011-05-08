-- Copyright (C) Alacner Zhang (alacner@gmail.com)

module('nginx.kit', package.seeall)

function print_r(sth)
    if type(sth) ~= "table" then
        if type(sth) == "nil" then
            sth = "nil"
        end
        print(sth)
        return
    end

    local space, deep = string.rep(' ', 4), 0
    local function _dump(t)
        for k,v in pairs(t) do
            local key = tostring(k)

            if type(v) == "table" then
                deep = deep + 2
                print(string.format("%s[%s] => Table\r\n%s(\r\n",
                                string.rep(space, deep - 1),
                                key,
                                string.rep(space, deep)
                        )
                    ) --print.
                _dump(v)

                print(string.format("%s)\r\n",string.rep(space, deep)))
                deep = deep - 2
            else
                print(string.format("%s[%s] => %s\r\n",
                                string.rep(space, deep + 1),
                                key,
                                tostring(v)
                        )
                    ) --print.
            end
        end
    end

    print(string.format("Table\r\n(\r\n"))
    _dump(sth)
    print(string.format(")\r\n"))
end


function split(str, pat)
   local t = {}  -- NOTE: use {n = 0} in Lua-5.0
   local fpat = "(.-)" .. pat
   local last_end = 1
   local s, e, cap = str:find(fpat, 1)
   while s do
      if s ~= 1 or cap ~= "" then
     table.insert(t,cap)
      end
      last_end = e+1
      s, e, cap = str:find(fpat, last_end)
   end
   if last_end <= #str then
      cap = str:sub(last_end)
      table.insert(t, cap)
   end
   return t
end
