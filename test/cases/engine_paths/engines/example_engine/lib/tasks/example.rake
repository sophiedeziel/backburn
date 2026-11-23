module ExampleTasks
  extend Rake::DSL

  def self.example_helper
    puts "done"
  end

  namespace :example do
    task(:run) { example_helper }
  end
end
