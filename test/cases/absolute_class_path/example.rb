module GenericModule
  # `::ExampleClass` reopens the top-level ExampleClass, not GenericModule::ExampleClass.
  class ::ExampleClass
    def example_method; end
  end

  class NestedClass; end        # unreferenced
end
