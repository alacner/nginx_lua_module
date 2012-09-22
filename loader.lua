local __EXITERROR = "DONTCATCHME298254"

function ngx.exit()
	ngx.eof()
	error(__EXITERROR)
end

local __FILE__ = ngx.server.SCRIPT_FILENAME

local function debug_trace(err)
	if err:find(__EXITERROR, 1, true) then return end
	local ret = {err}
	local lev = 2
	local cur = nil
	while true do
		cur = debug.getinfo(lev)
		if (not cur) or cur.short_src == __FILE__ then break end
		local name = cur.name
		if not name then
			name = "In main chunk"
		else
			name = "In function '"..name.."'"
		end
		table.insert(ret, "\t"..cur.short_src..":"..cur.currentline..": "..name)
		lev = lev + 1
	end
	if cur and cur.short_src == __FILE__ then
		while #ret > 0 and ret[#ret]:sub(1,4) == "\t[C]" do
			table.remove(ret)
		end
	end
	return table.concat(ret, "\n")
end

--The callback must be a function(user, password) returning true for success and false for failure
function ngx.require_auth(realm, callback)
	local auth = ngx.header.Authorization
	local success = false
	if auth and auth:sub(1,6):lower() == "basic " then
		if not base64 then require("base64") end
		auth = auth:sub(7)
		auth = base64.decode(auth)
		local pw = auth:find(":",1,true)
		if pw then
			success = callback(auth:sub(1,pw-1),auth:sub(pw+1))
		end
	end
	if not success then
		ngx.set_status(401)
		ngx.set_header('WWW-Authenticate', 'Basic realm="'..realm..'"')
		ngx.print("Please authenticate")
		ngx.exit()
	end
end

local file = ngx.get_variable("document_root") .. ngx.server.REQUEST_URI
local f = io.open(file, "r")

if f then
	io.close(f)

	local __FILE__ = file
	local __EXITERROR = nil
	ngx.server.SCRIPT_FILENAME = __FILE__

	f = nil
	file = nil

	ngx.set_header("Content-Type", "text/html")
	local isok, err = xpcall(dofile, debug_trace, __FILE__)
	if not isok then
		if err then
			ngx.print("<h1>Critical: Lua error</h1><pre>"..err.."</pre>")
			ngx.set_status(500)
		else
			return
		end
	else
		ngx.set_status(200)
	end
else
	ngx.print("404 - File not found")
	ngx.set_status(404)
end
ngx.eof()
