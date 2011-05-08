-- Copyright (C) Alacner Zhang (alacner@gmail.com)

module('nginx.boundary', package.seeall)

require ("nginx.kit")

function get(ct)
    if not ct then return false end
    local _,_,boundary = string.find (ct, "boundary%=(.-)$")
    if not boundary then return false end
    return  "--"..boundary 
end

--
-- Create a table containing the headers of a multipart/form-data field
--
function split(...)
    local request_data, boundary = ...

    if not request_data then return false end; 
    if not boundary then return false end; 

    local files, posts = {}, {}

    local post_temp = nginx.kit.split(request_data, boundary)
    for i,pd in ipairs(post_temp) do 

        local headers = {}
        local hdrdata, post_val = string.match(pd, "(.+)\r\n\r\n(.+)\r\n")
        --printl(hdrdata)

        if hdrdata then
            string.gsub (hdrdata, '([^%c%s:]+):%s+([^\n]+)', function(type,val)
                type = string.lower(type)
                headers[type] = val
            end)
        end

        local t = {}
        local hcd = headers["content-disposition"]
        if hcd then
            string.gsub(hcd, ';%s*([^%s=]+)="(.-)"', function(attr, val)
                t[attr] = val
             end)
            -- Filter POST or FILE
            if headers["content-type"] then
                -- name,type,size,tmp_name,error
                local file = {}
                file['type'] = headers["content-type"]
                file['name'] = t["filename"]
                file['data'] = post_val; 
                file['size'] = string.len(post_val); 

                files[t.name] = file
            else
                posts[t.name] = post_val 
            end 
        end
    end

    return posts, files
end
