module GenericModule
  class NestedClass; end

  module NestedModule
    def NestedClass.example_method; end    # GenericModule::NestedClass.example_method

    def ExampleClass.dispatch(name)        # ExampleClass.dispatch, at the top level
      send(name)                           # may call any singleton method of ExampleClass
    end
  end
end
