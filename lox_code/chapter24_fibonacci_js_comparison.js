console.log("chapter24_fibinacci.lox");

function fib(n) {
  if (n < 2) return n;
  return fib(n - 2) + fib(n - 1);
}

var start = new Date();
console.log(fib(35));
console.log(new Date() - start);
