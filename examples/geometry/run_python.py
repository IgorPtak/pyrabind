import math
import numpy as np
import geometry as geo

a = geo.Vec2(1.0, 2.0)
b = geo.Vec2(4.0, 6.0)
c = 2.0 * a + b
assert math.isclose(c.x, 6.0)
assert math.isclose(c.y, 10.0)
assert math.isclose(c.norm, math.hypot(6.0, 10.0))

u = np.array([1.0, 2.0, 3.0], dtype=np.float64)
v = np.array([4.0, 5.0, 6.0], dtype=np.float64)
assert math.isclose(geo.array_dot(u, v), 32.0)
print("geometry Python example passed")
