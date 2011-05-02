--Copyright (c) 2011-2015 Zhihua Zhang (alacner@gmail.com)
ngx.set_header('Content-Type', "text/html");

--local f = io.open('/root/test.jpg', 'rb')
--ngx.print(f:read("*a"))
print = ngx.print
local kit = require("kit")
local print_r  = kit.print_r
--ngx.print(ngx.set_cookie)
ngx.set_cookie('love', '123456') -- name, value, expire, path, domain, secure 
ngx.set_cookie('isopen', 'true', 1000, '/', 'test.com') -- name, value, expire, path, domain, secure 
print_r(ngx)
local http = require("socket.http")
local r, e, h = http.request("http://www.6uu.com")
print_r(h)
--ngx.set_header("Set-Cookie", 'love=xxx;');
do return end
--ngx.print(table.concat(p))
--[[
]]
ngx.print('hhhhhhhhhhh')
ngx.print('<br/>')
ngx.print(ngx.random)
ngx.print('<br/>')
ngx.print(ngx.server.QUERY_STRING)
print(os.time())
local Flexihash = require "Flexihash"
local flexihash = Flexihash.New()
flexihash:addTarget('10.249.196.117|11211|0')
flexihash:addTarget('10.249.196.118|11211|0')
flexihash:addTarget('10.249.196.119|11211|0')
flexihash:addTarget('10.249.196.120|11211|0')
flexihash:addTarget('10.249.196.121|11211|0')
flexihash:addTarget('10.249.196.122|11211|0')
flexihash:addTarget('10.249.196.123|11211|0')
flexihash:addTarget('10.249.196.123|11211|0')
flexihash:addTarget('10.249.196.124|11211|0')
print_r(flexihash:getAllTargets())
local memcache_key = os.time();
local config = flexihash:lookup(memcache_key)
ngx.set_header('X-Memc-Flags', config);
print(memcache_key, ' => ', config)
print("\r\n memcached\r\n")

--[=====[
require "Memcached"
--local memc = Memcached.New('127.0.0.1', 11211)
--memc:set('1234', 1234)
--print(memc:get(1234))

local socket = require("socket")
local sc = socket.connect('127.0.0.1', 11211)
--local sc = socket.connect('10.249.196.117', 11211)
sc:send("get " .. config .. "\r\n")
local answer = sc:receive()
print(answer)

print_r(ngx)
print"------ end -----"
--]=====]
