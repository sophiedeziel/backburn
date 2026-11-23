module GenericModule
  class NestedClass; end

  # Module.nesting is [OtherClass, GenericModule]: NestedClass is GenericModule::NestedClass.
  class ::OtherClass
    def NestedClass.example_method; end
  end
end
