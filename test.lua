--package.path = package.path .. '/usr/local/lib/lua/5.1/?.lua;/usr/local/lib/lua/5.1/?/core.lua;;'
--package.cpath = package.cpath .. '/usr/local/lib/lua/5.1/?.so;/usr/local/lib/lua/5.1/?/core.so;;';

require "print_r"
local nix = require ("nix")
print_r(os.time())
print_r(package.preload)
print_r(require)
print_r(nix.crc32(22))



local socket = require("socket")
local sc = socket.connect('10.249.196.117', 11211)
sc:send("get 1234\r\n")
local answer = sc:receive()
print(answer)


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
local memcache_key = os.time();
local config = flexihash:lookup(memcache_key)
print(memcache_key, ' => ', config)
