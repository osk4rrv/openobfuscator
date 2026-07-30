local escaped = "quote:\" slash:\\ tab:\t"
local long = [=[long:[[]]:literal
second]=]

local function calculate(a, b)
    return a + b, a * b, a ^ 2
end

local function relay(...)
    return ...
end

local sum, product, power = calculate(7, 6)
local left, middle, right = relay("alpha", 99, "omega")
local numeric = 0x10 + 1.5e1

io.write("args=", select("#", ...), ";first=", tostring((select(1, ...))), "\n")
io.write("escaped=", escaped, "\n")
io.write("long=", (long:gsub("\n", "|")), "\n")
io.write("calc=", sum, ",", product, ",", power, ";numeric=", numeric, "\n")
io.write("relay=", left, ",", middle, ",", right, "\n")
