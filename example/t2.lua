--Copyright (c) 2011-2015 Zhihua Zhang (alacner@gmail.com)
ngx.set_header('Content-Type', "text/html");
local print = ngx.print
print(os.time())
require "lp"
include('test.lp')
ngx.eof()
