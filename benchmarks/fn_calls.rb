def func(a)
  return a;
end

i = 0
while i < 1000000
  func(i)
  i += 1
end
