--Copyright (c) 2011-2015 Zhihua Zhang (alacner@gmail.com)
--ngx.set_header('Location', "http://www.google.com");
--ngx.set_cookie('love', '123456') -- name, value, expire, path, domain, secure 
--ngx.set_cookie('aa', 'ssssss') -- name, value, expire, path, domain, secure 
print = ngx.print
local kit = require("kit")
local print_r  = kit.print_r

--ngx.set_header('Content-Type', "image/png");
ngx.set_header('Content-Type', "text/html");

--local f = io.open('/root/repos/nginx_lua_module/example/t1.lua', 'rb')
--local f = io.open('/root/test.jpg', 'rb')
--local t = f:read("*a")
--ngx.print(t)

local f = io.open('/root/480x480.png', 'rb')
--local t = f:read("*a")
--log(a)
--local t = f:read(8192)
--ngx.print(t)
--print_r(ngx)
--ngx.flush()
--ngx.print(t)
--print_r(ngx)
--ngx.flush()
--ngx.print(t)
--print_r(ngx)
--local f = io.open('/root/test.jpg', 'rb')
--local f = assert(io.open(arg[1], "rb"))
f:close()
--ngx.print(ngx.set_cookie)
local t = os.time()
ngx.set_cookie('love1', "me") -- name, value, expire, path, domain, secure 
--ngx.set_cookie('love2', "me", 100) -- name, value, expire, path, domain, secure 
--ngx.set_cookie('love3', '123456') -- name, value, expire, path, domain, secure 
--ngx.set_cookie('isopen', 'true', 1000, '/', '192.168.137.126') -- name, value, expire, path, domain, secure 
ngx.set_header('X-Memc-Flags', "11111111111111");
ngx.set_header('X-Memc-Flags', "2222222222222222222222222");
ngx.set_header('X-Memc-Flags', "f1111122222222333333334444445");
print_r(ngx)
ngx.eof()
do return end

local http = require("socket.http")
local r, e, h, l = http.request("http://www.6uu.com")
print_r(h)
print_r(l)
print_r(m)
ngx.eof()

do return end


local socket = require("socket")
local tcp = socket.tcp()
tcp:settimeout(1)
local n,e,h = tcp:connect("www.google.com","80")
print_r(n)
--print_r(e)
print_r(h)
ngx.eof()

do return end


TIMEOUT = 1;
local http = require("socket.http")
local r, e, h = http.request("http://www.6uu.com")
print_r(h)
do return end
--ngx.set_header('X-Memc-Flags', "f1111122222222333333334444445");
--ngx.set_header("Set-Cookie", 'love=xxx;');
ngx.print('exit?')

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
--]=====]

local socket = require("socket")
local sc = socket.connect('127.0.0.1', 11211)
sc:settimeout(1)
--local sc = socket.connect('10.249.196.117', 11211)
sc:send("get " .. config .. "\r\n")
local answer = sc:receive()
print(answer)

print_r(ngx)
print"------ end -----"
