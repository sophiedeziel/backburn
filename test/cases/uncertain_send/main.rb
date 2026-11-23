Dispatcher.new.run(ARGV[0])
Exporter.new.export(ARGV[1])
Reporter.new.report
Factory.build(ARGV[2])
