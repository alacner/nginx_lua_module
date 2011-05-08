-- Copyright (C) Alacner Zhang (alacner@gmail.com)

--module('nginx', package.seeall)

print = ngx.print

require("nginx.kit")
require("nginx.boundary")
require("nginx.urlcode")

print_r  = nginx.kit.print_r

local ngx_post_request_data = ngx.post["request_data"]

ngx.post = nil

if ngx_post_request_data then
    local posts, files = {}, {};
    local boundary = nginx.boundary.get(ngx.header["Content-Type"])
    if boundary then
        posts, files = nginx.boundary.split(ngx_post_request_data, boundary)
    else
        nginx.urlcode.parsequery(ngx_post_request_data, posts)
    end
    ngx.post, ngx.files, posts, files = posts, files, nil, nil
end
