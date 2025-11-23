-- simple wrk mix: choose between small and big files
local paths = { "/small.txt", "/big.bin" }

request = function()
    local p = paths[math.random(1, #paths)]
    return wrk.format("GET", p)
end
