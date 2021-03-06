// |reftest| skip-if(!this.hasOwnProperty("SIMD"))
var BUGNUMBER = 946042;
var float32x4 = SIMD.float32x4;
var int32x4 = SIMD.int32x4;

var summary = 'int32x4 add';

function test() {
  print(BUGNUMBER + ": " + summary);

  var a = int32x4(1, 2, 3, 4);
  var b = int32x4(10, 20, 30, 40);
  var c = SIMD.int32x4.add(a, b);
  assertEq(c.x, 11);
  assertEq(c.y, 22);
  assertEq(c.z, 33);
  assertEq(c.w, 44);

  var INT32_MAX = Math.pow(2, 31) - 1;
  var INT32_MIN = -Math.pow(2, 31);

  var d = int32x4(INT32_MAX, INT32_MIN, INT32_MAX, INT32_MIN);
  var e = int32x4(1, -1, 0, 0);
  var f = SIMD.int32x4.add(d, e);
  assertEq(f.x, INT32_MIN);
  assertEq(f.y, INT32_MAX);
  assertEq(f.z, INT32_MAX);
  assertEq(f.w, INT32_MIN);

  if (typeof reportCompare === "function")
    reportCompare(true, true);
}

test();

