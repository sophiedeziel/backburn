class CleanupWorker
  include Shoryuken::Worker          # the queue library calls #perform: a root by mixin

  def perform(_msg, body)
    Trimmer.new.trim(body)
  end
end

class Trimmer < Struct.new(:x)
  def trim(s) = s.strip
  def to_s
    super                            # overrides a library method: a root
  end
  def unused_trim(s) = s.chomp
end
