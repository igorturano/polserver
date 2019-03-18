const { print } = process._linkedBinding("basic");
const { start } = process._linkedBinding("tsfn");

let myFunct = function() {
  print("Called with args: ", Array.from(arguments));
};

let val = start(myFunct, require);
print("start value:", val);
