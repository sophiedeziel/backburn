module ExampleMixin
  def perform; end              # unreferenced: GenericModule::ExampleMixin is found first
end

module GenericModule
  module ExampleMixin
    def perform
      helper_method
    end

    def helper_method; end
  end

  # Module.nesting is [ExampleClass, GenericModule]: GenericModule::ExampleMixin.
  class ::ExampleClass
    include ExampleMixin
    include Sidekiq::Job
  end
end
