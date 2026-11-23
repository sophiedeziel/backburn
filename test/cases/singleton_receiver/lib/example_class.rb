class ExampleClass; end

def ExampleClass.entry_method
  helper_method
end

def ExampleClass.helper_method; end

def ExampleClass.other_method; end    # uncertain: the send in ExampleClass.dispatch may call it
