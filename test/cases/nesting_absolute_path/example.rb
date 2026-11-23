module OtherModule
  module ExampleMixin
    def perform; end            # unreferenced: OtherModule is not in the nesting below
  end
end

module GenericModule
  module ExampleMixin
    def perform
      helper_method
    end

    def helper_method; end
  end

  # Module.nesting is [OtherModule::ExampleClass, GenericModule]: GenericModule::ExampleMixin.
  class ::OtherModule::ExampleClass
    include ExampleMixin
    include Sidekiq::Job
  end
end
