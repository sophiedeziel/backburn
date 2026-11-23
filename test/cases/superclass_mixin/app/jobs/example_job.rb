class ExampleJob < ExampleBaseJob
  def perform
    example_helper
  end

  def example_helper; end
end
