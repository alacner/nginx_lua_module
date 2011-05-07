--Copyright (c) 2011-2015 Zhihua Zhang (alacner@gmail.com)
module('kit', package.seeall)

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
